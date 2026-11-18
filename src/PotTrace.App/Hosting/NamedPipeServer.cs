using PotTrace.Contracts;
using System.IO;
using System.IO.Pipes;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace PotTrace.App.Hosting;
public sealed class NamedPipeServer:IAsyncDisposable
{
    private readonly string name;
    private readonly HostLifetimeCoordinator life;
    private readonly Func<PlaybackEvent,CancellationToken,Task<long>> ingest;
    private readonly Func<Guid,CancellationToken,Task> disconnected;
    private readonly Func<string,Task> control;
    private readonly CancellationTokenSource stop=new();
    private readonly ConcurrentDictionary<Guid,Client> clients=[];
    private readonly ConcurrentDictionary<int,Task> handlers=[];
    private Task? listener;
    private int handlerId;
    public event Action<Exception>? Error;
    public NamedPipeServer(string name,HostLifetimeCoordinator life,Func<PlaybackEvent,CancellationToken,Task<long>> ingest,Func<Guid,CancellationToken,Task> disconnected,Func<string,Task> control)
    {this.name=name;this.life=life;this.ingest=ingest;this.disconnected=disconnected;this.control=control;}
    public void Start()=>listener=ListenAsync();
    private async Task ListenAsync()
    {
        while(!stop.IsCancellationRequested)
        {
            var pipe=new NamedPipeServerStream(name,PipeDirection.InOut,NamedPipeServerStream.MaxAllowedServerInstances,PipeTransmissionMode.Byte,PipeOptions.Asynchronous|PipeOptions.CurrentUserOnly,65536,65536);
            try{await pipe.WaitForConnectionAsync(stop.Token);}
            catch(OperationCanceledException){pipe.Dispose();break;}
            int id=Interlocked.Increment(ref handlerId);var task=HandleAsync(pipe);handlers[id]=task;
            _=task.ContinueWith(_=>{handlers.TryRemove(id,out var ignored);},TaskScheduler.Default);
        }
    }
    private async Task HandleAsync(NamedPipeServerStream pipe)
    {
        await using var owned=pipe;Guid instance=Guid.Empty;Client? client=null;
        try
        {
            using var handshake=CancellationTokenSource.CreateLinkedTokenSource(stop.Token);handshake.CancelAfter(5000);
            if(!GetNamedPipeClientProcessId(pipe.SafePipeHandle,out uint pid))throw new IOException("Cannot identify pipe client");
            using(var process=Process.GetProcessById((int)pid))
                if(process.SessionId!=Process.GetCurrentProcess().SessionId)throw new InvalidDataException("Wrong login session");
            var hello=await FrameCodec.ReadAsync(pipe,handshake.Token);
            if(hello?.Type=="Control")
            {
                if(hello.Command=="Status")await pipe.WriteAsync(FrameCodec.Encode(new(){Type="Status",ProcessId=Environment.ProcessId,PlayerCount=life.PlayerCount}),stop.Token);
                else await control(hello.Command??"");return;
            }
            if(hello?.Type!="Hello"||hello.InstanceId==Guid.Empty||hello.ProcessId!=(int)pid)throw new InvalidDataException("Invalid handshake");
            instance=hello.InstanceId;client=new(pipe);
            if(!clients.TryAdd(instance,client))throw new InvalidDataException("Instance already connected");
            life.OnPlayerConnected(instance);
            await client.SendAsync(new(){Type="Status",InstanceId=instance,Command="Connected",ProcessId=Environment.ProcessId,PlayerCount=life.PlayerCount},stop.Token);
            while(!stop.IsCancellationRequested)
            {
                var message=await FrameCodec.ReadAsync(pipe,stop.Token);if(message is null)break;
                if(message.Type!="Events"||message.InstanceId!=instance||message.Events!.Any(e=>e.InstanceId!=instance))throw new InvalidDataException("Invalid instance batch");
                long committed=0;foreach(var e in message.Events!)committed=await ingest(e,stop.Token);
                await client.SendAsync(new(){Type="Ack",InstanceId=instance,AckSequence=committed},stop.Token);
            }
        }
        catch(Exception ex) when(ex is IOException or OperationCanceledException or InvalidDataException or ArgumentException or System.Text.Json.JsonException)
        {if(!stop.IsCancellationRequested)Error?.Invoke(ex);}
        catch(Exception ex){Error?.Invoke(ex);}
        finally
        {
            if(client is not null && clients.TryGetValue(instance,out var registered)&&ReferenceEquals(registered,client))
            {
                clients.TryRemove(instance,out _);
                try{await disconnected(instance,CancellationToken.None);}catch(Exception ex){Error?.Invoke(ex);}
                life.OnPlayerDisconnected(instance);
            }
        }
    }
    public async Task RequestExitAsync(CancellationToken ct)
    {
        foreach(var pair in clients)
            try{await pair.Value.SendAsync(new(){Type="Control",InstanceId=pair.Key,Command="UserExit"},ct);}
            catch(IOException){ }
    }
    public async ValueTask DisposeAsync()
    {
        await stop.CancelAsync();if(listener is not null)await listener;
        await Task.WhenAll(handlers.Values);stop.Dispose();
    }
    [DllImport("kernel32.dll",SetLastError=true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe,out uint pid);
    private sealed class Client(NamedPipeServerStream pipe)
    {
        private readonly SemaphoreSlim writeGate=new(1,1);
        public async Task SendAsync(WireMessage message,CancellationToken ct)
        {
            await writeGate.WaitAsync(ct);
            try{await pipe.WriteAsync(FrameCodec.Encode(message),ct);}
            finally{writeGate.Release();}
        }
    }
}
