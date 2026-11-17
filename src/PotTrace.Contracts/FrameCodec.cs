using System.Buffers.Binary;
using System.Text.Json;
using System.Text.Json.Serialization;
namespace PotTrace.Contracts;
public static class FrameCodec
{
    public const int MaxFrameBytes = 1024 * 1024;
    public static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    { Converters = { new JsonStringEnumConverter() } };

    public static byte[] Encode(WireMessage message)
    {
        ValidateMessage(message);
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(message, JsonOptions);
        if (json.Length > MaxFrameBytes) throw new InvalidDataException("Frame too large");
        byte[] frame = new byte[json.Length + 4];
        BinaryPrimitives.WriteInt32LittleEndian(frame, json.Length);
        json.CopyTo(frame, 4);
        return frame;
    }

    public static async ValueTask<WireMessage?> ReadAsync(Stream stream, CancellationToken ct)
    {
        byte[] header = new byte[4];
        if (await stream.ReadAsync(header.AsMemory(0, 1), ct) == 0) return null;
        await stream.ReadExactlyAsync(header.AsMemory(1), ct);
        int length = BinaryPrimitives.ReadInt32LittleEndian(header);
        if (length <= 0 || length > MaxFrameBytes) throw new InvalidDataException("Invalid frame length");
        byte[] payload = new byte[length];
        await stream.ReadExactlyAsync(payload, ct);
        var message = JsonSerializer.Deserialize<WireMessage>(payload, JsonOptions) ?? throw new InvalidDataException("Null message");
        ValidateMessage(message);
        return message;
    }

    private static void ValidateMessage(WireMessage message)
    {
        if (message.Version != 1 || message.Type is not ("Hello" or "Events" or "Ack" or "Control" or "Status"))
            throw new InvalidDataException("Unsupported message");
        if (message.Type == "Events")
        {
            if (message.Events is not { Length: > 0 and <= 128 }) throw new InvalidDataException("Invalid event batch");
            foreach (var e in message.Events)
            {
                var validation = EventValidator.Validate(e);
                if (!validation.IsValid) throw new InvalidDataException(validation.Error);
            }
        }
    }
}
