using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using PotTrace.App.Controls;
using PotTrace.App.Services;
using PotTrace.App.ViewModels;
using PotTrace.Core;
using PotTrace.Storage;
using MessageBox=System.Windows.MessageBox;
using CaptureMode=PotTrace.Core.CaptureMode;
namespace PotTrace.App.Views;
public partial class MainWindow:Window
{
    private readonly App host;
    private readonly HistoryRepository repository;
    private readonly MainViewModel vm;
    private int page;
    private long detailGeneration;
    private bool loading;
    private HistoryFilter filter=new();
    public MainWindow(App host)
    {
        this.host=host;repository=new(host.Database);vm=new(repository.GetMoviesAsync,host.Settings.Mode);InitializeComponent();
        Loaded+=async (_,_)=>await Run(RefreshAsync);
        var statusTimer=new DispatcherTimer(TimeSpan.FromSeconds(1),DispatcherPriority.Background,(_,_)=>UpdateStatus(),Dispatcher);statusTimer.Start();
        Closed+=(_,_)=>statusTimer.Stop();
    }
    private async Task Run(Func<Task> operation)
    {try{await operation();}catch(Exception ex){MessageBox.Show(this,ex.Message,"操作未完成",MessageBoxButton.OK,MessageBoxImage.Warning);}}
    private async Task RefreshAsync()
    {
        var selected=(Movies.SelectedItem as MovieRow)?.MovieKey;
        filter=new(FromDate.SelectedDate is {} f?DateOnly.FromDateTime(f):null,ToDate.SelectedDate is {} t?DateOnly.FromDateTime(t):null,Search.Text.Trim() is {Length:>0} text?text:null,Offset:page*100,Limit:100);
        await vm.RefreshAsync(filter,default);Movies.ItemsSource=vm.Movies.Select(x=>new MovieRow(x)).ToArray();
        EmptyText.Visibility=vm.Movies.Count==0?Visibility.Visible:Visibility.Collapsed;
        SummaryText.Text=$"本页 {vm.Movies.Count} 部 · {vm.Movies.Sum(x=>x.OpenCount)} 次";PageText.Text=$"第 {page+1} 页";
        Movies.SelectedItem=Movies.Items.Cast<MovieRow>().FirstOrDefault(x=>x.MovieKey==selected)??Movies.Items.Cast<MovieRow>().FirstOrDefault();UpdateStatus();
    }
    private void UpdateStatus()
    {
        PauseButton.Content=host.Settings.RecordingPaused?"恢复统计":"暂停统计";
        StatusText.Text=$"{(host.Settings.RecordingPaused?"统计已暂停":host.PlayerCount==0?"等待 PotPlayer 连接":host.Status)}  ·  {host.PlayerCount} 个播放器  ·  {(host.Settings.Mode==CaptureMode.Simple?"精简":"详细")}采集  ·  时长按实际播放时间累计";
        CurrentText.Text=host.Latest?.SourcePath is {} path?$"当前：{Path.GetFileName(path)}  ·  {host.Latest.State}  ·  位置与倍速使用采样估算":"首次启用：PotPlayer → 选项 → 扩展功能 → 播放统计 → 勾选 PotTrace";
    }
    private void SetDetailed(bool value){vm.DetailedView=value;DetailPanel.Visibility=value?Visibility.Visible:Visibility.Collapsed;MovieColumn.Width=new GridLength(value?4:1,GridUnitType.Star);DetailColumn.Width=new GridLength(value?6:0,GridUnitType.Star);}
    private void SimpleClick(object sender,RoutedEventArgs e)=>SetDetailed(false);
    private void DetailedClick(object sender,RoutedEventArgs e)=>SetDetailed(true);
    private async void RefreshClick(object sender,RoutedEventArgs e){page=0;await Run(RefreshAsync);}
    private async void SearchKey(object sender,System.Windows.Input.KeyEventArgs e){if(e.Key==Key.Enter){page=0;await Run(RefreshAsync);}}
    private async void PreviousClick(object sender,RoutedEventArgs e){page=Math.Max(0,page-1);await Run(RefreshAsync);}
    private async void NextClick(object sender,RoutedEventArgs e){if(vm.Movies.Count==100){page++;await Run(RefreshAsync);}}
    private async void MovieSelected(object sender,SelectionChangedEventArgs e)
    {
        if(Movies.SelectedItem is not MovieRow movie)return;
        await Run(async ()=>
        {
            long request=++detailGeneration;
            var sessions=await repository.GetSessionsAsync(filter with {MovieKey=movie.MovieKey,Offset=0,Limit=1000},default);
            if(request!=detailGeneration)return;
            loading=true;Sessions.ItemsSource=sessions.Select(x=>new SessionRow(x)).ToArray();
            Sources.ItemsSource=sessions.DistinctBy(x=>x.SourceId).Select(x=>new SourceRow(x.SourceId,x.SourcePath)).ToArray();Sources.SelectedIndex=0;loading=false;
            Sessions.SelectedIndex=0;await LoadHeatAsync();
        });
    }
    private async void SessionSelected(object sender,SelectionChangedEventArgs e)
    {
        if(Sessions.SelectedItem is not SessionRow row)return;
        await Run(async ()=>
        {
            var segments=await repository.GetSegmentsAsync(row.Record.SessionId,default);
            if((Sessions.SelectedItem as SessionRow)?.Record.SessionId!=row.Record.SessionId)return;
            Notice.Text=MainViewModel.DetailNotice(row.Record)+$"  短片段合计：{Duration(row.Record.ShortPlayedMs)}";
            Segments.ItemsSource=segments.Select(x=>new SegmentRow(x)).ToArray();Timeline.SetSegments(segments);
        });
    }
    private async Task LoadHeatAsync()
    {
        if(Sources.SelectedItem is not SourceRow source)return;
        var segments=await repository.GetAllSegmentsAsync(filter with {SourceId=source.Id,Offset=0},default);
        if((Sources.SelectedItem as SourceRow)?.Id!=source.Id)return;
        var heat=HeatmapBuilder.Build(segments);Heat.SetHeat(heat);
        Hotspots.ItemsSource=heat.Where(x=>x.Count>=2).OrderByDescending(x=>x.Count).ThenByDescending(x=>x.EndMs-x.StartMs).Select(x=>new HotspotRow(x)).ToArray();
    }
    private async void SourceChanged(object sender,SelectionChangedEventArgs e){if(!loading)await Run(LoadHeatAsync);}
    private void HotspotSelected(object sender,SelectionChangedEventArgs e)
    {
        if(Hotspots.SelectedItem is not HotspotRow hot)return;
        var match=Segments.Items.Cast<SegmentRow>().FirstOrDefault(x=>x.Record.StartMs<hot.Record.EndMs&&x.Record.EndMs>hot.Record.StartMs);
        if(match is not null){Segments.SelectedItem=match;Segments.ScrollIntoView(match);}
    }
    private void ZoomChanged(object sender,RoutedPropertyChangedEventArgs<double> e){if(Timeline is not null)Timeline.Width=Math.Max(360,DetailPanel.ActualWidth-22)*e.NewValue;}
    private async void PauseClick(object sender,RoutedEventArgs e)=>await Run(async ()=>{await host.SaveSettingsAsync(host.Settings with {RecordingPaused=!host.Settings.RecordingPaused});UpdateStatus();});
    private void SettingsClick(object sender,RoutedEventArgs e){new SettingsWindow(host){Owner=this}.ShowDialog();UpdateStatus();}
    private void RulesClick(object sender,RoutedEventArgs e)=>MessageBox.Show(this,"当前影片停留严格超过 10 秒才计为一次打开；暂停只增加停留。\n累计时长为实际播放经过时间，重复观看重复累计。\n详细片段必须连续实际播放超过 5 秒，暂停、跳转、变速、失联均截断。\n同名（含扩展名、忽略大小写）归并影片；热度按实际来源分别计算。\n采集模式从下一会话生效；旧精简记录没有明细。\n采样存在边界误差，不提供帧级精度。","统计规则 · v1");
    private async void ExportClick(object sender,RoutedEventArgs e)
    {
        var dialog=new Microsoft.Win32.SaveFileDialog{Filter="JSON 明细|*.json|CSV 会话|*.csv",FileName="PotTrace-history"};if(dialog.ShowDialog(this)!=true)return;
        await Run(()=>ExportService.ExportAsync(repository,filter,dialog.FileName,dialog.FilterIndex==1,default));
    }
    private async void BackupClick(object sender,RoutedEventArgs e)
    {
        var dialog=new Microsoft.Win32.SaveFileDialog{Filter="SQLite 备份|*.db",FileName=$"PotTrace-{DateTime.Now:yyyyMMdd}"};if(dialog.ShowDialog(this)==true)await Run(()=>new BackupService(host.Database,host.Writer).CreateAsync(dialog.FileName,default));
    }
    private async void RestoreClick(object sender,RoutedEventArgs e)
    {
        var dialog=new Microsoft.Win32.OpenFileDialog{Filter="SQLite 备份|*.db"};if(dialog.ShowDialog(this)!=true)return;
        if(MessageBox.Show(this,"将用所选备份替换当前历史，并暂停统计。当前数据库会保留恢复前副本。继续？","恢复备份",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
        await Run(async ()=>{await host.SaveSettingsAsync(host.Settings with{RecordingPaused=true});await new BackupService(host.Database,host.Writer).RestoreAsync(dialog.FileName,default);await host.Writer.SetCaptureEnabledAsync(false,default);await RefreshAsync();});
    }
    private async void DeleteClick(object sender,RoutedEventArgs e)
    {
        if(Movies.SelectedItem is not MovieRow movie)return;
        if(MessageBox.Show(this,$"删除「{movie.FileName}」在当前打开日期范围内的完整会话及明细？\n当前正在播放的已删除会话，本次打开期间不再补记。","删除记录",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
        await Run(async ()=>{await new HistoryMaintenance(host.Database,host.Writer).DeleteAsync(filter with{MovieKey=movie.MovieKey},default);await RefreshAsync();});
    }
    public static string Duration(long ms){var t=TimeSpan.FromMilliseconds(ms);return $"{(int)t.TotalHours:00}:{t.Minutes:00}:{t.Seconds:00}";}
    private sealed record MovieRow(MovieSummary Record){public string MovieKey=>Record.MovieKey;public string FileName=>Record.FileName;public long OpenCount=>Record.OpenCount;public string Played=>Duration(Record.PlayedMs);public string Last=>Record.LastOpened.LocalDateTime.ToString("yyyy-MM-dd HH:mm");}
    private sealed record SessionRow(SessionRecord Record){public string Started=>Record.StartedUtc.LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss");public string Played=>Duration(Record.PlayedMs);public string Mode=>Record.Mode==CaptureMode.Simple?"精简":"详细";}
    private sealed record SourceRow(Guid Id,string Path){public string Label=>$"{Path} [{Id.ToString()[..8]}]";}
    private sealed record SegmentRow(SegmentRecord Record){public string WallStart=>Record.StartedUtc.LocalDateTime.ToString("MM-dd HH:mm:ss.fff");public string MediaRange=>$"{TimelineControl.Media(Record.StartMs)} — {TimelineControl.Media(Record.EndMs)}";public string Played=>$"{Record.PlayedMs/1000d:0.000} 秒";public string Reason=>Record.EndReason;}
    private sealed record HotspotRow(HeatInterval Record){public string Range=>$"{TimelineControl.Media(Record.StartMs)} — {TimelineControl.Media(Record.EndMs)}";public int Count=>Record.Count;}
}
