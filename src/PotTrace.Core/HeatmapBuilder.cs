namespace PotTrace.Core;
public static class HeatmapBuilder
{
    public static IReadOnlyList<HeatInterval> Build(IEnumerable<SegmentRecord> segments)
    {
        var result=new List<HeatInterval>();
        foreach(var source in segments.Where(s=>s.EndMs>s.StartMs && s.PlayedMs>5000).GroupBy(x=>x.SourceId))
        {
            var changes=new SortedDictionary<long,int>();
            foreach(var segment in source)
            { changes[segment.StartMs]=changes.GetValueOrDefault(segment.StartMs)+1;changes[segment.EndMs]=changes.GetValueOrDefault(segment.EndMs)-1; }
            int count=0;long? previous=null;
            foreach(var (position,delta) in changes)
            {
                if(previous is {} start && position>start && count>0)
                {
                    if(result.LastOrDefault() is {} last && last.SourceId==source.Key && last.EndMs==start && last.Count==count)
                        result[^1]=last with {EndMs=position};
                    else result.Add(new(source.Key,start,position,count));
                }
                count+=delta;previous=position;
            }
        }
        return result;
    }
}
