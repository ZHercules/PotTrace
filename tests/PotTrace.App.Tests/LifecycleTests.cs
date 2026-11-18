using PotTrace.App.Hosting;
using PotTrace.App.Settings;
using PotTrace.Core;
namespace PotTrace.App.Tests;
public class LifecycleTests
{
    [Fact] public void MultiplePlayersShareOneLifetimeAndDisconnectGrace()
    {
        var clock=new Clock();var host=new HostLifetimeCoordinator(clock);var a=Guid.NewGuid();var b=Guid.NewGuid();
        host.OnPlayerConnected(a);host.OnPlayerConnected(a);host.OnPlayerConnected(b);Assert.Equal(2,host.PlayerCount);
        host.OnPlayerDisconnected(a);Assert.False(host.ShouldExit);host.OnPlayerDisconnected(b);
        clock.Advance(2999);Assert.False(host.ShouldExit);clock.Advance(1);Assert.True(host.ShouldExit);
    }
    [Fact] public void ReconnectionAndVisibleHistoryCancelAutomaticExit()
    {
        var clock=new Clock();var host=new HostLifetimeCoordinator(clock);var id=Guid.NewGuid();
        host.OnPlayerConnected(id);host.OnPlayerDisconnected(id);clock.Advance(3000);
        host.SetWindowVisible(true);Assert.False(host.ShouldExit);host.SetWindowVisible(false);Assert.True(host.ShouldExit);
        host.OnPlayerConnected(id);Assert.False(host.ShouldExit);host.RequestUserExit();Assert.True(host.ShouldExit);
    }
    [Fact] public async Task PausedRecordingSurvivesSettingsReload()
    {
        var file=Path.Combine(Path.GetTempPath(),"PotTraceTests",Guid.NewGuid()+".json");
        try
        {
            await new SettingsStore(file).SaveAsync(new(CaptureMode.Detailed,true),default);
            var restored=await new SettingsStore(file).LoadAsync(default);
            Assert.True(restored.RecordingPaused);Assert.Equal(CaptureMode.Detailed,restored.Mode);
        }
        finally{File.Delete(file);}
    }
    private sealed class Clock:TimeProvider
    {
        private DateTimeOffset now=DateTimeOffset.UtcNow;
        public override DateTimeOffset GetUtcNow()=>now;
        public void Advance(int ms)=>now=now.AddMilliseconds(ms);
    }
}
