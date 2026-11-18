using System.IO.Pipes;
using PotTrace.App.Hosting;
using PotTrace.Contracts;
namespace PotTrace.App.Tests;
public class PipeTests
{
    [Fact] public async Task AcknowledgementFollowsDurableHandlerAndConnectionIsTracked()
    {
        var name="PotTraceTest."+Guid.NewGuid();var id=Guid.NewGuid();var life=new HostLifetimeCoordinator();long stored=0;
        await using var server=new NamedPipeServer(name,life,(e,ct)=>{stored=e.Sequence;return Task.FromResult(e.Sequence);},(_,_)=>Task.CompletedTask,_=>Task.CompletedTask);
        server.Start();using var client=new NamedPipeClientStream(".",name,PipeDirection.InOut,PipeOptions.Asynchronous);
        using var timeout=new CancellationTokenSource(5000);await client.ConnectAsync(timeout.Token);
        await client.WriteAsync(FrameCodec.Encode(new(){Type="Hello",InstanceId=id,ProcessId=Environment.ProcessId}),timeout.Token);
        var hello=await FrameCodec.ReadAsync(client,timeout.Token);Assert.Equal("Status",hello!.Type);Assert.Equal(1,life.PlayerCount);
        await client.WriteAsync(FrameCodec.Encode(new(){InstanceId=id,Events=[new(){InstanceId=id,Sequence=1,Kind=PlaybackEventKind.InstanceStarted}]}),timeout.Token);
        var ack=await FrameCodec.ReadAsync(client,timeout.Token);Assert.Equal(1,stored);Assert.Equal(1,ack!.AckSequence);
        await server.RequestExitAsync(timeout.Token);
        var control=await FrameCodec.ReadAsync(client,timeout.Token);Assert.Equal("UserExit",control!.Command);
    }
    [Fact] public void OnlyOneCoordinatorOwnsAChannel()
    {
        string channel="test"+Guid.NewGuid().ToString("N");
        using var first=new SingleInstanceCoordinator(channel);using var second=new SingleInstanceCoordinator(channel);
        Assert.True(first.IsOwner);Assert.False(second.IsOwner);Assert.Equal(first.PipeName,second.PipeName);
    }
}
