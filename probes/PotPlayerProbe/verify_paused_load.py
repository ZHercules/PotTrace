"""Capability probe, not a production playback gate.

Records first readable states before any post-load pause command, then tests
paused-to-new-file and natural playlist advance. Only generated silent media
and an isolated player under out/ are used. Fixture settings are restored.
"""
import argparse, ctypes, json, os, subprocess, time
from pathlib import Path
from window_probe import windows, u, CB, info
from make_fixture import make
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--variants',nargs='+',default=['baseline','pause-argument','prearmed','minimized','unfocused'])
parser.add_argument('--default-playback-mode',action='store_true')
args=parser.parse_args()
allowed={'baseline','pause-argument','prearmed','minimized','unfocused'}
if set(args.variants)-allowed:raise SystemExit('Unknown variant')
EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe'))).resolve()
if not EXE.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Isolated player required')
base=ROOT/('out/paused-load-'+str(time.time_ns()));base.mkdir()
ini=EXE.parent/'PotPlayerMini64.ini';before=ini.read_bytes()
ext=EXE.parent/'Extension/Playback/Statistics';config=ext/'pottrace-bridge.ini';oldconfig=config.read_bytes()
clock=ctypes.windll.kernel32.GetTickCount64;clock.restype=ctypes.c_ulonglong
u.IsIconic.argtypes=[ctypes.c_void_p];u.IsIconic.restype=ctypes.c_int
u.ShowWindow.argtypes=[ctypes.c_void_p,ctypes.c_int]
A=base/'首次载入.avi';B=base/'手动切换.avi';C=base/'episodes/episode-01.avi';D=base/'episodes/episode-02.avi'
for path,duration in [(A,30),(B,31),(C,4),(D,18)]:make(path,duration)
reports=[]

def guard(pid=None):
    others=[]
    @CB
    def scan(h,l):
        row=info(h)
        if row['cls']=='PotPlayer64' and row['pid']!=pid:others.append(row['pid'])
        return True
    u.EnumWindows(scan,0)
    if others:raise RuntimeError('Another player exists; no /current command will be sent')

def command_open(p,path,env):
    guard(p.pid)
    si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
    paths=path if isinstance(path,list) else [path]
    return subprocess.Popen([str(EXE),*[str(item) for item in paths],'/current','/seek=0'],env=env,startupinfo=si)

def snapshot(h):
    return {'at':clock(),'state':u.SendMessageW(h,0x400,0x5006,0),
            'position':u.SendMessageW(h,0x400,0x5004,0),'duration':u.SendMessageW(h,0x400,0x5002,0),'minimized':bool(u.IsIconic(h))}

def capture(h,seconds):
    rows=[];end=time.monotonic()+seconds
    while time.monotonic()<end:rows.append(snapshot(h));time.sleep(.01)
    return rows

def summarize(rows):
    active=[r for r in rows if r['state'] in (1,2) and r['duration']>0]
    return {'first_active':active[0] if active else None,'observed_playing':any(r['state']==2 for r in active),
            'max_position':max((r['position'] for r in active),default=0),'loaded':bool(active),
            'all_observed_active_paused':bool(active) and all(r['state']==1 for r in active)}

try:
    config.write_text('[PotTrace]\nChannel=pausedloadprobe\n',encoding='utf-16')
    for variant in args.variants:
        guard();data=base/variant;data.mkdir()
        text='\n'.join(line for line in before.decode('utf-16').splitlines() if not line.startswith(('PrevSpeed=','AutoPauseResume=','PauseOnMin=','RepeatPlay2=')))
        if args.default_playback_mode:text='\n'.join(line for line in text.splitlines() if not line.startswith('PlaybackMode='))
        additions={'minimized':'PauseOnMin=1','unfocused':'AutoPauseResume=1'}.get(variant,'')
        # Known repeat-playlist setting, verified via the real preferences UI.
        # The two episodes are supplied explicitly, avoiding temporary DPL albums.
        text=text.replace('[Settings]','[Settings]\nRepeatPlay2=1\n'+additions)
        ini.write_text(text,encoding='utf-16')
        env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),POTTRACE_TEST_WORKERS='observer',
                 POTTRACE_TEST_RAW_TRACE=str(data/'raw.tsv'),POTTRACE_TEST_EVENT_TRACE=str(data/'events.jsonl'))
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=6 if variant=='minimized' else 4 if variant=='unfocused' else 5
        command=[str(EXE),'/new']
        if variant!='prearmed':command.extend([str(A),'/seek=0'])
        if variant=='pause-argument':command.append('/pause')
        started=clock();p=subprocess.Popen(command,env=env,startupinfo=si);h=None;record={'player':str(EXE),'variant':variant,'default_playback_mode':args.default_playback_mode,'started':started}
        try:
            end=time.monotonic()+15
            while time.monotonic()<end:
                h=next((r['hwnd'] for r in windows(p.pid) if r['cls']=='PotPlayer64'),None)
                if h:break
                time.sleep(.01)
            else:raise RuntimeError('Owned window not created')
            child=None
            if variant=='prearmed':
                record['before_prearm']=snapshot(h);u.SendMessageW(h,0x400,0x5007,1);record['after_prearm']=snapshot(h)
                child=command_open(p,A,env)
            first=capture(h,4);record['startup']=summarize(first);record['startup_samples']=first
            if child:child.wait(timeout=5)
            # This pause is explicit and AFTER the initial capability observation.
            u.SendMessageW(h,0x400,0x5007,1);time.sleep(.15);record['before_switch']=snapshot(h)
            record['switch_requested']=clock();child=command_open(p,B,env)
            switched=capture(h,3);child.wait(timeout=5)
            # Exclude the old file's states while B is still opening.
            new=[r for r in switched if r['duration']==31000]
            record['switch']=summarize(new);record['switch_samples']=switched
            u.SendMessageW(h,0x400,0x5007,1)
            record['playlist_requested']=clock();child=command_open(p,[C,D],env)
            waiting=[];deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                row=snapshot(h);waiting.append(row)
                if row['duration']==4000 and row['state'] in (1,2):break
                time.sleep(.01)
            else:raise RuntimeError('Playlist first episode did not load')
            child.wait(timeout=5);record['playlist_first']=row
            # Explicitly play episode 1 to test its NATURAL completion into episode 2.
            record['episode_one_play_requested']=clock();u.SendMessageW(h,0x400,0x5007,2)
            advanced=[];deadline=time.monotonic()+12;second_at=None
            while time.monotonic()<deadline:
                row=snapshot(h);advanced.append(row)
                if row['duration']==18000 and row['state'] in (1,2):
                    if second_at is None:second_at=time.monotonic()
                    if time.monotonic()-second_at>2:break
                time.sleep(.01)
            second=[r for r in advanced if r['duration']==18000]
            record['auto_next']=summarize(second);record['auto_next_samples']=advanced
            record['playlist_open_samples']=waiting
            record['capability_passed']=all(record[stage]['all_observed_active_paused'] for stage in ('startup','switch','auto_next'))
            if not second:
                record['auto_next_failure_windows']=windows(p.pid,True)
                # Diagnostic only: never report a manual advance as natural advance.
                u.SendMessageW(h,0x319,h,11<<16)
                manual=capture(h,2)
                record['manual_next_diagnostic']=summarize([r for r in manual if r['duration']==18000])
                record['manual_next_samples']=manual
        finally:
            for w in windows(p.pid):
                if w['cls']=='PotPlayer64':u.SendMessageW(w['hwnd'],16,0,0)
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:p.terminate();p.wait()
            reports.append(record);(data/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
            print(json.dumps({k:v for k,v in record.items() if not k.endswith(('_samples','_windows'))},ensure_ascii=False),flush=True)
finally:
    ini.write_bytes(before);config.write_bytes(oldconfig)
    (base/'summary.json').write_text(json.dumps([{k:v for k,v in r.items() if not k.endswith(('_samples','_windows'))} for r in reports],indent=2),encoding='utf-8')
    print('Evidence:',base,flush=True)
if not reports or not all(r.get('capability_passed',False) for r in reports):raise SystemExit(1)
