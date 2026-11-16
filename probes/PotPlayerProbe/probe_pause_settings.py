"""Inspect real pause controls and their saved INI keys in an isolated player only."""
import os, sys, time, site, subprocess, json, argparse
from pathlib import Path
from window_probe import windows, u
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--inventory',action='store_true');parser.add_argument('--repeat-playlist',action='store_true');args=parser.parse_args()
site.addsitedir(r'D:\codex\PotTrace\.tools\python')
from pywinauto import Desktop
import win32gui, win32con
EXE=Path(os.environ.get('POTTRACE_TEST_PLAYER',str(ROOT/'out/player/PotPlayerMini64.exe'))).resolve()
if not EXE.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Isolated player required')
ini=EXE.parent/'PotPlayerMini64.ini'; before=ini.read_bytes()
base=ROOT/('out/pause-settings-'+str(time.time_ns()));base.mkdir()
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),POTTRACE_TEST_WORKERS='none')
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=5
p=subprocess.Popen([str(EXE),'/new'],env=env,startupinfo=si)
report={}
try:
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        main=next((r['hwnd'] for r in windows(p.pid) if r['cls']=='PotPlayer64'),None)
        if main:break
        time.sleep(.1)
    else:raise RuntimeError('No owned player window')
    win32gui.SetWindowPos(main,win32con.HWND_TOPMOST,0,0,0,0,win32con.SWP_NOMOVE|win32con.SWP_NOSIZE|win32con.SWP_NOACTIVATE)
    u.PostMessageW(main,0x100,0x74,0);u.PostMessageW(main,0x101,0x74,0)
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        h=next((r['hwnd'] for r in windows(p.pid) if r['cls']=='#32770'),None)
        if h:break
        time.sleep(.1)
    else:raise RuntimeError('No owned preferences window')
    ui=Desktop(backend='uia').window(handle=h)
    ui.child_window(title='General',control_type='TreeItem').wrapper_object().select()
    time.sleep(.4)
    if args.repeat_playlist:
        ui.child_window(title='Playback',control_type='TreeItem').wrapper_object().select();time.sleep(.3)
        native=Desktop(backend='win32').window(handle=h)
        control=next(c for c in native.descendants(class_name='ComboBox') if c.control_id()==3015 and c.is_visible())
        report['repeat_before']=control.selected_text();control.select('Enable: Repeat playlist');time.sleep(.15)
        report['repeat_after']=control.selected_text()
        ui.child_window(title='OK',control_type='Button').wrapper_object().invoke()
        raise SystemExit(0)
    if args.inventory:
        report['pages']={}
        for page in ('General','Playback'):
            ui.child_window(title=page,control_type='TreeItem').wrapper_object().select();time.sleep(.3)
            native=Desktop(backend='win32').window(handle=h)
            report['pages'][page]=[{'id':c.control_id(),'selected':c.selected_text(),'choices':c.item_texts()} for c in native.descendants(class_name='ComboBox') if c.is_visible()]
        ui.child_window(title='Cancel',control_type='Button').wrapper_object().invoke()
        raise SystemExit(0)
    report['controls']=[{'name':c.window_text(),'type':c.element_info.control_type,'id':c.element_info.automation_id} for c in ui.descendants() if c.element_info.control_type in ('CheckBox','ComboBox','Text')]
    for text in ['Pause playback when minimized','Pause playback when focus lost']:
        control=ui.child_window(title=text,control_type='CheckBox').wrapper_object()
        report[text]={'before':control.get_toggle_state()}
        # Checked means both audio/video, indeterminate is video-only.
        for _ in range(3):
            if control.get_toggle_state()==1:break
            control.toggle()
            time.sleep(.15)
        report[text]['after']=control.get_toggle_state()
    ui.child_window(title='OK',control_type='Button').wrapper_object().invoke()
    time.sleep(.3)
finally:
    for r in windows(p.pid):
        if r['cls']=='PotPlayer64':u.SendMessageW(r['hwnd'],16,0,0)
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.terminate();p.wait()
    changed=ini.read_bytes();(base/'settings-before.ini').write_bytes(before);(base/'settings-after.ini').write_bytes(changed)
    old=set(before.decode('utf-16').splitlines());report['changed_lines']=[line for line in changed.decode('utf-16').splitlines() if line not in old]
    ini.write_bytes(before);(base/'result.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({'evidence':str(base),'changed_lines':report['changed_lines'],**{k:v for k,v in report.items() if k not in ('controls','changed_lines')}},ensure_ascii=False),flush=True)
