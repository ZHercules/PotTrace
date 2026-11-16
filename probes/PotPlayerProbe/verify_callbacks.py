"""Repeatable tests against the isolated, already-enabled player."""
import os, subprocess, time, json
from pathlib import Path
from window_probe import windows,u
ROOT=Path(__file__).resolve().parents[2]
EXE=ROOT/'out/player/PotPlayerMini64.exe'
LOG=ROOT/'out/player/Extension/Playback/Statistics/probe-native.tsv'
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'))
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
def launch(media=False):
    offset=LOG.stat().st_size if LOG.exists() else 0
    args=[str(EXE),'/new']
    if media:args.insert(1,str(ROOT/'out/fixtures/测试 影片.avi'))
    p=subprocess.Popen(args,env=env,startupinfo=startup)
    for _ in range(60):
        rows=[r for r in windows(p.pid) if r['cls']=='PotPlayer64']
        if rows: return p,rows[0]['hwnd'],offset
        time.sleep(.1)
    raise RuntimeError('Player window did not appear')
def close(p,h):
    u.SendMessageW(h,16,0,0)
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.terminate();p.wait()
def read(offset):
    with LOG.open('rb') as f:f.seek(offset);return f.read().decode('utf-8').splitlines()
report={}
p,h,offset=launch()
try:
    time.sleep(2);lines=read(offset)
    assert any('\tInitialize\t' in s for s in lines),lines
    assert not any('\tOpen\t' in s for s in lines),lines
    report['automatic_init_without_media']=True
finally:close(p,h)
p,h,offset=launch(True)
try:
    time.sleep(3)
    report['playing_status']=u.SendMessageW(h,0x400,0x5006,0)
    u.SendMessageW(h,0x319,h,14<<16)
    time.sleep(.3) # Allow the renderer's already queued frames to settle before measuring the pause.
    before=u.SendMessageW(h,0x400,0x5004,0)
    report['paused_status']=u.SendMessageW(h,0x400,0x5006,0)
    time.sleep(11)
    after=u.SendMessageW(h,0x400,0x5004,0)
    assert abs(after-before)<100,(before,after)
    report['paused_11_seconds_position_change_ms']=after-before
    u.SendMessageW(h,0x319,h,14<<16);time.sleep(1)
    u.SendMessageW(h,0x400,0x5005,29000);time.sleep(3)
    lines=read(offset)
    for kind in ['Open','Start','Time','Pause','Resume','Complete']:
        assert any('\t'+kind+'\t' in s for s in lines),(kind,lines)
    report['callbacks']=['Initialize','Open','Start','Time','Pause','Resume','Complete']
    report['time_argument_order']='duration_ms, position_ms'
finally:close(p,h)
lines=read(offset)
report['close_callback']=any('\tClose\t' in s for s in lines)
report['finalize_callback']=any('\tFinalize\t' in s for s in lines)
dest=ROOT/'out/probe-result.json';dest.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
