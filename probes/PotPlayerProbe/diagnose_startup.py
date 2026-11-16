"""Compare callback entry, native observations and external control clocks.
Only the isolated test player and its temporary script/config are changed.
"""
import site,sys,struct,json,time,subprocess,os,ctypes,threading
from pathlib import Path
site.addsitedir(r'D:\codex\PotTrace\.tools\python')
import win32pipe,win32file,win32api,win32security
from window_probe import windows,u
ROOT=Path(__file__).resolve().parents[2]
EXT=ROOT/'out/player/Extension/Playback/Statistics'
script=EXT/'PlaybackStatistics - PotTrace.as';config=EXT/'pottrace-bridge.ini'
original=script.read_bytes();oldconfig=config.read_bytes()
log=EXT/'probe-native.tsv'
sid=win32security.ConvertSidToStringSid(win32security.GetTokenInformation(win32security.OpenProcessToken(win32api.GetCurrentProcess(),8),win32security.TokenUser)[0])
session=ctypes.c_ulong();ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(),ctypes.byref(session))
clock=ctypes.windll.kernel32.GetTickCount64;clock.restype=ctypes.c_ulonglong
name=rf'\\.\pipe\PotTrace.{sid}.{session.value}.diagnostic'
records=[]
try:
    instrument=original.decode('utf-8').replace('    if (enqueue == 0) return;', '''    if (enqueue == 0) return;
    uintptr probe = HostLoadLibrary(HostGetScriptFolder() + "\\\\PotTraceProbe.dll");
    uintptr log = HostGetProcAddress(probe,"Probe_CopyUtf8");
    string trace = "" + HostGetTickCount() + "\\t" + kind + "\\t" + path + "\\n";
    HostCallProcInt(log,"PI",HostString2UIntPtr(trace),trace.length());''')
    script.write_text(instrument,encoding='utf-8')
    config.write_text('[PotTrace]\nChannel=diagnostic\n',encoding='utf-16')
    for run in range(3):
        pipe=win32pipe.CreateNamedPipe(name,3,0,1,65536,65536,0,None)
        events=[];error=[]
        def serve():
            def receive():
                n=struct.unpack('<I',win32file.ReadFile(pipe,4)[1])[0]
                return json.loads(win32file.ReadFile(pipe,n)[1])
            def send(v):
                b=json.dumps(v).encode();win32file.WriteFile(pipe,struct.pack('<I',len(b))+b)
            try:
                win32pipe.ConnectNamedPipe(pipe,None);receive();send({'version':1,'type':'Status'})
                while True:
                    msg=receive();events.extend(msg['events']);send({'version':1,'type':'Ack','ackSequence':msg['events'][-1]['sequence']})
            except Exception as e:error.append(str(e))
        server=threading.Thread(target=serve,daemon=True);server.start()
        offset=log.stat().st_size
        env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'))
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=5
        start=clock();p=subprocess.Popen([str(ROOT/'out/player/PotPlayerMini64.exe'),str(ROOT/'out/fixtures/观看区间.avi'),'/new','/seek=0'],env=env,startupinfo=si)
        try:
            deadline=time.monotonic()+15;h=None
            while time.monotonic()<deadline:
                h=next((w['hwnd'] for w in windows(p.pid) if w['cls']=='PotPlayer64'),None)
                if h and u.SendMessageW(h,0x400,0x5006,0)==2:break
                time.sleep(.05)
            assert h
            playing=clock();u.SendMessageW(h,0x400,0x5007,1);paused=clock()
            time.sleep(11.1);end=clock()
        finally:
            if h:u.SendMessageW(h,16,0,0)
            p.wait(timeout=5);server.join(timeout=3);win32file.CloseHandle(pipe)
        with log.open('rb') as f:f.seek(offset);raw=f.read().decode('utf-8').splitlines()
        item={'run':run,'start':start,'playing':playing-start,'paused':paused-start,'end':end-start,'raw':raw,'events':events}
        records.append(item)
        print(json.dumps({**{k:v for k,v in item.items() if k!='events'},'events':[(e['kind'],e['monotonicMs']-start,e['state'],e['positionMs'],e['quality']) for e in events]},ensure_ascii=False),flush=True)
finally:
    script.write_bytes(original);config.write_bytes(oldconfig)
    (ROOT/'out/startup-diagnostic.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
