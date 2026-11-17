using PotTrace.Contracts;
using PotTrace.Core;
namespace PotTrace.Core.Tests;
public class EffectiveRateTests
{
    [Fact] public void InvalidPauseTailDoesNotInventPlaybackOrLosePausedDwell()
    {
        var s=new Scenario();s.Open();
        s.Tracker.Apply(s.Event(100,0,PlaybackState.Paused,PlaybackEventKind.Paused) with{PositionMs=null});
        var result=s.Send(120,225,PlaybackState.Paused,PlaybackEventKind.PositionDiscontinuity);
        Assert.Equal(0,result.Sessions.Single().PlayedMs);Assert.Equal(120,result.Sessions.Single().StayMs);
    }
    [Fact] public void ClockMismatchCutsPlaybackButPreservesConfirmedDwell()
    {
        var s=new Scenario();s.Open();s.Send(25,225,kind:PlaybackEventKind.PositionDiscontinuity);
        var result=s.Send(50,250);
        Assert.Equal(25,result.Sessions.Single().PlayedMs);Assert.Equal(50,result.Sessions.Single().StayMs);
    }
    [Fact] public void UnknownRateDoesNotValidateALongUnobservedPlayingInterval()
    {
        var s=new Scenario();s.Tracker.Apply(s.Event(0,0) with{Rate=null});
        var result=s.Tracker.Apply(s.Event(6000,6000) with{Rate=null});
        Assert.Equal(0,result.Sessions.Single().PlayedMs);
    }
    [Fact] public void UnknownRateIsNotSavedAsNormalSpeed()
    {
        var s=new Scenario();s.Tracker.Apply(s.Event(0,0,PlaybackState.Paused) with{Rate=null});
        s.Tracker.Apply(s.Event(11000,0) with{Rate=null});EngineUpdate result=EngineUpdate.Empty;
        for(int i=1;i<=240;i++)result=s.Tracker.Apply(s.Event(11000+i*25,i*50) with{Rate=null});
        Assert.Equal(6000,result.Sessions.Single().PlayedMs);Assert.Null(Assert.Single(result.Segments).Rate);
    }
    [Fact] public void EstimatedTransitionKeepsWallTimeAndObservationWindow()
    {
        var s=new Scenario();s.Open(PlaybackState.Paused);s.Send(11000,0,kind:PlaybackEventKind.Resumed);
        for(int i=1;i<=240;i++)s.Tracker.Apply(s.Event(11000+i*25,i*25) with{RateIsEstimated=true});
        s.Tracker.Apply(s.Event(17000,6000,kind:PlaybackEventKind.RateChanged) with{Rate=null,RateIsEstimated=true,RateWindowStartMs=16200});
        EngineUpdate result=EngineUpdate.Empty;
        for(int i=1;i<=240;i++)result=s.Tracker.Apply(s.Event(17000+i*25,6000+i*50) with{Rate=null,RateIsEstimated=true});
        Assert.Equal(12000,result.Sessions.Single().PlayedMs);var segment=Assert.Single(result.Segments);
        Assert.Equal(1.5d,segment.Rate);Assert.True(segment.RateIsEstimated);
        var change=Assert.Single(result.Timeline,e=>e.Kind=="RateChanged");
        Assert.Equal(s.Start.AddMilliseconds(16200),change.RateWindowStartUtc);Assert.Equal(s.Start.AddMilliseconds(17000),change.AtUtc);
    }
    [Fact] public void ShortSpeedPhasesRemainOneDetailedContinuousPlayback()
    {
        var s=new Scenario();s.Open(PlaybackState.Paused);s.Send(11000,0,kind:PlaybackEventKind.Resumed);
        for(int i=1;i<=120;i++)s.Tracker.Apply(s.Event(11000+i*25,i*25) with{RateIsEstimated=true});
        s.Tracker.Apply(s.Event(14000,3000,kind:PlaybackEventKind.RateChanged) with{Rate=null,RateIsEstimated=true,RateWindowStartMs=13200});
        for(int i=1;i<=120;i++)s.Tracker.Apply(s.Event(14000+i*25,3000+i*50) with{Rate=null,RateIsEstimated=true});
        var result=s.Tracker.Apply(s.Event(17000,9000,PlaybackState.Paused,PlaybackEventKind.Paused) with{RateIsEstimated=true});
        Assert.Equal(6000,Assert.Single(result.Segments).PlayedMs);
        Assert.Equal(0,result.Sessions.Single().ShortPlayedMs);
        Assert.Equal(1,Assert.Single(HeatmapBuilder.Build(result.Segments)).Count);
        Assert.Single(result.Timeline,e=>e.Kind=="RateChanged");
    }
}
