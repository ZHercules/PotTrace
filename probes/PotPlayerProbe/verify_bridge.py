"""Real player -> production bridge -> real SQLite smoke test. Uses isolated files."""
import os, subprocess, time, sqlite3, json
from pathlib import Path
from window_probe import windows,u
ROOT=Path(__file__).resolve().parents[2]
DATA=ROOT/('out/integration-data-'+str(time.time_ns()))
DATA.mkdir(parents=True)
(DATA/'settings.json').write_text('{"Mode":1}',encoding='utf-8')
config=ROOT/'out/player/Extension/Playback/Statistics/pottrace-bridge.ini'
config.write_text('[PotTrace]\nHostPath='+str(ROOT/'src/PotTrace.App/bin/Debug/net10.0-windows/PotTrace.exe')+'\nDataDirectory='+str(DATA)+'\nChannel=integration\n',encoding='utf-16')
EXE=ROOT/'out/player/PotPlayerMini64.exe'
env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(ROOT/'out/player-webview'),DOTNET_ROOT=str(ROOT.parents[1]/'.tools/dotnet'))
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
def launch(media=False):
    args=[str(EXE),'/new']
    if media:args.insert(1,str(ROOT/'out/fixtures/测试 影片.avi'));args.append('/seek=0')
    startup.wShowWindow=5 if media else 0
    p=subprocess.Popen(args,env=env,startupinfo=startup)
    for _ in range(70):
        rows=[r for r in windows(p.pid) if r['cls']=='PotPlayer64']
        if rows:return p,rows[0]['hwnd']
        time.sleep(.1)
    raise RuntimeError('Player did not start')
def close(p,h):
    u.SendMessageW(h,16,0,0)
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.terminate();p.wait()
def rows(sql):
    with sqlite3.connect(DATA/'pottrace.db') as c:return c.execute(sql).fetchall()
report={}
p,h=launch()
try:
    for _ in range(80):
        if (DATA/'pottrace.db').exists():break
        time.sleep(.1)
    assert (DATA/'pottrace.db').exists(),'Automatic host did not create its database'
    time.sleep(1)
    assert rows('SELECT count(*) FROM Session WHERE Valid=1')[0][0]==0
    report['automatic_host_without_media']=True
finally:close(p,h)
time.sleep(4)
p,h=launch(True)
try:
    time.sleep(6.5);u.SendMessageW(h,0x319,h,14<<16);time.sleep(5.5)
    result=rows('SELECT Payload FROM Session WHERE Valid=1')
    assert len(result)==1,result
    s=json.loads(result[0][0]);position=u.SendMessageW(h,0x400,0x5004,0)
    report['paused_media_position_ms']=position
    report['playback_error_ms']=s['PlayedMs']-position
    assert abs(s['PlayedMs']-position)<=500,(s,position)
    before=s['PlayedMs'];time.sleep(2)
    after=json.loads(rows('SELECT Payload FROM Session WHERE Valid=1')[0][0])['PlayedMs']
    assert before==after,(before,after)
    report['real_played_ms']=before;report['pause_does_not_add_time']=True
    report['segments']=rows('SELECT count(*) FROM Segment')[0][0]
finally:close(p,h)
time.sleep(4)
report['integrity_check']=rows('PRAGMA integrity_check')[0][0]
(ROOT/'out/bridge-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
