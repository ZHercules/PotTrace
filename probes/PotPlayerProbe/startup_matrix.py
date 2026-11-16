"""Repeated real-host startup boundary checks, with optional non-semantic trace stamps."""
import os,sys,time,json,sqlite3,subprocess,ctypes,argparse
from pathlib import Path
from window_probe import windows,u
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--runs',type=int,default=6);parser.add_argument('--stamp',action='store_true');parser.add_argument('--dwell',type=float,default=11)
parser.add_argument('--api',choices=['identity','thread'])
parser.add_argument('--workers',choices=['none','observer','sender','both'],default='both')
args=parser.parse_args();EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe'))).resolve()
if not EXE.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Only an isolated player under this repository out/ directory may be modified')
if args.runs<1 or args.dwell<=0:raise SystemExit('runs and dwell must be positive')
EXT=EXE.parent/'Extension/Playback/Statistics'
script=EXT/'PlaybackStatistics - PotTrace.as';before=script.read_bytes();ini=EXT/'pottrace-bridge.ini';config=ini.read_bytes()
clock=ctypes.windll.kernel32.GetTickCount64;clock.restype=ctypes.c_ulonglong
base=ROOT/('out/startup-matrix-'+str(time.time_ns()));base.mkdir();results=[]
def rows(db):
    if not db.exists():return []
    try:
        with sqlite3.connect(db) as c:return [json.loads(r[0]) for r in c.execute('SELECT Payload FROM Session')]
    except sqlite3.OperationalError:return []
try:
    source=before.decode('utf-8')
    if args.stamp:source=source.replace(' + position + ', ' + HostGetTickCount() + ')
    if args.api=='identity':source=source.replace('Send("Ready", "");','Send("Ready", HostGetPlayingFileName());')
    if args.api=='thread':
        source=source.replace('Send("Ready", "");','Send("Ready", ""); HostCreateThread(ProbeIdentity, 0);')
        source+='\nvoid ProbeIdentity(any@ param) { for(int i=0;i<100;i++) { Send("Identity",HostGetPlayingFileName());HostSleep(50);HostIncTimeOut(50); } }\n'
    script.write_text(source,encoding='utf-8')
    for run in range(args.runs):
        data=base/str(run);data.mkdir();(data/'settings.json').write_text('{"Mode":1}',encoding='utf-8')
        ini.write_text('[PotTrace]\nHostPath='+str(ROOT/'src/PotTrace.App/bin/Debug/net10.0-windows/PotTrace.exe')+'\nDataDirectory='+str(data)+'\nChannel=startupmatrix\n',encoding='utf-16')
        env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),DOTNET_ROOT=r'D:\codex\PotTrace\.tools\dotnet',POTTRACE_TEST_EVENT_TRACE=str(data/'events.jsonl'),POTTRACE_TEST_RAW_TRACE=str(data/'raw.tsv'))
        env['POTTRACE_TEST_WORKERS']=args.workers
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=5 if run%2==0 else 0
        start=clock();p=subprocess.Popen([str(EXE),str(ROOT/'out/fixtures/观看区间.avi'),'/new','/seek=0'],env=env,startupinfo=si);h=None
        try:
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                h=next((w['hwnd'] for w in windows(p.pid) if w['cls']=='PotPlayer64'),None)
                if h and u.SendMessageW(h,0x400,0x5006,0)==2:break
                time.sleep(.02)
            else:raise AssertionError('No playing state within 20s')
            ready=clock();u.SendMessageW(h,0x400,0x5007,1);paused=clock()
            time.sleep(args.dwell);checked=clock();observed=rows(data/'pottrace.db')
            report={'run':run,'visible':bool(run%2==0),'start':start,'ready':ready-start,'paused':paused-start,'checked':checked-start,'valid':sum(s['IsValid'] for s in observed),'stay':[s['StayMs'] for s in observed],'played':[s['PlayedMs'] for s in observed]}
            results.append(report);print(json.dumps(report),flush=True)
        finally:
            if h:u.SendMessageW(h,16,0,0)
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:p.terminate();p.wait()
            time.sleep(4)
    print('Evidence:',base,flush=True)
finally:
    script.write_bytes(before);ini.write_bytes(config);(base/'result.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
if not all(r['valid']==1 for r in results):raise SystemExit(1)
