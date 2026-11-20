"""Capture the standalone WPF window using an explicitly generated sample database."""
import site,os,subprocess,time,sys,ctypes,json
from pathlib import Path
from probe_tools import TOOLS
site.addsitedir(str(TOOLS / 'python'))
from pywinauto import Application
from window_probe import windows
import win32gui
import win32api
def capture(win,path):
    hwnd=win.handle
    try:
        win32gui.SetWindowPos(hwnd,-1,0,0,0,0,0x13);time.sleep(.4)
        win.capture_as_image().save(path)
    finally:win32gui.SetWindowPos(hwnd,-2,0,0,0,0,0x13)
ROOT=Path(__file__).resolve().parents[2]
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=1
env=dict(os.environ,DOTNET_ROOT=str(TOOLS / 'dotnet'))
theme='Light' if '--light' in sys.argv else 'Dark'
settings=ROOT/'out/ui-data/settings.json';before=settings.read_bytes() if settings.exists() else None
settings.write_text(json.dumps({'Theme':theme}),encoding='utf-8')
p=subprocess.Popen([str(ROOT/'src/PotTrace.App/bin/Release/net10.0-windows/PotTrace.exe'),'--show','--data-dir',str(ROOT/'out/ui-data'),'--channel','uireview'],env=env,startupinfo=startup)
try:
    app=Application(backend='uia').connect(process=p.pid,timeout=10)
    deadline=time.monotonic()+15;handle=None
    while time.monotonic()<deadline:
        handle=next((w['hwnd'] for w in windows(p.pid) if w['title']=='PotTrace · 播放统计'),None)
        if handle:break
        time.sleep(.1)
    assert handle,'History window was not created'
    # UIA can mark a window occluded by an unrelated topmost app off-screen.
    # Bring only our owned fixture forward before querying its controls.
    win32gui.SetWindowPos(handle,-1,0,0,0,0,0x13)
    win=app.window(handle=handle)
    try:win.wait('visible',timeout=15)
    except Exception:
        print('UI wait failed:',p.poll(),windows(p.pid),flush=True);raise
    time.sleep(1)
    dest=ROOT/'artifacts/ui-review';dest.mkdir(parents=True,exist_ok=True)
    capture(win,dest/('simple-'+theme.lower()+'.png'))
    win.child_window(title='详细',control_type='RadioButton').select();time.sleep(1)
    win.child_window(title='观看会话',control_type='Text').wait('visible',timeout=5)
    capture(win,dest/('detailed-'+theme.lower()+'.png'))
    heading=win.child_window(title='片段热度',control_type='Text').rectangle()
    legend=win.child_window(title='覆盖次数 · 每秒采样',control_type='Text').rectangle()
    scale=ctypes.windll.user32.GetDpiForWindow(handle)/96
    previous=win32api.GetCursorPos()
    try:
        win32api.SetCursorPos(((heading.left+legend.right)//2,heading.bottom+round(36*scale)))
        time.sleep(.3)
        bounds=win.rectangle()
        chart=win.capture_as_image().crop((heading.left-bounds.left,heading.top-bounds.top,
            legend.right-bounds.left,heading.bottom-bounds.top+round(108*scale)))
        chart.save(dest/('heat-hover-'+theme.lower()+'.png'))
    finally:win32api.SetCursorPos(previous)
    win.child_window(title='设置',control_type='MenuItem').invoke()
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        dialog_handle=next((w['hwnd'] for w in windows(p.pid) if w['title']=='PotTrace · 设置'),None)
        if dialog_handle:break
        time.sleep(.1)
    assert dialog_handle,windows(p.pid)
    dialog=app.window(handle=dialog_handle)
    dialog.wait('visible',timeout=5)
    capture(dialog,dest/('settings-'+theme.lower()+'.png'))
    print('Captured simple, detailed and settings windows')
finally:
    # This script owns this explicitly launched history-only test process.
    p.terminate();p.wait(timeout=5)
    if before is None:settings.unlink()
    else:settings.write_bytes(before)
