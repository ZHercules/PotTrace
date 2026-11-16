namespace PotTrace.Contracts;

public sealed record WireMessage
{
    public int Version { get; init; } = 1;
    public string Type { get; init; } = "Events";
    public Guid InstanceId { get; init; }
    public long AckSequence { get; init; }
    public PlaybackEvent[]? Events { get; init; }
    public string? Command { get; init; }
    public int ProcessId { get; init; }
}
public sealed record ValidationResult(bool IsValid, string? Error = null);
