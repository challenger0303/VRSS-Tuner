using System.Buffers.Binary;
using System.Numerics;

namespace PicoGaze;

internal readonly record struct EyeFrame(long Epoch, long SourceTime,
    uint LeftStatus, uint RightStatus, uint CombinedStatus,
    Vector3 Left, Vector3 Right, Vector3 Combined)
{
    public bool CombinedValid => (CombinedStatus & 2) != 0;
    public bool LeftValid => (LeftStatus & 0x100) != 0;
    public bool RightValid => (RightStatus & 0x100) != 0;
}

internal static class EyePacket
{
    // BridgeSplit E: the header's morphology flags are zero. Native gaze validity
    // belongs to the three status words, not the morphology or openness flags.
    public static bool TryDecode(ReadOnlySpan<byte> bytes, out EyeFrame frame)
    {
        frame = default;
        if (bytes.Length != 83 || bytes[0] != 'E' || bytes[1] is not (1 or 3) || bytes[2] != 0)
            return false;
        long epoch = BinaryPrimitives.ReadInt64LittleEndian(bytes[3..]);
        long time = BinaryPrimitives.ReadInt64LittleEndian(bytes[11..]);
        if (epoch <= 0 || time <= 0) return false;
        uint left = BinaryPrimitives.ReadUInt32LittleEndian(bytes[19..]);
        uint right = BinaryPrimitives.ReadUInt32LittleEndian(bytes[23..]);
        uint combined = BinaryPrimitives.ReadUInt32LittleEndian(bytes[27..]);
        if (((left | right | combined) & ~0x906u) != 0) return false;
        for (int at = 31; at < bytes.Length; at += 4)
            if (!float.IsFinite(BinaryPrimitives.ReadSingleLittleEndian(bytes[at..]))) return false;
        frame = new(epoch, time, left, right, combined,
            Vector(bytes, 31), Vector(bytes, 43), Vector(bytes, 55));
        return true;
    }

    private static Vector3 Vector(ReadOnlySpan<byte> bytes, int at) => new(
        BinaryPrimitives.ReadSingleLittleEndian(bytes[at..]),
        BinaryPrimitives.ReadSingleLittleEndian(bytes[(at + 4)..]),
        BinaryPrimitives.ReadSingleLittleEndian(bytes[(at + 8)..]));
}
