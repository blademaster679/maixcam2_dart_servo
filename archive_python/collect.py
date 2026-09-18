"""Read E907 console telemetry into a new run directory. Sends no commands."""
import argparse,csv,json,re,time
from pathlib import Path
PATTERN=re.compile(r'SERVO,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(-?\d+)')
FIELDS=['seq','servo','pwm','pulse_us','frequency_hz','before_tick','after_tick','rc']

def parse(line):
    m=PATTERN.search(line)
    if not m:return None
    r=dict(zip(FIELDS,map(int,m.groups())))
    if not 1<=r['servo']<=4 or r['pwm']!=r['servo']+3:
        raise ValueError('unexpected servo/PWM mapping')
    if not 1400<=r['pulse_us']<=1600 or r['frequency_hz']!=333:
        raise ValueError('unexpected pulse/frequency')
    return r

def main():
    p=argparse.ArgumentParser(description=__doc__)
    g=p.add_mutually_exclusive_group(required=True);g.add_argument('--port');g.add_argument('--input')
    p.add_argument('--baud',type=int,default=115200,help='must match relocated console configuration')
    p.add_argument('--out',required=True);p.add_argument('--bec-v',type=float)
    a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=False)
    if a.port:
        import serial
        source=serial.Serial(a.port,a.baud,timeout=1)
    else:source=open(a.input,'rb')
    meta=dict(vars(a),clock='board ticks; host receive clock is NOT actuation time',
              tick_hz=None,started_unix=time.time(),measurement_source='video_manual',complete=False)
    previous=None
    try:
        with source,(out/'raw.log').open('wb') as raw,(out/'commands.csv').open('w',newline='') as csvfile:
            w=csv.DictWriter(csvfile,fieldnames=FIELDS+['host_receive_ns','measured_angle_deg']);w.writeheader()
            while True:
                line=source.readline()
                if not line:
                    if a.input:break
                    continue
                raw.write(line);raw.flush()
                text=line.decode(errors='replace')
                m=re.search(r'SERVO_META,tick_hz,(\d+)',text)
                if m:meta['tick_hz']=int(m.group(1))
                try:r=parse(text)
                except ValueError as e:print('Rejected:',e);continue
                if r:
                    if previous is not None and r['seq']!=previous+1:
                        print('WARNING sequence gap/restart',previous,r['seq'])
                    previous=r['seq'];r.update(host_receive_ns=time.monotonic_ns(),measured_angle_deg='')
                    w.writerow(r);csvfile.flush();print(text.strip())
                if 'SERVO_END,' in text:
                    meta['complete']=True;meta['end_record']=text.strip();break
    except KeyboardInterrupt:
        print('Collector stopped. This does NOT stop the board; issue servo_bench stop on its shell.')
    finally:
        (out/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
if __name__=='__main__':main()
