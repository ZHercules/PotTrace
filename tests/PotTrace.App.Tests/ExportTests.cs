using PotTrace.App.Services;
using PotTrace.Contracts;
using PotTrace.Core;
using PotTrace.Storage;
using System.Text.Json;
namespace PotTrace.App.Tests;
public class ExportTests
{
    [Fact] public async Task JsonExportPreservesEstimatedRateWindowFromRealDatabase()
    {
        string folder=Path.Combine(Path.GetTempPath(),"PotTraceExport-"+Guid.NewGuid());Directory.CreateDirectory(folder);
        try {
            var db=await Database.CreateAsync(Path.Combine(folder,"history.db"));var writer=new IngestionWriter(db,new SessionTracker(CaptureMode.Detailed));
            var id=Guid.NewGuid();var epoch=Guid.NewGuid();var origin=DateTimeOffset.Parse("2026-11-17T00:00:00Z");long seq=0;
            PlaybackEvent E(long at,long pos,PlaybackEventKind kind,PlaybackState state=PlaybackState.Playing)=>new(){InstanceId=id,MediaEpoch=epoch,SourcePath=@"D:\fixture.mp4",Sequence=++seq,MonotonicMs=at,ObservedAtUtc=origin.AddMilliseconds(at),PositionMs=pos,State=state,Kind=kind,Rate=2,RateIsEstimated=true};
            await writer.ApplyAsync(E(0,0,PlaybackEventKind.MediaOpened,PlaybackState.Paused),default);
            await writer.ApplyAsync(E(11000,0,PlaybackEventKind.Resumed),default);
            await writer.ApplyAsync(E(17000,12000,PlaybackEventKind.RateChanged) with{Rate=null,RateWindowStartMs=16200},default);
            var path=Path.Combine(folder,"export.json");await ExportService.ExportAsync(new(db),new(),path,true,default);
            using var json=JsonDocument.Parse(await File.ReadAllTextAsync(path));
            var timeline=json.RootElement.GetProperty("Timeline");
            var change=Assert.Single(timeline.EnumerateArray(),x=>x.GetProperty("Kind").GetString()=="RateChanged");
            Assert.True(change.GetProperty("RateIsEstimated").GetBoolean());
            Assert.Equal(origin.AddMilliseconds(16200),change.GetProperty("RateWindowStartUtc").GetDateTimeOffset());
        } finally {Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();Directory.Delete(folder,true);}
    }
    [Theory]
    [InlineData("=cmd.avi","\"'=cmd.avi\"")]
    [InlineData("a\"b,c\n.avi","\"a\"\"b,c\n.avi\"")]
    public void CsvEscapesCellsAndNeutralizesFormulas(string raw,string expected)=>Assert.Equal(expected,ExportService.Cell(raw));
}
