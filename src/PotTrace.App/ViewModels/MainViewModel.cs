using PotTrace.Storage;
using PotTrace.Core;
namespace PotTrace.App.ViewModels;
public sealed class MainViewModel
{
    private readonly Func<HistoryFilter,CancellationToken,Task<IReadOnlyList<MovieSummary>>> query;
    private long generation;
    public MainViewModel(Func<HistoryFilter,CancellationToken,Task<IReadOnlyList<MovieSummary>>> query,CaptureMode mode=CaptureMode.Simple){this.query=query;CaptureMode=mode;}
    public CaptureMode CaptureMode {get;}
    public bool DetailedView {get;set;}
    public IReadOnlyList<MovieSummary> Movies {get;private set;}=[];
    public async Task RefreshAsync(HistoryFilter filter,CancellationToken ct)
    {long request=Interlocked.Increment(ref generation);var result=await query(filter,ct);ct.ThrowIfCancellationRequested();if(request==generation)Movies=result;}
    public static string DetailNotice(SessionRecord session)=>session.Mode==CaptureMode.Simple?"当时未采集明细":"仅显示实际播放超过 5 秒的连续片段；重复观看独立列出。";
    public static string SessionStatus(SessionRecord session)=>session.IsValid?"已计入":$"已停留 {session.StayMs/1000d:0.0} / 10 秒，尚未计入";
}
