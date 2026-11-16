using PotTrace.Contracts;
using PotTrace.Core;
using Microsoft.Data.Sqlite;
using System.Text.Json;
namespace PotTrace.Storage;
public sealed class IngestionWriter
{
    private readonly Database database;
    private readonly SessionTracker tracker;
    private IReadOnlyList<SessionRecord> activity=[];
    public IReadOnlyList<SessionRecord> Activity=>Volatile.Read(ref activity);
    public IngestionWriter(Database database,SessionTracker tracker)
    {
        this.database=database;this.tracker=tracker;
        using var c=database.Open();using var cmd=Database.Command(c,null,"SELECT Value FROM Metadata WHERE Key='tracker'");
        if(cmd.ExecuteScalar() is string json)tracker.ImportCheckpoint(json);
    }
    public async Task<IngestReceipt> ApplyAsync(PlaybackEvent e,CancellationToken ct)
    {
        var validation=EventValidator.Validate(e);if(!validation.IsValid)throw new InvalidDataException(validation.Error);
        await database.WriteGate.WaitAsync(ct);
        string before=tracker.ExportCheckpoint();
        try
        {
            using var c=database.Open();using var tx=c.BeginTransaction();
            using var last=Database.Command(c,tx,"SELECT Sequence FROM IngestionCheckpoint WHERE InstanceId=$id",("$id",e.InstanceId.ToString()));
            long sequence=Convert.ToInt64(last.ExecuteScalar()??0L);
            if(sequence>=e.Sequence)return new(sequence,true);
            using var deleted=Database.Command(c,tx,"SELECT 1 FROM DeletionTombstone WHERE InstanceId=$id AND MediaEpoch=$epoch",("$id",e.InstanceId.ToString()),("$epoch",e.MediaEpoch?.ToString()));
            if(deleted.ExecuteScalar() is null)Persist(c,tx,tracker.Apply(e));
            using var ack=Database.Command(c,tx,"INSERT INTO IngestionCheckpoint VALUES($id,$seq) ON CONFLICT(InstanceId) DO UPDATE SET Sequence=$seq",("$id",e.InstanceId.ToString()),("$seq",e.Sequence));ack.ExecuteNonQuery();
            SaveCheckpoint(c,tx);tx.Commit();Volatile.Write(ref activity,tracker.CurrentSessions);return new(e.Sequence,false);
        }
        catch{tracker.ImportCheckpoint(before);throw;}
        finally{database.WriteGate.Release();}
    }
    public void SetNextCaptureMode(CaptureMode mode)=>tracker.SetNextCaptureMode(mode);
    public IReadOnlyList<Guid> ActiveInstances=>tracker.ActiveInstances;
    public Task DisconnectAsync(Guid instanceId,CancellationToken ct)=>ChangeAsync(()=>tracker.Disconnect(instanceId),ct);
    public Task SetCaptureEnabledAsync(bool enabled,CancellationToken ct)=>ChangeAsync(()=>tracker.SetCaptureEnabled(enabled,DateTimeOffset.UtcNow,Environment.TickCount64),ct);
    private async Task ChangeAsync(Func<EngineUpdate> change,CancellationToken ct)
    {
        await database.WriteGate.WaitAsync(ct);string before=tracker.ExportCheckpoint();
        try{using var c=database.Open();using var tx=c.BeginTransaction();Persist(c,tx,change());SaveCheckpoint(c,tx);tx.Commit();Volatile.Write(ref activity,tracker.CurrentSessions);}
        catch{tracker.ImportCheckpoint(before);throw;}
        finally{database.WriteGate.Release();}
    }
    internal void DiscardSessions(IReadOnlySet<Guid> sessionIds)=>tracker.DiscardSessions(sessionIds);
    internal string CaptureCheckpoint()=>tracker.ExportCheckpoint();
    internal void RestoreCheckpoint(string checkpoint)=>tracker.ImportCheckpoint(checkpoint);
    internal void PublishActivity()=>Volatile.Write(ref activity,tracker.CurrentSessions);
    internal void ReloadCheckpoint(SqliteConnection c)
    {
        using var cmd=Database.Command(c,null,"SELECT Value FROM Metadata WHERE Key='tracker'");
        tracker.ImportCheckpoint(cmd.ExecuteScalar() is string json?json:new SessionTracker().ExportCheckpoint());
    }
    internal void SaveCheckpoint(SqliteConnection c,SqliteTransaction tx)
    {using var cmd=Database.Command(c,tx,"INSERT INTO Metadata VALUES('tracker',$json) ON CONFLICT(Key) DO UPDATE SET Value=$json",("$json",tracker.ExportCheckpoint()));cmd.ExecuteNonQuery();}

    internal static void Persist(SqliteConnection c,SqliteTransaction tx,EngineUpdate update)
    {
        foreach(var s in update.Sessions)
        {
            if(!s.IsValid && s.EndedUtc is not null)
            {using var remove=Database.Command(c,tx,"DELETE FROM Session WHERE Id=$id",("$id",s.SessionId.ToString()));remove.ExecuteNonQuery();continue;}
            using var movie=Database.Command(c,tx,"INSERT OR IGNORE INTO Movie VALUES($key,$name)",("$key",s.MovieKey),("$name",System.IO.Path.GetFileName(s.SourcePath)));movie.ExecuteNonQuery();
            using var source=Database.Command(c,tx,"INSERT OR IGNORE INTO MediaSource VALUES($id,$key,$path)",("$id",s.SourceId.ToString()),("$key",s.MovieKey),("$path",s.SourcePath));source.ExecuteNonQuery();
            var local=TimeZoneInfo.ConvertTime(s.StartedUtc,TimeZoneInfo.FindSystemTimeZoneById(s.TimeZoneId));
            using var cmd=Database.Command(c,tx,"INSERT INTO Session VALUES($id,$source,$key,$date,$utc,$valid,$played,$json) ON CONFLICT(Id) DO UPDATE SET Valid=$valid,PlayedMs=$played,Payload=$json",
                ("$id",s.SessionId.ToString()),("$source",s.SourceId.ToString()),("$key",s.MovieKey),("$date",local.ToString("yyyy-MM-dd")),("$utc",s.StartedUtc.ToString("O")),("$valid",s.IsValid?1:0),("$played",s.PlayedMs),("$json",JsonSerializer.Serialize(s)));cmd.ExecuteNonQuery();
        }
        var retained=update.Sessions.Where(s=>s.IsValid||s.EndedUtc is null).Select(s=>s.SessionId).ToHashSet();
        foreach(var d in update.Days.Where(d=>retained.Contains(d.SessionId)))
        {using var cmd=Database.Command(c,tx,"INSERT INTO SessionDay VALUES($id,$date,$stay,$play,$short) ON CONFLICT(SessionId,Date) DO UPDATE SET StayMs=$stay,PlayedMs=$play,ShortPlayedMs=$short",("$id",d.SessionId.ToString()),("$date",d.Date.ToString("yyyy-MM-dd")),("$stay",d.StayMs),("$play",d.PlayedMs),("$short",d.ShortPlayedMs));cmd.ExecuteNonQuery();}
        foreach(var s in update.Segments)
        {using var cmd=Database.Command(c,tx,"INSERT INTO Segment VALUES($id,$session,$source,$start,$end,$json) ON CONFLICT(Id) DO UPDATE SET EndMs=$end,Payload=$json",("$id",s.SegmentId.ToString()),("$session",s.SessionId.ToString()),("$source",s.SourceId.ToString()),("$start",s.StartMs),("$end",s.EndMs),("$json",JsonSerializer.Serialize(s)));cmd.ExecuteNonQuery();}
        foreach(var e in update.Timeline)
        {using var cmd=Database.Command(c,tx,"INSERT OR IGNORE INTO TimelineEvent VALUES($session,$seq,$json)",("$session",e.SessionId.ToString()),("$seq",e.Sequence),("$json",JsonSerializer.Serialize(e)));cmd.ExecuteNonQuery();}
    }
}
