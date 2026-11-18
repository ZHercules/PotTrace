namespace PotTrace.Storage;
public sealed record HistoryFilter(DateOnly? OpenedFrom=null,DateOnly? OpenedTo=null,string? Search=null,string? MovieKey=null,Guid? SourceId=null,int Offset=0,int Limit=200);
public sealed record DailyUsageFilter(DateOnly From,DateOnly To);
public sealed record MovieSummary(string MovieKey,string FileName,long OpenCount,long PlayedMs,DateTimeOffset LastOpened);
public sealed record DailyUsage(DateOnly Date,long PlayedMs,long ShortPlayedMs);
public sealed record IngestReceipt(long CommittedSequence,bool Duplicate);
