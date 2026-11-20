using PotTrace.App.ViewModels;
using PotTrace.Core;

namespace PotTrace.App.Tests;

public class HeatSeriesTests
{
    [Fact]
    public void SamplesExactSecondsWithExclusiveEndsAndZeroInGaps()
    {
        var series=new HeatSeries([
            new HeatInterval(Guid.Empty,500,2250,1),new(Guid.Empty,2250,4000,3),
            new(Guid.Empty,5100,5500,4),new(Guid.Empty,6000,7005,2)]);
        Assert.Equal(8,series.EndSecond);
        Assert.Equal(new[]{0,1,1,3,0,0,2,2,0},Enumerable.Range(0,9).Select(second=>series.CountAt(second)));
        Assert.Equal(3,series.Peak);
    }

    [Fact]
    public void LongConstantRunsNeedOnlyTheirEndpoints()
    {
        var series=new HeatSeries([new(Guid.Empty,0,100000000,5)]);
        Assert.Equal(5,series.CountAt(90000));
        Assert.Equal(0,series.CountAt(100000));
        Assert.Equal(3,series.Points.Count);
        Assert.Empty(new HeatSeries([]).Points);
    }
}
