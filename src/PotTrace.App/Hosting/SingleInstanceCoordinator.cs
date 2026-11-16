using System.Diagnostics;
using System.Security.Principal;
namespace PotTrace.App.Hosting;
public sealed class SingleInstanceCoordinator:IDisposable
{
    private readonly Mutex mutex;
    public SingleInstanceCoordinator(string channel)
    {
        if(channel.Length>60||channel.Any(c=>!char.IsAsciiLetterOrDigit(c)&&c!='-'))throw new ArgumentException("Invalid channel",nameof(channel));
        PipeName=$"PotTrace.{WindowsIdentity.GetCurrent().User!.Value}.{Process.GetCurrentProcess().SessionId}.{channel}";
        mutex=new Mutex(false,@"Local\"+PipeName,out bool created);IsOwner=created;
    }
    public string PipeName {get;}
    public bool IsOwner {get;}
    public void Dispose()=>mutex.Dispose();
}
