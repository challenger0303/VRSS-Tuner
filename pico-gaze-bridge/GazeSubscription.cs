using System.Globalization;
using System.Net;
using System.Security.Cryptography;
using System.Text;

namespace PicoGaze;

// All timestamps are local monotonic QPC ticks. Only this peer's ACK authorizes
// an epoch; E datagrams can never select or change the subscription's source.
internal sealed class GazeSubscription
{
    public const int RenewalMilliseconds = 1000;
    public const int AckTimeoutMilliseconds = 3000;

    private readonly IPEndPoint headset;
    private readonly GazeStream stream;
    private readonly long frequency;
    private long lastRenewal, lastAck;
    private bool renewed, acknowledged, enabled, closed;

    public string Id { get; private set; }
    public long Epoch { get; private set; }

    public GazeSubscription(IPEndPoint headset, EyeGeometry[] eyes, Calibration gain,
        long frequency, string? id = null)
    {
        this.headset = new(headset.Address, headset.Port);
        this.frequency = frequency;
        stream = new(eyes, gain);
        Id = id ?? NewId();
        if (!IsHexId(Id)) throw new ArgumentException("Subscription id must be 16 lowercase hex digits", nameof(id));
    }

    public byte[]? Renew(long nowQpc)
    {
        Expire(nowQpc);
        if (closed || (renewed && nowQpc - lastRenewal < frequency * RenewalMilliseconds / 1000)) return null;
        renewed = true;
        lastRenewal = nowQpc;
        return Encoding.ASCII.GetBytes($"PXR_GAZE_SUB id={Id}");
    }

    public byte[]? Receive(ReadOnlySpan<byte> bytes, IPEndPoint peer, long arrivalQpc)
    {
        Expire(arrivalQpc);
        if (closed || !peer.Equals(headset)) return null;
        if (TryReadAck(bytes, out long epoch, out bool active))
        {
            // Epochs increase within a confirmed subscription. A delayed ACK
            // must not undo a newer disable. Recovery requires a fresh identity.
            if (acknowledged && epoch < Epoch) return null;
            stream.ConfirmEpoch(epoch, newSubscription: !acknowledged);
            Epoch = epoch;
            lastAck = arrivalQpc;
            acknowledged = true;
            enabled = active;
            return null;
        }
        if (!acknowledged || !enabled || bytes.Length != 83 || bytes[1] != 1 ||
            !EyePacket.TryDecode(bytes, out var frame) || frame.Epoch != Epoch) return null;
        return stream.Accept(bytes, arrivalQpc, frequency);
    }

    public byte[]? Close()
    {
        if (closed) return null;
        closed = true;
        acknowledged = enabled = false;
        return Encoding.ASCII.GetBytes($"PXR_GAZE_STOP id={Id}");
    }

    private void Expire(long nowQpc)
    {
        if (!acknowledged || nowQpc - lastAck < frequency * AckTimeoutMilliseconds / 1000) return;
        acknowledged = enabled = false;
        Epoch = 0;
        renewed = false;
        string expiredId = Id;
        do { Id = NewId(); } while (Id == expiredId);
        // Rotate only on this confirmed -> expired transition. The next Renew
        // sends immediately; unacknowledged retries keep the new identity.
    }

    private bool TryReadAck(ReadOnlySpan<byte> bytes, out long epoch, out bool active)
    {
        epoch = 0;
        active = false;
        // Reject non-ASCII instead of letting Encoding replace bytes in an ACK.
        foreach (byte value in bytes)
            if (value > 127) return false;
        var fields = Encoding.ASCII.GetString(bytes).Split(' ');
        if (fields.Length != 4 || fields[0] != "PXR_GAZE_ACK" || fields[1] != $"id={Id}" ||
            !fields[2].StartsWith("epoch=", StringComparison.Ordinal) ||
            !IsHexId(fields[2][6..]) ||
            !long.TryParse(fields[2].AsSpan(6), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out epoch) ||
            epoch <= 0 || fields[3] is not ("enabled=0" or "enabled=1")) return false;
        active = fields[3] == "enabled=1";
        return true;
    }

    private static bool IsHexId(string value) => value.Length == 16 &&
        value.All(character => character is >= '0' and <= '9' or >= 'a' and <= 'f');

    private static string NewId() => Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(8));
}
