using PotTrace.Core;
using System.IO;
using System.Text.Json;
namespace PotTrace.App.Settings;
public sealed record CaptureSettings(CaptureMode Mode=CaptureMode.Simple,bool RecordingPaused=false,bool Enabled=true,string Theme="Dark",string[]? ExcludedDirectories=null,int HotKeyModifiers=3,int HotKey=0x48);
public sealed class SettingsStore
{
    private readonly string path;
    private readonly SemaphoreSlim gate=new(1,1);
    public SettingsStore(string path) { this.path=path; }
    public async Task<CaptureSettings> LoadAsync(CancellationToken ct)
    {
        if(!File.Exists(path))return new();
        await using var file=File.OpenRead(path);
        return await JsonSerializer.DeserializeAsync<CaptureSettings>(file,cancellationToken:ct)??new();
    }
    public async Task SaveAsync(CaptureSettings settings,CancellationToken ct)
    {
        await gate.WaitAsync(ct);
        string temporary=path+".tmp";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
            await using(var file=new FileStream(temporary,FileMode.Create,FileAccess.Write,FileShare.None,4096,FileOptions.WriteThrough))
            {await JsonSerializer.SerializeAsync(file,settings,cancellationToken:ct);await file.FlushAsync(ct);file.Flush(true);}
            File.Move(temporary,path,true);
        }
        finally {gate.Release();}
    }
}
