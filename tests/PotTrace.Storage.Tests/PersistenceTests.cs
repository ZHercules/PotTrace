using PotTrace.Contracts;
using PotTrace.Core;
namespace PotTrace.Storage.Tests;

public class PersistenceTests
{
    [Fact] public async Task RestartAndReplayCannotDuplicateSession()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);var e=f.Event(11000);
        await f.Writer.ApplyAsync(e,default);
        var restarted=new IngestionWriter(f.Db,new(CaptureMode.Detailed,TimeZoneInfo.Utc));
        await restarted.ApplyAsync(e,default);await restarted.ApplyAsync(f.Event(12000),default);
        var row=Assert.Single(await f.Repo.GetMoviesAsync(new(),default));
        Assert.Equal(1,row.OpenCount);Assert.Equal(12000,row.PlayedMs);
        Assert.Equal("ok",await f.Db.IntegrityCheckAsync());
    }
    [Fact] public async Task SimpleModeKeepsDailyFactsWithoutSegments()
    {
        using var f=await Fixture.Create(CaptureMode.Simple);await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(20000);
        var sessions=await f.Repo.GetSessionsAsync(new(),default);
        Assert.Empty(await f.Repo.GetSegmentsAsync(Assert.Single(sessions).SessionId,default));
        Assert.Equal(new long[]{10000,10000},(await f.Repo.GetDailyUsageAsync(new(new(2026,11,16),new(2026,11,17)),default)).Select(x=>x.PlayedMs));
    }
    [Fact] public async Task InvalidOpenDoesNotAppearInHistory()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(9000,PlaybackEventKind.MediaClosed);
        Assert.Empty(await f.Repo.GetMoviesAsync(new(),default));
    }
    [Fact] public async Task SameNameSourcesShareSummaryButNotHeat()
    {
        using var f=await Fixture.Create();await f.Send(0,PlaybackEventKind.MediaOpened);await f.Send(12000,PlaybackEventKind.MediaClosed);
        f.Epoch=Guid.NewGuid();f.Path=@"E:\Other\movie.avi";await f.Send(15000,PlaybackEventKind.MediaOpened);await f.Send(27000,PlaybackEventKind.MediaClosed);
        var movies=await f.Repo.GetMoviesAsync(new(),default);Assert.Equal(2,Assert.Single(movies).OpenCount);
        var sessions=await f.Repo.GetSessionsAsync(new(),default);Assert.Equal(2,sessions.Select(x=>x.SourceId).Distinct().Count());
    }
}

internal sealed class Fixture:IDisposable
{
    public required Database Db {get;init;}
    public required IngestionWriter Writer {get;init;}
    public required HistoryRepository Repo {get;init;}
    public Guid Instance {get;}=Guid.NewGuid(); public Guid Epoch {get;set;}=Guid.NewGuid();
    public string Path {get;set;}=@"D:\Movies\movie.avi";private long sequence;private long origin;
    public static async Task<Fixture> Create(CaptureMode mode=CaptureMode.Detailed)
    {
        var path=System.IO.Path.Combine(System.IO.Path.GetTempPath(),"PotTraceTests",Guid.NewGuid()+".db");
        var db=await Database.CreateAsync(path);
        return new(){Db=db,Writer=new(db,new(mode,TimeZoneInfo.Utc)),Repo=new(db)};
    }
    public PlaybackEvent Event(long ms,PlaybackEventKind kind=PlaybackEventKind.Snapshot)
    {
        if(kind==PlaybackEventKind.MediaOpened)origin=ms;
        return new(){InstanceId=Instance,MediaEpoch=Epoch,Sequence=++sequence,Kind=kind,SourcePath=Path,MonotonicMs=ms,
            ObservedAtUtc=new DateTimeOffset(2026,11,16,23,59,50,TimeSpan.Zero).AddMilliseconds(ms),PositionMs=ms-origin,DurationMs=60000,Rate=1,State=PlaybackState.Playing};
    }
    public Task<IngestReceipt> Send(long ms,PlaybackEventKind kind=PlaybackEventKind.Snapshot)=>Writer.ApplyAsync(Event(ms,kind),default);
    public void Dispose(){ Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools(); foreach(var suffix in new[]{"","-wal","-shm"})try{File.Delete(Db.Path+suffix);}catch(IOException){} }
}
