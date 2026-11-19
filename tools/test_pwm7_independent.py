#!/usr/bin/env python3
"""PWM7/A31 only: Linux PWM, 333 Hz / 1500 us for 60 s, then restore.

No E907 launch, I2C unbind, GPIO request/direction change or display handling.
"""
import argparse
from contextlib import ExitStack
import fcntl
import json
import mmap
import os
from pathlib import Path
import platform
import re
import signal
import struct
import time
import uuid


def read32(region, offset):
    return struct.unpack_from('<I', region, offset)[0]


def write_value(path, value):
    path.write_text(str(value))


def run():
    if os.geteuid() != 0 or platform.machine() != 'aarch64':
        raise RuntimeError('Requires root on the AArch64 board')
    cancelled = False

    def cancel(signum, frame):
        nonlocal cancelled
        cancelled = True

    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, cancel)
    logs = Path(__file__).resolve().parent / 'servo_pwm7_independent_logs' / uuid.uuid4().hex[:12]
    logs.mkdir(parents=True)

    def save(name, value):
        (logs / name).write_text(json.dumps(value, indent=2) + '\n')

    with ExitStack() as stack:
        for name in ('/run/lock/maixcam2-servo-scope.lock',
                     '/run/lock/maixcam2-servo-v11.lock', '/tmp/servo_e907.lock'):
            lock = stack.enter_context(open(name, 'a+'))
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
        stack.callback(os.close, fd)
        pad = stack.enter_context(mmap.mmap(fd, 4096, offset=0x02304000))
        pwm = stack.enter_context(mmap.mmap(fd, 8192, access=mmap.ACCESS_READ, offset=0x06060000))
        core = stack.enter_context(mmap.mmap(fd, 4096, access=mmap.ACCESS_READ, offset=0x02340000))
        gpio = stack.enter_context(mmap.mmap(fd, 4096, access=mmap.ACCESS_READ, offset=0x04800000))
        if read32(core, 0x54) & (3 << 16) != 3 << 16:
            raise RuntimeError('E907 is not held in reset; stop the previous test first')
        if 'console=ttyS1' in Path('/proc/cmdline').read_text():
            raise RuntimeError('UART1 console uses A31')
        if re.search(r'\bgpio-31\s+\(', Path('/sys/kernel/debug/gpio').read_text()):
            raise RuntimeError('A31 is owned by a GPIO user')
        for process in Path('/proc').iterdir():
            if not process.name.isdigit():
                continue
            try:
                for entry in (process / 'fd').iterdir():
                    try:
                        if os.readlink(entry) == '/dev/ttyS1':
                            raise RuntimeError('UART1 is open')
                    except FileNotFoundError:
                        pass
            except FileNotFoundError:
                pass
        chips = list(Path('/sys/bus/platform/devices/6061000.pwm1/pwm').glob('pwmchip*'))
        if len(chips) != 1:
            raise RuntimeError('PWM1 controller driver must be bound')
        chip = chips[0]
        node = chip / 'pwm3'  # Physical PWM1 controller channel 3 = PWM7.
        if node.exists() or read32(pwm, 0x1044) & 1:
            raise RuntimeError('PWM7 is already exported or enabled')
        debug = Path('/sys/kernel/debug/pwm').read_text()
        section = debug.split('platform/6061000.pwm1, 4 PWM devices', 1)[1].split('\n\n', 1)[0]
        line = next(line for line in section.splitlines() if 'pwm-3' in line)
        if 'requested' in line or 'enabled' in line:
            raise RuntimeError('PWM7 is owned by another consumer')

        def untouched():
            return {
                'pads_a24_a25_a30': [read32(pad, o) for o in (0x0c, 0x18, 0x54)],
                'gpio_a31_config': read32(gpio, 128),
                'other_pwm_registers': {
                    str(ch): [read32(pwm, (ch // 4) * 4096 + o)
                              for o in (20 * (ch % 4), 8 + 20 * (ch % 4), 0xb0 + 4 * (ch % 4))]
                    for ch in range(7)},
                'bindings': {name: os.path.realpath('/sys/bus/platform/devices/' + name + '/driver')
                             for name in ('4850000.i2c', '6060000.pwm0', '6061000.pwm1')},
                'e907': [read32(core, o) for o in (0x24c, 0x54)],
            }

        before = untouched()
        old_pad = read32(pad, 0x60)
        save('before.json', dict(before, a31_pad=old_pad))
        exported = False
        pad_changed = False
        error = None
        elapsed = 0.0
        samples = []
        try:
            if cancelled:
                raise RuntimeError('Cancelled before output')
            write_value(chip / 'export', 3)
            exported = True
            write_value(node / 'period', 3003003)
            write_value(node / 'polarity', 'normal')
            write_value(node / 'duty_cycle', 1500000)
            pad_changed = True
            expected_pad = (old_pad & ~(7 << 16)) | (3 << 16)
            struct.pack_into('<I', pad, 0x60, expected_pad)
            if read32(pad, 0x60) != expected_pad or cancelled:
                raise RuntimeError('Mux failed or test cancelled')
            write_value(node / 'enable', 1)
            started = time.monotonic()
            print('ACTIVE: PWM7/A31 only; 333 Hz, 1500 us, 60 seconds; logs=' + str(logs), flush=True)
            while not cancelled and time.monotonic() - started < 60:
                low, high, control = (read32(pwm, o) for o in (0x103c, 0x10bc, 0x1044))
                if (low, high, control) != (36072, 36000, 31) or read32(pad, 0x60) != expected_pad:
                    raise RuntimeError('PWM7 register or mux check failed')
                if untouched() != before:
                    raise RuntimeError('Unrelated resource changed during test')
                samples.append(read32(pwm, 0x1040))
                time.sleep(0.1)
            elapsed = time.monotonic() - started
            if not cancelled and len(set(samples)) < 2:
                raise RuntimeError('PWM7 counter did not change')
        except BaseException as exc:
            error = str(exc)
            raise
        finally:
            cleanup_errors = []
            if exported:
                try:
                    write_value(node / 'enable', 0)
                    write_value(chip / 'unexport', 3)
                except OSError as exc:
                    cleanup_errors.append(str(exc))
            if pad_changed:
                struct.pack_into('<I', pad, 0x60, old_pad)
            after = untouched()
            save('after.json', dict(after, a31_pad=read32(pad, 0x60)))
            cleaned = (not cleanup_errors and after == before and read32(pad, 0x60) == old_pad
                       and not (read32(pwm, 0x1044) & 1) and not node.exists())
            result = dict(backend='Linux', pwm=7, pin='A31', frequency_hz=333, pulse_us=1500,
                          elapsed_s=elapsed, cancelled=cancelled, error=error,
                          cleanup_verified=cleaned, cleanup_errors=cleanup_errors,
                          unrelated_resources_unchanged=after == before,
                          counter_unique_samples=len(set(samples)), physical_waveform_verified=False)
            save('result.json', result)
            print(json.dumps(result), flush=True)
            if not cleaned:
                raise RuntimeError('Cleanup verification failed; inspect logs')
        return 130 if cancelled else 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='Start the bounded 60-second output')
    args = parser.parse_args()
    if not args.run:
        parser.print_help()
    else:
        try:
            raise SystemExit(run())
        except (OSError, RuntimeError) as exc:
            print('ERROR: ' + str(exc), flush=True)
            raise SystemExit(1)
