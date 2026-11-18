using PotTrace.Contracts;
using System.Text.Json;
namespace PotTrace.Core;
public sealed class SessionTracker
{
    private CaptureMode mode;
    private readonly TimeZoneInfo zone;
    private bool enabled=true;
    private readonly Dictionary<Guid,ActiveSession> active=[];
    private readonly Dictionary<Guid,long> sequences=[];
    public SessionTracker(CaptureMode mode=CaptureMode.Simple,TimeZoneInfo? timeZone=null)
    { this.mode=mode;zone=timeZone??TimeZoneInfo.Local; }

    public EngineUpdate Apply(PlaybackEvent e)
    {
        long lastSequence=sequences.GetValueOrDefault(e.InstanceId);
        if (lastSequence>=e.Sequence) return EngineUpdate.Empty;
        if(lastSequence>0 && e.Sequence!=lastSequence+1)e=e with {Quality=ObservationQuality.Gap};
        sequences[e.InstanceId]=e.Sequence;
        if(!enabled) return EngineUpdate.Empty;
        var changed=new List<ActiveSession>();
        active.TryGetValue(e.InstanceId,out var current);
        if (current is not null && e.MediaEpoch is {} epoch && epoch!=current.Record.MediaEpoch)
        {
            Close(current,current.Last,"MediaChanged");changed.Add(current);active.Remove(e.InstanceId);current=null;
        }
        if(current is null)
        {
            if (e.MediaEpoch is null || string.IsNullOrEmpty(e.SourcePath) || e.Kind is PlaybackEventKind.MediaClosed or PlaybackEventKind.Completed or PlaybackEventKind.Stopped or PlaybackEventKind.InstanceExited || e.State is not (PlaybackState.Playing or PlaybackState.Paused))
                return Snapshot(changed);
            current=new ActiveSession
            {
                Record=new SessionRecord {SessionId=Guid.NewGuid(),InstanceId=e.InstanceId,MediaEpoch=e.MediaEpoch.Value,
                    SourceId=MovieIdentity.SourceId(e.SourcePath,e.SourceVersion),SourcePath=e.SourcePath,MovieKey=MovieIdentity.GetKey(e.SourcePath),
                    StartedUtc=e.ObservedAtUtc,Mode=mode,TimeZoneId=zone.Id}, Last=e
            };
            active[e.InstanceId]=current;
        }
        else
        {
            var previous=current.Last;
            long dt=Math.Max(0,e.MonotonicMs-previous.MonotonicMs);
            bool gap=e.Kind is PlaybackEventKind.Gap or PlaybackEventKind.Suspended || e.Quality==ObservationQuality.Gap;
            long stay=!gap && previous.State is PlaybackState.Playing or PlaybackState.Paused ? dt : 0;
            long played=0;
            if(!gap && previous.State==PlaybackState.Playing && e.Kind is not (PlaybackEventKind.Seeked or PlaybackEventKind.PositionDiscontinuity) && previous.PositionMs is {} from && e.PositionMs is {} to)
            {
                long delta=to-from;
                // Unknown speed is not normal speed. Only short, advancing
                // samples within the existing absolute tolerance may bridge it.
                bool plausible=previous.Rate is {} rate
                    ? Math.Abs(delta-dt*rate)<=Math.Max(500,dt*rate*.20)
                    : dt>0 && dt<=500 && delta<=500;
                if(delta>0 && plausible)
                    played=dt;
            }
            current.Record=current.Record with {StayMs=current.Record.StayMs+stay,PlayedMs=current.Record.PlayedMs+played};
            current.Record=current.Record with {IsValid=current.Record.StayMs>10000};
            AddDays(current,previous.ObservedAtUtc,dt,stay,played);
            current.Segments.Observe(current.Record,previous,e,played);
            RefreshShortDays(current);
            if(e.Kind!=PlaybackEventKind.Snapshot)
                current.Timeline.Add(new(current.Record.SessionId,e.Sequence,e.ObservedAtUtc,e.Kind.ToString(),e.PositionMs)
                {RateIsEstimated=e.RateIsEstimated,RateWindowStartUtc=e.RateWindowStartMs is {} start&&start>=0&&start<=e.MonotonicMs
                    ? e.ObservedAtUtc.AddMilliseconds(start-e.MonotonicMs):null});
            current.Last=e;
        }
        if(e.Kind is PlaybackEventKind.Completed or PlaybackEventKind.Stopped or PlaybackEventKind.MediaClosed or PlaybackEventKind.InstanceExited)
        { Close(current,e,e.Kind.ToString());active.Remove(e.InstanceId); }
        changed.Add(current);
        return Snapshot(changed);
    }

    public EngineUpdate SetCaptureEnabled(bool enabled,DateTimeOffset utc,long monotonicMs)
    {
        this.enabled=enabled;
        if(enabled) return EngineUpdate.Empty;
        var closed=active.Values.ToArray();
        foreach(var state in closed) Close(state,state.Last,"RecordingPaused");
        active.Clear();
        return Snapshot(closed);
    }
    public void SetNextCaptureMode(CaptureMode nextMode)=>mode=nextMode;
    public string ExportCheckpoint()=>JsonSerializer.Serialize(new TrackerCheckpoint(enabled,mode,active,sequences));
    public void ImportCheckpoint(string json)
    {
        var checkpoint=JsonSerializer.Deserialize<TrackerCheckpoint>(json)??throw new InvalidDataException("Invalid checkpoint");
        enabled=checkpoint.Enabled;mode=checkpoint.Mode;active.Clear();sequences.Clear();
        foreach(var item in checkpoint.Active)active.Add(item.Key,item.Value);
        foreach(var item in checkpoint.Sequences)sequences.Add(item.Key,item.Value);
    }
    public EngineUpdate Disconnect(Guid instanceId)
    {
        if(!active.Remove(instanceId,out var state))return EngineUpdate.Empty;
        Close(state,state.Last,"Disconnected");return Snapshot([state]);
    }
    public IReadOnlyList<Guid> ActiveInstances=>active.Keys.ToArray();
    public IReadOnlyList<SessionRecord> CurrentSessions=>active.Values.Select(x=>x.Record).ToArray();
    public void DiscardSessions(IReadOnlySet<Guid> sessionIds)
    {foreach(var pair in active.Where(x=>sessionIds.Contains(x.Value.Record.SessionId)).ToArray())active.Remove(pair.Key);}

    private void AddDays(ActiveSession state,DateTimeOffset start,long span,long stay,long played)
    {
        if(span==0) return;
        long remaining=span,stayLeft=stay,playLeft=played;
        while(remaining>0)
        {
            var local=TimeZoneInfo.ConvertTime(start,zone);
            var date=DateOnly.FromDateTime(local.DateTime);
            var next=TimeZoneInfo.ConvertTimeToUtc(date.AddDays(1).ToDateTime(TimeOnly.MinValue,DateTimeKind.Unspecified),zone);
            long slice=Math.Min(remaining,Math.Max(1,(long)(next-start.UtcDateTime).TotalMilliseconds));
            long s=remaining==slice?stayLeft:(long)Math.Round(stay*(double)slice/span);
            long p=remaining==slice?playLeft:(long)Math.Round(played*(double)slice/span);
            var day=state.Days.GetValueOrDefault(date)??new SessionDayRecord(state.Record.SessionId,date,0,0,0);
            state.Days[date]=day with {StayMs=day.StayMs+s,PlayedMs=day.PlayedMs+p};
            remaining-=slice;stayLeft-=s;playLeft-=p;start=start.AddMilliseconds(slice);
        }
    }

    private void RefreshShortDays(ActiveSession state)
    {
        foreach(var key in state.Days.Keys.ToArray())state.Days[key]=state.Days[key] with {ShortPlayedMs=0};
        var shorts=state.Segments.All.Where(x=>x.PlayedMs<=5000).ToArray();
        state.Record=state.Record with {ShortPlayedMs=shorts.Sum(x=>x.PlayedMs)};
        foreach(var segment in shorts)
        {
            // A segment cannot contain a pause or a seek; allocate its real duration by local midnight.
            long remaining=segment.PlayedMs;var start=segment.StartedUtc;
            while(remaining>0)
            {
                var date=DateOnly.FromDateTime(TimeZoneInfo.ConvertTime(start,zone).DateTime);
                var midnight=TimeZoneInfo.ConvertTimeToUtc(date.AddDays(1).ToDateTime(TimeOnly.MinValue,DateTimeKind.Unspecified),zone);
                long slice=Math.Min(remaining,Math.Max(1,(long)(midnight-start.UtcDateTime).TotalMilliseconds));
                var day=state.Days.GetValueOrDefault(date)??new(state.Record.SessionId,date,0,0,0);
                state.Days[date]=day with {ShortPlayedMs=day.ShortPlayedMs+slice};remaining-=slice;start=start.AddMilliseconds(slice);
            }
        }
    }
    private static void Close(ActiveSession state,PlaybackEvent e,string reason)
    { state.Segments.End(reason);state.Record=state.Record with {EndedUtc=e.ObservedAtUtc,EndReason=reason}; }
    private static EngineUpdate Snapshot(IEnumerable<ActiveSession> states)
    {
        var items=states.ToArray();
        var detailed=items.Where(x=>x.Record.IsValid && x.Record.Mode==CaptureMode.Detailed).ToArray();
        return new(items.Select(x=>x.Record).ToArray(),items.SelectMany(x=>x.Days.Values).ToArray(),
            detailed.SelectMany(x=>x.Segments.All.Where(s=>s.PlayedMs>5000)).ToArray(),detailed.SelectMany(x=>x.Timeline).ToArray());
    }
    public sealed record TrackerCheckpoint(bool Enabled,CaptureMode Mode,Dictionary<Guid,ActiveSession> Active,Dictionary<Guid,long> Sequences);
    public sealed class ActiveSession
    {
        public required SessionRecord Record {get;set;}
        public required PlaybackEvent Last {get;set;}
        public Dictionary<DateOnly,SessionDayRecord> Days {get;set;}=[];
        public SegmentTracker Segments {get;set;}=new();
        public List<TimelineEventRecord> Timeline {get;set;}=[];
    }
}
