"""Registration-free, generated-media-only graph identity/lifecycle experiment."""
import os,sys,json,time,ctypes,subprocess,argparse,site,hashlib,threading,sqlite3
from pathlib import Path
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'probes/PotPlayerProbe'))
from probe_tools import TOOLS
from window_probe import windows,u,CB,info
from make_fixture import make
parser=argparse.ArgumentParser()
for flag in ['rate-transition-probe','suspend-probe','restart-host','precision-probe','rate-probe','ingest','detailed-flow','baseline','startup-only','presentation','preview','error-reopen','preload']:parser.add_argument('--'+flag,action='store_true')
parser.add_argument('--bridge-config',choices=['Debug','Release'],default='Debug')
parser.add_argument('--startup-play-seconds',type=int,default=4)
parser.add_argument('--frame-rate',type=int,default=30)
parser.add_argument('--transition-speed',type=float,choices=[.5,1.1,1.5,2],default=2)
parser.add_argument('--transition-hold',type=float,choices=[6.2,8.0],default=6.2)
parser.add_argument('--held-preload',choices=['resume','cancel'])
parser.add_argument('--format',choices=['avi','mp4','mkv'],default='avi')
args=parser.parse_args()
if args.transition_speed==2 and args.transition_hold>6.2:parser.error('The 30-second fixture cannot hold 2x for that long')
if args.precision_probe and args.bridge_config!='Debug':parser.error('Precision probe requires explicit native diagnostics from Debug bridge')
if not 4<=args.startup_play_seconds<=12:parser.error('Startup playback fixture must be between 4 and 12 seconds')
if args.restart_host and not args.ingest:parser.error('Host restart requires an explicitly owned online fixture')
if args.precision_probe and (not args.ingest or not args.startup_only):parser.error('Precision probe requires isolated online startup-only mode')
if args.suspend_probe and (not args.ingest or not args.startup_only or args.startup_play_seconds!=4 or args.precision_probe or args.restart_host or args.rate_probe):parser.error('Suspension probe requires an exclusive four-second online startup-only run')
if args.rate_transition_probe and (not args.ingest or not args.startup_only or args.bridge_config!='Debug' or args.suspend_probe or args.rate_probe or args.precision_probe or args.restart_host):parser.error('Rate transition requires an exclusive Debug online startup-only run')
if args.frame_rate not in [1,24,30,60]:parser.error('Use an explicit supported fixture frame rate')
if args.detailed_flow and (not args.ingest or args.startup_only):parser.error('Detailed flow requires full online ingestion')
if args.ingest and args.baseline:parser.error('Graph ingestion requires the graph collector')
if args.held_preload and (args.format=='avi' or args.startup_only):parser.error('Held preload requires mp4/mkv and a full run')
EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe'))).resolve()
if not EXE.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Only isolated player allowed')
AX=ROOT/'out/directshow-probe/Release/PotTraceGraphProbe.ax'
native_inputs=[*(ROOT/'probes/DirectShowProbe').glob('*.h'),ROOT/'probes/DirectShowProbe/GraphProbe.cpp',*[ROOT/'native/PotTrace.Bridge/include'/name for name in ['BridgeSupport.h','ObservationFence.h','TerminalDelivery.h','MpscQueue.h','GraphPacket.h','EffectiveRate.h']]]
if not args.baseline and (not AX.exists() or any(path.stat().st_mtime_ns>AX.stat().st_mtime_ns for path in native_inputs)):raise SystemExit('Rebuild native probe before running; missing or stale binary')
ini=EXE.parent/'PotPlayerMini64.ini';before=ini.read_bytes()
bridge=EXE.parent/'Extension/Playback/Statistics/pottrace-bridge.ini';oldbridge=bridge.read_bytes()
bridge_dll=bridge.parent/'PotTrace.Bridge.dll';old_bridge_dll=bridge_dll.read_bytes() if args.ingest else None
base=ROOT/('out/graph-probe-'+str(time.time_ns()));base.mkdir()
A=base/f'source-a/same-name.{args.format}';B=base/f'source-b/same-name.{args.format}';C=base/f'episodes/episode-01.{args.format}';D=base/f'episodes/episode-02.{args.format}'
for path,seconds,color in ((A,30,'red'),(B,31,'blue'),(C,12 if args.held_preload else 4,'red'),(D,18,'blue')):
 if args.format=='avi':make(path,seconds)
 else:
  site.addsitedir(str(TOOLS / 'python'));import imageio_ffmpeg
  path.parent.mkdir(parents=True,exist_ok=True)
  subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-f','lavfi','-i',f'color=c={color}:size=320x180:rate={args.frame_rate}','-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t',str(seconds),'-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-c:a','aac','-y',str(path)],check=True,timeout=60)
clock=ctypes.windll.kernel32.GetTickCount64;clock.restype=ctypes.c_ulonglong
u.SendMessageTimeoutW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t,ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_size_t)]
u.SendMessageTimeoutW.restype=ctypes.c_ssize_t
def send(h,msg,wparam=0,lparam=0):
 result=ctypes.c_size_t()
 if not u.SendMessageTimeoutW(h,msg,wparam,lparam,3,300,ctypes.byref(result)):raise TimeoutError('Owned player query/control timed out')
 return ctypes.c_ssize_t(result.value).value
def guard(pid=None):
 others=[]
 @CB
 def each(h,l):
  r=info(h)
  if r['cls']=='PotPlayer64' and r['pid']!=pid:others.append(r['pid'])
  return True
 u.EnumWindows(each,0)
 if others:raise RuntimeError('Another player exists; /current routing refused')
def snapshot(h):
 at=clock()
 try:
  row={'queryStarted':at,'state':send(h,0x400,0x5006),'position':send(h,0x400,0x5004),'duration':send(h,0x400,0x5002)}
  return {**row,'at':clock()}
 except TimeoutError:return {'at':at,'state':None,'position':None,'duration':None,'query_timeout':True}
def observe(h,seconds):
 rows=[];end=time.monotonic()+seconds
 while time.monotonic()<end:rows.append(snapshot(h));time.sleep(.02)
 return rows
def load(p,paths,env):
 guard(p.pid);si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
 return subprocess.Popen([str(EXE),*[str(path) for path in paths],'/current','/seek=0'],env=env,startupinfo=si)

def near_duration(row,expected):return row['duration'] is not None and abs(row['duration']-expected)<100

presentation_stop=threading.Event();presentation_thread=None
def watch_presentation(h,rows):
 from ctypes import wintypes as wt
 gdi=ctypes.WinDLL('gdi32');gdi.GetPixel.argtypes=[wt.HDC,ctypes.c_int,ctypes.c_int];gdi.GetPixel.restype=ctypes.c_uint32
 u.GetDC.argtypes=[wt.HWND];u.GetDC.restype=wt.HDC;u.ReleaseDC.argtypes=[wt.HWND,wt.HDC]
 dc=u.GetDC(None)
 try:
  while not presentation_stop.is_set():
   start=clock();rect=wt.RECT();point=wt.POINT();u.GetClientRect(h,ctypes.byref(rect));point.x=rect.right//4;point.y=int(rect.bottom*.43);u.ClientToScreen(h,ctypes.byref(point))
   color=gdi.GetPixel(dc,point.x,point.y);r=color&255;g=(color>>8)&255;b=(color>>16)&255
   rows.append({'begin':start,'end':clock(),'rgb':[r,g,b],'color':'red' if r>200 and g<60 and b<60 else 'blue' if b>200 and r<60 and g<60 else 'unknown'})
   presentation_stop.wait(.01)
 finally:u.ReleaseDC(None,dc)

def witness(h,p,label):
 # Capture only the owned player client. Presence of a file handle is evidence
 # of an open file, not proof of decoding; Windows open_files may omit handles.
 from ctypes import wintypes as wt
 site.addsitedir(str(TOOLS / 'python'))
 import psutil
 from PIL import ImageGrab,ImageStat
 row={'started':clock(),'state':snapshot(h)}
 row['open_fixture_files']=[f.path for f in psutil.Process(p.pid).open_files() if Path(f.path).is_relative_to(base)]
 rect=wt.RECT();origin=wt.POINT()
 u.GetClientRect(h,ctypes.byref(rect));u.ClientToScreen(h,ctypes.byref(origin))
 u.SetWindowPos.argtypes=[wt.HWND,wt.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
 u.SetWindowPos(h,-1,0,0,0,0,0x13);time.sleep(.1)
 bbox=(origin.x,origin.y,origin.x+rect.right,origin.y+rect.bottom)
 picture=ImageGrab.grab(bbox=bbox);picture.save(base/(label+'.png'))
 cx,cy=picture.width//2,picture.height//2
 rgb=ImageStat.Stat(picture.crop((cx-10,cy-10,cx+10,cy+10)).convert('RGB')).mean
 # The playlist may occupy the client center. Require a substantial pure-color
 # area, not one center pixel or colored UI text, as the independent witness.
 raw=picture.convert('RGB').tobytes();pixels=list(zip(raw[0::3],raw[1::3],raw[2::3]));red=sum(r>200 and g<60 and b<60 for r,g,b in pixels)/len(pixels);blue=sum(b>200 and r<60 and g<60 for r,g,b in pixels)/len(pixels)
 row['center_rgb']=rgb;row['color_fraction']={'red':red,'blue':blue};row['color']='red' if red>.05 and blue<.01 else 'blue' if blue>.05 and red<.01 else 'unknown'
 row['ended']=clock();return row
guard();p=None;report={'player':str(EXE),'baseline':args.baseline,'preload':args.preload,'held_preload':args.held_preload,'error_reopen':args.error_reopen,'preview':args.preview,'presentation':args.presentation,'renderer_watch':os.environ.get('POTTRACE_DSHOW_RENDERER_WATCH')=='1','state_only':os.environ.get('POTTRACE_DSHOW_STATE_ONLY')=='1','format':args.format,'frame_rate':args.frame_rate,'startup_play_seconds':args.startup_play_seconds,'sources':list(map(str,[A,B,C,D])),'actions_completed':False,'deployment_eligible':False,'probe_sha256':hashlib.sha256(AX.read_bytes()).hexdigest() if not args.baseline else None}
try:
 text='\n'.join(l for l in before.decode('utf-16').splitlines() if not l.startswith(('PrevSpeed=','PauseOnMin=','AutoPauseResume=','RepeatPlay2=','SeamlessPlay=')))
 if '[Override\\' in text:raise RuntimeError('Fixture already has filter overrides; refuse to replace them')
 text=text.replace('[Settings]','[Settings]\nRepeatPlay2=1\nSeamlessPlay='+('1' if args.preload else '0'))
 if args.preview:text=text.replace('[Settings]','[Settings]\nPreviewThumbnail=1')
 if not args.baseline:
  text+='\n[Override\\0000]\nType=1\nPath='+str(AX)+'\nCLSID={284611F2-2EB1-44A2-9C75-803C2C144D71}\nName=PotTrace isolated graph probe\nDisabled=0\nFilterType=0\nMerit=2097152\nMeritHi=0\nmod0000={73646976-0000-0010-8000-00AA00389B71}\nmod0001={00000000-0000-0000-0000-000000000000}\n[Override\\0001]\nType=-2\n'
 ini.write_text(text,encoding='utf-16');(base/'configured.ini').write_bytes(ini.read_bytes())
 bridge.write_text('[PotTrace]\nChannel=graphprobe\n',encoding='utf-16')
 env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),POTTRACE_TEST_WORKERS='observer',POTTRACE_TEST_RAW_TRACE=str(base/'script.tsv'),POTTRACE_TEST_EVENT_TRACE=str(base/'bridge.jsonl'),POTTRACE_DSHOW_TRACE=str(base/'graph.jsonl'))
 if args.ingest:
  data=base/'data';data.mkdir();(data/'settings.json').write_text('{"Mode":1}',encoding='utf-8')
  app=ROOT/'src/PotTrace.App/bin'/args.bridge_config/'net10.0-windows/PotTrace.exe'
  report['host_config']=args.bridge_config
  report['host_path']=str(app)
  if not app.exists():raise RuntimeError('Build real statistics host before online test')
  bridge_dll.write_bytes((ROOT/'out/native'/args.bridge_config/'PotTrace.Bridge.dll').read_bytes())
  report['bridge_sha256']=hashlib.sha256(bridge_dll.read_bytes()).hexdigest()
  report['bridge_config']=args.bridge_config
  bridge.write_text('[PotTrace]\nSourceMode=DirectShow\nHostPath='+str(app)+'\nDataDirectory='+str(data)+'\nChannel=graph'+str(time.time_ns())+'\n',encoding='utf-16')
  env.pop('POTTRACE_TEST_WORKERS',None);env.pop('POTTRACE_DSHOW_STATE_ONLY',None)
  env.update(DOTNET_ROOT=str(TOOLS / 'dotnet'),POTTRACE_GRAPH_BRIDGE=str(bridge_dll),POTTRACE_DSHOW_RENDERER_WATCH='1')
  report['ingest']=True;report['state_only']=False;report['renderer_watch']=True
 si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=5
 report['start']=clock();p=subprocess.Popen([str(EXE),str(A),'/new','/seek=0'],env=env,startupinfo=si)
 deadline=time.monotonic()+15;h=None
 while time.monotonic()<deadline:
  h=next((w['hwnd'] for w in windows(p.pid) if w['cls']=='PotPlayer64'),None)
  if h:break
  if p.poll() is not None:raise RuntimeError('Player exited during startup')
  time.sleep(.02)
 if not h:raise RuntimeError('No owned window')
 report['main_hwnd']=h
 if args.presentation:
  from ctypes import wintypes as wt
  u.SetWindowPos.argtypes=[wt.HWND,wt.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
  u.SetWindowPos(h,-1,0,0,0,0,0x13);report['presentation_samples']=[]
  presentation_thread=threading.Thread(target=watch_presentation,args=(h,report['presentation_samples']),daemon=True);presentation_thread.start()
 first=observe(h,args.startup_play_seconds);report['startup_samples']=first
 report['first_active']=next((r for r in first if r['state'] in (1,2) and near_duration(r,30000)),None)
 if not report['first_active']:raise AssertionError('First source never became active')
 report['pause_requested']=clock();send(h,0x400,0x5007,1);report['pause_samples']=observe(h,1.2)
 if args.suspend_probe:
  site.addsitedir(str(TOOLS / 'python'));import psutil
  def active_record():
   with sqlite3.connect(base/'data/pottrace.db') as db:return json.loads(db.execute('SELECT Payload FROM Session').fetchone()[0])
  before_suspend=active_record();owned_process=psutil.Process(p.pid)
  report['suspension']={'began':clock(),'before':before_suspend}
  try:
   owned_process.suspend();time.sleep(8)
  finally:owned_process.resume()
  report['suspension']['resumed']=clock();observe(h,.7)
  after_suspend=active_record();report['suspension']['after']=after_suspend
  assert after_suspend['SessionId']==before_suspend['SessionId'],'Suspension created a duplicate session'
  assert after_suspend['PlayedMs']==before_suspend['PlayedMs'],'Suspended pause invented playback'
  assert after_suspend['StayMs']-before_suspend['StayMs']<1500,'Unobserved suspended interval counted as dwell'
  assert not after_suspend['IsValid'],'Unknown interval incorrectly qualified a short opening'
 if args.rate_probe:
  report['rate_probe_started']=clock();send(h,0x111,10246)
  for _ in range(10):send(h,0x111,10248)
  send(h,0x400,0x5007,2);report['rate_probe_samples']=observe(h,3);send(h,0x400,0x5007,1);observe(h,.7);send(h,0x111,10246);report['rate_probe_ended']=clock()
 if args.rate_transition_probe:
  send(h,0x111,10246);send(h,0x400,0x5005,0);observe(h,.7)
  with sqlite3.connect(base/'data/pottrace.db') as db:played_before=json.loads(db.execute('SELECT Payload FROM Session').fetchone()[0])['PlayedMs']
  transition={'started':clock(),'changes':[],'target_speed':args.transition_speed};report['rate_transition']=transition
  transition['hold_seconds']=args.transition_hold
  send(h,0x400,0x5007,2);transition['normal_before']=observe(h,args.transition_hold)
  transition['changes'].append(clock())
  for unused in range(round(abs(args.transition_speed-1)*10)):send(h,0x111,10248 if args.transition_speed>1 else 10247)
  transition['changed_speed']=observe(h,args.transition_hold);transition['changes'].append(clock())
  send(h,0x111,10246);transition['normal_after']=observe(h,args.transition_hold)
  send(h,0x400,0x5007,1);transition['ended']=clock();observe(h,.7)
  with sqlite3.connect(base/'data/pottrace.db') as db:played_after=json.loads(db.execute('SELECT Payload FROM Session').fetchone()[0])['PlayedMs']
  transition['recorded_ms']=played_after-played_before
  wire=[json.loads(line) for line in (base/'bridge.jsonl').read_text(encoding='utf-8').splitlines()]
  transition['rate_events']=[event for event in wire if event['kind']=='RateChanged' and transition['started']<=event['monotonicMs']<=transition['ended']]
  slopes={label:(values[-1]['position']-values[0]['position'])/(values[-1]['at']-values[0]['at']) for label,values in ((name,transition[name]) for name in ['normal_before','changed_speed','normal_after'])}
  transition['independent_position_slopes']=slopes
  def settled_slope(values):
   samples=[e for e in values if e['at']>=values[-1]['at']-2500];x=[e['at']-samples[0]['at'] for e in samples];y=[e['position'] for e in samples];mx=sum(x)/len(x);my=sum(y)/len(y)
   return sum((a-mx)*(b-my) for a,b in zip(x,y))/sum((a-mx)**2 for a in x)
  settled={name:settled_slope(transition[name]) for name in slopes};transition['independent_settled_slopes']=settled
  assert .85<settled['normal_before']<1.15 and args.transition_speed*.9<settled['changed_speed']<args.transition_speed*1.1 and .85<settled['normal_after']<1.15,'The requested speed changes did not take effect after settling'
  assert abs(transition['recorded_ms']-(transition['ended']-transition['started']))<=150,'Real playing duration diverged during rate changes'
  assert len(transition['rate_events'])==2,'Live speed changes missing or fragmented into extra boundaries'
  assert all(e.get('rateIsEstimated') and e.get('rate') is None and e.get('rateWindowStartMs') is not None and e['rateWindowStartMs']<e['monotonicMs'] for e in transition['rate_events']),'Estimated transition fabricated exact rate or lost observation window'
  with sqlite3.connect(base/'data/pottrace.db') as db:
   transition['segments']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Segment')]
   transition['timeline']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM TimelineEvent')]
  if args.startup_play_seconds==4:
   assert len(transition['segments'])==1,'Continuous rate changes fragmented detailed playback'
   assert transition['segments'][0]['PlayedMs']==transition['recorded_ms'],'Continuous detail lost playing time during rate changes'
  assert all(s.get('RateIsEstimated') for s in transition['segments']),'Effective segment speed mislabeled as exact setting'
  saved=[e for e in transition['timeline'] if e['Kind']=='RateChanged']
  assert len(saved)==2 and all(e.get('RateWindowStartUtc') and e.get('RateIsEstimated') for e in saved),'Database lost estimated transition windows'
 if args.ingest:
  database=base/'data/pottrace.db';deadline=time.monotonic()+10
  while not database.exists() and time.monotonic()<deadline:time.sleep(.1)
  if not database.exists():raise AssertionError('Real host did not create SQLite database')
  with sqlite3.connect(database) as db:
   report['online_initial_sessions']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Session')]
  if len(report['online_initial_sessions'])!=1:raise AssertionError('Graph did not create exactly one online session')
 if args.restart_host:
  site.addsitedir(str(TOOLS / 'python'));import psutil
  def owned_hosts():
   result=[]
   for process in psutil.process_iter(['name']):
    if (process.info['name'] or '').lower()!='pottrace.exe':continue
    try:
     if Path(process.exe()).resolve()==app.resolve() and str(data).lower() in [a.lower() for a in process.cmdline()]:result.append(process)
    except (psutil.NoSuchProcess,psutil.AccessDenied):pass
   return result
  hosts=owned_hosts()
  if len(hosts)!=1:raise AssertionError('Did not find exactly one fixture-owned host')
  report['host_restart']={'old_pid':hosts[0].pid,'killed_at':clock()};hosts[0].kill();hosts[0].wait(timeout=5)
  deadline=time.monotonic()+15;replacement=[]
  while time.monotonic()<deadline:
   replacement=[x for x in owned_hosts() if x.pid!=report['host_restart']['old_pid']]
   if replacement:break
   observe(h,.1)
  if len(replacement)!=1:raise AssertionError('Fixture host did not recover exactly once')
  report['host_restart'].update(new_pid=replacement[0].pid,restarted_at=clock());observe(h,1.2)
  with sqlite3.connect(database) as db:recovered=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Session')]
  original=report['online_initial_sessions'][0]
  if len(recovered)!=1 or recovered[0]['SessionId']!=original['SessionId'] or recovered[0]['PlayedMs']!=original['PlayedMs']:raise AssertionError('Host replay duplicated or changed paused playback')
  report['host_restart']['session_preserved']=True
 if args.precision_probe:
  observe(h,11);report['precision_segments']=[]
  with sqlite3.connect(database) as db:held=json.loads(db.execute('SELECT Payload FROM Session').fetchone()[0])
  report['paused_played_delta']=held['PlayedMs']-report['online_initial_sessions'][0]['PlayedMs']
  if report['paused_played_delta']!=0:raise AssertionError('Paused dwell added playing duration')
  for milliseconds in [4900,5100]:
   send(h,0x400,0x5005,10000);observe(h,.7);started=clock();send(h,0x400,0x5007,2);observe(h,milliseconds/1000);send(h,0x400,0x5007,1);ended=clock();observe(h,.7)
   report['precision_segments'].append({'requested_ms':milliseconds,'started':started,'ended':ended})
  with sqlite3.connect(database) as db:recorded=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Segment')]
  report['precision_recorded_segments']=recorded
  if len(recorded)!=1 or not 5000<recorded[0]['PlayedMs']<5250:raise AssertionError('4.9/5.1 second real segment threshold mismatch: '+json.dumps(recorded))
  report['precision_dwell']=[]
  def native_events():
   result=[]
   for line in (base/'bridge.jsonl').read_text('utf-8').splitlines():
    try:result.append(json.loads(line))
    except json.JSONDecodeError:pass # a final in-progress diagnostic line
   return result
  for milliseconds in [9900,10100]:
   previous={e['mediaEpoch'] for e in native_events() if e['kind']=='MediaOpened'};child=load(p,[A],env);deadline=time.monotonic()+10;opened=None
   while time.monotonic()<deadline:
    opened=next((e for e in native_events() if e['kind']=='MediaOpened' and e['mediaEpoch'] not in previous),None)
    if opened:break
    time.sleep(.01)
   if not opened:raise AssertionError('Reopen readiness not recorded')
   send(h,0x400,0x5007,1);target=opened['monotonicMs']+milliseconds
   while clock()<target:time.sleep(.005)
   requested=clock();send(h,0x319,h,13<<16);observe(h,1);child.wait(timeout=5)
   closed=next((e for e in native_events() if e['kind']=='MediaClosed' and e['mediaEpoch']==opened['mediaEpoch']),None)
   if not closed:raise AssertionError('Close boundary absent')
   with sqlite3.connect(database) as db:rows=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Session')]
   retained=[s for s in rows if s['MediaEpoch'].lower()==opened['mediaEpoch'].lower()]
   report['precision_dwell'].append({'requested_ms':milliseconds,'opened':opened,'closed':closed,'stop_requested':requested,'retained':retained})
   if bool(retained)!=(milliseconds>10000):raise AssertionError('9.9/10.1 second real dwell threshold mismatch')
 if args.detailed_flow:
  report['paused_hold']=observe(h,11)
  with sqlite3.connect(database) as db:report['after_paused_hold']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Session')]
  if not report['after_paused_hold'][0]['IsValid']:raise AssertionError('Paused dwell did not qualify')
  report['paused_played_delta']=report['after_paused_hold'][0]['PlayedMs']-report['online_initial_sessions'][0]['PlayedMs']
  if report['paused_played_delta']!=0:raise AssertionError('Paused dwell added playing duration')
  report['repeats']=[]
  for repeat in range(2):
   send(h,0x400,0x5005,10000);observe(h,.6);started=clock();send(h,0x400,0x5007,2);samples=observe(h,8.4);send(h,0x400,0x5007,1);ended=clock();observe(h,.7)
   report['repeats'].append({'started':started,'ended':ended,'samples':samples})
  with sqlite3.connect(database) as db:report['online_repeat_segments']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Segment')]
  overlap=[s for s in report['online_repeat_segments'] if s['StartMs']<=11000 and s['EndMs']>=17000]
  if len(overlap)!=2:raise AssertionError('Expected two independent detailed repeat segments: '+json.dumps(report['online_repeat_segments']))
  child=subprocess.Popen([str(EXE),str(D),'/add','/current'],env=env,startupinfo=si);child.wait(timeout=5);observe(h,1)
  with sqlite3.connect(database) as db:
   current=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Session')]
  if any(row['SourcePath']!=str(A) for row in current):raise AssertionError('Queue-only source admitted')
  report['queue_only_excluded']=True
  send(h,0x111,10246)
  for _ in range(10):send(h,0x111,10248)
  send(h,0x400,0x5005,0);observe(h,.8)
  with sqlite3.connect(database) as db:rate_before=json.loads(db.execute('SELECT Payload FROM Session').fetchone()[0])['PlayedMs']
  rate_start=clock();send(h,0x400,0x5007,2);rate_samples=observe(h,12);send(h,0x400,0x5007,1);rate_end=clock();observe(h,.7)
  with sqlite3.connect(database) as db:rate_after=json.loads(db.execute('SELECT Payload FROM Session').fetchone()[0])['PlayedMs']
  report['double_speed']={'started':rate_start,'ended':rate_end,'recorded_ms':rate_after-rate_before,'elapsed_ms':rate_end-rate_start,'samples':rate_samples}
  if abs((rate_after-rate_before)-(rate_end-rate_start))>150:raise AssertionError('Double-speed real playing duration differs by more than 150 ms')
  if not 23000<=rate_samples[-1]['position']<=25000:raise AssertionError('Double speed not independently observed')
  send(h,0x111,10246)
 if args.preview:
  from ctypes import wintypes as wt
  site.addsitedir(str(TOOLS / 'python'));import win32gui,win32con
  from PIL import ImageGrab
  win32gui.SetWindowPos(h,win32con.HWND_TOPMOST,0,0,0,0,win32con.SWP_NOMOVE|win32con.SWP_NOSIZE|win32con.SWP_NOACTIVATE)
  rect=win32gui.GetClientRect(h);origin=win32gui.ClientToScreen(h,(0,0));report['preview_client_rect']=rect
  # Verified default skin has the seek bar 80 px above its client bottom.
  # Move only the cursor over the owned player; restore its original location.
  cursor=win32gui.GetCursorPos();report['preview_started']=clock()
  try:
   for fraction in (.2,.4,.7):
    x=int((rect[2]//2)*fraction);y=rect[3]-80
    ctypes.windll.user32.SetCursorPos(origin[0]+x,origin[1]+y);observe(h,2)
    ImageGrab.grab(bbox=(origin[0],origin[1],origin[0]+rect[2],origin[1]+rect[3])).save(base/('preview-'+str(fraction)+'.png'))
  finally:ctypes.windll.user32.SetCursorPos(*cursor)
  report['preview_ended']=clock()
 if not args.startup_only:
  send(h,0x400,0x5005,5000);report['seek_samples']=observe(h,.5);send(h,0x400,0x5007,2);report['resume_samples']=observe(h,1.5)
  report['switch_requested']=clock();child=load(p,[B],env);report['switch_samples']=observe(h,3);child.wait(timeout=5)
  if args.detailed_flow:
   send(h,0x400,0x5007,1);report['second_source_hold']=observe(h,11)
  report['playlist_requested']=clock();child=load(p,[C,D],env)
  if args.held_preload:
   report['playlist_samples']=observe(h,2);child.wait(timeout=5)
   send(h,0x400,0x5007,1);report['held_pause_started']=clock();report['held_before']=witness(h,p,'held-before')
   report['held_samples']=observe(h,11);report['held_after']=witness(h,p,'held-after')
   if report['held_before']['color']!='red' or report['held_after']['color']!='red':raise AssertionError('Paused current-episode display not independently confirmed')
   if any(r['state']!=1 or not near_duration(r,12000) for r in report['held_samples']):raise AssertionError('Held current episode did not stay paused')
   if args.held_preload=='resume':
    report['held_resume_requested']=clock();send(h,0x400,0x5007,2);report['playlist_samples']+=observe(h,14);report['next_display']=witness(h,p,'next-display')
    if report['next_display']['color']!='blue':raise AssertionError('Next-episode display not independently confirmed')
  else:report['playlist_samples']=observe(h,8);child.wait(timeout=5)
  if args.held_preload!='cancel':
   report['natural_next_active']=next((r for r in report['playlist_samples'] if near_duration(r,18000) and r['state'] in (1,2)),None)
   if not report['natural_next_active']:raise AssertionError('Natural episode advance not observed')
   if args.detailed_flow:report['next_episode_valid_hold']=observe(h,9)
  if args.error_reopen:
   report['stop_requested']=clock();send(h,0x319,h,13<<16);report['stop_samples']=observe(h,1)
   if not any(r['state'] in (-1,0) for r in report['stop_samples']):raise AssertionError('Stop/unload not observed')
   report['reopen_requested']=clock();child=load(p,[A],env);report['reopen_samples']=observe(h,3);child.wait(timeout=5)
   if not any(r['state']==2 and near_duration(r,30000) for r in report['reopen_samples']):raise AssertionError('Reopen not observed')
   bad=base/'invalid.mp4';bad.write_bytes(b'PotTrace intentionally invalid generated media')
   report['invalid_source']=str(bad);report['invalid_requested']=clock();child=load(p,[bad],env);report['invalid_samples']=observe(h,2);child.wait(timeout=5)
   report['invalid_dialogs']=[w for w in windows(p.pid) if w['cls']=='#32770']
   for w in report['invalid_dialogs']:u.PostMessageW(w['hwnd'],16,0,0)
   report['recovery_requested']=clock();child=load(p,[A],env);report['recovery_samples']=observe(h,3);child.wait(timeout=5)
   if not any(r['state']==2 and near_duration(r,30000) for r in report['recovery_samples']):raise AssertionError('Recovery after invalid media not observed')
 report['actions_completed']=True
except BaseException as error:
 report['failure']={'type':type(error).__name__,'message':str(error)}
 raise
finally:
 presentation_stop.set()
 if presentation_thread:presentation_thread.join(timeout=2)
 if p:
  for w in windows(p.pid):
   if w['cls']=='PotPlayer64':u.PostMessageW(w['hwnd'],16,0,0)
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:report['forced_termination']=True;p.terminate();p.wait()
  report['exit_code']=p.returncode
 time.sleep(.15);ini.write_bytes(before);bridge.write_bytes(oldbridge)
 if old_bridge_dll is not None:bridge_dll.write_bytes(old_bridge_dll)
 report['fixture_restored']=ini.read_bytes()==before and bridge.read_bytes()==oldbridge and (old_bridge_dll is None or bridge_dll.read_bytes()==old_bridge_dll)
 if args.ingest and (base/'data/pottrace.db').exists():
  time.sleep(3)
  with sqlite3.connect(base/'data/pottrace.db') as db:
   report['database_integrity']=db.execute('PRAGMA integrity_check').fetchone()[0]
   report['database_sessions']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Session')]
   report['database_segments']=[json.loads(row[0]) for row in db.execute('SELECT Payload FROM Segment')]
 if (base/'graph.jsonl').exists():
  events=[json.loads(l) for l in (base/'graph.jsonl').read_text(encoding='utf-8').splitlines()]
  report['graph_event_count']=len(events);report['graph_sources']=sorted({s for e in events for s in e['sources']})
  report['connected_runs']=[e for e in events if e['event']=='RunAfter' and e['inputConnected'] and e['outputConnected']]
  report['log_lost']=max((e['lost'] for e in events),default=0)
  report['sink_lost']=max((e.get('sinkLost',0) for e in events),default=0)
  expected=[str(A)] if args.startup_only else list(map(str,[A,B,C] if args.held_preload=='cancel' else [A,B,C,D]))
  # The custom graph enumerator returns E_FAIL at the apparent end. Preserve
  # that uncertainty; independent evidence comes from the exact connected input
  # pin's owner implementing IFileSourceFilter, not from an incomplete enumeration.
  report['graph_enumeration_uncertain']=any(e['uncertain'] for e in report['connected_runs'])
  report['missing_expected_sources']=[path for path in expected if not any(e.get('upstreamSource')==path and e.get('upstreamResult',-1)>=0 and e['result']>=0 for e in report['connected_runs'])]
  if args.held_preload:
   report['unexpected_next_while_held']=[e['at'] for e in events if e.get('upstreamSource')==str(D) and e['inputConnected'] and e['at']<report.get('held_resume_requested',2**64)]
  if args.error_reopen:
   report['invalid_connected_runs']=[e['at'] for e in report['connected_runs'] if e.get('upstreamSource')==report.get('invalid_source')]
   report['stop_boundary_found']=any(e['event']=='StopAfter' and report.get('stop_requested',2**64)<=e['at']<report.get('reopen_requested',0) for e in events)
   for label in ['reopen','recovery']:
    report[label+'_identity_found']=any(e['at']>=report.get(label+'_requested',2**64) and e.get('upstreamSource')==str(A) and e.get('upstreamResult',-1)>=0 for e in report['connected_runs'])
  used={e['instance'] for e in report['connected_runs']}
  destroyed={e['instance'] for e in events if e['event']=='Destroy'}
  report['missing_destroy_records']=sorted(used-destroyed)
 else:report['graph_event_count']=0
 healthy_exit=not report.get('forced_termination') and report.get('exit_code')==0
 source_checks=bool(report.get('connected_runs')) and not any(report.get(key) for key in ['missing_expected_sources','log_lost','sink_lost','missing_destroy_records','unexpected_next_while_held','invalid_connected_runs'])
 if args.error_reopen:source_checks=source_checks and all(report.get(key) for key in ['stop_boundary_found','reopen_identity_found','recovery_identity_found'])
 report['basic_probe_passed']=report['actions_completed'] and healthy_exit and (args.baseline or source_checks)
 (base/'result.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
 print(json.dumps({'evidence':str(base),**{k:report[k] for k in ['player','baseline','preload','held_preload','format','exit_code','graph_event_count','log_lost','missing_expected_sources','missing_destroy_records','unexpected_next_while_held','graph_enumeration_uncertain'] if k in report}},ensure_ascii=False),flush=True)
if report.get('forced_termination') or report.get('exit_code')!=0:raise SystemExit(1)
if report.get('sink_lost'):raise SystemExit(1)
if not args.baseline and (not report.get('connected_runs') or report.get('missing_expected_sources') or report.get('log_lost') or report.get('missing_destroy_records') or report.get('unexpected_next_while_held')):raise SystemExit(1)
if args.error_reopen and not args.baseline and (report.get('invalid_connected_runs') or not report.get('stop_boundary_found') or not report.get('reopen_identity_found') or not report.get('recovery_identity_found')):raise SystemExit(1)
