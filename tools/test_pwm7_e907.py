#!/usr/bin/env python3
"""PWM7/A31 only: E907 PWM, 333 Hz / 1500 us for 300 s, then restore.

Leases PWM1 only; no I2C unbind, GPIO direction change or PWM0 control.
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
import subprocess
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
    logs = Path(__file__).resolve().parent / 'servo_pwm7_e907_logs' / uuid.uuid4().hex[:12]
    logs.mkdir(parents=True)

    def save(name, value):
        (logs / name).write_text(json.dumps(value, indent=2) + '\n')

    with ExitStack() as stack:
        lock_fds = []
        for name in ('/run/lock/maixcam2-servo-scope.lock',
                     '/run/lock/maixcam2-servo-v11.lock', '/tmp/servo_e907.lock'):
            lock = stack.enter_context(open(name, 'a+'))
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            lock_fds.append(lock.fileno())
        fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
        stack.callback(os.close, fd)
        pad = stack.enter_context(mmap.mmap(fd, 4096, offset=0x02304000))
        pwm = stack.enter_context(mmap.mmap(fd, 8192, offset=0x06060000))
        clocks = stack.enter_context(mmap.mmap(fd, 4096, offset=0x04870000))
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
        if 'requested' in section or 'enabled' in section or any(chip.glob('pwm[0-9]*')):
            raise RuntimeError('PWM1 controller has an active consumer')
        if any(read32(pwm, 4096 + 8 + 20 * ch) & 1 for ch in range(4)):
            raise RuntimeError('PWM1 hardware has an enabled channel')

        def untouched():
            return {
                'pads_a24_a25_a30': [read32(pad, o) for o in (0x0c, 0x18, 0x54)],
                'gpio_a31_config': read32(gpio, 128),
                'other_pwm_registers': {
                    str(ch): [read32(pwm, (ch // 4) * 4096 + o)
                              for o in (20 * (ch % 4), 8 + 20 * (ch % 4), 0xb0 + 4 * (ch % 4))]
                    for ch in range(7)},
                'bindings': {name: os.path.realpath('/sys/bus/platform/devices/' + name + '/driver')
                             for name in ('4850000.i2c', '6060000.pwm0')},
            }

        before = untouched()
        old_pad = read32(pad, 0x60)
        save('before.json', dict(before, a31_pad=old_pad))
        old_regs = [read32(pwm, o) for o in (0x1044, 0x103c, 0x10bc)]
        old_clocks = [read32(clocks, o) for o in (0x08, 0x10)]
        old_core = [read32(core, o) for o in (0x0c, 0x30, 0x54, 0x24c)]
        save('saved_hardware.json', dict(pwm7=old_regs, clocks=old_clocks, core=old_core))
        driver = Path('/sys/bus/platform/drivers/axera-pwm')
        leased = False
        child = None
        error = None
        elapsed = 0.0
        samples = []
        expected_pad = (old_pad & ~(7 << 16)) | (3 << 16)
        def put(region, off, value):
            struct.pack_into('<I', region, off, value)
        try:
            if cancelled:
                raise RuntimeError('Cancelled before output')
            write_value(driver / 'unbind', '6061000.pwm1')
            leased = True
            base = Path(__file__).resolve().parent
            with (logs / 'e907.log').open('w') as log:
                child = subprocess.Popen([str(base / 'e907_pwm7_loader'), '--leased-pwm7',
                                          str(base / 'e907_pwm7.bin')], stdout=log, stderr=subprocess.STDOUT,
                                         pass_fds=lock_fds)
                deadline = time.monotonic() + 10
                while not read32(pwm, 0x1044) & 1:
                    if child.poll() is not None or cancelled or time.monotonic() >= deadline:
                        raise RuntimeError('E907 did not enable PWM7; inspect e907.log')
                    time.sleep(0.01)
                started = time.monotonic()
                print('ACTIVE: E907 PWM7/A31 only; 333 Hz, 1500 us, 300 seconds; logs=' + str(logs), flush=True)
                last_progress = started
                while child.poll() is None:
                    elapsed = time.monotonic() - started
                    if cancelled or elapsed > 310:
                        raise RuntimeError('Cancelled or duration exceeded')
                    low, high, control = (read32(pwm, o) for o in (0x103c, 0x10bc, 0x1044))
                    if (low, high) != (36072, 36000) or control not in (30, 31):
                        raise RuntimeError('PWM7 count/control check failed')
                    if control == 30 and elapsed < 299.8:
                        raise RuntimeError('PWM7 stopped early')
                    if read32(pad, 0x60) != expected_pad or untouched() != before:
                        raise RuntimeError('Mux or unrelated-resource check failed')
                    samples.append(read32(pwm, 0x1040))
                    if time.monotonic() - last_progress >= 30:
                        print('PROGRESS elapsed_s=%.1f control=%d' % (elapsed, control), flush=True)
                        last_progress = time.monotonic()
                    time.sleep(0.1)
                elapsed = time.monotonic() - started
                if child.returncode or not 299.8 <= elapsed <= 305 or len(set(samples)) < 2:
                    raise RuntimeError('E907 completion, duration or counter verification failed')
                if read32(pwm, 0x1044) & 1:
                    raise RuntimeError('E907 failed to stop PWM7')
        except BaseException as exc:
            error = str(exc)
            raise
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                # Never SIGKILL: loader must confirm E907 reset before freeing CMM.
                child.wait()
            reset_ok = read32(core, 0x54) & (3 << 16) == 3 << 16
            if not reset_ok:
                raise RuntimeError('E907 reset not confirmed; retain lease and power-cycle')
            if leased:
                put(pwm, 0x1044, 0x1e)
                put(pwm, 0x103c, old_regs[1]); put(pwm, 0x10bc, old_regs[2])
                put(pwm, 0x1044, old_regs[0])
                put(pad, 0x60, old_pad)
                put(clocks, 0xbc, (1 << 26) & ~old_clocks[0])
                put(clocks, 0xb8, (1 << 26) & old_clocks[0])
                put(clocks, 0xcc, 1 & ~old_clocks[1])
                put(clocks, 0xc8, 1 & old_clocks[1])
                write_value(driver / 'bind', '6061000.pwm1')
            after = untouched()
            save('after.json', dict(after, a31_pad=read32(pad, 0x60)))
            cleaned = (after == before and read32(pad, 0x60) == old_pad
                       and [read32(pwm, o) for o in (0x1044, 0x103c, 0x10bc)] == old_regs
                       and [read32(core, o) for o in (0x0c, 0x30, 0x54, 0x24c)] == old_core
                       and (read32(clocks, 0x08) ^ old_clocks[0]) & (1 << 26) == 0
                       and (read32(clocks, 0x10) ^ old_clocks[1]) & 1 == 0
                       and Path('/sys/bus/platform/devices/6061000.pwm1/driver').exists())
            result = dict(backend='E907', pwm=7, pin='A31', frequency_hz=333, pulse_us=1500,
                          elapsed_s=elapsed, cancelled=cancelled, error=error,
                          cleanup_verified=cleaned, unrelated_resources_unchanged=after == before,
                          counter_unique_samples=len(set(samples)), physical_waveform_verified=False)
            save('result.json', result)
            print(json.dumps(result), flush=True)
            if not cleaned:
                raise RuntimeError('Cleanup verification failed; inspect logs')
        return 130 if cancelled else 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='Start the bounded 300-second output')
    args = parser.parse_args()
    if not args.run:
        parser.print_help()
    else:
        try:
            raise SystemExit(run())
        except (OSError, RuntimeError) as exc:
            print('ERROR: ' + str(exc), flush=True)
            raise SystemExit(1)
