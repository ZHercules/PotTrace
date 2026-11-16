using System.IO;
using System.IO.Pipes;
using System.Windows;
using System.Windows.Threading;
using PotTrace.App.Hosting;
using PotTrace.App.Settings;
using PotTrace.Contracts;
using PotTrace.Core;
using PotTrace.Storage;
namespace PotTrace.App;
public partial class App
{
    private SingleInstanceCoordinator? instance;
    private NamedPipeServer? server;
    private readonly HostLifetimeCoordinator lifetime=new();
    private DispatcherTimer? timer;
    private Window? window;
    private System.Windows.Forms.NotifyIcon? tray;
    private string dataDirectory="";
    private bool stopping;
    private readonly Services.SourceResolver sources=new();
    public Database Database {get;private set;}=null!;
    public IngestionWriter Writer {get;private set;}=null!;
    public CaptureSettings Settings {get;private set;}=new();
    public SettingsStore SettingsStore {get;private set;}=null!;
    public PlaybackEvent? Latest {get;private set;}
    public string Status {get;private set;}="等待 PotPlayer 连接";
    public int PlayerCount=>lifetime.PlayerCount;
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        string Arg(string key,string fallback){int index=Array.IndexOf(e.Args,key);return index>=0&&index+1<e.Args.Length?e.Args[index+1]:fallback;}
        dataDirectory=Path.GetFullPath(Arg("--data-dir",Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"PotTrace")));
        try
        {
            Directory.CreateDirectory(dataDirectory);
            instance=new(Arg("--channel","main"));
            if(!instance.IsOwner)
            {
                if(!e.Args.Contains("--background"))
                {using var pipe=new NamedPipeClientStream(".",instance.PipeName,PipeDirection.InOut,PipeOptions.Asynchronous);using var ct=new CancellationTokenSource(3000);await pipe.ConnectAsync(ct.Token);await pipe.WriteAsync(FrameCodec.Encode(new(){Type="Control",Command="Show"}),ct.Token);}
                Shutdown();return;
            }
            SettingsStore=new(Path.Combine(dataDirectory,"settings.json"));Settings=await SettingsStore.LoadAsync(default);
            ApplyTheme();
            Database=await Database.CreateAsync(Path.Combine(dataDirectory,"pottrace.db"));
            Writer=new(Database,new SessionTracker(Settings.Mode));Writer.SetNextCaptureMode(Settings.Mode);
            if(Settings.RecordingPaused||!Settings.Enabled)await Writer.SetCaptureEnabledAsync(false,default);
            server=new(instance.PipeName,lifetime,async (item,ct)=>
            {
                if(item.SourcePath is {} path&&ExclusionPolicy.IsExcluded(path,Settings.ExcludedDirectories??[]))
                {
                    await Writer.DisconnectAsync(item.InstanceId,ct);
                    var ignored=await Writer.ApplyAsync(item with {SourcePath=null,MediaEpoch=null,Kind=PlaybackEventKind.InstanceStarted,State=PlaybackState.Unknown},ct);
                    return ignored.CommittedSequence;
                }
                item=sources.Resolve(item);var result=await Writer.ApplyAsync(item,ct);Latest=item;Status="正在采集";return result.CommittedSequence;
            },async (id,ct)=>{sources.Forget(id);await Writer.DisconnectAsync(id,ct);},command=>Dispatcher.InvokeAsync(()=>{if(command=="Show")ShowHistory();}).Task);
            server.Error+=ex=>{Status="采集连接异常，请查看诊断日志";Log(ex);};server.Start();
            tray=new(){Icon=System.Drawing.SystemIcons.Application,Text="PotTrace 播放统计",Visible=true};
            var menu=new System.Windows.Forms.ContextMenuStrip();menu.Items.Add("播放统计",null,(_,_)=>ShowHistory());
            menu.Items.Add("暂停 / 恢复统计",null,async (_,_)=>await SaveSettingsAsync(Settings with {RecordingPaused=!Settings.RecordingPaused}));
            menu.Items.Add("退出 PotTrace",null,async (_,_)=>await StopAsync(true));tray.ContextMenuStrip=menu;tray.DoubleClick+=(_,_)=>ShowHistory();
            if(!e.Args.Contains("--background"))ShowHistory();
            timer=new DispatcherTimer(TimeSpan.FromMilliseconds(500),DispatcherPriority.Background,async (_,_)=>{if(lifetime.ShouldExit)await StopAsync(false);},Dispatcher);timer.Start();
        }
        catch(Exception ex)
        {Log(ex);if(!e.Args.Contains("--background"))System.Windows.MessageBox.Show("PotTrace 启动失败。数据已保留。\n"+ex.Message,"PotTrace");Shutdown(1);}
    }
    public async Task SaveSettingsAsync(CaptureSettings settings)
    {
        await SettingsStore.SaveAsync(settings,default);Settings=settings;
        Writer.SetNextCaptureMode(settings.Mode);await Writer.SetCaptureEnabledAsync(!settings.RecordingPaused&&settings.Enabled,default);
        Status=settings.RecordingPaused?"统计已暂停":"正在采集";
    }
    public void ShowHistory()
    {
        if(window is null)
        {
            window=new Views.MainWindow(this);
            window.Closing+=(_,e)=>{if(!stopping){e.Cancel=true;window.Hide();lifetime.SetWindowVisible(false);}};
        }
        lifetime.SetWindowVisible(true);window.Show();window.WindowState=WindowState.Normal;window.Activate();
    }
    private async Task StopAsync(bool requested)
    {
        if(stopping)return;stopping=true;timer?.Stop();
        if(server is not null)
        {
            if(requested){using var ct=new CancellationTokenSource(1500);try{await server.RequestExitAsync(ct.Token);}catch(OperationCanceledException){ }}
            await server.DisposeAsync();
        }
        tray?.Dispose();Shutdown();
    }
    public void ApplyTheme()
    {
        Resources.MergedDictionaries.Clear();
        Resources.MergedDictionaries.Add(new ResourceDictionary{Source=new Uri($"Themes/{(Settings.Theme=="Light"?"Light":"Dark")}.xaml",UriKind.Relative)});
        Resources.MergedDictionaries.Add(new ResourceDictionary{Source=new Uri("Themes/Controls.xaml",UriKind.Relative)});
    }
    private void Log(Exception ex)
    {try{File.AppendAllText(Path.Combine(dataDirectory,"diagnostics.log"),$"{DateTimeOffset.UtcNow:O} {ex.GetType().Name}: {ex.Message}\n");}catch(IOException){ }}
    protected override void OnExit(ExitEventArgs e){tray?.Dispose();instance?.Dispose();base.OnExit(e);}
}
