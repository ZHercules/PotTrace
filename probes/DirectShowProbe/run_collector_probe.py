"""Production AX smoke/control/churn checks, only in a disposable player copy."""
import argparse, ctypes, hashlib, json, os, site, sqlite3, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'probes/PotPlayerProbe'))
from window_probe import windows,u,CB,info
site.addsitedir(r'D:\codex\PotTrace\.tools\python')
import imageio_ffmpeg, psutil
from run_stable_suite import validate_switch

clock=ctypes.windll.kernel32.GetTickCount64;clock.restype=ctypes.c_ulonglong
u.SendMessageTimeoutW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t,ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_size_t)]
u.SendMessageTimeoutW.restype=ctypes.c_ssize_t
def send(h,message,w=0,l=0):
    result=ctypes.c_size_t()
    if not u.SendMessageTimeoutW(h,message,w,l,3,400,ctypes.byref(result)):raise TimeoutError('Owned player message timed out')
    return ctypes.c_ssize_t(result.value).value
def guard(owned=None):
    others=[]
    @CB
    def each(h,l):
        row=info(h)
        if row['cls']=='PotPlayer64' and row['pid']!=owned:others.append(row['pid'])
        return True
    u.EnumWindows(each,0)
    if others:raise RuntimeError('Another player exists; refusing /current control')
def record(path):
    if not path.exists():return []
    with sqlite3.connect(path,timeout=2) as db:return [json.loads(r[0]) for r in db.execute('SELECT Payload FROM Session')]
def resources(pid):
    p=psutil.Process(pid)
    return dict(at=clock(),threads=p.num_threads(),handles=p.num_handles(),private_bytes=p.memory_info().private)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--switches',type=int,default=0);parser.add_argument('--controls',action='store_true');parser.add_argument('--hold',type=int,default=0);parser.add_argument('--baseline',action='store_true');parser.add_argument('--ui-controls',action='store_true');parser.add_argument('--restarts',type=int,default=1)
    parser.add_argument('--first-enable',action='store_true');parser.add_argument('--auto-next',action='store_true')
    args=parser.parse_args();guard()
    exe=(ROOT/'out/player/PotPlayerMini64.exe').resolve()
    if not exe.is_relative_to((ROOT/'out').resolve()):raise RuntimeError('Only isolated player allowed')
    base=ROOT/('out/collector-probe-'+str(time.time_ns()));base.mkdir();data=base/'data';data.mkdir()
    source=base/'a.mp4';other=base/'b.mp4'
    episode=base/'episode-1.mp4';next_episode=base/'episode-2.mp4'
    fixtures=[(source,'red',60),(other,'blue',61)]
    if args.auto_next:fixtures.extend([(episode,'red',4),(next_episode,'blue',18)])
    for path,color,seconds in fixtures:
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-f','lavfi','-i',f'color=c={color}:size=320x180:rate=30','-t',str(seconds),'-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-y',str(path)],check=True,timeout=30)
    ini=exe.with_suffix('.ini');extension=exe.parent/'Extension/Playback/Statistics';bridge=extension/'PotTrace.Bridge.dll';bridge_ini=extension/'pottrace-bridge.ini'
    old={p:p.read_bytes() if p.exists() else None for p in (ini,bridge,bridge_ini)}
    ax=base/'PotTrace.Collector.ax';ax.write_bytes((ROOT/'out/collector/Release/PotTrace.Collector.ax').read_bytes())
    host=ROOT/'src/PotTrace.App/bin/Release/net10.0-windows/PotTrace.exe'
    channel='collector'+str(time.time_ns());policy=data/'capture-policy.json'
    def set_policy(enabled,paused,revision):
        temp=policy.with_suffix('.tmp');temp.write_text(json.dumps(dict(Enabled=enabled,RecordingPaused=paused,Revision=revision)),encoding='utf-8');temp.replace(policy)
    if not args.first_enable:set_policy(True,False,1)
    (data/'settings.json').write_text('{"Mode":1}',encoding='utf-8')
    config=base/'pottrace-collector.ini'
    config.write_text('[PotTrace]\nDiagnostics=1\nBridgePath='+str(bridge)+'\nPolicyPath='+str(policy)+'\nPlayerIni='+str(ini)+'\n',encoding='utf-16')
    env={k:v for k,v in os.environ.items() if not k.startswith('POTTRACE_')}
    env.update(DOTNET_ROOT=r'D:\codex\PotTrace\.tools\dotnet',WEBVIEW2_USER_DATA_FOLDER=str(base/'webview'))
    report=dict(command=sys.argv,host_sha256=digest(host),player=str(exe),player_sha256=digest(exe),collector_sha256=digest(ax),channel=channel,baseline=args.baseline,deployment_eligible=False,phases=[],checks={},resources=[])
    player=None;db=data/'pottrace.db'
    def phase(name):
        health=base/'collector-health.json'
        report['phases'].append(dict(name=name,at=clock(),sessions=record(db),health=json.loads(health.read_text()) if health.exists() else None))
    def wait_window(p):
        end=time.monotonic()+20
        while time.monotonic()<end:
            found=next((w['hwnd'] for w in windows(p.pid) if w['cls']=='PotPlayer64'),None)
            if found:return found
            if p.poll() is not None:raise RuntimeError(f'Player exited before window: code={p.returncode}')
            time.sleep(.03)
        raise TimeoutError('No player window')
    def wait_play(h):
        end=time.monotonic()+15
        while time.monotonic()<end:
            try:
                if send(h,0x400,0x5006)==2:return
            except TimeoutError:
                report.setdefault('startup_query_timeouts',[]).append(clock())
            time.sleep(.03)
        raise TimeoutError('Player did not play')
    def close(p,h):
        u.PostMessageW(h,16,0,0);p.wait(timeout=10)
        if p.returncode!=0:raise RuntimeError('Abnormal player exit')
    try:
        text=old[ini].decode('utf-16')
        if '[Override\\' in text:raise RuntimeError('Fixture already has overrides')
        text='\n'.join(line for line in text.splitlines() if not line.startswith(('PrevSpeed=','PauseOnMin=','AutoPauseResume=','RepeatPlay2=','SeamlessPlay=')))
        text=text.replace('[Settings]','[Settings]\nRepeatPlay2=1\nSeamlessPlay=0')
        text+='\n[Override\\0000]\nType=1\nPath='+str(ax)+'\nCLSID={A5092F25-09C8-46EB-9138-638E3C0B4287}\nName=PotTrace Collector\nDisabled=0\nFilterType=0\nMerit=2097152\nMeritHi=0\nmod0000={73646976-0000-0010-8000-00AA00389B71}\nmod0001={00000000-0000-0000-0000-000000000000}\n[Override\\0001]\nType=-2\n'
        if args.baseline:text=text[:text.index('\n[Override\\')]
        ini.write_text(text,encoding='utf-16');(base/'configured.ini').write_bytes(ini.read_bytes())
        bridge.write_bytes((ROOT/'out/native/Release/PotTrace.Bridge.dll').read_bytes());report['bridge_sha256']=digest(bridge)
        bridge_ini.write_text('[PotTrace]\nSourceMode=DirectShow\nCaptureBackend=Collector\nHostPath='+str(host)+'\nDataDirectory='+str(data)+'\nChannel='+channel+'\n',encoding='utf-16')
        report['config_sha256']=digest(ini);report['collector_config_sha256']=digest(config);report['bridge_config_sha256']=digest(bridge_ini);report['start']=clock()
        player=subprocess.Popen([str(exe),str(source),'/new','/seek=0'],env=env)
        h=wait_window(player);wait_play(h);report['playing_seen']=clock()
        if args.first_enable:
            time.sleep(1);phase('before_first_enable')
            report['checks']['missing_policy_disabled']=not policy.exists() and not record(db) and not report['phases'][-1]['health']['enabled']
            subprocess.run([str(host),'--collector','--channel',channel,'--data-dir',str(data)],env=env,check=True,timeout=10)
            from pywinauto import Application
            host_pid=next(p.pid for p in psutil.process_iter(['name','cmdline']) if p.info['name']=='PotTrace.exe' and channel in (p.info['cmdline'] or []))
            application=Application(backend='uia').connect(process=host_pid)
            main=application.window(title='PotTrace · 播放统计');main.wait('visible',timeout=10)
            main.child_window(title='设置',control_type='MenuItem').invoke()
            checkbox=main.child_window(auto_id='CaptureEnabled',control_type='CheckBox');checkbox.wait('visible',timeout=5);checkbox.toggle()
            main.child_window(title='保存设置',control_type='Button').invoke();time.sleep(.6)
            saved=json.loads(policy.read_text(encoding='utf-8'))
            report['checks']['first_enable_saved']=saved['Enabled'] and not saved['RecordingPaused'] and saved['Revision']==1
            main.close();phase('first_enabled')
        time.sleep(6.3);send(h,0x400,0x5007,1);time.sleep(1);phase('paused_start')
        modules=[m.path for m in psutil.Process(player.pid).memory_maps()]
        report['checks']['production_ax_loaded']=any(Path(m)==ax for m in modules)
        time.sleep(10.3);phase('paused_end');report['resources'].append(resources(player.pid))
        rows=record(db);report['checks']['online_record']=len(rows)==1 and rows[0]['IsValid'] and rows[0]['PlayedMs']>5000
        before=next(p['sessions'] for p in report['phases'] if p['name']=='paused_start');after=rows
        report['checks']['pause_does_not_add_play']=bool(before and after and after[0]['PlayedMs']==before[0]['PlayedMs'] and after[0]['StayMs']>before[0]['StayMs']+10000)
        if args.baseline:report['checks']={'baseline_no_collector':not any(Path(m)==ax for m in modules),'baseline_no_graph_history':not rows}
        if args.hold:
            print(json.dumps({'hold_pid':player.pid,'hwnd':h,'evidence':str(base),'seconds':args.hold}),flush=True);time.sleep(args.hold)
        if args.ui_controls and not args.baseline:
            from pywinauto import Application
            u.PostMessageW(h,0x100,0x74,0);u.PostMessageW(h,0x101,0x74,0);time.sleep(.5)
            application=Application(backend='win32').connect(process=player.pid)
            dialog=application.window(class_name='#32770',title_re='Preferences.*')
            listing=dialog.child_window(control_id=3242,class_name='SysListView32').wrapper_object()
            if listing.item_count()!=1 or not listing.get_item(0).is_checked():raise RuntimeError('Unexpected isolated plugin list')
            try:
                listing.get_item(0).click(where='check');dialog.child_window(control_id=3043,class_name='Button').click();time.sleep(.7);phase('ui_disabled')
                time.sleep(2);phase('ui_disabled_hold')
                a,b=report['phases'][-2:]
                report['checks']['ui_disable_stops_capture']=not a['health']['enabled'] and not b['health']['enabled'] and sum(s['StayMs'] for s in a['sessions'])==sum(s['StayMs'] for s in b['sessions'])
            finally:
                if not listing.get_item(0).is_checked():listing.get_item(0).click(where='check')
                dialog.child_window(control_id=3043,class_name='Button').click();time.sleep(.7);phase('ui_reenabled')
                report['checks']['ui_reenable_resumes']=report['phases'][-1]['health']['enabled']
                dialog.child_window(control_id=1,class_name='Button').click()
        if args.controls and not args.baseline:
            phase('before_disable');set_policy(False,False,2);time.sleep(.5);phase('disabled_settled');send(h,0x400,0x5007,2);time.sleep(2);phase('disabled_playing')
            a=report['phases'][-2]['sessions'];b=report['phases'][-1]['sessions'];report['checks']['disabled_no_play']=sum(r['PlayedMs'] for r in a)==sum(r['PlayedMs'] for r in b)
            set_policy(True,False,3);time.sleep(6.3);phase('enabled_again');report['checks']['reenable_new_observed_session']=len(record(db))>=2
        for i in range(args.switches):
            guard(player.pid);target=other if i%2==0 else source
            child=subprocess.Popen([str(exe),str(target),'/current','/seek=0'],env=env);child.wait(timeout=8)
            time.sleep(.18)
            if not args.baseline:
                deadline=time.monotonic()+3;row={}
                while time.monotonic()<deadline:
                    active=next((r for r in record(db) if r['EndedUtc'] is None and r['SourcePath']==str(target)),None)
                    row=dict(index=i,at=clock(),expected_source=str(target),source=active['SourcePath'] if active else None,
                             duration_ms=send(h,0x400,0x5002),expected_duration_ms=61000 if target==other else 60000,
                             health=json.loads((base/'collector-health.json').read_text()))
                    if not validate_switch(row):break
                    time.sleep(.05)
                report.setdefault('switch_evidence',[]).append(row)
                if validate_switch(row):raise AssertionError('Switch validation failed: '+str(validate_switch(row)))
            if i%10==9:report['resources'].append(resources(player.pid))
        if args.switches:
            time.sleep(1);phase('after_switches')
            if not args.baseline:
                report['checks']['every_switch_observed']=len(report['switch_evidence'])==args.switches
                before=sum(r['PlayedMs'] for r in record(db));time.sleep(1.2)
                report['checks']['post_churn_recording']=sum(r['PlayedMs'] for r in record(db))>before+800
                send(h,0x319,h,13<<16);time.sleep(1);phase('stopped_after_churn')
                report['checks']['retired_slots_reclaimed']=report['phases'][-1]['health']['slots']==0
                guard(player.pid);child=subprocess.Popen([str(exe),str(other),'/current','/seek=0'],env=env);child.wait(timeout=8);wait_play(h)
        if args.auto_next and not args.baseline:
            guard(player.pid);child=subprocess.Popen([str(exe),str(episode),str(next_episode),'/current','/seek=0'],env=env);child.wait(timeout=8);wait_play(h)
            time.sleep(.5);send(h,0x400,0x5007,1);time.sleep(.5);phase('episode_paused')
            time.sleep(10.5);phase('episode_paused_hold')
            first=next((r for r in record(db) if r['SourcePath']==str(episode) and r['EndedUtc'] is None),None)
            before=next((r for r in report['phases'][-2]['sessions'] if r['SourcePath']==str(episode)),None)
            report['checks']['queued_episode_not_recorded']=not any(r['SourcePath']==str(next_episode) for r in record(db))
            report['checks']['episode_pause_counted']=bool(first and before and first['IsValid'] and first['StayMs']>before['StayMs']+10000 and first['PlayedMs']==before['PlayedMs'] and abs(send(h,0x400,0x5002)-4000)<100)
            report['episode_resume_at']=clock();send(h,0x400,0x5007,2);deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                if abs(send(h,0x400,0x5002)-18000)<100:
                    report.setdefault('next_player_seen_at',clock())
                    if any(r['SourcePath']==str(next_episode) and r['EndedUtc'] is None for r in record(db)):break
                time.sleep(.03)
            report['next_seen_at']=clock();phase('automatic_next')
            report['checks']['automatic_next_identity']=abs(send(h,0x400,0x5002)-18000)<100 and any(r['SourcePath']==str(next_episode) and r['EndedUtc'] is None for r in record(db))
            time.sleep(10.5);send(h,0x400,0x5007,1);time.sleep(.5);phase('automatic_next_valid')
            report['checks']['next_valid_playback']=any(r['SourcePath']==str(next_episode) and r['IsValid'] and r['PlayedMs']>10000 for r in record(db))
        if args.controls and not args.baseline:
            phase('before_user_exit')
            subprocess.run([str(host),'--channel',channel,'--data-dir',str(data),'--exit'],env=env,check=True,timeout=10)
            time.sleep(2);phase('user_exit_settled');send(h,0x400,0x5007,2);time.sleep(4);phase('after_user_exit')
            owned_hosts=[p.pid for p in psutil.process_iter(['name','cmdline']) if p.info['name']=='PotTrace.exe' and channel in (p.info['cmdline'] or [])]
            report['checks']['user_exit_no_restart']=not owned_hosts
            a=report['phases'][-2]['sessions'];b=report['phases'][-1]['sessions'];report['checks']['user_exit_no_record']=sum(r['PlayedMs'] for r in a)==sum(r['PlayedMs'] for r in b)
        close(player,h);report['checks']['normal_exit']=True;player=None;time.sleep(4)
        report['first_run_sessions']=record(db)
        if not args.hold:
            for restart in range(args.restarts):
                guard();player=subprocess.Popen([str(exe),str(other),'/new','/seek=0'],env=env);h=wait_window(player);wait_play(h);time.sleep(2)
                phase('restart_'+str(restart+1))
                if not args.baseline:report['checks']['restart_'+str(restart+1)+'_auto_record']=any(r['SourcePath']==str(other) and r['EndedUtc'] is None for r in record(db))
                close(player,h);player=None;time.sleep(4)
        with sqlite3.connect(db) as connection:
            report['integrity']=connection.execute('PRAGMA integrity_check').fetchone()[0]
            report['segments']=[json.loads(r[0]) for r in connection.execute('SELECT Payload FROM Segment')]
        report['checks']['integrity']=report['integrity']=='ok'
        if args.auto_next:
            ids={r['SessionId'] for r in record(db) if r['SourcePath']==str(next_episode)}
            report['checks']['next_detailed_segment']=any(r['SessionId'] in ids and r['PlayedMs']>5000 for r in report['segments'])
    except BaseException as ex:
        report['failure']=dict(type=type(ex).__name__,message=str(ex));raise
    finally:
        if player and player.poll() is None:
            for w in windows(player.pid):
                if w['cls']=='PotPlayer64':u.PostMessageW(w['hwnd'],16,0,0)
            try:player.wait(timeout=8)
            except subprocess.TimeoutExpired:report['forced_termination']=True;player.terminate();player.wait(timeout=5)
        if player:report['exit_code']=player.poll()
        owned_hosts=[p for p in psutil.process_iter(['name','cmdline']) if p.info['name']=='PotTrace.exe' and channel in (p.info['cmdline'] or [])]
        if owned_hosts:
            subprocess.run([str(host),'--channel',channel,'--data-dir',str(data),'--exit'],env=env,timeout=10,check=True)
            for owned_host in owned_hosts:owned_host.wait(timeout=10)
        for path,content in old.items():
            if content is None:path.unlink(missing_ok=True)
            else:path.write_bytes(content)
        report['fixture_restored']=all(p.read_bytes()==content if content is not None else not p.exists() for p,content in old.items())
        report['sessions']=record(db);report['passed']=not report.get('failure') and not report.get('forced_termination') and bool(report['checks']) and all(report['checks'].values())
        (base/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'evidence':str(base),'passed':report['passed'],'checks':report['checks'],'failure':report.get('failure')},ensure_ascii=False),flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
