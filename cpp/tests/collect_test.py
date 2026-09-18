import csv,json,pathlib,subprocess,sys,tempfile
exe,repo=sys.argv[1:]
with tempfile.TemporaryDirectory() as tmp:
 root=pathlib.Path(tmp)
 def collect(text,code=0):
  n=len(list(root.iterdir()));p=root/f'{n}.log';p.write_text(text);out=root/f'{n}_out'
  r=subprocess.run([exe,'--input',str(p),'--out',str(out)],capture_output=True)
  assert r.returncode==code,(r.returncode,r.stderr)
  assert (out/'raw.log').read_text()==text
  return list(csv.DictReader((out/'commands.csv').open())),json.loads((out/'metadata.json').read_text())
 for name,count in [('e907_single_servo2_video_20260918_183642.log',9),('e907_four_servos_20260918_193218.log',36)]:
  rows,meta=collect((pathlib.Path(repo)/'reports'/name).read_text())
  assert len(rows)==count and meta['complete']
  assert all(not r['submit_monotonic_ns'] and not r['host_receive_ns'] for r in rows)
 rows,meta=collect('BENCH_CMD,2,15,1600,333,1000000,1100000,4294967295,0\nBENCH_CMD,3,15,1500,333,2000000,2100000,5,0\nBENCH_END,0\nRESTORE evidence\n')
 assert len(rows)==8 and meta['complete'] and meta['sequence_gaps']==0
 assert [r['servo'] for r in rows[:4]]==['1','2','3','4']
 assert rows[0]['submit_monotonic_ns']=='1000000' and rows[4]['e907_snapshot_tick']=='5'
 rows,meta=collect('BENCH mask=0xf pulse_us=1600 (all selected outputs verified)\nPASS: check\n')
 assert len(rows)==4 and not meta['complete']
 rows,meta=collect('SERVO_META,tick_hz,1000\nSERVO,2,1,4,1522,333,10,11,0\nSERVO_END,done\n')
 assert len(rows)==1 and rows[0]['before_tick']=='10' and meta['tick_hz']==1000 and meta['complete']
 for bad in ['BENCH_CMD,2,16,1600,333,1,2,3,0','BENCH_CMD,2,15,1700,333,1,2,3,0','BENCH_CMD,2,15,1500,333,2,1,3,0','BENCH_CMD,broken']:
  rows,meta=collect(bad+'\nBENCH_END,0\n',1);assert not rows and not meta['complete']
 rows,meta=collect('BENCH_CMD,2,15,1500,333,1,2,3,-1\nBENCH_END,1\n')
 assert len(rows)==4 and not meta['complete']
 rows,meta=collect('BENCH_CMD,2,15,1500,333,1,2,3,0');assert not rows and not meta['complete']
print('PASS: legacy logs, grouped commands, clocks, end status, malformed/truncated input')
