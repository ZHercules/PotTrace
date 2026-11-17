namespace PotTrace.Contracts;
public static class EventValidator
{
    public static ValidationResult Validate(PlaybackEvent e)
    {
        if (e.ProtocolVersion != 1) return new(false, "Unsupported protocol version");
        if (e.InstanceId == Guid.Empty || e.Sequence <= 0) return new(false, "Invalid instance or sequence");
        if (e.MonotonicMs < 0 || e.PositionMs < 0 || e.DurationMs < 0) return new(false, "Negative time");
        if (e.Rate is { } rate && (!double.IsFinite(rate) || rate <= 0)) return new(false, "Invalid playback rate");
        if(e.RateWindowStartMs is {} from && (from<0||from>e.MonotonicMs||!e.RateIsEstimated||e.Kind!=PlaybackEventKind.RateChanged)) return new(false,"Invalid rate observation window");
        if (!Enum.IsDefined(e.Kind) || !Enum.IsDefined(e.State) || !Enum.IsDefined(e.Quality)) return new(false, "Invalid enum");
        if (e.Kind == PlaybackEventKind.MediaOpened && (e.MediaEpoch is null || e.MediaEpoch == Guid.Empty || string.IsNullOrWhiteSpace(e.SourcePath)))
            return new(false, "Media open requires identity");
        return new(true);
    }
}
