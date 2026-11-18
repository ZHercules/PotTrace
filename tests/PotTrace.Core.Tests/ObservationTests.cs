using PotTrace.Contracts;
using PotTrace.Core;
namespace PotTrace.Core.Tests;
public class ObservationTests
{
    [Fact] public void FrameQuantizationDoesNotLoseRealPlaybackTime()
    {
        var tracker=new SessionTracker();var id=Guid.NewGuid();var epoch=Guid.NewGuid();
        PlaybackEvent E(int seq,long ms,long pos)=>new(){InstanceId=id,MediaEpoch=epoch,SourcePath=@"D:\movie.avi",Sequence=seq,MonotonicMs=ms,PositionMs=pos,State=PlaybackState.Playing,ObservedAtUtc=DateTimeOffset.UtcNow.AddMilliseconds(ms)};
        tracker.Apply(E(1,0,0));tracker.Apply(E(2,250,200));
        var result=tracker.Apply(E(3,500,500));Assert.Equal(500,result.Sessions.Single().PlayedMs);
    }
    [Fact] public void MissingSequenceCutsTheInterval()
    {
        var tracker=new SessionTracker();var id=Guid.NewGuid();var epoch=Guid.NewGuid();
        PlaybackEvent E(int seq,long ms)=>new(){InstanceId=id,MediaEpoch=epoch,SourcePath=@"D:\movie.avi",Sequence=seq,MonotonicMs=ms,PositionMs=ms,State=PlaybackState.Playing};
        tracker.Apply(E(1,0));var result=tracker.Apply(E(3,12000));
        Assert.Equal(0,result.Sessions.Single().PlayedMs);Assert.False(result.Sessions.Single().IsValid);
    }
}
