"""Isolated manual/API probe; never targets the user's installed player."""
import os, subprocess, time, sys, site
from pathlib import Path
from window_probe import windows, u

ROOT = Path(__file__).resolve().parents[2]
PLAYER = ROOT / 'out/player/PotPlayerMini64.exe'
env = dict(os.environ, WEBVIEW2_USER_DATA_FOLDER=str(ROOT / 'out/player-webview'))
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
process = subprocess.Popen([str(PLAYER), '/new'], env=env, startupinfo=startup)
try:
    time.sleep(2)
    main = next(r['hwnd'] for r in windows(process.pid) if r['cls']=='PotPlayer64')
    for attempt in range(3):
        u.PostMessageW(main, 0x100, 0x74, 0)
        u.PostMessageW(main, 0x101, 0x74, 0)
        time.sleep(1)
        if any(r['cls']=='#32770' for r in windows(process.pid)): break
    site.addsitedir(str(ROOT.parents[1] / '.tools/python'))
    from pywinauto import Desktop
    rows = windows(process.pid, True)
    dialog = next(r['hwnd'] for r in rows if r['cls']=='#32770')
    ui = Desktop(backend='uia').window(handle=dialog)
    store=ui.child_window(title='Store settings into .ini file',control_type='CheckBox').wrapper_object()
    print('StoreIni',store.get_toggle_state())
    if store.get_toggle_state()==0: store.toggle()
    node=ui.child_window(title='Extensions',control_type='TreeItem').wrapper_object()
    node.expand()
    time.sleep(.3)
    ui.child_window(title='Playback Statistics',control_type='TreeItem').wrapper_object().select()
    time.sleep(.3)
    plugin=ui.child_window(title='PotTrace Probe',control_type='ListItem').wrapper_object()
    print('Plugin checked', plugin.iface_toggle.CurrentToggleState)
    if plugin.iface_toggle.CurrentToggleState==0: plugin.iface_toggle.Toggle()
    ui.child_window(title='OK',control_type='Button').wrapper_object().invoke()
    time.sleep(.5)
    subprocess.run([str(PLAYER),str(ROOT/'out/fixtures/测试 影片.avi'),'/current'],env=env,startupinfo=startup,timeout=5)
    time.sleep(3)
    print('Position query',u.SendMessageW(main,0x400,0x5004,0),'Duration',u.SendMessageW(main,0x400,0x5002,0), flush=True)
    print('Pause return',u.SendMessageW(main,0x319,main,14<<16),flush=True)
    time.sleep(11)
    print('Paused position',u.SendMessageW(main,0x400,0x5004,0),flush=True)
    print('Resume return',u.SendMessageW(main,0x319,main,14<<16),flush=True)
    time.sleep(2)
    u.SendMessageW(main,0x400,0x5005,20000)
    time.sleep(2)
    u.SendMessageW(main,0x319,main,13<<16)
    time.sleep(1)
    print((ROOT/'out/player/Extension/Playback/Statistics/probe-native.tsv').read_text(encoding='utf-8')[-5000:])
finally:
    for row in windows(process.pid):
        if row['cls']=='PotPlayer64': u.SendMessageW(row['hwnd'],16,0,0)
    try: process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
