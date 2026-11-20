"""Live registry-profile acceptance using two same-name videos and the installed host."""
import argparse
from contextlib import closing, contextmanager
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import site
import sqlite3
import subprocess
import sys
import tempfile
import time
import winreg

ctypes.windll.user32.SetProcessDPIAware()
ROOT = Path(__file__).resolve().parents[2]
site.addsitedir(str(ROOT / '.tools/python'))
from PIL import ImageGrab
import imageio_ffmpeg
import psutil
from run_collector_probe import clock, guard, send, windows, u


def wait_for(test, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if result := test():
            return result
        time.sleep(.05)
    raise TimeoutError('Expected playback observation did not arrive')


def value(key, name):
    try:
        return winreg.QueryValueEx(key, name)
    except FileNotFoundError:
        return None


def idle():
    guard()
    if any(p.info['name'] == 'PotPlayerMini64.exe' for p in psutil.process_iter(['name'])):
        raise RuntimeError('Another player is running')


@contextmanager
def preload_profile(player, report):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\DAUM\PotPlayer64') as install:
        if Path(winreg.QueryValueEx(install, 'ProgramPath')[0]) != player or player.with_suffix('.ini').exists():
            raise RuntimeError('Expected registered player and registry configuration')
    def settings():
        return winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\DAUM\PotPlayerMini64\Settings',
                              access=winreg.KEY_QUERY_VALUE | winreg.KEY_SET_VALUE)
    with settings() as key:
        before = value(key, 'SeamlessPlay')
        renderer = value(key, 'VideoRen2')
        report['renderer_before']=renderer
        idle()
        winreg.SetValueEx(key, 'SeamlessPlay', 0, winreg.REG_DWORD, 1)
    try:
        yield
    finally:
        idle()
        with settings() as key:
            current = value(key, 'SeamlessPlay')
            if current != before:
                if current != (1, winreg.REG_DWORD):
                    raise RuntimeError('Preload preference changed independently')
                if before is None:
                    winreg.DeleteValue(key, 'SeamlessPlay')
                else:
                    winreg.SetValueEx(key, 'SeamlessPlay', 0, before[1], before[0])
            report['preferences_restored'] = value(key, 'SeamlessPlay') == before and value(key, 'VideoRen2') == renderer
            report['renderer_after']=value(key,'VideoRen2')


def snapshot(db, paths):
    result = []
    with closing(sqlite3.connect(db.as_uri() + '?mode=ro', uri=True)) as con:
        con.execute('BEGIN')
        for path in paths:
            rows = con.execute('SELECT s.Payload FROM Session s JOIN MediaSource m ON m.Id=s.SourceId '
                               'WHERE m.Path=? ORDER BY s.StartedUtc', (str(path),))
            for payload, in rows:
                session = json.loads(payload)
                segments = con.execute("SELECT Payload FROM Segment WHERE SessionId=? ORDER BY json_extract(Payload,'$.Order')",
                                       (session['SessionId'],))
                result.append(dict(source=path.parent.name, id=session['SessionId'], valid=session['IsValid'],
                                   ended=session['EndedUtc'] is not None, played=session['PlayedMs'], stay=session['StayMs'],
                                   segments=[{k: seg[k] for k in ('StartMs', 'EndMs', 'PlayedMs', 'Order')}
                                             for raw, in segments for seg in [json.loads(raw)]]))
    return result


def witness(pid, hwnd, paths):
    opened = {Path(f.path) for f in psutil.Process(pid).open_files()}
    rect, origin = wintypes.RECT(), wintypes.POINT()
    u.GetClientRect(hwnd, ctypes.byref(rect))
    u.ClientToScreen(hwnd, ctypes.byref(origin))
    u.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
    topmost = bool(u.GetWindowLongW(hwnd,-20) & 8)
    try:
        u.SetWindowPos(hwnd,-1,0,0,0,0,0x13);time.sleep(.15)
        img = ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + rect.right, origin.y + rect.bottom),all_screens=True)
    finally:
        if not topmost:u.SetWindowPos(hwnd,-2,0,0,0,0,0x13)
    pixels = img.resize((80, 60)).convert('RGB').get_flattened_data()
    red = sum(r > 100 and r > g * 1.6 and r > b * 1.6 for r,g,b in pixels)
    blue = sum(b > 100 and b > r * 1.6 and b > g * 1.6 for r,g,b in pixels)
    return dict(open=[p.parent.name for p in paths if p in opened],
                color='red' if red > max(250,blue*2) else 'blue' if blue > max(250,red*2) else 'unknown',
                duration=send(hwnd,0x400,0x5002), position=send(hwnd,0x400,0x5004))


def main(player,equal_duration=False):
    if not __debug__:
        raise RuntimeError('Assertions are required')
    player = player.resolve()
    record = json.loads((player.parent/'Extension/Playback/Statistics/pottrace-install.json').read_text(encoding='utf-8-sig'))
    assert record['RegistryBackend']
    db = Path(record['DataRoot'])/'pottrace.db'
    base = Path(tempfile.mkdtemp(dir=ROOT/'out', prefix='confirmed-preload-'))
    print(base, flush=True)
    paths = [base / folder / (base.name+'.mp4') for folder in ('A','B')]
    for path,color,duration in zip(paths,('red','blue'),(17,17 if equal_duration else 40)):
        path.parent.mkdir()
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-loglevel','error','-f','lavfi','-i',
                        f'color={color}:size=480x270:rate=30','-t',str(duration),'-c:v','libx264',
                        '-preset','ultrafast',str(path)],check=True)
    report = {'runs':[], 'equal_duration':equal_duration,'package':record['Version'],'registry_backend':True}
    try:
        with preload_profile(player,report):
            for cancel in (True,False):
                idle()
                run = {'cancel':cancel}
                report['runs'].append(run)
                proc = subprocess.Popen([str(player),*map(str,paths),'/new','/seek=0'],
                                        env=dict(os.environ,WEBVIEW2_USER_DATA_FOLDER=str(base/'webview')))
                try:
                    hwnd = wait_for(lambda:next((w['hwnd'] for w in windows(proc.pid) if w['cls']=='PotPlayer64'),None),15)
                    wait_for(lambda:send(hwnd,0x400,0x5006)==2 and abs(send(hwnd,0x400,0x5002)-17000)<150,10)
                    started = time.monotonic()
                    run['playing_tick'] = clock()
                    time.sleep(7)
                    send(hwnd,0x400,0x5007,1)
                    run['first_wall_ms'] = round((time.monotonic()-started)*1000)
                    time.sleep(.8)
                    run['held_before'] = snapshot(db,paths)
                    time.sleep(11)
                    run['red'] = witness(proc.pid,hwnd,paths)
                    health_file = player.parent/'Extension/Playback/Statistics/collector-health.json'
                    if health_file.exists():
                        run['health'] = json.loads(health_file.read_bytes())
                    run['held_after'] = snapshot(db,paths)
                    before,after = run['held_before'],run['held_after']
                    run['only_expected_current'] = (len(after)==len(report['runs']) and
                        all(s['source']=='A' for s in before+after) and bool(after) and after[-1]['valid'])
                    if after and not after[-1]['ended']:
                        current=after[-1]
                        earlier=next((s for s in before if s['id']==current['id']),None)
                        if earlier:
                            run['pause_verified'] = (earlier['played']==current['played'] and current['stay']-earlier['stay']>10000)
                        else:
                            # The first checkpoint may predate delivery of a named Time callback.
                            # Check the reconciled dwell and observe a second persisted paused point.
                            time.sleep(1.2)
                            later=next((s for s in snapshot(db,paths) if s['id']==current['id']),None)
                            run['pause_verified'] = bool(later and later['played']==current['played'] and
                                later['stay']>current['stay'] and current['stay']-current['played']>10000)
                        run['first_error_ms']=current['played']-run['first_wall_ms']
                    if not cancel:
                        resumed=time.monotonic()
                        send(hwnd,0x400,0x5007,2)
                        def next_playing():
                            return (abs(send(hwnd,0x400,0x5002)-(17000 if equal_duration else 40000))<150
                                    and send(hwnd,0x400,0x5006)==2 and 0<=send(hwnd,0x400,0x5004)<1200)
                        wait_for(next_playing,18)
                        second_started=time.monotonic()
                        run['first_total_wall_ms']=run['first_wall_ms']+round((second_started-resumed)*1000)
                        time.sleep(7)
                        run['blue']=witness(proc.pid,hwnd,paths)
                        send(hwnd,0x400,0x5007,1)
                        first_pass_ms=round((time.monotonic()-second_started)*1000)
                        time.sleep(.65)
                        run['blue_first_end_ms']=send(hwnd,0x400,0x5004)
                        send(hwnd,0x400,0x5005,0)
                        wait_for(lambda:send(hwnd,0x400,0x5004)<1200,3)
                        time.sleep(.55)
                        run['blue_repeat_start_ms']=send(hwnd,0x400,0x5004)
                        repeat_started=time.monotonic()
                        send(hwnd,0x400,0x5007,2)
                        time.sleep(7)
                        send(hwnd,0x400,0x5007,1)
                        run['second_wall_ms']=first_pass_ms+round((time.monotonic()-repeat_started)*1000)
                        time.sleep(1)
                        run['blue_repeat_end_ms']=send(hwnd,0x400,0x5004)
                finally:
                    for w in windows(proc.pid):
                        if w['cls']=='PotPlayer64':u.PostMessageW(w['hwnd'],16,0,0)
                    proc.wait(timeout=20)
                    run['exit_code']=proc.returncode
                time.sleep(.8)
                run['closed']=snapshot(db,paths)
                assert proc.returncode==0
            rows=report['runs'][-1]['closed']
            with closing(sqlite3.connect(db.as_uri()+'?mode=ro',uri=True)) as con:
                totals = con.execute('SELECT COUNT(DISTINCT s.MovieKey), COUNT(*), SUM(s.PlayedMs) '
                                     'FROM Session s JOIN MediaSource m ON m.Id=s.SourceId '
                                     'WHERE s.Valid=1 AND m.Path IN (?,?)', tuple(map(str,paths))).fetchone()
                report['blue_visits_at_5s'] = con.execute('SELECT COUNT(*) FROM Segment g JOIN MediaSource m '
                    'ON m.Id=g.SourceId WHERE m.Path=? AND g.StartMs<=5000 AND g.EndMs>5000',(str(paths[1]),)).fetchone()[0]
            report['movie_summary'] = dict(movies=totals[0], times=totals[1], played_ms=totals[2])
            checks = report['checks'] = {
                'preload_held_on_red': all(set(r['red']['open'])=={'A','B'} and r['red']['color']=='red' for r in report['runs']),
                'natural_next_blue': run['blue']['color']=='blue',
                'source_counts': [r['source'] for r in rows]==['A','A','B'] and all(r['valid'] and r['ended'] for r in rows),
                'same_name_summary': totals==(1,3,sum(r['played'] for r in rows)),
                'blue_5s_covered_twice': report['blue_visits_at_5s']==2,
                'segment_counts': [len(row['segments']) for row in rows]==[1,2,2],
                'segment_totals': all(all(s['PlayedMs']>5000 for s in row['segments']) and
                                      abs(sum(s['PlayedMs'] for s in row['segments'])-row['played'])<100 for row in rows),
                'pause_dwell_only': all(r.get('pause_verified') for r in report['runs']),
                'preloaded_source_excluded': all(r['only_expected_current'] for r in report['runs']),
                'initial_play_time': all(abs(r.get('first_error_ms',10000))<100 for r in report['runs']),
                'initial_position_time': all(r['held_after'] and abs(r['held_after'][-1]['played']-r['red']['position'])<100 for r in report['runs']),
            }
            if checks['source_counts']:
                checks['first_episode_total'] = abs(rows[1]['played']-run['red']['duration'])<100
                checks['second_episode_wall'] = abs(rows[2]['played']-run['second_wall_ms'])<100
                external_played=run['blue_first_end_ms']+run['blue_repeat_end_ms']-run['blue_repeat_start_ms']
                checks['second_episode_positions'] = abs(rows[2]['played']-external_played)<100
                checks['repeated_range'] = all(s['StartMs']<100 and s['EndMs']>6000 for s in rows[-1]['segments'])
        report['passed'] = all(checks.values()) and report['preferences_restored']
        if not report['passed']:
            raise AssertionError([name for name, passed in checks.items() if not passed])
    finally:
        (base/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print('passed',report.get('passed',False),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player',required=True,type=Path)
    parser.add_argument('--equal-duration',action='store_true')
    args=parser.parse_args()
    main(args.player,args.equal_duration)
