"""Manual planar angle measurement. Records video observations, never PWM-derived angles."""
import argparse,csv,hashlib,math
from pathlib import Path

def angle(pivot,zero,tip):
    a=(zero[0]-pivot[0],pivot[1]-zero[1]);b=(tip[0]-pivot[0],pivot[1]-tip[1])
    if math.hypot(*a)<5 or math.hypot(*b)<5:raise ValueError('points too close to pivot')
    return math.degrees(math.atan2(a[0]*b[1]-a[1]*b[0],a[0]*b[0]+a[1]*b[1]))

def main():
    import cv2
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('video');p.add_argument('--servo',type=int,choices=range(1,5),required=True)
    p.add_argument('--seq',type=int,required=True,help='manually matched command sequence; 0 means unknown')
    p.add_argument('--frame',type=int,default=0);p.add_argument('--out',default='measurements.csv')
    a=p.parse_args();path=Path(a.video).resolve()
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    cap=cv2.VideoCapture(str(path))
    if not cap.isOpened():raise SystemExit('Cannot open video')
    fps=cap.get(cv2.CAP_PROP_FPS);total=int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if total<=0:raise SystemExit('Video frame count unavailable; transcode to seekable video first')
    frame=max(0,min(total-1,a.frame));points=[]
    cv2.namedWindow('measure',cv2.WINDOW_AUTOSIZE)
    def mouse(event,x,y,flags,param):
        if event==cv2.EVENT_LBUTTONDOWN:
            if len(points)<3:points.append((x,y))
            else:points[2]=(x,y)
    cv2.setMouseCallback('measure',mouse)
    out=Path(a.out);new=not out.exists() or out.stat().st_size==0
    fields=['video','sha256','servo','command_seq_manual','frame','reported_pts_ms','fps_metadata',
            'nominal_frame_time_s','pivot_x','pivot_y','zero_x','zero_y','tip_x','tip_y','angle_deg','source']
    try:
        with out.open('a',newline='',encoding='utf-8') as stream:
            w=csv.DictWriter(stream,fieldnames=fields)
            if new:w.writeheader()
            while True:
                cap.set(cv2.CAP_PROP_POS_FRAMES,frame);ok,img=cap.read()
                if not ok:raise RuntimeError('Frame read failed: '+str(frame))
                pts=cap.get(cv2.CAP_PROP_POS_MSEC)
                for i,pt in enumerate(points):
                    cv2.circle(img,pt,4,(0,255,0),-1);cv2.putText(img,str(i),pt,0,0.6,(0,255,0),1)
                label='click: pivot, ZERO direction, tip | A/D frame J/L 10frames S save R reset Q quit'
                cv2.putText(img,label,(8,22),0,0.45,(0,255,255),1)
                cv2.putText(img,f'frame={frame} servo={a.servo} command_seq={a.seq}',(8,45),0,0.6,(0,255,255),1)
                cv2.imshow('measure',img);key=cv2.waitKey(30)&255
                if key in (ord('q'),27):break
                if key==ord('r'):points.clear()
                if key in (ord('a'),ord('d'),ord('j'),ord('l')):
                    frame=max(0,min(total-1,frame+{ord('a'):-1,ord('d'):1,ord('j'):-10,ord('l'):10}[key]))
                    if len(points)==3:points.pop() # keep fixed pivot/zero, require new measured tip
                if key==ord('s') and len(points)==3:
                    try:deg=angle(*points)
                    except ValueError as e:print(e);continue
                    row=dict(video=str(path),sha256=h.hexdigest(),servo=a.servo,command_seq_manual=a.seq,
                        frame=frame,reported_pts_ms=pts,fps_metadata=fps,nominal_frame_time_s=frame/fps if fps>0 else '',
                        angle_deg=deg,source='manual_video_planar')
                    for prefix,pt in zip(('pivot','zero','tip'),points):row[prefix+'_x'],row[prefix+'_y']=pt
                    w.writerow(row);stream.flush();print('Saved measured angle',deg,'frame',frame)
    finally:cap.release();cv2.destroyAllWindows()
if __name__=='__main__':main()
