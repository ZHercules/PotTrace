using Microsoft.Data.Sqlite;
using PotTrace.Core;
using System.Text.Json;
namespace PotTrace.Storage;
public sealed class BackupService
{
    private readonly Database database;
    private readonly IngestionWriter writer;
    public BackupService(Database database,IngestionWriter writer) {this.database=database;this.writer=writer;}
    private void CheckDifferent(string other)
    {if(System.IO.Path.GetFullPath(other).Equals(System.IO.Path.GetFullPath(database.Path),StringComparison.OrdinalIgnoreCase))throw new ArgumentException("Backup must use a different file");}
    public async Task CreateAsync(string destination,CancellationToken ct)
    {
        CheckDifferent(destination);await database.WriteGate.WaitAsync(ct);
        try{using var source=database.Open();using var target=new SqliteConnection(new SqliteConnectionStringBuilder{DataSource=destination,Pooling=false}.ToString());target.Open();source.BackupDatabase(target);}
        finally{database.WriteGate.Release();}
    }
    public async Task RestoreAsync(string backup,CancellationToken ct)
    {
        CheckDifferent(backup);
        using var source=new SqliteConnection(new SqliteConnectionStringBuilder{DataSource=backup,Mode=SqliteOpenMode.ReadOnly,Pooling=false}.ToString());await source.OpenAsync(ct);
        using(var check=Database.Command(source,null,"PRAGMA integrity_check"))if((string?)await check.ExecuteScalarAsync(ct)!="ok")throw new InvalidDataException("Backup integrity check failed");
        using(var version=Database.Command(source,null,"SELECT max(Version) FROM SchemaVersion"))if(Convert.ToInt32(await version.ExecuteScalarAsync(ct))!=1)throw new InvalidDataException("Unsupported backup schema");
        foreach(string sql in new[]{
            "SELECT MovieKey,Name FROM Movie LIMIT 0","SELECT Id,MovieKey,Path FROM MediaSource LIMIT 0",
            "SELECT Id,SourceId,MovieKey,OpenedDate,StartedUtc,Valid,PlayedMs,Payload FROM Session LIMIT 0",
            "SELECT SessionId,Date,StayMs,PlayedMs,ShortPlayedMs FROM SessionDay LIMIT 0",
            "SELECT Id,SessionId,SourceId,StartMs,EndMs,Payload FROM Segment LIMIT 0",
            "SELECT SessionId,Sequence,Payload FROM TimelineEvent LIMIT 0","SELECT InstanceId,Sequence FROM IngestionCheckpoint LIMIT 0",
            "SELECT Key,Value FROM Metadata LIMIT 0","SELECT InstanceId,MediaEpoch FROM DeletionTombstone LIMIT 0","SELECT SourceId,GroupId FROM SourceGroup LIMIT 0"})
        {using var schema=Database.Command(source,null,sql);using var fields=await schema.ExecuteReaderAsync(ct);}
        using(var checkpoint=Database.Command(source,null,"SELECT Value FROM Metadata WHERE Key='tracker'"))
            if(await checkpoint.ExecuteScalarAsync(ct) is string json)new SessionTracker().ImportCheckpoint(json);
        using(var payloads=Database.Command(source,null,"SELECT Payload FROM Session"))
        using(var rows=await payloads.ExecuteReaderAsync(ct))
            while(await rows.ReadAsync(ct))
            {var s=JsonSerializer.Deserialize<SessionRecord>(rows.GetString(0));if(s is null||s.SessionId==Guid.Empty||s.PlayedMs<0||s.StayMs<0)throw new InvalidDataException("Invalid session in backup");}
        await database.WriteGate.WaitAsync(ct);
        try
        {
            using var target=database.Open();
            string recovery=database.Path+".before-restore-"+Guid.NewGuid().ToString("N");
            using(var rollback=new SqliteConnection(new SqliteConnectionStringBuilder{DataSource=recovery,Pooling=false}.ToString())){rollback.Open();target.BackupDatabase(rollback);}
            // SQLite's backup API replaces the live destination transactionally, including WAL state.
            source.BackupDatabase(target);writer.ReloadCheckpoint(target);writer.PublishActivity();
        }
        finally{database.WriteGate.Release();}
    }
}
