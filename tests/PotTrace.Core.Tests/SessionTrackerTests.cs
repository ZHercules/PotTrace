using PotTrace.Contracts;
using PotTrace.Core;

namespace PotTrace.Core.Tests;

public class SessionTrackerTests
{
    [Theory]
    [InlineData(10000,false)]
    [InlineData(10001,true)]
    public void StayThresholdIsStrict(long elapsed, bool valid)
    {
        var s=new Scenario(); s.Open(PlaybackState.Paused);
        var r=s.Send(elapsed,0,PlaybackState.Paused).Sessions.Single();
        Assert.Equal(valid,r.IsValid); Assert.Equal(0,r.PlayedMs);
    }
    [Fact] public void PlayThenPauseCountsOnlyPlayback()
    {
        var s=new Scenario(); s.Open(); s.Send(6000,6000,PlaybackState.Paused,PlaybackEventKind.Paused);
        var r=s.Send(11000,6000,PlaybackState.Paused).Sessions.Single();
        Assert.True(r.IsValid); Assert.Equal(6000,r.PlayedMs); Assert.Equal(11000,r.StayMs);
    }
    [Fact] public void DoubleSpeedDoesNotDoubleRealTime()
    {
        var s=new Scenario(); s.Open(rate:2);
        var r=s.Send(12000,24000,rate:2).Sessions.Single();
        Assert.Equal(12000,r.PlayedMs);
    }
    [Fact] public void JumpDoesNotCountSkippedMedia()
    {
        var s=new Scenario(); s.Open(); s.Send(11000,11000);
        var r=s.Send(12000,1200000,kind:PlaybackEventKind.Seeked).Sessions.Single();
        Assert.Equal(11000,r.PlayedMs);
    }
    [Fact] public void PauseResumeDoesNotCreateAnotherSession()
    {
        var s=new Scenario(); s.Open(); var a=s.Send(6000,6000,PlaybackState.Paused,PlaybackEventKind.Paused).Sessions.Single();
        s.Send(12000,6000,kind:PlaybackEventKind.Resumed);
        var b=s.Send(18000,12000).Sessions.Single();
        Assert.Equal(a.SessionId,b.SessionId); Assert.Equal(12000,b.PlayedMs);
    }
    [Fact] public void SystemClockRollbackDoesNotMakeNegativeDuration()
    {
        var s=new Scenario();s.Open();
        var r=s.Send(11000,11000,utc:s.Start.AddHours(-1)).Sessions.Single();
        Assert.Equal(11000,r.PlayedMs);
    }
    [Fact] public void SuspensionGapCannotAccumulateTime()
    {
        var s=new Scenario();s.Open();s.Send(11000,11000);
        var r=s.Send(3600000,11000,PlaybackState.Paused,PlaybackEventKind.Suspended).Sessions.Single();
        Assert.Equal(11000,r.PlayedMs);Assert.Equal(11000,r.StayMs);
    }
    [Fact] public void CrossMidnightPreservesDailyFactsInSimpleMode()
    {
        var s=new Scenario(CaptureMode.Simple,new DateTimeOffset(2026,11,16,23,59,50,TimeSpan.Zero)); s.Open();
        var update=s.Send(20000,20000);
        Assert.Equal(new long[]{10000,10000},update.Days.OrderBy(x=>x.Date).Select(x=>x.PlayedMs));
        Assert.Empty(update.Segments);
    }
    [Fact] public void DuplicateObservationDoesNotAddTime()
    {
        var s=new Scenario();s.Open();var e=s.Event(11000,11000);
        var a=s.Tracker.Apply(e);var b=s.Tracker.Apply(e);
        Assert.Equal(11000,a.Sessions.Single().PlayedMs);Assert.Empty(b.Sessions);
    }
    [Fact] public void RecordingPauseRequiresNewQualification()
    {
        var s=new Scenario();s.Open();s.Send(11000,11000);
        s.Tracker.SetCaptureEnabled(false,s.Start.AddMilliseconds(11000),11000);
        Assert.Empty(s.Send(20000,20000).Sessions);
        s.Tracker.SetCaptureEnabled(true,s.Start.AddMilliseconds(21000),21000);
        var r=s.Send(22000,22000).Sessions.Single();
        Assert.False(r.IsValid);Assert.Equal(0,r.PlayedMs);
    }
    [Fact] public void StopAndReopenCreateDifferentSessions()
    {
        var s=new Scenario();s.Open();var a=s.Send(11000,11000).Sessions.Single();
        s.Send(11000,11000,PlaybackState.Stopped,PlaybackEventKind.Stopped);s.Epoch=Guid.NewGuid();
        var b=s.Send(12000,0,kind:PlaybackEventKind.MediaOpened).Sessions.Single();
        Assert.NotEqual(a.SessionId,b.SessionId);
    }
    [Fact] public void SameNameIgnoresDirectoryButPreservesExtension()
    {
        Assert.Equal(MovieIdentity.GetKey(@"D:\A\电影.MKV"),MovieIdentity.GetKey(@"E:\B\电影.mkv"));
        Assert.NotEqual(MovieIdentity.GetKey(@"D:\电影.mp4"),MovieIdentity.GetKey(@"D:\电影.mkv"));
    }
}

internal sealed class Scenario(CaptureMode mode=CaptureMode.Detailed, DateTimeOffset? start=null)
{
    public SessionTracker Tracker {get;}=new(mode,TimeZoneInfo.Utc);
    public DateTimeOffset Start {get;}=start??new DateTimeOffset(2026,11,16,12,0,0,TimeSpan.Zero);
    public Guid Instance {get;}=Guid.NewGuid();
    public Guid Epoch {get;set;}=Guid.NewGuid();
    private long sequence;
    public PlaybackEvent Event(long ms,long pos,PlaybackState state=PlaybackState.Playing,PlaybackEventKind kind=PlaybackEventKind.Snapshot,double rate=1,DateTimeOffset? utc=null)
        =>new(){InstanceId=Instance,MediaEpoch=Epoch,Sequence=++sequence,MonotonicMs=ms,ObservedAtUtc=utc??Start.AddMilliseconds(ms),PositionMs=pos,DurationMs=3600000,State=state,Kind=kind,SourcePath=@"D:\电影.mkv",Rate=rate};
    public EngineUpdate Send(long ms,long pos,PlaybackState state=PlaybackState.Playing,PlaybackEventKind kind=PlaybackEventKind.Snapshot,double rate=1,DateTimeOffset? utc=null)=>Tracker.Apply(Event(ms,pos,state,kind,rate,utc));
    public void Open(PlaybackState state=PlaybackState.Playing,double rate=1)=>Send(0,0,state,PlaybackEventKind.MediaOpened,rate);
}
