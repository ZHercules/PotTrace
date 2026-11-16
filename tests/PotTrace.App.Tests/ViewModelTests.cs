using PotTrace.App.ViewModels;
using PotTrace.Storage;
using PotTrace.Core;
namespace PotTrace.App.Tests;
public class ViewModelTests
{
    [Fact] public void ViewingDetailedDoesNotChangeCaptureMode()
    {
        var vm=new MainViewModel((_,_)=>Task.FromResult<IReadOnlyList<MovieSummary>>([]),CaptureMode.Simple);
        vm.DetailedView=true;Assert.Equal(CaptureMode.Simple,vm.CaptureMode);
        Assert.Contains("未采集明细",MainViewModel.DetailNotice(new(){Mode=CaptureMode.Simple}));
        Assert.Contains("尚未计入",MainViewModel.SessionStatus(new(){StayMs=8200}));
    }
    [Fact] public async Task OlderQueryCannotOverwriteNewerFilter()
    {
        var first=new TaskCompletionSource<IReadOnlyList<MovieSummary>>();
        var vm=new MainViewModel((filter,_)=>filter.Search=="old"?first.Task:Task.FromResult<IReadOnlyList<MovieSummary>>([new("NEW","new.avi",1,12000,DateTimeOffset.UtcNow)]));
        var old=vm.RefreshAsync(new(Search:"old"),default);await vm.RefreshAsync(new(Search:"new"),default);
        first.SetResult([new("OLD","old.avi",1,12000,DateTimeOffset.UtcNow)]);await old;
        Assert.Equal("NEW",Assert.Single(vm.Movies).MovieKey);
    }
}
