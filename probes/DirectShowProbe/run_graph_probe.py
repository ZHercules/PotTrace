"""Registration-free, generated-media-only graph identity/lifecycle experiment."""
import os,sys,json,time,ctypes,subprocess,argparse,site
from pathlib import Path
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'probes/PotPlayerProbe'))
from window_probe import windows,u,CB,info
from make_fixture import make
parser=argparse.ArgumentParser();parser.add_argument('--baseline',action='store_true');parser.add_argument('--startup-only',action='store_true');parser.add_argument('--error-reopen',action='store_true');parser.add_argument('--preload',action='store_true');parser.add_argument('--held-preload',choices=['resume','cancel']);parser.add_argument('--format',choices=['avi','mp4','mkv'],default='avi');args=parser.parse_args()
if args.held_preload and (args.format=='avi' or args.startup_only):parser.error('Held preload requires mp4/mkv and a full run')
EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe'))).resolve()
if not EXE.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Only isolated player allowed')
AX=ROOT/'out/directshow-probe/Release/PotTraceGraphProbe.ax'
ini=EXE.parent/'PotPlayerMini64.ini';before=ini.read_bytes()
bridge=EXE.parent/'Extension/Playback/Statistics/pottrace-bridge.ini';oldbridge=bridge.read_bytes()
base=ROOT/('out/graph-probe-'+str(time.time_ns()));base.mkdir()
A=base/f'source-a/same-name.{args.format}';B=base/f'source-b/same-name.{args.format}';C=base/f'episodes/episode-01.{args.format}';D=base/f'episodes/episode-02.{args.format}'
for path,seconds,color in ((A,30,'red'),(B,31,'blue'),(C,12 if args.held_preload else 4,'red'),(D,18,'blue')):
 if args.format=='avi':make(path,seconds)
 else:
  site.addsitedir(r'D:\codex\PotTrace\.tools\python');import imageio_ffmpeg
  path.parent.mkdir(parents=True,exist_ok=True)
  subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-f','lavfi','-i',f'color=c={color}:size=320x180:rate=30','-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t',str(seconds),'-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-c:a','aac','-y',str(path)],check=True,timeout=60)
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

def witness(h,p,label):
 # Capture only the owned player client. Presence of a file handle is evidence
 # of an open file, not proof of decoding; Windows open_files may omit handles.
 from ctypes import wintypes as wt
 site.addsitedir(r'D:\codex\PotTrace\.tools\python')
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
guard();p=None;report={'player':str(EXE),'baseline':args.baseline,'preload':args.preload,'held_preload':args.held_preload,'error_reopen':args.error_reopen,'format':args.format,'sources':list(map(str,[A,B,C,D])),'actions_completed':False,'deployment_eligible':False}
try:
 text='\n'.join(l for l in before.decode('utf-16').splitlines() if not l.startswith(('PrevSpeed=','PauseOnMin=','AutoPauseResume=','RepeatPlay2=','SeamlessPlay=')))
 if '[Override\\' in text:raise RuntimeError('Fixture already has filter overrides; refuse to replace them')
 text=text.replace('[Settings]','[Settings]\nRepeatPlay2=1\nSeamlessPlay='+('1' if args.preload else '0'))
 if not args.baseline:
  text+='\n[Override\\0000]\nType=1\nPath='+str(AX)+'\nCLSID={284611F2-2EB1-44A2-9C75-803C2C144D71}\nName=PotTrace isolated graph probe\nDisabled=0\nFilterType=0\nMerit=2097152\nMeritHi=0\nmod0000={73646976-0000-0010-8000-00AA00389B71}\nmod0001={00000000-0000-0000-0000-000000000000}\n[Override\\0001]\nType=-2\n'
 ini.write_text(text,encoding='utf-16');(base/'configured.ini').write_bytes(ini.read_bytes())
 bridge.write_text('[PotTrace]\nChannel=graphprobe\n',encoding='utf-16')
 env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),POTTRACE_TEST_WORKERS='observer',POTTRACE_TEST_RAW_TRACE=str(base/'script.tsv'),POTTRACE_TEST_EVENT_TRACE=str(base/'bridge.jsonl'),POTTRACE_DSHOW_TRACE=str(base/'graph.jsonl'))
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
 first=observe(h,4);report['startup_samples']=first
 report['first_active']=next((r for r in first if r['state'] in (1,2) and near_duration(r,30000)),None)
 if not report['first_active']:raise AssertionError('First source never became active')
 report['pause_requested']=clock();send(h,0x400,0x5007,1);report['pause_samples']=observe(h,1.2)
 if not args.startup_only:
  send(h,0x400,0x5005,5000);report['seek_samples']=observe(h,.5);send(h,0x400,0x5007,2);report['resume_samples']=observe(h,1.5)
  report['switch_requested']=clock();child=load(p,[B],env);report['switch_samples']=observe(h,3);child.wait(timeout=5)
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
 if p:
  for w in windows(p.pid):
   if w['cls']=='PotPlayer64':u.PostMessageW(w['hwnd'],16,0,0)
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:report['forced_termination']=True;p.terminate();p.wait()
  report['exit_code']=p.returncode
 time.sleep(.15);ini.write_bytes(before);bridge.write_bytes(oldbridge)
 if (base/'graph.jsonl').exists():
  events=[json.loads(l) for l in (base/'graph.jsonl').read_text(encoding='utf-8').splitlines()]
  report['graph_event_count']=len(events);report['graph_sources']=sorted({s for e in events for s in e['sources']})
  report['connected_runs']=[e for e in events if e['event']=='RunAfter' and e['inputConnected'] and e['outputConnected']]
  report['log_lost']=max((e['lost'] for e in events),default=0)
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
 source_checks=bool(report.get('connected_runs')) and not any(report.get(key) for key in ['missing_expected_sources','log_lost','missing_destroy_records','unexpected_next_while_held','invalid_connected_runs'])
 if args.error_reopen:source_checks=source_checks and all(report.get(key) for key in ['stop_boundary_found','reopen_identity_found','recovery_identity_found'])
 report['basic_probe_passed']=report['actions_completed'] and healthy_exit and (args.baseline or source_checks)
 (base/'result.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
 print(json.dumps({'evidence':str(base),**{k:report[k] for k in ['player','baseline','preload','held_preload','format','exit_code','graph_event_count','log_lost','missing_expected_sources','missing_destroy_records','unexpected_next_while_held','graph_enumeration_uncertain'] if k in report}},ensure_ascii=False),flush=True)
if report.get('forced_termination') or report.get('exit_code')!=0:raise SystemExit(1)
if not args.baseline and (not report.get('connected_runs') or report.get('missing_expected_sources') or report.get('log_lost') or report.get('missing_destroy_records') or report.get('unexpected_next_while_held')):raise SystemExit(1)
if args.error_reopen and not args.baseline and (report.get('invalid_connected_runs') or not report.get('stop_boundary_found') or not report.get('reopen_identity_found') or not report.get('recovery_identity_found')):raise SystemExit(1)
