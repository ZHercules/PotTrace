namespace PotTrace.Core;
public enum CaptureMode { Simple, Detailed }
public sealed record SessionRecord
{
    public Guid SessionId {get;init;}
    public Guid InstanceId {get;init;}
    public Guid MediaEpoch {get;init;}
    public Guid SourceId {get;init;}
    public string SourcePath {get;init;}="";
    public string MovieKey {get;init;}="";
    public DateTimeOffset StartedUtc {get;init;}
    public DateTimeOffset? EndedUtc {get;init;}
    public string TimeZoneId {get;init;}="UTC";
    public long StayMs {get;init;}
    public long PlayedMs {get;init;}
    public long ShortPlayedMs {get;init;}
    public bool IsValid {get;init;}
    public CaptureMode Mode {get;init;}
    public string? EndReason {get;init;}
}
public sealed record SessionDayRecord(Guid SessionId,DateOnly Date,long StayMs,long PlayedMs,long ShortPlayedMs);
public sealed record SegmentRecord(Guid SegmentId,Guid SessionId,Guid SourceId,int Order,DateTimeOffset StartedUtc,DateTimeOffset EndedUtc,long StartMs,long EndMs,long PlayedMs,double Rate,string EndReason);
public sealed record TimelineEventRecord(Guid SessionId,long Sequence,DateTimeOffset AtUtc,string Kind,long? PositionMs);
public sealed record HeatInterval(Guid SourceId,long StartMs,long EndMs,int Count);
public sealed record EngineUpdate(IReadOnlyList<SessionRecord> Sessions,IReadOnlyList<SessionDayRecord> Days,IReadOnlyList<SegmentRecord> Segments,IReadOnlyList<TimelineEventRecord> Timeline)
{
    public static EngineUpdate Empty {get;}=new([],[],[],[]);
}
