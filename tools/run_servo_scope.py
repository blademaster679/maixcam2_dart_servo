#!/usr/bin/env python3
"""Three explicitly invoked, bounded scope cases for PCB PWM01/A25."""
import argparse
import fcntl
import hashlib
import json
import mmap
import os
from pathlib import Path
import platform
import selectors
import signal
import struct
import subprocess
import sys
import time
import uuid

BASE = Path(__file__).resolve().parent
CASES = {
    1: {"backend": "E907", "frequency_hz": 333},
    2: {"backend": "Linux", "frequency_hz": 333},
    3: {"backend": "Linux", "frequency_hz": 50},
}
FILES = {"e907_live_probe", "e907_servo.bin", "e907_probe.bin", "servo_scope_linux", "run_scope.py"}
BL_FIELDS = ("period", "duty_cycle", "polarity", "enable")
DEVICES = ("6060000.pwm0", "6061000.pwm1", "4850000.i2c")


def verify_files(base):
    seen = set()
    for line in (base / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        name = name.lstrip("*")
        if name not in FILES or name in seen:
            raise RuntimeError("Unexpected checksum entry: " + name)
        if hashlib.sha256((base / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError("Checksum mismatch: " + name)
        seen.add(name)
    if seen != FILES:
        raise RuntimeError("Incomplete scope test package")


def chip_path(device="6060000.pwm0"):
    paths = list((Path("/sys/bus/platform/devices") / device / "pwm").glob("pwmchip*"))
    if len(paths) != 1:
        raise RuntimeError("PWM controller unavailable: " + device)
    return paths[0]


def write_value(path, value):
    path.write_text(str(value))


def read_backlight(node):
    try:
        return {field: (node / field).read_text().strip() for field in BL_FIELDS}
    except FileNotFoundError:
        return None


def restore_backlight(chip, saved):
    node = chip / "pwm3"
    if not node.exists():
        write_value(chip / "export", 3)
    # A new export starts with period=0; writing enable first returns EINVAL.
    if (node / "period").read_text().strip() == "0":
        write_value(node / "period", saved["period"])
    write_value(node / "enable", 0)
    write_value(node / "duty_cycle", 0)
    write_value(node / "period", saved["period"])
    write_value(node / "polarity", saved["polarity"])
    write_value(node / "duty_cycle", saved["duty_cycle"])
    write_value(node / "enable", saved["enable"])
    if read_backlight(node) != saved:
        raise RuntimeError("Backlight restore check failed")


def snapshot():
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        with mmap.mmap(fd, 8192, access=mmap.ACCESS_READ, offset=0x06060000) as registers:
            controls = [struct.unpack_from("<I", registers, offset)[0]
                        for offset in (0x1044, 0x1030, 0x1c, 0x08)]
        with mmap.mmap(fd, 4096, access=mmap.ACCESS_READ, offset=0x02304000) as registers:
            pads = [struct.unpack_from("<I", registers, offset)[0]
                    for offset in (0x60, 0x54, 0x18, 0x0c)]
    finally:
        os.close(fd)
    return {
        "servo_controls": controls,
        "servo_pads": pads,
        "pwmchips": {p.name: str(p.resolve()) for p in Path("/sys/class/pwm").glob("pwmchip*")},
        "bindings": {device: os.path.realpath("/sys/bus/platform/devices/" + device + "/driver")
                     for device in DEVICES},
        "pwm_debug": Path("/sys/kernel/debug/pwm").read_text(),
    }


def pause_display(paused):
    # Pause the respawn supervisor first; then enumerate its current UI child.
    for executable in (b"/maixapp/apps/launcher/launcher_daemon", b"/maixapp/apps/launcher/launcher"):
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                if (entry / "cmdline").read_bytes().split(b"\0")[0] != executable:
                    continue
                state = next(line for line in (entry / "status").read_text().splitlines()
                             if line.startswith("State:")).split()[1]
                if state in ("T", "t"):
                    continue  # Preserve processes already stopped by their owner.
                pid = int(entry.name)
                os.kill(pid, signal.SIGSTOP)
                paused.append(pid)
                for attempt in range(200):
                    try:
                        state = next(line for line in (entry / "status").read_text().splitlines()
                                     if line.startswith("State:")).split()[1]
                    except FileNotFoundError:
                        break
                    if state in ("T", "t"):
                        break
                    time.sleep(0.01)
                else:
                    raise RuntimeError("Display process did not pause")
            except (FileNotFoundError, ProcessLookupError):
                continue


def resume_display(paused):
    for pid in reversed(paused):
        try:
            os.kill(pid, signal.SIGCONT)
        except ProcessLookupError:
            pass


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


class DisplayLease:
    def __init__(self, logdir):
        self.logdir = logdir
        self.paused = []
        self.saved = None
        self.restore_needed = False
        self.before = None

    def __enter__(self):
        self.before = snapshot()
        save_json(self.logdir / "before.json", self.before)
        if any(value & 1 for value in self.before["servo_controls"]):
            raise RuntimeError("Servo PWM already active; stop the other test first")
        self.saved = read_backlight(chip_path() / "pwm3")
        try:
            pause_display(self.paused)
            chip = chip_path()
            current = read_backlight(chip / "pwm3")
            if current is not None:
                self.saved = current
            if self.saved is not None:
                if int(self.saved["period"]) <= 0 or self.saved["polarity"] != "normal":
                    raise RuntimeError("Display PWM is not fully initialized; retry after the UI starts")
                save_json(self.logdir / "backlight_before.json", self.saved)
                self.restore_needed = True
            if current is not None:
                write_value(chip / "pwm3" / "enable", 0)
                write_value(chip / "unexport", 3)
            print("Display parked; preparing requested PWM output", flush=True)
            return self
        except BaseException:
            self.close()
            raise

    def close(self):
        try:
            if self.restore_needed:
                restore_backlight(chip_path(), self.saved)
                save_json(self.logdir / "backlight_after.json", read_backlight(chip_path() / "pwm3"))
            after = snapshot()
            save_json(self.logdir / "after.json", after)
            if any(value & 1 for value in after["servo_controls"]):
                raise RuntimeError("Servo PWM still enabled after test")
            for key in ("servo_pads", "pwmchips", "bindings"):
                if after[key] != self.before[key]:
                    raise RuntimeError("Resource restore mismatch: " + key)
        finally:
            paused, self.paused = self.paused, []
            resume_display(paused)

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


def case_command(case, base):
    if case == 1:
        return [str(base / "e907_live_probe"), "--scope-servo", "3", str(base / "e907_servo.bin")]
    return [str(base / "servo_scope_linux"), "--hz", str(CASES[case]["frequency_hz"])]


def run_case(case, base):
    verify_files(base)
    if platform.machine() != "aarch64" or os.geteuid() != 0:
        raise RuntimeError("Run as root on the AArch64 board")
    logdir = base / "logs" / ("case%d_" % case + uuid.uuid4().hex[:12])
    logdir.mkdir(parents=True)
    metadata = dict(CASES[case], case=case, pcb_silkscreen="1", pin="A25", pwm=1,
                    logical_servo=3, mask=4, pulse_us=1500, duration_ms=60000,
                    physical_waveform_verified=False, mechanical_motion=None)
    save_json(logdir / "metadata.json", metadata)
    print("Case %d: %s, %d Hz, 1500 us, 60 s; PCB 1 / PWM01 / A25" %
          (case, CASES[case]["backend"], CASES[case]["frequency_hz"]), flush=True)
    print("Logs: " + str(logdir), flush=True)
    child = None
    cancelled = False
    previous_handlers = {}

    def stop(signum, frame):
        nonlocal cancelled
        cancelled = True
        if child is not None and child.poll() is None:
            child.terminate()

    try:
        with (logdir / "output.log").open("w", buffering=1) as output:
            for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
                previous_handlers[sig] = signal.signal(sig, stop)
            with open("/run/lock/maixcam2-servo-scope.lock", "a+") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with DisplayLease(logdir):
                    if cancelled:
                        raise RuntimeError("Cancelled before output")
                    child = subprocess.Popen(case_command(case, base), cwd=base,
                                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             text=True, bufsize=1)
                    (logdir / "pid.txt").write_text(str(child.pid) + "\n")
                    deadline = time.monotonic() + 90
                    termination_sent = False
                    selector = selectors.DefaultSelector()
                    try:
                        selector.register(child.stdout, selectors.EVENT_READ)
                        while child.poll() is None:
                            if (cancelled or time.monotonic() > deadline) and not termination_sent:
                                child.terminate()
                                termination_sent = True
                            for key, events in selector.select(timeout=0.2):
                                line = key.fileobj.readline()
                                if line:
                                    output.write(line)
                                    print(line, end="", flush=True)
                        for line in child.stdout:
                            output.write(line)
                            print(line, end="", flush=True)
                    finally:
                        selector.close()
                        if child.poll() is None:
                            child.terminate()
                            # Never SIGKILL the CMM loader. Wait for its cleanup
                            # before resuming any display/resource users.
                            child.wait()
                    code = child.returncode
                metadata.update(exit_code=code, interrupted=cancelled, cleanup_verified=True)
                save_json(logdir / "result.json", metadata)
                print("Output stopped; pins, I2C0, PWM numbering and display restored.", flush=True)
                return 130 if cancelled else code
    except BaseException as error:
        metadata.update(error=str(error), interrupted=cancelled, cleanup_verified=False)
        save_json(logdir / "result.json", metadata)
        raise
    finally:
        for sig, handler in previous_handlers.items():
            signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=int, nargs="?", choices=CASES)
    parser.add_argument("--list", action="store_true", help="List cases without accessing hardware")
    parser.add_argument("--check", action="store_true", help="Check package and board; never output PWM")
    args = parser.parse_args()
    if args.case is None or args.list:
        for case, info in CASES.items():
            print("%d: %-5s %3d Hz, 1500 us, 60 seconds, PCB 1 / PWM01 / A25" %
                  (case, info["backend"], info["frequency_hz"]))
        if not args.check:
            return 0
    if args.check:
        verify_files(BASE)
        if platform.machine() != "aarch64" or os.geteuid() != 0:
            raise RuntimeError("Board check requires root on AArch64")
        state = snapshot()
        if any(value & 1 for value in state["servo_controls"]):
            raise RuntimeError("A servo output is already active")
        print("CHECK OK: files valid; four servo outputs disabled. No PWM was started.")
        return 0
    return run_case(args.case, BASE)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
