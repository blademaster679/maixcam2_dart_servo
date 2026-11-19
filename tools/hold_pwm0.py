#!/usr/bin/env python3
"""Explicit continuous 333 Hz / 1500 us hold on PCB 0 (PWM00/A24)."""
import argparse
import fcntl
import json
import mmap
import os
from pathlib import Path
import platform
import signal
import struct
import subprocess
import time
import uuid

from run_servo_scope import DisplayLease, save_json, snapshot

BASE = Path(__file__).resolve().parent


def process_identity(pid):
    fields = Path('/proc/%d/stat' % pid).read_text().rsplit(') ', 1)[1].split()
    return fields[19], fields[0]


def live_session():
    try:
        session = json.loads((BASE / 'session.json').read_text())
        ticks, state = process_identity(session['pid'])
        args = Path('/proc/%d/cmdline' % session['pid']).read_bytes().split(b'\0')
        script = str(BASE / 'hold_pwm0.py').encode()
        if ticks != session['start_ticks'] or state == 'Z':
            return None
        if script not in args and b'hold_pwm0.py' not in args:
            return None
        return session
    except (OSError, ValueError, KeyError):
        return None


def status():
    session = live_session()
    if session is None:
        print('Continuous PWM0 hold is not running.')
        return 1
    state = snapshot()
    fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
    try:
        with mmap.mmap(fd, 4096, access=mmap.ACCESS_READ, offset=0x06060000) as registers:
            low = struct.unpack_from('<I', registers, 0)[0]
            high = struct.unpack_from('<I', registers, 0xb0)[0]
    finally:
        os.close(fd)
    verified = (state['servo_controls'][3] == 0x1f and
                not any(value & 1 for value in state['servo_controls'][:3]) and
                low == 36072 and high == 36000 and
                ((state['servo_pads'][3] >> 16) & 7) == 3)
    print(json.dumps(dict(session, process_alive=True, register_check_pass=verified,
                          low_count=low, high_count=high,
                          servo_controls=state['servo_controls']), indent=2))
    return 0 if verified else 1


def stop():
    session = live_session()
    if session is None:
        print('No matching continuous PWM0 hold process; no signal sent.')
        return 0
    os.kill(session['pid'], signal.SIGTERM)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if live_session() is None:
            result_file = Path(session['run_dir']) / 'result.json'
            if result_file.exists() and json.loads(result_file.read_text()).get('cleanup_verified'):
                print('PWM0 stopped; display, pins and I2C0 restored.')
                return 0
            raise RuntimeError('Process exited; inspect its log to confirm cleanup')
        time.sleep(0.1)
    raise RuntimeError('Still waiting for safe cleanup; do not use SIGKILL')


def run():
    if platform.machine() != 'aarch64' or os.geteuid() != 0:
        raise RuntimeError('Requires root on the AArch64 board')
    subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=BASE, check=True)
    child = None
    cancelled = False

    def on_stop(signum, frame):
        nonlocal cancelled
        cancelled = True
        if child is not None and child.poll() is None:
            child.terminate()

    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, on_stop)
    with open('/run/lock/maixcam2-servo-scope.lock', 'a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        logdir = BASE / 'logs' / uuid.uuid4().hex[:12]
        logdir.mkdir(parents=True)
        session = dict(pid=os.getpid(), start_ticks=process_identity(os.getpid())[0],
                       run_dir=str(logdir), pcb_silkscreen='0', pin='A24', pwm=0,
                       logical_servo=4, mask=8, frequency_hz=333, pulse_us=1500,
                       duration='continuous_until_signal')
        save_json(BASE / 'session.json', session)
        save_json(logdir / 'metadata.json', session)
        try:
            with DisplayLease(logdir):
                if cancelled:
                    raise RuntimeError('Cancelled before output')
                print('HOLD_START: PCB 0 / PWM00 / A24, 333 Hz, 1500 us, no duration limit', flush=True)
                with (logdir / 'commands.log').open('w', buffering=1) as output:
                    child = subprocess.Popen([str(BASE / 'e907_live_probe'), '--hold-servo', '4',
                                              str(BASE / 'e907_servo.bin'), '--pulse-us', '1500'],
                                             cwd=BASE, stdout=output, stderr=subprocess.STDOUT)
                    (logdir / 'loader.pid').write_text(str(child.pid) + '\n')
                    try:
                        while child.poll() is None:
                            if cancelled:
                                child.terminate()
                            time.sleep(0.1)
                    finally:
                        if child.poll() is None:
                            child.terminate()
                            child.wait()
                    code = child.returncode
            save_json(logdir / 'result.json', dict(session, exit_code=code,
                      stop_requested=cancelled, cleanup_verified=True))
            print('HOLD_STOPPED: four servo outputs disabled; display and pins restored', flush=True)
            return code
        except BaseException as error:
            save_json(logdir / 'result.json', dict(session, error=str(error), cleanup_verified=False))
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--run', action='store_true')
    action.add_argument('--status', action='store_true')
    action.add_argument('--stop', action='store_true')
    args = parser.parse_args()
    return run() if args.run else stop() if args.stop else status()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError) as error:
        print('ERROR: ' + str(error), flush=True)
        raise SystemExit(1)
