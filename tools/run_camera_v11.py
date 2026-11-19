#!/usr/bin/env python3
"""Test the MaixCAM2 camera on the V1.1 carrier's FPC2 connector."""
import argparse
from contextlib import ExitStack
import fcntl
import hashlib
import json
import mmap
import os
from pathlib import Path
import platform
import re
import signal
import socket
import struct
import subprocess
import sys
import time

BASE = Path(__file__).resolve().parent
PAD_BASE = 0x02300000
PAD_OFFSETS = {"A8": 0x6c, "A9": 0x78}
LOCKS = ("/run/lock/maixcam2-camera-v11.lock",
         "/run/lock/maixcam2-servo-scope.lock",
         "/run/lock/maixcam2-servo-v11.lock",
         "/run/lock/maixcam2-dual-imu.lock", "/tmp/servo_e907.lock")


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


class CameraPads:
    """Save and restore A8/A9, including multimedia initialization changes."""
    def __enter__(self):
        self.restored = False
        fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        try:
            self.registers = mmap.mmap(fd, 4096, flags=mmap.MAP_SHARED,
                                      prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=PAD_BASE)
        finally:
            os.close(fd)
        self.before = self.snapshot()
        return self

    def snapshot(self):
        return {pin: struct.unpack_from("<I", self.registers, offset)[0]
                for pin, offset in PAD_OFFSETS.items()}

    def __exit__(self, exc_type, exc_value, traceback):
        try:
            for pin, offset in PAD_OFFSETS.items():
                struct.pack_into("<I", self.registers, offset, self.before[pin])
            self.restored = self.snapshot() == self.before
            if not self.restored:
                raise RuntimeError("Camera pin restore verification failed")
        finally:
            self.registers.close()


def check_board():
    if os.geteuid() != 0 or platform.machine() != "aarch64":
        raise RuntimeError("Requires root on the MaixCAM2 board")
    if Path("/proc/device-tree/model").read_text().rstrip("\x00\n") != "MaixCAM2":
        raise RuntimeError("Unsupported board model")
    if not Path("/dev/i2c-7").exists():
        raise RuntimeError("I2C7 device is unavailable")
    if not Path("/sys/bus/platform/devices/4857000.i2c/driver").is_symlink():
        raise RuntimeError("I2C7 driver is unbound")
    gpio = Path("/sys/kernel/debug/gpio").read_text()
    if re.search(r"\bgpio-\s*(8|9)\s+\(", gpio):
        raise RuntimeError("A8/A9 are owned by a GPIO consumer")
    busy = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            for fd in (proc / "fd").iterdir():
                try:
                    device = os.readlink(fd)
                except OSError:
                    continue
                if re.fullmatch(r"/dev/(?:i2c-7|spidev[^/]*|video\d+|media\d+|"
                                r"ax_?(?:vin|isp|ivps|mipi[_-]?rx)\w*)", device):
                    busy.append((int(proc.name), device))
        except FileNotFoundError:
            continue
    if busy:
        raise RuntimeError("Camera or sensor bus already in use: " + str(busy))


def run(output):
    library = BASE / "camera_v11_i2c.so"
    capture_script = BASE / "camera_v11_capture.py"
    if not library.is_file() or not capture_script.is_file():
        raise RuntimeError("Build and run the package using tools/build_camera_v11.sh")
    if os.geteuid() != 0 or platform.machine() != "aarch64":
        raise RuntimeError("Requires root on the MaixCAM2 board")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {"carrier_revision": "V1.1", "connector": "FPC2", "camera_i2c": 7,
              "hostname": socket.gethostname(), "status": "FAIL", "pins_restored": None,
              "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
              "capture_sha256": hashlib.sha256(capture_script.read_bytes()).hexdigest()}
    pads = None
    begin = time.monotonic()
    try:
        with ExitStack() as stack:
            for name in LOCKS:
                lock = stack.enter_context(open(name, "a+"))
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            check_board()
            pads = CameraPads()
            with pads:
                result["pads_before"] = pads.before
                env = os.environ.copy()
                env["LD_PRELOAD"] = str(library)
                with (output / "capture.log").open("w") as log:
                    child = subprocess.Popen([sys.executable, "-u", str(capture_script),
                                              "--out", str(output)], env=env,
                                             stdout=log, stderr=subprocess.STDOUT)
                    try:
                        result["exit_code"] = child.wait(timeout=25)
                    finally:
                        if child.poll() is None:
                            child.terminate()
                            try:
                                child.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                child.kill()
                                child.wait()
                result["pads_after_capture"] = pads.snapshot()
            log_text = (output / "capture.log").read_text(errors="replace")
            result["i2c_read_write_errors"] = len(re.findall(
                r"i2c_(?:read|write):.*[Ff]ailed", log_text))
            result["i2c_routed_opens"] = log_text.count("CAMERA_I2C_ROUTE /dev/i2c-0 -> /dev/i2c-7")
            result["mux_reapplications"] = log_text.count("CAMERA_I2C7_MUX_REAPPLIED")
            if result["exit_code"] != 0:
                raise RuntimeError("Camera capture failed; inspect capture.log")
            if result["i2c_read_write_errors"] or not result["i2c_routed_opens"]:
                raise RuntimeError("Camera I2C adaptation check failed")
            result.update(json.loads((output / "capture.json").read_text()))
            if result["valid_frames"] != 90:
                raise RuntimeError("Incomplete camera capture")
            if (output / "frame.png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                raise RuntimeError("Sample PNG was not saved")
            result["status"] = "PASS"
    except BaseException as error:
        result["error"] = str(error)
        raise
    finally:
        if pads is not None:
            result["pins_restored"] = pads.restored
        result["elapsed_seconds"] = time.monotonic() - begin
        save_json(output / "result.json", result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
    return 0


def on_signal(signum, frame):
    raise KeyboardInterrupt("Stop requested")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="New output directory")
    args = parser.parse_args()
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, on_signal)
    try:
        raise SystemExit(run(args.out))
    except KeyboardInterrupt:
        raise SystemExit(130)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
