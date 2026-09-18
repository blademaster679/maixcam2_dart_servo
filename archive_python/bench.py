"""Linux/MaixPy bench test, NOT E907 firmware. No inferred angle feedback."""
import argparse
import csv
import json
import math
from pathlib import Path
import time

FIELDS = ['seq', 'event', 'servo', 'pwm', 'pulse_us', 'frequency_hz',
          'before_ns', 'after_ns', 'measured_angle_deg', 'status']

def validate(c):
    f = c['frequency_hz']
    if f not in (50, 333):
        raise ValueError('frequency_hz must be 50 or 333')
    if not 0.5 <= c['hold_s'] <= 30 or not 1 <= c['repetitions'] <= 10:
        raise ValueError('invalid hold/repetitions')
    if not c['offsets_us'] or len(c['offsets_us']) > 100:
        raise ValueError('invalid offsets')
    if len(c['channels']) != 4:
        raise ValueError('exactly four channels required')
    mappings = [('B2',4), ('B3',5), ('A30',6), ('A31',7)]
    if sorted((x['pin'],x['pwm']) for x in c['channels']) != sorted(mappings):
        raise ValueError('expected B2/PWM4 B3/PWM5 A30/PWM6 A31/PWM7')
    if sorted(x['servo'] for x in c['channels']) != [1,2,3,4]:
        raise ValueError('servo IDs must be 1..4')
    for x in c['channels']:
        if not 500 <= x['min_us'] <= x['center_us'] <= x['max_us'] <= 2500:
            raise ValueError('invalid pulse bounds')
        for d in [0] + c['offsets_us']:
            p = x['center_us'] + d
            if not math.isfinite(p) or not x['min_us'] <= p <= x['max_us'] or p >= 1e6/f:
                raise ValueError('pulse outside configured limits')

def plan(c):
    for rep in range(c['repetitions']):
        for x in c['channels']:
            for d in c['offsets_us']:
                yield x, x['center_us'] + d

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--config', default=str(Path(__file__).with_name('config.json')))
    ap.add_argument('--out', required=True, help='new run directory; never overwrites')
    ap.add_argument('--run', action='store_true', help='actually enable MaixCAM2 PWM')
    args = ap.parse_args()
    c = json.loads(Path(args.config).read_text(encoding='utf-8'))
    validate(c)
    out = Path(args.out); out.mkdir(parents=True, exist_ok=False)
    c.update(mode='hardware_linux' if args.run else 'DRY_RUN', unix_start=time.time(),
             clock='time.monotonic_ns; command timestamps, not physical motion',
             measured_angle_source='video annotation; absent in commands.csv')
    (out/'metadata.json').write_text(json.dumps(c, ensure_ascii=False, indent=2),encoding='utf-8')
    devices = {}
    old_functions = {}
    seq = 0
    with (out/'commands.csv').open('w',newline='',encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS); writer.writeheader()
        def command(x, pulse, event):
            nonlocal seq
            seq += 1; before = time.monotonic_ns()
            status = 'DRY_RUN'
            try:
                if args.run:
                    devices[x['servo']].duty(pulse*c['frequency_hz']/10000.0)
                    status = 'API_OK_NOT_FEEDBACK'
            except Exception:
                status = 'API_ERROR'; raise
            finally:
                after = time.monotonic_ns()
                writer.writerow(dict(seq=seq,event=event,servo=x['servo'],pwm=x['pwm'],
                    pulse_us=pulse,frequency_hz=c['frequency_hz'],before_ns=before,
                    after_ns=after,measured_angle_deg='',status=status)); stream.flush()
                print(seq,event,'servo',x['servo'],'pulse_us',pulse,status,flush=True)
        try:
            if args.run:
                from maix import pwm, pinmap, err, sys as maixsys
                if maixsys.device_id() != 'maixcam2':
                    raise RuntimeError('This pin map is for MaixCAM2 only')
                for x in c['channels']:
                    old_functions[x['pin']] = pinmap.get_pin_function(x['pin'])
                    err.check_raise(pinmap.set_pin_function(x['pin'], 'PWM'+str(x['pwm'])), 'pinmux')
                    dev = pwm.PWM(x['pwm'],freq=c['frequency_hz'],duty=0,enable=False)
                    devices[x['servo']] = dev
                    command(x,x['center_us'],'initial_neutral')
                    err.check_raise(dev.enable(), 'enable PWM')
                time.sleep(3)
            for x,pulse in plan(c):
                command(x,pulse,'step')
                if args.run: time.sleep(c['hold_s'])
        except KeyboardInterrupt:
            print('Interrupted: disable PWM; no forced return movement.')
        finally:
            # Last normal step is neutral. On error/interrupt disable immediately.
            for sid,dev in devices.items():
                try: err.check_raise(dev.disable(), 'disable PWM')
                except Exception as e: print('DISABLE FAILED',sid,repr(e))
            if args.run and old_functions:
                for pin,func in old_functions.items():
                    try: err.check_raise(pinmap.set_pin_function(pin,func),'restore pinmux')
                    except Exception as e: print('PINMUX RESTORE FAILED',pin,repr(e))
            (out/'end.json').write_text(json.dumps({'unix_end':time.time(),'last_seq':seq}),encoding='utf-8')
    print('Saved',out)

if __name__ == '__main__': main()
