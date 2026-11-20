using PotTrace.Core;

namespace PotTrace.App.ViewModels;

public sealed class HeatSeries
{
    public readonly record struct Sample(long Second,int Count);
    private readonly List<Sample> points=[];
    public IReadOnlyList<Sample> Points=>points;
    public long EndSecond {get;}
    public int Peak {get;}

    public HeatSeries(IReadOnlyList<HeatInterval> intervals)
    {
        if(intervals.Count==0)return;
        long next=0;
        foreach(var interval in intervals)
        {
            long start=CeilingSecond(interval.StartMs),end=CeilingSecond(interval.EndMs);
            if(start>=end)continue;
            AddRun(next,start,0);
            AddRun(start,end,interval.Count);
            Peak=Math.Max(Peak,interval.Count);
            next=end;
        }
        EndSecond=CeilingSecond(intervals[^1].EndMs);
        AddRun(next,EndSecond+1,0);
    }

    private static long CeilingSecond(long ms)=>ms/1000+(ms%1000>0?1:0);

    private void AddRun(long start,long end,int count)
    {
        if(end<=start)return;
        points.Add(new(start,count));
        if(end-start>1)points.Add(new(end-1,count));
    }

    public int CountAt(long second)
    {
        if(second<0||second>EndSecond||points.Count==0)return 0;
        int low=0,high=points.Count;
        while(low<high)
        {
            int middle=low+(high-low)/2;
            if(points[middle].Second<=second)low=middle+1;else high=middle;
        }
        return points[low-1].Count;
    }
}
