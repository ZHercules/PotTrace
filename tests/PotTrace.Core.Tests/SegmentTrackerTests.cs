using PotTrace.Contracts;
using PotTrace.Core;
namespace PotTrace.Core.Tests;
public class SegmentTrackerTests
{
    [Theory]
    [InlineData(5000,0)]
    [InlineData(5001,1)]
    public void SegmentThresholdIsStrict(long length,int count)
    {
        var s=new Scenario();s.Open(PlaybackState.Paused);s.Send(11000,0,kind:PlaybackEventKind.Resumed);
        var r=s.Send(11000+length,length,PlaybackState.Paused,PlaybackEventKind.Paused);
        Assert.Equal(count,r.Segments.Count);
        Assert.Equal(count==0?length:0,r.Sessions.Single().ShortPlayedMs);
    }
    [Fact] public void ShortSegmentsDoNotJoinAcrossPause()
    {
        var s=new Scenario();s.Open();s.Send(4000,4000,PlaybackState.Paused,PlaybackEventKind.Paused);
        s.Send(11000,4000,kind:PlaybackEventKind.Resumed);
        var r=s.Send(15000,8000,PlaybackState.Paused,PlaybackEventKind.Paused);
        Assert.Empty(r.Segments);Assert.Equal(8000,r.Sessions.Single().ShortPlayedMs);
    }
    [Fact] public void RepeatKeepsSeparateSegmentsAndCountsCoverageTwice()
    {
        var s=new Scenario();s.Open();s.Send(8000,8000);s.Send(8000,0,kind:PlaybackEventKind.Seeked);
        var r=s.Send(16000,8000,PlaybackState.Paused,PlaybackEventKind.Paused);
        Assert.Equal(2,r.Segments.Count);Assert.Equal(16000,r.Sessions.Single().PlayedMs);
        var heat=HeatmapBuilder.Build(r.Segments);
        Assert.Equal(2,Assert.Single(heat).Count);
    }
    [Fact] public void InvalidSessionDoesNotExposeSegments()
    {
        var s=new Scenario();s.Open();var r=s.Send(6000,6000,PlaybackState.Stopped,PlaybackEventKind.Stopped);
        Assert.Empty(r.Segments);
    }
    [Fact] public void RateChangeKeepsContinuousPlaybackAndRealTime()
    {
        var s=new Scenario();s.Open();s.Send(6000,6000,kind:PlaybackEventKind.RateChanged,rate:2);
        var r=s.Send(12000,18000,PlaybackState.Paused,PlaybackEventKind.Paused,2);
        var segment=Assert.Single(r.Segments);Assert.Equal(12000,segment.PlayedMs);
        Assert.Equal(1.5d,segment.Rate);Assert.True(segment.RateIsEstimated);
        Assert.Single(r.Timeline,e=>e.Kind=="RateChanged");
    }
    [Fact] public void HalfOpenIntervalsDoNotDoubleCountAdjacentEndpoints()
    {
        var source=Guid.NewGuid();
        SegmentRecord S(long a,long b)=>new(Guid.NewGuid(),Guid.NewGuid(),source,1,DateTimeOffset.UtcNow,DateTimeOffset.UtcNow,a,b,b-a,1,"Paused");
        var heat=HeatmapBuilder.Build([S(600000,620000),S(600000,620000),S(610000,630000)]);
        Assert.Equal(new[]{(600000L,610000L,2),(610000L,620000L,3),(620000L,630000L,1)},heat.Select(x=>(x.StartMs,x.EndMs,x.Count)));
    }
    [Fact] public void DifferentSourcesDoNotShareHeat()
    {
        SegmentRecord S(Guid source)=>new(Guid.NewGuid(),Guid.NewGuid(),source,1,DateTimeOffset.UtcNow,DateTimeOffset.UtcNow,0,8000,8000,1,"Paused");
        var heat=HeatmapBuilder.Build([S(Guid.NewGuid()),S(Guid.NewGuid())]);
        Assert.Equal(2,heat.Count);Assert.All(heat,x=>Assert.Equal(1,x.Count));
    }
}
