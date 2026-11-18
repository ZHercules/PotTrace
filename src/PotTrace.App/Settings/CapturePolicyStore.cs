using System.IO;
using System.Text.Json;

namespace PotTrace.App.Settings;

public sealed record CapturePolicy(bool Enabled=false,bool RecordingPaused=true,long Revision=0);

public sealed class CapturePolicyStore(string path)
{
    private readonly SemaphoreSlim gate=new(1,1);
    public static CapturePolicyStore? ForBackend(string path,bool collector)=>collector||File.Exists(path)?new(path):null;
    public async Task<CapturePolicy> LoadAsync(CancellationToken ct)
    {
        for(int attempt=0;attempt<4;++attempt)
        {
        try
        {
            await using var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
            if(file.Length>4096)return new();
            using var document=await JsonDocument.ParseAsync(file,cancellationToken:ct);
            var root=document.RootElement;
            if(root.ValueKind!=JsonValueKind.Object)return new();
            var keys=root.EnumerateObject().Select(p=>p.Name).ToArray();
            if(keys.Length!=3||keys.Distinct().Count()!=3||!keys.Contains("Enabled")||!keys.Contains("RecordingPaused")||!keys.Contains("Revision"))return new();
            var policy=new CapturePolicy(root.GetProperty("Enabled").GetBoolean(),root.GetProperty("RecordingPaused").GetBoolean(),root.GetProperty("Revision").GetInt64());
            return policy.Revision>0?policy:new();
        }
        catch(Exception ex) when(ex is IOException or UnauthorizedAccessException)
        {if(attempt==3)return new();await Task.Delay(2,ct);}
        catch(Exception ex) when(ex is JsonException or InvalidOperationException or FormatException){return new();}
        }
        return new();
    }
    public async Task SaveAsync(CapturePolicy policy,CancellationToken ct)
    {
        await gate.WaitAsync(ct);
        try
        {
            var current=await LoadAsync(ct);
            if(policy.Revision<=current.Revision)throw new InvalidOperationException("Capture policy revision must increase");
            await WriteAsync(policy,ct);
        }
        finally {gate.Release();}
    }
    public async Task<CapturePolicy> UpdateAsync(bool enabled,bool paused,CancellationToken ct)
    {
        await gate.WaitAsync(ct);
        try
        {
            var current=await LoadAsync(ct);
            var next=new CapturePolicy(enabled,paused,checked(current.Revision+1));
            await WriteAsync(next,ct);return next;
        }
        finally {gate.Release();}
    }
    private async Task WriteAsync(CapturePolicy policy,CancellationToken ct)
    {
        var temporary=path+".tmp";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
            await using(var file=new FileStream(temporary,FileMode.Create,FileAccess.Write,FileShare.None,4096,FileOptions.WriteThrough))
            {await JsonSerializer.SerializeAsync(file,policy,cancellationToken:ct);await file.FlushAsync(ct);file.Flush(true);}
            if(File.Exists(path))File.Replace(temporary,path,null);
            else File.Move(temporary,path);
        }
        finally {if(File.Exists(temporary))File.Delete(temporary);}
    }
}
