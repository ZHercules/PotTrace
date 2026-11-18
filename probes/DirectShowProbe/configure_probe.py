"""Inspect/configure ONLY the isolated player's external-filter preferences."""
import os,sys,site,time,json,subprocess,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'probes/PotPlayerProbe'))
from window_probe import windows,u
site.addsitedir(r'D:\codex\PotTrace\.tools\python')
from pywinauto import Desktop
import win32gui,win32con
parser=argparse.ArgumentParser();parser.add_argument('--template',action='store_true');parser.add_argument('--preload',action='store_true');parser.add_argument('--preview',action='store_true');args=parser.parse_args()
EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe'))).resolve()
if not EXE.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Isolated player required')
ini=EXE.parent/'PotPlayerMini64.ini';before=ini.read_bytes()
base=ROOT/('out/dshow-config-'+str(time.time_ns()));base.mkdir()
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),POTTRACE_TEST_WORKERS='none')
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=5
p=subprocess.Popen([str(EXE),'/new'],env=env,startupinfo=si);report={}
def wait(fn,seconds=12):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  if result:=fn():return result
  time.sleep(.1)
 raise RuntimeError('Owned UI condition timed out')
try:
 h=wait(lambda:next((r['hwnd'] for r in windows(p.pid) if r['cls']=='PotPlayer64'),None))
 win32gui.SetWindowPos(h,win32con.HWND_TOPMOST,0,0,0,0,win32con.SWP_NOMOVE|win32con.SWP_NOSIZE|win32con.SWP_NOACTIVATE)
 u.PostMessageW(h,0x100,0x74,0);u.PostMessageW(h,0x101,0x74,0)
 pref=wait(lambda:next((r['hwnd'] for r in windows(p.pid) if r['cls']=='#32770'),None))
 ui=Desktop(backend='uia').window(handle=pref)
 if args.preview:
  ui.child_window(title='Playback',control_type='TreeItem').wrapper_object().select();time.sleep(.3)
  report['controls']=[{'name':c.window_text(),'type':c.element_info.control_type,'id':c.element_info.automation_id} for c in ui.descendants() if c.element_info.control_type in ('CheckBox','ComboBox')]
  control=ui.child_window(title='Show thumbnail previews on nav. bar',control_type='CheckBox').wrapper_object()
  if not control.get_toggle_state():control.toggle()
  ui.child_window(title='OK',control_type='Button').wrapper_object().invoke();raise SystemExit(0)
 if args.preload:
  ui.child_window(title='Playback',control_type='TreeItem').wrapper_object().expand();time.sleep(.2)
  report['tree']=[c.window_text() for c in ui.descendants(control_type='TreeItem')]
  ui.child_window(title='Advanced',control_type='TreeItem').wrapper_object().select();time.sleep(.3)
  native=Desktop(backend='win32').window(handle=pref)
  controls=[c for c in native.descendants(class_name='ComboBox') if c.is_visible()]
  report['choices']=[{'id':c.control_id(),'selected':c.selected_text(),'choices':c.item_texts()} for c in controls]
  found=False
  for control in controls:
   for choice in control.item_texts():
    if 'preload' in choice.lower() and 'combine' not in choice.lower():control.select(choice);found=True;break
   if found:break
  if not found:raise RuntimeError('Preload-only choice not found')
  ui.child_window(title='OK',control_type='Button').wrapper_object().invoke();raise SystemExit(0)
 # Tree uses the installed English resource, not guessed message numbers.
 ui.child_window(title='Filter Control',control_type='TreeItem').wrapper_object().expand();time.sleep(.2)
 report['tree']=[c.window_text() for c in ui.descendants(control_type='TreeItem') if 'Filter' in c.window_text()]
 ui.child_window(title='Filter Priority (Overall)',control_type='TreeItem').wrapper_object().select();time.sleep(.3)
 report['controls']=[{'name':c.window_text(),'type':c.element_info.control_type,'id':c.element_info.automation_id} for c in ui.descendants() if c.element_info.control_type in ('Button','List','ListItem','RadioButton')]
 if args.template:
  ui.child_window(title='Add registered filter...',control_type='Button').wrapper_object().invoke()
  popup=wait(lambda:next((r['hwnd'] for r in windows(p.pid) if r['cls']=='#32770' and r['hwnd']!=pref),None))
  view=Desktop(backend='uia').window(handle=popup);time.sleep(.3)
  report['popup']=[{'name':c.window_text(),'type':c.element_info.control_type,'id':c.element_info.automation_id} for c in view.descendants() if c.element_info.control_type in ('ListItem','Button')]
  item=view.child_window(title='Color Space Converter',control_type='ListItem').wrapper_object();item.select()
  view.child_window(title='OK',control_type='Button').wrapper_object().invoke();time.sleep(.3)
  prefer=ui.child_window(title='Prefer',control_type='RadioButton').wrapper_object()
  u.SendMessageW(prefer.handle,0xF5,0,0);time.sleep(.15)
  ui.child_window(title='OK',control_type='Button').wrapper_object().invoke()
 else:ui.child_window(title='Cancel',control_type='Button').wrapper_object().invoke()
finally:
 for w in windows(p.pid):
  if w['cls']=='PotPlayer64':u.PostMessageW(w['hwnd'],16,0,0)
 try:p.wait(timeout=5)
 except subprocess.TimeoutExpired:p.terminate();p.wait()
 after=ini.read_bytes();(base/'before.ini').write_bytes(before);(base/'after.ini').write_bytes(after);ini.write_bytes(before)
 old=set(before.decode('utf-16').splitlines());report['new_lines']=[l for l in after.decode('utf-16').splitlines() if l not in old]
 (base/'result.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
 print(json.dumps({'evidence':str(base),'tree':report.get('tree'),'new_lines':report['new_lines'],'choices':report.get('choices')},ensure_ascii=False),flush=True)
