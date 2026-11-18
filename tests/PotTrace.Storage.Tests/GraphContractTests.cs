using PotTrace.Contracts;
using PotTrace.Core;
namespace PotTrace.Storage.Tests;

// The graph producer uses the existing protocol. Exercise its nullable initial
// position and observed seek boundaries through the real transaction/checkpoint.
public class GraphContractTests
{
    [Theory]
    [InlineData(10000, false)]
    [InlineData(10001, true)]
    public async Task PausedReadyAndSeekPreserveStrictDwellWithoutInventingPlayback(long end, bool valid)
    {
        using var f=await Fixture.Create();
        async Task Send(long at,PlaybackEventKind kind,long? position)=>await f.Writer.ApplyAsync(
            f.Event(at,kind) with {State=PlaybackState.Paused,PositionMs=position,Quality=ObservationQuality.Observed},default);
        await Send(0,PlaybackEventKind.MediaOpened,null);
        for(long at=250;at<=9500;at+=250)await Send(at,PlaybackEventKind.Snapshot,0);
        await Send(9750,PlaybackEventKind.Seeked,null);
        await Send(end,PlaybackEventKind.MediaClosed,5000);
        var movies=await f.Repo.GetMoviesAsync(new(),default);
        if(valid){var movie=Assert.Single(movies);Assert.Equal(1,movie.OpenCount);Assert.Equal(0,movie.PlayedMs);
            var session=Assert.Single(await f.Repo.GetSessionsAsync(new(),default));Assert.Equal(end,session.StayMs);
            Assert.Empty(await f.Repo.GetSegmentsAsync(session.SessionId,default));}
        else Assert.Empty(movies);
        Assert.Equal("ok",await f.Db.IntegrityCheckAsync());
    }

    [Fact]
    public async Task ObservedRepeatedIntervalsPersistIndependentlyAndYieldHeatCountTwo()
    {
        using var f=await Fixture.Create();
        async Task Send(long at,PlaybackEventKind kind,long? position,PlaybackState state)=>await f.Writer.ApplyAsync(
            f.Event(at,kind) with {State=state,PositionMs=position,Quality=ObservationQuality.Observed},default);
        await Send(0,PlaybackEventKind.MediaOpened,null,PlaybackState.Paused);
        for(long at=250;at<=11750;at+=250)await Send(at,PlaybackEventKind.Snapshot,10000,PlaybackState.Paused);
        for(int repeat=0;repeat<2;repeat++){
            long start=12000+repeat*10000;
            if(repeat>0)await Send(start-1000,PlaybackEventKind.Seeked,10000,PlaybackState.Paused);
            await Send(start,PlaybackEventKind.Resumed,10000,PlaybackState.Playing);
            for(long delta=250;delta<8000;delta+=250)await Send(start+delta,PlaybackEventKind.Snapshot,10000+delta,PlaybackState.Playing);
            await Send(start+8000,PlaybackEventKind.Paused,18000,PlaybackState.Paused);
        }
        await Send(30250,PlaybackEventKind.MediaClosed,18000,PlaybackState.Paused);
        var session=Assert.Single(await f.Repo.GetSessionsAsync(new(),default));
        Assert.Equal(16000,session.PlayedMs);Assert.Equal(30250,session.StayMs);
        var segments=await f.Repo.GetSegmentsAsync(session.SessionId,default);
        Assert.Equal(2,segments.Count);Assert.All(segments,s=>{Assert.Equal(8000,s.PlayedMs);Assert.Equal(10000,s.StartMs);Assert.Equal(18000,s.EndMs);});
        Assert.Equal(2,segments.Select(s=>s.Order).Distinct().Count());
        Assert.True(segments[0].EndedUtc<=segments[1].StartedUtc);
        var heat=Assert.Single(HeatmapBuilder.Build(segments));Assert.Equal(2,heat.Count);
    }
}
