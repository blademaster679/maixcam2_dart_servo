#!/usr/bin/env python3
"""Read-only Linux-side inventory for the standard RT-Thread boot path.
Never starts E907 or writes peripheral registers. This does not assess the
separate experimental RAM/CMM loader in tools/e907_live_probe.c.
Exit 2 means standard RT-Thread prerequisites have not been established.
Run on the board: python3 board_preflight.py > board_preflight.json
"""
import datetime
import gzip
import json
from pathlib import Path
import platform


def read(path):
    try:
        return Path(path).read_bytes().decode(errors="replace").rstrip("\x00\n")
    except OSError:
        return None


def probe():
    try:
        config = gzip.decompress(Path('/proc/config.gz').read_bytes()).decode()
    except (OSError, EOFError):
        config = ''
    names = ('CONFIG_AXERA_RISCV_DRV', 'CONFIG_AX_RISCV_SUPPORT',
             'CONFIG_AX_RISCV_LOAD_ROOTFS', 'CONFIG_REMOTEPROC')
    flags = {}
    for name in names:
        flags[name] = next((line.split('=', 1)[1] for line in config.splitlines()
                            if line.startswith(name + '=')),
                           'disabled' if '# ' + name + ' is not set' in config else 'unknown')
    driver = Path('/sys/bus/platform/drivers/ax_riscv')
    bound = sorted(p.name for p in driver.iterdir()
                   if p.is_symlink() and p.name != 'module') if driver.is_dir() else []
    dt = Path('/proc/device-tree')
    nodes = []
    if dt.is_dir():
        for path in dt.rglob('*'):
            if path.is_dir() and any(s in path.name.lower() for s in ('riscv', 'remoteproc')):
                nodes.append({'path': str(path), 'status': read(path / 'status'),
                              'compatible': read(path / 'compatible')})
    blockers = []
    if flags['CONFIG_AX_RISCV_SUPPORT'] != 'y':
        blockers.append('Kernel AX_RISCV_SUPPORT is not verified enabled')
    if not bound:
        blockers.append('No bound ax_riscv platform device')
    # Even with kernel support, firmware, memory reservation and console need review.
    blockers.append('Matching E907 firmware, reserved memory and console must be verified before loading')
    return {
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'hostname': platform.node(), 'kernel': platform.release(),
        'model': read('/proc/device-tree/model'), 'image_version': read('/boot/ver'),
        'kernel_flags': flags, 'ax_riscv_bound_devices': bound,
        'riscv_dt_nodes': nodes,
        'pwm': read('/sys/kernel/debug/pwm'), 'iomem': read('/proc/iomem'),
        'cmdline': read('/proc/cmdline'), 'blockers': blockers,
        'e907_execution_tested_by_this_script': False,
        'deployment_path': 'standard RT-Thread boot; excludes experimental RAM loader',
        'scope': 'Read-only Linux preflight; no PWM outputs or firmware changes',
    }


if __name__ == '__main__':
    print(json.dumps(probe(), ensure_ascii=False, indent=2))
    raise SystemExit(2)
