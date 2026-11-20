"""Diagnostic pipe receiver; fixture events only, never a production listener."""
import site,sys,struct,json,time,subprocess,os,ctypes
from pathlib import Path
from probe_tools import TOOLS
site.addsitedir(str(TOOLS / 'python'))
import win32pipe,win32file,win32api,win32security,win32process
from window_probe import windows,u
ROOT=Path(__file__).resolve().parents[2]
sid=win32security.ConvertSidToStringSid(win32security.GetTokenInformation(win32security.OpenProcessToken(win32api.GetCurrentProcess(),8),win32security.TokenUser)[0])
session_id=ctypes.c_ulong();ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(),ctypes.byref(session_id));session=session_id.value
name=rf'\\.\pipe\PotTrace.{sid}.{session}.integration'
h=win32pipe.CreateNamedPipe(name,3,0,1,65536,65536,0,None)
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=5 if '--visible' in sys.argv else 0
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),DOTNET_ROOT=str(TOOLS / 'dotnet'))
p=subprocess.Popen([str(ROOT/'out/player/PotPlayerMini64.exe'),str(ROOT/'out/fixtures/测试 影片.avi'),'/new','/seek=0'],env=env,startupinfo=startup)
def receive():
    n=struct.unpack('<I',win32file.ReadFile(h,4)[1])[0]
    return json.loads(win32file.ReadFile(h,n)[1])
def send(value):
    b=json.dumps(value).encode();win32file.WriteFile(h,struct.pack('<I',len(b))+b)
events=[]
try:
    win32pipe.ConnectNamedPipe(h,None);hello=receive();send({'version':1,'type':'Status'})
    start=time.monotonic();paused=False
    while time.monotonic()-start<13:
        batch=receive();events.extend(batch['events']);send({'version':1,'type':'Ack','ackSequence':batch['events'][-1]['sequence']})
        if not paused and time.monotonic()-start>6:
            rows=[r for r in windows(p.pid) if r['cls']=='PotPlayer64']
            if rows:u.SendMessageW(rows[0]['hwnd'],0x319,rows[0]['hwnd'],14<<16);paused=True
finally:
    for r in windows(p.pid):
        if r['cls']=='PotPlayer64':u.SendMessageW(r['hwnd'],16,0,0)
    p.wait(timeout=5);win32file.CloseHandle(h)
    (ROOT/'out/bridge-events.json').write_text(json.dumps(events,indent=2,ensure_ascii=False),encoding='utf-8')
    print([(e['kind'],e['monotonicMs']-events[0]['monotonicMs'],e['positionMs'],e['state']) for e in events])
