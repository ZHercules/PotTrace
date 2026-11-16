using Microsoft.Data.Sqlite;
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
        await database.WriteGate.WaitAsync(ct);
        try
        {
            using var target=database.Open();
            using(var rollback=new SqliteConnection(new SqliteConnectionStringBuilder{DataSource=database.Path+".before-restore",Pooling=false}.ToString())){rollback.Open();target.BackupDatabase(rollback);}
            // SQLite's backup API replaces the live destination transactionally, including WAL state.
            source.BackupDatabase(target);writer.ReloadCheckpoint(target);
        }
        finally{database.WriteGate.Release();}
    }
}
