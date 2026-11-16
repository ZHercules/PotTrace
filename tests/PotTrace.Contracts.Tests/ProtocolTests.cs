using PotTrace.Contracts;
using System.Text;

namespace PotTrace.Contracts.Tests;

public class ProtocolTests
{
    [Fact]
    public async Task UnicodePathRoundTripsAcrossShortReads()
    {
        var expected = new PlaybackEvent { InstanceId = Guid.NewGuid(), MediaEpoch = Guid.NewGuid(), Sequence = 1,
            Kind = PlaybackEventKind.MediaOpened, SourcePath = "D:\\电影 文件\\片段.avi", State = PlaybackState.Paused };
        var bytes = FrameCodec.Encode(new WireMessage { Type = "Events", Events = [expected] });
        using var stream = new ShortReadStream(bytes);
        var actual = await FrameCodec.ReadAsync(stream, default);
        Assert.Equal(expected.SourcePath, Assert.Single(actual!.Events!).SourcePath);
    }

    [Fact]
    public async Task TruncatedFrameCannotBecomeAnEvent()
    {
        using var stream = new MemoryStream([20, 0, 0, 0, 123]);
        await Assert.ThrowsAsync<EndOfStreamException>(async () => await FrameCodec.ReadAsync(stream, default));
    }

    [Fact]
    public async Task ExcessiveFrameIsRejectedBeforeAllocation()
    {
        using var stream = new MemoryStream([1, 0, 16, 0]);
        await Assert.ThrowsAsync<InvalidDataException>(async () => await FrameCodec.ReadAsync(stream, default));
    }

    [Theory]
    [InlineData(2, 1)]
    [InlineData(1, 0)]
    public void InvalidVersionOrSequenceRejected(int version, long sequence)
    {
        var result = EventValidator.Validate(new PlaybackEvent { ProtocolVersion=version, InstanceId=Guid.NewGuid(), Sequence=sequence });
        Assert.False(result.IsValid);
    }

    private sealed class ShortReadStream(byte[] data) : MemoryStream(data)
    {
        public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken ct = default) => base.ReadAsync(buffer[..Math.Min(3,buffer.Length)],ct);
    }
}
