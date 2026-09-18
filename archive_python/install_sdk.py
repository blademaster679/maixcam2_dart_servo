"""Integrate E907 sources into a local SDK, keeping a backup; never flash."""
import argparse
import hashlib
from pathlib import Path
import shutil

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk');a=p.parse_args()
    root=Path(a.sdk).resolve(); here=Path(__file__).resolve().parent
    driver=root/'riscv/drivers/pwm/drv_pwm.c'
    text=driver.read_text()
    extension=(here/'firmware/pwm_us_extension.c.inc').read_text()
    if 'int servo_pwm_set_us(' not in text:
        reference=(here/'research/riscv__drivers__pwm__drv_pwm.c').read_text()
        if text.replace('\r\n','\n') != reference.replace('\r\n','\n'):
            raise SystemExit('SDK driver differs from reviewed source. Review changes before integration.')
        backup=driver.with_suffix('.c.servo_backup')
        if backup.exists(): raise SystemExit('Backup exists: refusing overwrite')
        shutil.copy2(driver,backup)
        driver.write_text(text+extension)
    app=root/'riscv/applications/servo_bench.c'
    payload=(here/'firmware/servo_bench.c').read_bytes()
    if app.exists() and app.read_bytes()!=payload:
        raise SystemExit('Existing servo_bench.c differs; preserving it')
    app.write_bytes(payload)
    print('Integrated; no build or flash performed.')
    print('driver SHA256',hashlib.sha256(driver.read_bytes()).hexdigest())
    print('UART1 console relocation and Linux PWM ownership remain board integration prerequisites.')
if __name__=='__main__':main()
