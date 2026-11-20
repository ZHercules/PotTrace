import os,subprocess,sys,site,time,json,ctypes,shutil,tempfile,zipfile
from pathlib import Path
import argparse,sqlite3,hashlib
parser=argparse.ArgumentParser()
parser.add_argument('--package',required=True,type=Path)
parser.add_argument('--fixture',required=True,type=Path)
parser.add_argument('--registry-fixture',type=Path,
                    help='1.7.23172.0 renderer description exported from a different Windows user')
args=parser.parse_args()
if not __debug__:raise RuntimeError('Acceptance checks require Python assertions')
if not (args.fixture/'PotPlayerMini64.ini').is_file():
    raise ValueError('Choose a disposable portable fixture with an existing INI')
descriptor=None
if args.registry_fixture:
    from registry_profile import load_ini,user_sid
    descriptor=json.loads(args.registry_fixture.read_text(encoding='utf-8'))
    if descriptor['source_sid']==user_sid():
        raise RuntimeError('Use a separate Windows user for registry migration playback tests')
root=Path(__file__).resolve().parents[2]
site.addsitedir(str(root/'.tools/python'))
import psutil
sys.path.insert(0,str(root/'probes/PotPlayerProbe'))
from window_probe import u,windows
case=Path(tempfile.mkdtemp(prefix='installed-collector-',dir=root/'out'))
exe=None
def hosts():
    return [p for p in psutil.process_iter(['name','exe']) if (p.info['name'] or '').lower()=='pottrace.exe']
if hosts():raise RuntimeError('Existing PotTrace; refusing shared channel')
pkg=args.package.resolve()
with zipfile.ZipFile(pkg) as z:z.extractall(case/'extracted')
package=next((case/'extracted').iterdir())
player=case/'隔离 播放器'
# The development fixture carries probe DLLs. Copy only the clean player inputs.
shutil.copytree(args.fixture,player,ignore=shutil.ignore_patterns('PotTrace.Bridge.dll','PlaybackStatistics - PotTrace.as','pottrace-bridge.ini','pottrace-install.json','PotTrace.Collector.ax','pottrace-collector.ini'))
ini=player/'PotPlayerMini64.ini'
original=ini.read_text(encoding='utf-16')
assert '[Override' not in original
report={}
def evidence(**values):
    report.update(values)
    (case/'acceptance.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if descriptor is not None:
    hashes={}
    for name in ('PotPlayerMini64.exe','PotPlayer64.dll'):
        hashes[name]=hashlib.sha256((player/name).read_bytes()).hexdigest()
        if hashes[name]!=descriptor['hashes'][name]:
            raise ValueError('Fixture and installed player binaries differ')
    renderer=descriptor['renderer']
    if type(renderer) is not int or not 0<=renderer<=0xffffffff:raise ValueError('Expected DWORD renderer selection')
    settings=load_ini(ini)
    settings.set('Settings','VideoRen2',str(renderer))
    with ini.open('w',encoding='utf-16') as stream:settings.write(stream,space_around_delimiters=False)
    evidence(player_hashes=hashes,renderer_setting=renderer,independent_windows_user=True)
    # This extracted installer can only target this disposable folder. Keep the
    # shipped package and its compatibility gate intact throughout the experiment.
    installer=package/'Install.ps1'
    code=installer.read_text(encoding='utf-8-sig')
    guard_line="if($dll.VersionInfo.FileVersion -notmatch '^1, 7, 22859, 0$')"
    if code.count(guard_line)!=1:raise ValueError('Unrecognized installer compatibility check')
    literal=str(player).replace("'","''")
    code=code.replace(guard_line,"if($PotPlayerPath -ne '"+literal+"' -or $dll.VersionInfo.FileVersion -notmatch '^1, 7, 23172, 0$')")
    installer.write_text(code,encoding='utf-8-sig')
with ini.open('a',encoding='utf-16') as f:f.write('\n[PotTraceFixture]\nOtherSetting=keep\n')
import imageio_ffmpeg
media=case/'验收.mp4'
subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-loglevel','error','-f','lavfi','-i','color=red:size=320x180:rate=30','-t','45','-c:v','libx264','-preset','ultrafast',str(media)],check=True)
env={k:v for k,v in os.environ.items() if not k.startswith('POTTRACE_')};env.update(LOCALAPPDATA=str(case/'Local'),WEBVIEW2_USER_DATA_FOLDER=str(case/'webview'))
def run(action):
    p=subprocess.run([os.environ['COMSPEC'],'/d','/c',str(package/(action+'.cmd')),str(player)],input=b'\r\n',env=env,timeout=90,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if p.returncode:raise RuntimeError(p.stdout.decode('gbk',errors='replace'))
def press(keys):
    for k in keys:u.keybd_event(k,0,0,0)
    time.sleep(.12)
    for k in reversed(keys):u.keybd_event(k,0,2,0)
def visible():
    for p in hosts():
        if Path(p.info['exe'] or '')==exe:
            for w in windows(p.pid):
                if '播放统计' in w['title'] and u.IsWindowVisible(w['hwnd']):return p,w['hwnd']
def wait_visible():
    for _ in range(300):
        result=visible()
        if result:return result
        time.sleep(.1)
    raise RuntimeError('Expected visible owned history window')
record=player/'Extension/Playback/Statistics/pottrace-install.json'
try:
    run('快速安装');r=json.loads(record.read_text(encoding='utf-8-sig'));exe=next(Path(f) for f in r['AppFiles'] if Path(f).name=='PotTrace.exe');menu=Path(r['MenuShortcut'])
    assert menu.exists()
    evidence(installation=True)
    if descriptor is not None:
        with (record.parent/'pottrace-collector.ini').open('a',encoding='utf-16') as stream:
            stream.write('Diagnostics=1\n')
    print('installed',case,flush=True)
    time.sleep(2);press([17,18,16,72]);p,h=wait_visible();print('cold hotkey visible',p.pid,flush=True)
    from pywinauto import Application
    main=Application(backend='uia').connect(process=p.pid).window(title='PotTrace · 播放统计')
    main.child_window(title='设置',control_type='MenuItem').invoke()
    enabled=main.child_window(auto_id='CaptureEnabled',control_type='CheckBox')
    if not enabled.get_toggle_state():enabled.toggle()
    main.child_window(auto_id='Mode',control_type='ComboBox').select(1)
    main.child_window(title='保存设置',control_type='Button').invoke()
    time.sleep(.6)
    assert json.loads((Path(r['DataRoot'])/'capture-policy.json').read_text())['Enabled']
    u.PostMessageW(h,16,0,0)
    for _ in range(20):
        if not visible():break
        time.sleep(.05)
    assert not visible()
    press([17,18,72]);p2,h2=wait_visible();assert p2.pid==p.pid
    evidence(cold_and_warm_hotkeys=True)
    print('warm hotkey visible same pid',p.pid,flush=True)
    subprocess.run([str(exe),'--exit'],env=env,check=True,timeout=12)
    p.wait(timeout=10)
    sys.path.insert(0,str(root/'probes/DirectShowProbe'))
    from run_collector_probe import guard,send
    guard()
    playing=subprocess.Popen([str(player/'PotPlayerMini64.exe'),str(media),'/new','/seek=0'],env=env)
    try:
        db=Path(r['DataRoot'])/'pottrace.db'
        def records(table):
            with sqlite3.connect(db) as con:return [json.loads(v) for v, in con.execute('SELECT Payload FROM '+table)]
        for _ in range(200):
            handle=next((w['hwnd'] for w in windows(playing.pid) if w['cls']=='PotPlayer64'),None)
            if handle:
                try:
                    if send(handle,0x400,0x5006)==2:break
                except TimeoutError:pass
            if playing.poll() is not None:raise RuntimeError('Owned player exited during startup')
            time.sleep(.1)
        else:raise TimeoutError('Playback did not start')
        modules={Path(m.path) for m in psutil.Process(playing.pid).memory_maps()}
        for dll in ['PotTrace.Collector.ax','PotTrace.Bridge.dll']:assert record.parent/dll in modules,dll
        evidence(collector_and_bridge_loaded=True)
        time.sleep(7);send(handle,0x400,0x5007,1);time.sleep(1)
        def active():
            return next((row for row in records('Session') if row['SourcePath']==str(media) and row['EndedUtc'] is None),None)
        before=active();time.sleep(11);after=active()
        evidence(pause_state=send(handle,0x400,0x5006),session_count=len(records('Session')),segment_count=len(records('Segment')))
        health=record.parent/'collector-health.json'
        if health.is_file():evidence(collector_health=json.loads(health.read_text()))
        if before is None or after is None:raise AssertionError('No active session for the played test file; see acceptance.json')
        assert after['Mode']==1 and after['IsValid'] and before['PlayedMs']==after['PlayedMs'] and after['StayMs']-before['StayMs']>10000,(before,after)
        segments=[seg for seg in records('Segment') if seg['SessionId']==after['SessionId']]
        assert len(segments)==1 and segments[0]['PlayedMs']>5000 and segments[0]['EndMs']>segments[0]['StartMs'],segments
        evidence(detailed_session=True,played_ms=after['PlayedMs'],paused_stay_delta_ms=after['StayMs']-before['StayMs'])
        press([17,18,72]);ui,h3=wait_visible();assert ui.pid!=p.pid
        print('installed AX/Bridge loaded; detailed record',after['PlayedMs'],segments[0]['StartMs'],segments[0]['EndMs'],flush=True)
        print('player host hotkey visible',ui.pid,flush=True)
    finally:
        for win in windows(playing.pid):
            if win['cls']=='PotPlayer64':u.PostMessageW(win['hwnd'],16,0,0)
        playing.wait(timeout=15)
    assert playing.returncode==0
finally:
    if exe and any(Path(p.info['exe'] or '')==exe for p in hosts()):
        subprocess.run([str(exe),'--exit','--data-dir',r['DataRoot']],env=env,timeout=12,check=True)
        for _ in range(100):
            if not any(Path(p.info['exe'] or '')==exe for p in hosts()):break
            time.sleep(.1)
    if record.exists() and exe is not None:
        run('快速卸载')
        assert not record.exists() and not exe.exists() and not menu.exists()
        assert not (record.parent/'PotTrace.Collector.ax').exists()
        restored=ini.read_text(encoding='utf-16')
        assert 'A5092F25' not in restored and '[Override' not in restored and 'OtherSetting=keep' in restored
        with sqlite3.connect(Path(r['DataRoot'])/'pottrace.db') as con:
            assert con.execute('PRAGMA integrity_check').fetchone()==('ok',)
        evidence(uninstalled=True,database_integrity=True)
        print('uninstalled; database retained:',(Path(r['DataRoot'])/'pottrace.db').exists(),flush=True)
