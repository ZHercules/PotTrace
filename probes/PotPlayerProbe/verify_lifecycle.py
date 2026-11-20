"""Multi-player, crash restart, user exit and persisted pause against real processes."""
import site,os,subprocess,time,json,struct,ctypes,sqlite3
from pathlib import Path
from probe_tools import TOOLS
site.addsitedir(str(TOOLS / 'python'))
import win32file,win32security,win32api,pywintypes
from window_probe import windows,u
from make_fixture import make
ROOT=Path(__file__).resolve().parents[2];DATA=ROOT/('out/lifecycle-data-'+str(time.time_ns()));DATA.mkdir()
APP=ROOT/'src/PotTrace.App/bin/Debug/net10.0-windows/PotTrace.exe'
PLAYER=ROOT/'out/player/PotPlayerMini64.exe';MEDIA=ROOT/'out/fixtures/lifecycle.avi';make(MEDIA,120)
(DATA/'settings.json').write_text('{"Mode":1}',encoding='utf-8')
(ROOT/'out/player/Extension/Playback/Statistics/pottrace-bridge.ini').write_text('[PotTrace]\nHostPath='+str(APP)+'\nDataDirectory='+str(DATA)+'\nChannel=lifecycle\n',encoding='utf-16')
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),DOTNET_ROOT=str(TOOLS / 'dotnet'))
sid=win32security.ConvertSidToStringSid(win32security.GetTokenInformation(win32security.OpenProcessToken(win32api.GetCurrentProcess(),8),win32security.TokenUser)[0])
session=ctypes.c_ulong();ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(),ctypes.byref(session))
pipe_name=rf'\\.\pipe\PotTrace.{sid}.{session.value}.lifecycle'
def start_player():
    info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=5
    return subprocess.Popen([str(PLAYER),str(MEDIA),'/new','/seek=0'],env=env,startupinfo=info)
def stop_player(p):
    if p.poll() is not None:return
    for w in windows(p.pid):
        if w['cls']=='PotPlayer64':u.SendMessageW(w['hwnd'],16,0,0)
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.terminate();p.wait()
def status():
    try:h=win32file.CreateFile(pipe_name,0xC0000000,0,None,3,0,None)
    except pywintypes.error:return None
    try:
        payload=json.dumps({'version':1,'type':'Control','command':'Status'}).encode()
        win32file.WriteFile(h,struct.pack('<I',len(payload))+payload)
        length=struct.unpack('<I',win32file.ReadFile(h,4)[1])[0]
        return json.loads(win32file.ReadFile(h,length)[1])
    except pywintypes.error:return None
    finally:win32file.CloseHandle(h)
def wait_for(test,seconds=12):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        time.sleep(.15)
    raise AssertionError('Condition did not become true')
def query(sql):
    with sqlite3.connect(DATA/'pottrace.db') as c:return c.execute(sql).fetchall()
def host_command(flag):
    info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
    return subprocess.Popen([str(APP),flag,'--data-dir',str(DATA),'--channel','lifecycle'],env=env,startupinfo=info)
players=[];report={}
try:
    players=[start_player(),start_player()]
    connected=wait_for(lambda:(s if (s:=status()) and s['playerCount']==2 else None))
    report['two_players_one_host_pid']=connected['processId'];time.sleep(13)
    count=query('SELECT count(*) FROM Session WHERE Valid=1')[0][0];assert count==2,count
    report['independent_valid_sessions']=count
    # Terminate only the PID returned by this test channel, then verify bounded relaunch/replay.
    handle=win32api.OpenProcess(1,False,connected['processId']);win32api.TerminateProcess(handle,99);handle.Close()
    recovered=wait_for(lambda:(s if (s:=status()) and s['processId']!=connected['processId'] and s['playerCount']==2 else None))
    time.sleep(2);assert query('SELECT count(*) FROM Session WHERE Valid=1')[0][0]==2
    report['crash_restart_without_duplicate']=True
    host_command('--exit').wait(timeout=5);wait_for(lambda:status() is None);time.sleep(5)
    assert status() is None;report['user_exit_suppresses_relaunch']=True
    host_command('--background');wait_for(lambda:(s if (s:=status()) and s['playerCount']==2 else None))
    report['manual_restart_reconnects']=True
    stop_player(players[0]);wait_for(lambda:(s if (s:=status()) and s['playerCount']==1 else None));assert status() is not None
    stop_player(players[1]);wait_for(lambda:status() is None,8);report['last_player_exits_host']=True
    before=query('SELECT count(*) FROM Session WHERE Valid=1')[0][0]
    (DATA/'settings.json').write_text('{"Mode":1,"RecordingPaused":true}',encoding='utf-8')
    players=[start_player()];wait_for(lambda:status());time.sleep(12)
    assert query('SELECT count(*) FROM Session WHERE Valid=1')[0][0]==before
    report['paused_setting_survives_restart']=True
finally:
    for p in players:stop_player(p)
    time.sleep(4)
(ROOT/'out/lifecycle-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
