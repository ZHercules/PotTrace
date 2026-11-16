namespace PotTrace.Contracts;

public enum PlaybackEventKind { InstanceStarted, MediaOpened, Snapshot, Paused, Resumed, Seeked, RateChanged, BufferingStarted, BufferingEnded, Completed, Stopped, MediaClosed, Suspended, Gap, InstanceExited }
public enum PlaybackState { Unknown, Playing, Paused, Buffering, Stopped }
public enum ObservationQuality { Observed, Estimated, Gap }

public sealed record PlaybackEvent
{
    public int ProtocolVersion { get; init; } = 1;
    public Guid InstanceId { get; init; }
    public Guid? MediaEpoch { get; init; }
    public long Sequence { get; init; }
    public PlaybackEventKind Kind { get; init; }
    public DateTimeOffset ObservedAtUtc { get; init; } = DateTimeOffset.UtcNow;
    public long MonotonicMs { get; init; }
    public string? SourcePath { get; init; }
    public string? SourceVersion { get; init; }
    public long? PositionMs { get; init; }
    public long? DurationMs { get; init; }
    public PlaybackState State { get; init; }
    public double? Rate { get; init; }
    public ObservationQuality Quality { get; init; }
}
