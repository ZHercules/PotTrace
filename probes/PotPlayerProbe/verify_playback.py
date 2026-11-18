"""Real seek/repeat/rate/queue/reopen acceptance using generated videos only."""
import os,time,subprocess,json,sqlite3,ctypes,sys
from pathlib import Path
from window_probe import windows,u,CB,info
from make_fixture import make
ROOT=Path(__file__).resolve().parents[2];DATA=ROOT/('out/playback-data-'+str(time.time_ns()));DATA.mkdir()
MEDIA=ROOT/'out/fixtures/观看区间.avi';QUEUED=ROOT/'out/fixtures/仅在队列.avi';make(MEDIA,120);make(QUEUED,120)
EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe')))
APP=Path(os.environ.get('POTTRACE_TEST_APP',str(ROOT/'src/PotTrace.App/bin/Debug/net10.0-windows/PotTrace.exe')))
(DATA/'settings.json').write_text('{"Mode":1}',encoding='utf-8')
(EXE.parent/'Extension/Playback/Statistics/pottrace-bridge.ini').write_text('[PotTrace]\nHostPath='+str(APP)+'\nDataDirectory='+str(DATA)+'\nChannel=playback\n',encoding='utf-16')
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),DOTNET_ROOT=r'D:\codex\PotTrace\.tools\dotnet',POTTRACE_TEST_EVENT_TRACE=str(DATA/'native-events.jsonl'))
if 'POTTRACE_TEST_APP' in os.environ:env.pop('DOTNET_ROOT',None)
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=5
def wait_for(fn,seconds=15):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if value:=fn():return value
        time.sleep(.1)
    raise AssertionError('Condition timed out')
def query(sql):
    with sqlite3.connect(DATA/'pottrace.db') as c:return c.execute(sql).fetchall()
def sessions():return [json.loads(x[0]) for x in query('SELECT Payload FROM Session WHERE Valid=1')]
def segments():return [json.loads(x[0]) for x in query('SELECT Payload FROM Segment')]
def pause():u.SendMessageW(h,0x400,0x5007,1);time.sleep(.7)
def play():u.SendMessageW(h,0x400,0x5007,2)
def seek(ms):u.SendMessageW(h,0x400,0x5005,ms);time.sleep(.7)
def command(value):u.SendMessageW(h,0x111,value,0)
def position():return u.SendMessageW(h,0x400,0x5004,0)
# /current is used only after proving there is no unrelated player window.
other=[]
@CB
def inspect(hwnd,l):
    if info(hwnd)['cls']=='PotPlayer64':other.append(hwnd)
    return True
u.EnumWindows(inspect,0)
if other:raise RuntimeError('Close the separate test players before this exclusive fixture run; no player was changed')
ini=EXE.parent/'PotPlayerMini64.ini'
ini.write_text('\n'.join(line for line in ini.read_text(encoding='utf-16').splitlines() if not line.startswith('PrevSpeed=')),encoding='utf-16')
p=subprocess.Popen([str(EXE),str(MEDIA),'/new','/seek=0'],env=env,startupinfo=startup)
report={}
try:
    h=wait_for(lambda:next((w['hwnd'] for w in windows(p.pid) if w['cls']=='PotPlayer64'),None))
    wait_for(lambda:u.SendMessageW(h,0x400,0x5006,0)==2);pause();report['paused_at_unix_ms']=int(time.time()*1000);time.sleep(11)
    assert len(sessions())==1;report['paused_open_is_valid']=True
    if '--startup-only' in sys.argv:
        print(json.dumps(report),flush=True)
        raise SystemExit(0)
    for repeat in range(2):seek(10000);play();time.sleep(8.4);pause()
    detail=segments();assert len(detail)>=2,detail
    overlap=[s for s in detail if s['StartMs']<=11000 and s['EndMs']>=17000]
    assert len(overlap)==2,detail;assert len(sessions())==1
    report['repeated_intervals_independent']=len(overlap)
    # Adding a different fixture to the queue must not create a media session.
    add=subprocess.Popen([str(EXE),str(QUEUED),'/add','/current'],env=env,startupinfo=startup);add.wait(timeout=10);time.sleep(1)
    assert all(s['SourcePath']==str(MEDIA) for s in sessions());report['queued_only_not_counted']=True
    command(10246)
    for _ in range(10):command(10248)
    seek(30000);before=sessions()[0]['PlayedMs'];start_pos=position();play();began=time.monotonic();time.sleep(12);pause();elapsed=(time.monotonic()-began-.7)*1000
    after=sessions()[0]['PlayedMs'];media_delta=position()-start_pos
    assert 21000<=media_delta<=27000,media_delta
    assert abs((after-before)-elapsed)<=1000,(after-before,elapsed,media_delta)
    report['double_speed_actual_ms']=after-before;report['double_speed_media_ms']=media_delta
    command(10246)
    reopen=subprocess.Popen([str(EXE),str(MEDIA),'/current','/seek=0'],env=env,startupinfo=startup);reopen.wait(timeout=10);time.sleep(12)
    assert len(sessions())==2,sessions();report['same_file_reopen_adds_one']=True
finally:
    for w in windows(p.pid):
        if w['cls']=='PotPlayer64':u.SendMessageW(w['hwnd'],16,0,0)
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.terminate();p.wait()
    time.sleep(4)
    name='startup-result.json' if '--startup-only' in sys.argv else 'playback-result.json'
    (ROOT/'out'/name).write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
