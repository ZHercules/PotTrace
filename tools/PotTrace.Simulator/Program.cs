using System.Text.Json;
using PotTrace.Contracts;
using PotTrace.Core;
using PotTrace.Storage;
string Arg(string name,string fallback=""){int at=Array.IndexOf(args,name);return at>=0&&at+1<args.Length?args[at+1]:fallback;}
string scenario=Arg("--scenario");
if(scenario.Length>0)
{
    var events=JsonSerializer.Deserialize<PlaybackEvent[]>(await File.ReadAllTextAsync(scenario),FrameCodec.JsonOptions)??[];
    var tracker=new SessionTracker(CaptureMode.Detailed);
    foreach(var e in events)
    {
        var valid=EventValidator.Validate(e);if(!valid.IsValid)throw new InvalidDataException(valid.Error);
        var message=new WireMessage{InstanceId=e.InstanceId,Events=[e]};var frame=FrameCodec.Encode(message);
        await using var input=new MemoryStream(frame);if(await FrameCodec.ReadAsync(input,default) is null)throw new Exception("Roundtrip failed");
        var update=tracker.Apply(e);foreach(var s in update.Sessions)Console.WriteLine($"{s.SessionId}: valid={s.IsValid}, play={s.PlayedMs}, stay={s.StayMs}, segments={update.Segments.Count}");
    }
    Console.WriteLine($"Validated {events.Length} events");return;
}
string databasePath=Arg("--seed");if(databasePath.Length==0){Console.WriteLine("--scenario <json> --validate | --seed <new database path>");return;}
if(File.Exists(databasePath))throw new IOException("Seed requires a new database so real history cannot be overwritten");
var database=await Database.CreateAsync(databasePath);var writer=new IngestionWriter(database,new(CaptureMode.Detailed));
var instance=Guid.NewGuid();long sequence=0,monotonic=0;var start=DateTimeOffset.UtcNow.AddDays(-2);
foreach(var filename in new[]{"星际旅行 · 纪录片.mkv","城市雨夜.mp4","同名影片.avi"})
{
    for(int session=0;session<3;session++)
    {
        var epoch=Guid.NewGuid();string path=Path.Combine(@"D:\示例影片",filename);long begin=monotonic;
        async Task Event(long elapsed,long position,PlaybackEventKind kind,PlaybackState state=PlaybackState.Playing)
        {monotonic=begin+elapsed;await writer.ApplyAsync(new(){InstanceId=instance,MediaEpoch=epoch,Sequence=++sequence,SourcePath=path,Kind=kind,ObservedAtUtc=start.AddMilliseconds(monotonic),MonotonicMs=monotonic,PositionMs=position,DurationMs=120000,State=state,Rate=1},default);}
        await Event(0,10000,PlaybackEventKind.MediaOpened);
        await Event(18000,28000,PlaybackEventKind.Paused,PlaybackState.Paused);
        await Event(25000,28000,PlaybackEventKind.Resumed);
        await Event(25001,15000,PlaybackEventKind.Seeked);
        await Event(40001,30000,PlaybackEventKind.MediaClosed);
        monotonic+=3600000;
    }
}
var repo=new HistoryRepository(database);var movies=await repo.GetMoviesAsync(new(),default);
if(movies.Count!=3||movies.Any(x=>x.OpenCount!=3||x.PlayedMs!=99000))throw new Exception("Seed statistics do not match the scenario");
Console.WriteLine($"Seed verified: {movies.Count} movies, {movies.Sum(x=>x.OpenCount)} sessions, {movies.Sum(x=>x.PlayedMs)} ms");
