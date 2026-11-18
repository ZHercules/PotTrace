"""Build a new synthetic 100k-session fixture; never touch real history."""
import sqlite3,json,time,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'out/ui-data/pottrace.db'
dest=ROOT/('out/benchmark-'+str(time.time_ns())+'.db')
with sqlite3.connect(source) as original, sqlite3.connect(dest) as c:
    original.backup(c)
    row=c.execute('SELECT * FROM Session LIMIT 1').fetchone()
    segment=json.loads(c.execute('SELECT Payload FROM Segment LIMIT 1').fetchone()[0])
    payload=json.loads(row[7]);movie=row[2];source_id=row[1]
    c.executescript('DELETE FROM Segment;DELETE FROM TimelineEvent;DELETE FROM SessionDay;DELETE FROM Session;DELETE FROM Metadata;DELETE FROM IngestionCheckpoint;')
    began=time.perf_counter()
    for n in range(100000):
        session=str(uuid.UUID(int=n+1));payload['SessionId']=session
        c.execute('INSERT INTO Session VALUES(?,?,?,?,?,?,?,?)',(session,source_id,movie,row[3],row[4],1,33000,json.dumps(payload)))
        segment['SessionId']=session;segment['SegmentId']=str(uuid.UUID(int=n+1))
        c.execute('INSERT INTO Segment VALUES(?,?,?,?,?,?)',(segment['SegmentId'],session,source_id,segment['StartMs'],segment['EndMs'],json.dumps(segment)))
    c.commit()
    report={'database':str(dest),'sessions':100000,'segments':100000,'seed_seconds':time.perf_counter()-began}
    began=time.perf_counter();result=c.execute('SELECT MovieKey,count(*),sum(PlayedMs) FROM Session WHERE Valid=1 GROUP BY MovieKey ORDER BY max(StartedUtc) DESC LIMIT 100').fetchall()
    report['summary_ms']=(time.perf_counter()-began)*1000
    assert result[0][1:]==(100000,3300000000),result
    began=time.perf_counter();rows=c.execute('SELECT Payload FROM Session WHERE Valid=1 AND MovieKey=? ORDER BY StartedUtc DESC LIMIT 1000',(movie,)).fetchall()
    assert len(rows)==1000;report['session_page_ms']=(time.perf_counter()-began)*1000
    report['integrity']=c.execute('PRAGMA integrity_check').fetchone()[0]
(ROOT/'out/benchmark-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
