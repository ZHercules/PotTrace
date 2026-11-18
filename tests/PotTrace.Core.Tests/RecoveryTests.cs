using PotTrace.Contracts;
using PotTrace.Core;
namespace PotTrace.Core.Tests;
public class RecoveryTests
{
    [Fact] public void CheckpointRoundTripKeepsSessionIdentityAndDeduplicatesReplay()
    {
        var s=new Scenario();s.Open();var e=s.Event(11000,11000);var original=s.Tracker.Apply(e).Sessions.Single();
        var resumed=new SessionTracker(CaptureMode.Detailed,TimeZoneInfo.Utc);
        resumed.ImportCheckpoint(s.Tracker.ExportCheckpoint());
        Assert.Empty(resumed.Apply(e).Sessions);
        var next=resumed.Apply(s.Event(12000,12000)).Sessions.Single();
        Assert.Equal(original.SessionId,next.SessionId);Assert.Equal(12000,next.PlayedMs);
    }
    [Fact] public void DisconnectClosesAtLastReliableObservation()
    {
        var s=new Scenario();s.Open();s.Send(11000,11000);
        var r=s.Tracker.Disconnect(s.Instance).Sessions.Single();
        Assert.Equal(11000,r.PlayedMs);Assert.NotNull(r.EndedUtc);Assert.Equal("Disconnected",r.EndReason);
    }
}
