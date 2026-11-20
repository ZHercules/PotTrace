"""Review the real WPF history against a copy of a completed online fixture DB."""
import argparse,json,os,site,sqlite3,subprocess,sys,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'probes/PotPlayerProbe'))
from probe_tools import TOOLS
site.addsitedir(str(TOOLS / 'python'))
from pywinauto import Application
import win32gui
from window_probe import windows

parser=argparse.ArgumentParser()
parser.add_argument('evidence',type=Path)
args=parser.parse_args()
evidence=args.evidence.resolve()
if not evidence.is_relative_to((ROOT/'out').resolve()):raise SystemExit('Only isolated recorded fixtures allowed')
record=json.loads((evidence/'result.json').read_text(encoding='utf-8'))
if not record.get('basic_probe_passed') or not record.get('detailed_flow',record.get('double_speed')):raise SystemExit('Use a completed detailed online run')
dest=ROOT/('out/graph-ui-'+str(time.time_ns()));dest.mkdir()
data=dest/'data';data.mkdir()
with sqlite3.connect(f'file:{(evidence/"data/pottrace.db").as_posix()}?mode=ro',uri=True) as source,sqlite3.connect(data/'pottrace.db') as target:source.backup(target)
(data/'settings.json').write_text('{"Mode":1,"Theme":"Dark"}',encoding='utf-8')
with sqlite3.connect(data/'pottrace.db') as db:
 sessions=[json.loads(x[0]) for x in db.execute('SELECT Payload FROM Session')]
 segments=[json.loads(x[0]) for x in db.execute('SELECT Payload FROM Segment')]
a=next(s for s in sessions if 'source-a' in s['SourcePath'])
a_segments=sorted([s for s in segments if s['SessionId']==a['SessionId']],key=lambda s:s['Order'])
assert len(a_segments)==4
# Use observed endpoints: the first progressing sample after a seek may be a
# few ms beyond the requested 10000. Do not invent that unobserved coverage.
repeat_heat_start=max(s['StartMs'] for s in a_segments[1:])
repeat_heat_end=min(s['EndMs'] for s in a_segments[1:])
assert repeat_heat_end-repeat_heat_start>=6000 and a_segments[0]['EndMs']<repeat_heat_start
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=1
p=subprocess.Popen([str(ROOT/'src/PotTrace.App/bin/Debug/net10.0-windows/PotTrace.exe'),'--show','--data-dir',str(data),'--channel','graphui'+str(time.time_ns())],env=dict(os.environ,DOTNET_ROOT=str(TOOLS / 'dotnet')),startupinfo=si)
report={'evidence':str(evidence),'original_database_modified':False,'passed':False,'repeat_heat_start_ms':repeat_heat_start,'repeat_heat_end_ms':repeat_heat_end}
def wait_for(check,description):
 deadline=time.monotonic()+15
 while time.monotonic()<deadline:
  value=check()
  if value:return value
  time.sleep(.1)
 raise AssertionError(description)
def rows(win,name):
 return win.child_window(auto_id=name,control_type='DataGrid').children(control_type='DataItem')
def cell_text(row):
 return [item.window_text() for item in row.descendants(control_type='Text')]
def snapshot(win,name):
 win.capture_as_image().save(dest/(name+'.png'))
try:
 app=Application(backend='uia').connect(process=p.pid,timeout=10)
 handle=wait_for(lambda:next((w['hwnd'] for w in windows(p.pid) if w['title']=='PotTrace · 播放统计'),None),'History window missing')
 win32gui.SetWindowPos(handle,-1,0,0,0,0,0x13);win=app.window(handle=handle)
 movie=wait_for(lambda:next((row for row in rows(win,'Movies') if 'same-name.mp4' in cell_text(row)),None),'Actual same-name movie summary missing')
 report['movie_summary']=cell_text(movie)
 assert report['movie_summary'][1]=='2',report['movie_summary']
 movie.select();win.child_window(title='详细',control_type='Button').invoke()
 session_rows=wait_for(lambda:rows(win,'Sessions') if len(rows(win,'Sessions'))==2 else None,'Same-name sessions were not distinct')
 report['sessions']=[cell_text(row) for row in session_rows]
 expected_played=f'{a["PlayedMs"]//3600000:02}:{a["PlayedMs"]//60000%60:02}:{a["PlayedMs"]//1000%60:02}'
 next(row for row in session_rows if expected_played in cell_text(row)).select()
 wait_for(lambda:len(rows(win,'Segments'))==4,'Four actual detailed segments missing')
 report['segments']=[cell_text(row) for row in rows(win,'Segments')]
 for cells,segment in zip(report['segments'],a_segments):
  assert f'{segment["PlayedMs"]/1000:.3f} 秒' in cells,(cells,segment)
 snapshot(win,'actual-segments')
 win.child_window(title='高频区间',control_type='TabItem').select()
 wait_for(lambda:any('3' in cell_text(row) for row in rows(win,'Hotspots')),'Real repeated coverage count 3 missing')
 report['hotspots']=[cell_text(row) for row in rows(win,'Hotspots')]
 def media(ms):return f'{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02}.{ms%1000:03}'
 expected_range=f'{media(repeat_heat_start)} — {media(repeat_heat_end)}'
 assert any(expected_range in cells and '3' in cells for cells in report['hotspots']),report['hotspots']
 snapshot(win,'actual-hotspots')
 # A deliberate heat-source selection must survive periodic list refreshes.
 combo=win.child_window(auto_id='Sources',control_type='ComboBox')
 combo.expand();time.sleep(.2)
 next(item for item in combo.descendants(control_type='ListItem') if 'source-b' in item.window_text()).select();combo.collapse()
 wait_for(lambda:not rows(win,'Hotspots'),'Source B should have no >5 second heat segments')
 time.sleep(6)
 assert not rows(win,'Hotspots'),'Periodic refresh overrode the explicitly selected heat source'
 report['manual_heat_source_survives_refresh']=True;report['passed']=True
except BaseException as error:
 report['failure']={'type':type(error).__name__,'message':str(error)}
 raise
finally:
 # This process owns only the copied fixture database; preserve original evidence.
 p.terminate();p.wait(timeout=5)
 (dest/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps({'output':str(dest),**report},ensure_ascii=False))
