using PotTrace.Core;
using Microsoft.Data.Sqlite;
using System.Text.Json;
namespace PotTrace.Storage;
public sealed class HistoryRepository
{
    private readonly Database database;
    public HistoryRepository(Database database)=>this.database=database;
    internal static string Where=>"s.Valid=1 AND ($from IS NULL OR s.OpenedDate >= $from) AND ($to IS NULL OR s.OpenedDate <= $to) AND ($search IS NULL OR instr(upper(s.MovieKey),upper($search))>0) AND ($movie IS NULL OR s.MovieKey=$movie) AND ($source IS NULL OR s.SourceId=$source)";
    internal static void Filter(SqliteCommand command,HistoryFilter filter)
    {
        command.Parameters.AddWithValue("$from",(object?)filter.OpenedFrom?.ToString("yyyy-MM-dd")??DBNull.Value);
        command.Parameters.AddWithValue("$to",(object?)filter.OpenedTo?.ToString("yyyy-MM-dd")??DBNull.Value);
        command.Parameters.AddWithValue("$search",(object?)filter.Search??DBNull.Value);
        command.Parameters.AddWithValue("$source",(object?)filter.SourceId?.ToString()??DBNull.Value);
        command.Parameters.AddWithValue("$movie",(object?)filter.MovieKey??DBNull.Value);
        command.Parameters.AddWithValue("$limit",Math.Clamp(filter.Limit,1,100000));command.Parameters.AddWithValue("$offset",Math.Max(0,filter.Offset));
    }
    public async Task<IReadOnlyList<MovieSummary>> GetMoviesAsync(HistoryFilter filter,CancellationToken ct)
    {
        using var c=database.Open();using var cmd=c.CreateCommand();
        cmd.CommandText=$"SELECT s.MovieKey,m.Name,count(*),sum(s.PlayedMs),max(s.StartedUtc) FROM Session s JOIN Movie m ON m.MovieKey=s.MovieKey WHERE {Where} GROUP BY s.MovieKey ORDER BY max(s.StartedUtc) DESC LIMIT $limit OFFSET $offset";Filter(cmd,filter);
        using var r=await cmd.ExecuteReaderAsync(ct);var result=new List<MovieSummary>();
        while(await r.ReadAsync(ct))result.Add(new(r.GetString(0),r.GetString(1),r.GetInt64(2),r.GetInt64(3),DateTimeOffset.Parse(r.GetString(4))));return result;
    }
    public async Task<IReadOnlyList<SessionRecord>> GetSessionsAsync(HistoryFilter filter,CancellationToken ct)
    {
        using var c=database.Open();using var cmd=c.CreateCommand();cmd.CommandText=$"SELECT s.Payload FROM Session s WHERE {Where} ORDER BY s.StartedUtc DESC LIMIT $limit OFFSET $offset";Filter(cmd,filter);
        return await ReadJson<SessionRecord>(cmd,ct);
    }
    public async Task<IReadOnlyList<SegmentRecord>> GetSegmentsAsync(Guid sessionId,CancellationToken ct)
    {using var c=database.Open();using var cmd=Database.Command(c,null,"SELECT Payload FROM Segment WHERE SessionId=$id ORDER BY StartMs",("$id",sessionId.ToString()));return (await ReadJson<SegmentRecord>(cmd,ct)).OrderBy(x=>x.Order).ToArray();}
    public async Task<IReadOnlyList<SegmentRecord>> GetAllSegmentsAsync(HistoryFilter filter,CancellationToken ct)
    {using var c=database.Open();using var cmd=c.CreateCommand();cmd.CommandText=$"SELECT g.Payload FROM Segment g JOIN Session s ON s.Id=g.SessionId WHERE {Where} ORDER BY s.StartedUtc,g.StartMs";Filter(cmd,filter);return await ReadJson<SegmentRecord>(cmd,ct);}
    public async Task<IReadOnlyList<TimelineEventRecord>> GetTimelineAsync(Guid sessionId,CancellationToken ct)
    {using var c=database.Open();using var cmd=Database.Command(c,null,"SELECT Payload FROM TimelineEvent WHERE SessionId=$id ORDER BY Sequence",("$id",sessionId.ToString()));return await ReadJson<TimelineEventRecord>(cmd,ct);}
    public async Task<IReadOnlyList<DailyUsage>> GetDailyUsageAsync(DailyUsageFilter filter,CancellationToken ct)
    {
        using var c=database.Open();using var cmd=Database.Command(c,null,"SELECT d.Date,sum(d.PlayedMs),sum(d.ShortPlayedMs) FROM SessionDay d JOIN Session s ON s.Id=d.SessionId WHERE s.Valid=1 AND d.Date >= $from AND d.Date <= $to GROUP BY d.Date ORDER BY d.Date",("$from",filter.From.ToString("yyyy-MM-dd")),("$to",filter.To.ToString("yyyy-MM-dd")));
        using var r=await cmd.ExecuteReaderAsync(ct);var result=new List<DailyUsage>();while(await r.ReadAsync(ct))result.Add(new(DateOnly.Parse(r.GetString(0)),r.GetInt64(1),r.GetInt64(2)));return result;
    }
    internal static async Task<IReadOnlyList<T>> ReadJson<T>(SqliteCommand cmd,CancellationToken ct)
    {using var r=await cmd.ExecuteReaderAsync(ct);var result=new List<T>();while(await r.ReadAsync(ct))result.Add(JsonSerializer.Deserialize<T>(r.GetString(0))!);return result;}
}
