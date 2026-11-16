using PotTrace.Contracts;
namespace PotTrace.Storage.Tests;
public class MaintenanceTests
{
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
}
