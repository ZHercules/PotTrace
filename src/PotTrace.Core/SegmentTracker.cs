using PotTrace.Contracts;
namespace PotTrace.Core;

public sealed class SegmentTracker
{
    public List<SegmentRecord> Finished {get;set;}=[];
    public SegmentRecord? Current {get;set;}
    public IEnumerable<SegmentRecord> All=>Current is null?Finished:Finished.Append(Current);

    public void Observe(SessionRecord session,PlaybackEvent previous,PlaybackEvent next,long played)
    {
        if(played>0 && previous.PositionMs is {} start && next.PositionMs is {} end)
        {
            Current ??= new(Guid.NewGuid(),session.SessionId,session.SourceId,Finished.Count+1,previous.ObservedAtUtc,
                previous.ObservedAtUtc,start,start,0,previous.Rate??1,"Playing");
            Current=Current with {EndMs=end,EndedUtc=next.ObservedAtUtc,PlayedMs=Current.PlayedMs+played};
        }
        if(next.Kind is not (PlaybackEventKind.Snapshot or PlaybackEventKind.MediaOpened or PlaybackEventKind.Resumed)
            || next.State!=PlaybackState.Playing || (played==0 && next.MonotonicMs>previous.MonotonicMs && previous.State==PlaybackState.Playing))
            End(next.Kind==PlaybackEventKind.Snapshot?"Discontinuity":next.Kind.ToString());
    }

    public void End(string reason)
    {
        if(Current is null) return;
        Finished.Add(Current with {EndReason=reason});Current=null;
    }
}
