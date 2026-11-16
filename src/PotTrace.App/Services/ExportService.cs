using System.IO;
using System.Text;
using System.Text.Json;
using PotTrace.Storage;
namespace PotTrace.App.Services;
public static class ExportService
{
    public static string Cell(string value)
    {if(value.Length>0 && "=+-@\t\r".Contains(value[0]))value="'"+value;return "\""+value.Replace("\"","\"\"")+"\"";}
    public static async Task ExportAsync(HistoryRepository repository,HistoryFilter filter,string path,bool json,CancellationToken ct)
    {
        var sessions=await repository.GetAllSessionsAsync(filter,ct);
        if(json)
        {
            var segments=await repository.GetAllSegmentsAsync(filter,ct);
            await File.WriteAllTextAsync(path,JsonSerializer.Serialize(new {RuleVersion=1,Sessions=sessions,Segments=segments},new JsonSerializerOptions{WriteIndented=true}),new UTF8Encoding(false),ct);
        }
        else
        {
            var csv=new StringBuilder("文件名,来源,打开时间,结束时间,实际播放毫秒,短片段毫秒,采集模式\r\n");
            foreach(var s in sessions)csv.AppendLine(string.Join(',',Cell(Path.GetFileName(s.SourcePath)),Cell(s.SourcePath),Cell(s.StartedUtc.ToString("O")),Cell(s.EndedUtc?.ToString("O")??""),s.PlayedMs,s.ShortPlayedMs,s.Mode));
            await File.WriteAllTextAsync(path,csv.ToString(),new UTF8Encoding(true),ct);
        }
    }
}
