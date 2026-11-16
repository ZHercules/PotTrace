using PotTrace.Contracts;
using System.Collections.Concurrent;
using System.IO;
namespace PotTrace.App.Services;
public sealed class SourceResolver
{
    private readonly ConcurrentDictionary<(Guid,Guid),string> versions=[];
    public PlaybackEvent Resolve(PlaybackEvent e)
    {
        if(e.MediaEpoch is not {} epoch||e.SourcePath is not {} path)return e;
        var key=(e.InstanceId,epoch);
        string version=versions.GetOrAdd(key,_=>Read(path,epoch));
        if(e.Kind is PlaybackEventKind.MediaClosed or PlaybackEventKind.Completed or PlaybackEventKind.Stopped)versions.TryRemove(key,out _);
        return e with {SourceVersion=version};
    }
    public void Forget(Guid instance){foreach(var key in versions.Keys.Where(k=>k.Item1==instance))versions.TryRemove(key,out _);}
    private static string Read(string path,Guid epoch)
    {
        try{var file=new FileInfo(path);return file.Exists?$"{file.Length}:{file.LastWriteTimeUtc.Ticks}":"unknown:"+epoch;}
        catch(Exception ex) when(ex is IOException or UnauthorizedAccessException or ArgumentException){return "unknown:"+epoch;}
    }
}
