using PotTrace.Contracts;
namespace PotTrace.Storage.Tests;
public class MaintenanceTests
{
    [Fact] public async Task RestoringTheRecoveryCopyPreservesBothVersions()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(12000,PlaybackEventKind.MediaClosed);
        string backup=f.Db.Path+".backup";
        try
        {
            var service=new BackupService(f.Db,f.Writer);await service.CreateAsync(backup,default);
            f.Epoch=Guid.NewGuid();await f.Send(20000,PlaybackEventKind.MediaOpened);await f.Send(32000,PlaybackEventKind.MediaClosed);
            Assert.Equal(2,Assert.Single(await f.Repo.GetMoviesAsync(new(),default)).OpenCount);
            await service.RestoreAsync(backup,default);
            Assert.Equal(1,Assert.Single(await f.Repo.GetMoviesAsync(new(),default)).OpenCount);
            string recovery=Directory.GetFiles(Path.GetDirectoryName(f.Db.Path)!,Path.GetFileName(f.Db.Path)+".before-restore*").Single();
            await service.RestoreAsync(recovery,default);
            Assert.Equal(2,Assert.Single(await f.Repo.GetMoviesAsync(new(),default)).OpenCount);
        }
        finally{File.Delete(backup);foreach(var path in Directory.GetFiles(Path.GetDirectoryName(f.Db.Path)!,Path.GetFileName(f.Db.Path)+".before-restore*"))File.Delete(path);}
    }
    [Fact] public async Task FailedDeleteRollsBackLiveTrackerAndHistoryTogether()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(12000);
        var id=Assert.Single(await f.Repo.GetSessionsAsync(new(),default)).SessionId;
        using var c=new Microsoft.Data.Sqlite.SqliteConnection($"Data Source={f.Db.Path}");c.Open();
        using var cmd=c.CreateCommand();cmd.CommandText="CREATE TRIGGER fail_checkpoint BEFORE UPDATE ON Metadata BEGIN SELECT RAISE(FAIL,'injected write failure'); END;";cmd.ExecuteNonQuery();
        await Assert.ThrowsAsync<Microsoft.Data.Sqlite.SqliteException>(()=>new HistoryMaintenance(f.Db,f.Writer).DeleteAsync(new(),default));
        cmd.CommandText="DROP TRIGGER fail_checkpoint";cmd.ExecuteNonQuery();
        await f.Send(13000);
        var row=Assert.Single(await f.Repo.GetSessionsAsync(new(),default));
        Assert.Equal(id,row.SessionId);Assert.Equal(13000,row.PlayedMs);
        Assert.Equal(id,Assert.Single(f.Writer.Activity).SessionId);
        await new HistoryMaintenance(f.Db,f.Writer).DeleteAsync(new(),default);
        Assert.Empty(f.Writer.Activity);
    }
    [Fact] public async Task DeleteIncludesCrossMidnightDaysAndCannotBeResurrected()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(20000);
        var maintenance=new HistoryMaintenance(f.Db,f.Writer);
        Assert.Equal(1,await maintenance.DeleteAsync(new(OpenedTo:new(2026,11,16)),default));
        await f.Send(21000);Assert.Empty(await f.Repo.GetMoviesAsync(new(),default));
        Assert.Empty(await f.Repo.GetDailyUsageAsync(new(new(2026,11,16),new(2026,11,17)),default));
        Assert.Empty(await f.Repo.GetAllSegmentsAsync(new(),default));
    }
    [Fact] public async Task BackupRestoresFactsAndRejectsInvalidDatabase()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(12000,PlaybackEventKind.MediaClosed);
        string backup=f.Db.Path+".backup",bad=f.Db.Path+".bad";
        try
        {
            var service=new BackupService(f.Db,f.Writer);await service.CreateAsync(backup,default);
            await new HistoryMaintenance(f.Db,f.Writer).DeleteAsync(new(),default);Assert.Empty(await f.Repo.GetMoviesAsync(new(),default));
            await service.RestoreAsync(backup,default);Assert.Equal(12000,Assert.Single(await f.Repo.GetMoviesAsync(new(),default)).PlayedMs);
            await File.WriteAllTextAsync(bad,"invalid sqlite");await Assert.ThrowsAnyAsync<Exception>(()=>service.RestoreAsync(bad,default));
            Assert.Single(await f.Repo.GetMoviesAsync(new(),default));Assert.Equal("ok",await f.Db.IntegrityCheckAsync());
        }
        finally{File.Delete(backup);File.Delete(bad);}
    }
    [Fact] public async Task ValidSqliteWithWrongSchemaCannotReplaceHistory()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(12000,PlaybackEventKind.MediaClosed);
        string bad=f.Db.Path+".wrong-schema";
        try
        {
            using(var c=new Microsoft.Data.Sqlite.SqliteConnection($"Data Source={bad};Pooling=False"))
            {c.Open();using var cmd=c.CreateCommand();cmd.CommandText="CREATE TABLE SchemaVersion(Version INTEGER);INSERT INTO SchemaVersion VALUES(1);";cmd.ExecuteNonQuery();}
            await Assert.ThrowsAnyAsync<Exception>(()=>new BackupService(f.Db,f.Writer).RestoreAsync(bad,default));
            Assert.Single(await f.Repo.GetMoviesAsync(new(),default));
        }finally{File.Delete(bad);}
    }
}
