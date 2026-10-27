using System.Buffers.Binary;
using System.Globalization;
using System.Numerics;

namespace PicoGaze;

internal sealed record EyeGeometry(double[] Projection, double[] Rotation);
internal sealed record Calibration(double X = 1, double Y = 1, double LeftDegrees = 0, double RightDegrees = 0);

internal static class GazeConversion
{
    public static Dictionary<string, string> ReadFields(string path) => File.ReadLines(path)
        .Where(line => line.Contains('=') && !line.TrimStart().StartsWith('#'))
        .Select(line => line.Split('=', 2))
        .ToDictionary(parts => parts[0].Trim(), parts => parts[1].Trim(), StringComparer.Ordinal);

    public static EyeGeometry[] ReadGeometry(string path)
    {
        var fields = ReadFields(path);
        double[] Values(string key, int count)
        {
            var values = fields[key].Split(',').Select(value => double.Parse(value, CultureInfo.InvariantCulture)).ToArray();
            if (values.Length != count || values.Any(value => !double.IsFinite(value)))
                throw new FormatException($"Invalid {key}");
            return values;
        }
        var result = Enumerable.Range(0, 2).Select(eye => new EyeGeometry(
            Values($"projection{eye}", 4), Values($"eyeToHead{eye}", 9))).ToArray();
        foreach (var eye in result)
        {
            var p = eye.Projection;
            if (!(p[0] < 0 && p[1] > 0 && p[2] < 0 && p[3] > 0)) throw new FormatException("Invalid projection");
        }
        return result;
    }

    public static Calibration ReadCalibration(string path)
    {
        var fields = ReadFields(path);
        double Number(string key) => double.Parse(fields[key], CultureInfo.InvariantCulture);
        var gain = new Calibration(Number("x_gain"), Number("y_gain"), Number("left_deg"), Number("right_deg"));
        if (!double.IsFinite(gain.X) || !double.IsFinite(gain.Y) ||
            !double.IsFinite(gain.LeftDegrees) || !double.IsFinite(gain.RightDegrees) ||
            gain.X is < .1 or > 2 || Math.Abs(gain.Y) is < .1 or > 2 ||
            Math.Abs(gain.LeftDegrees) > 5 || Math.Abs(gain.RightDegrees) > 5)
            throw new FormatException("Invalid calibration");
        return gain;
    }

    public static bool TryProject(EyeFrame frame, EyeGeometry[] eyes, Calibration gain, out Vector4 locations)
    {
        locations = default;
        var gaze = frame.Combined;
        if (!frame.CombinedValid || gaze.Z >= -1e-6f || gaze.LengthSquared() < 1e-12f) return false;
        if (!Project(gaze, eyes[0], gain, gain.LeftDegrees, out var left) ||
            !Project(gaze, eyes[1], gain, gain.RightDegrees, out var right)) return false;
        locations = new(left.X, left.Y, right.X, right.Y);
        return true;
    }

    private static bool Project(Vector3 direction, EyeGeometry eye, Calibration gain, double degrees, out Vector2 location)
    {
        location = default;
        // Native PICO gaze is head-relative, right/up/back: forward is negative Z.
        // The E packet has a direction, not a fixation distance. Rotate the same
        // combined ray into each eye; do not invent convergence or two measurements.
        double forward = -direction.Z;
        double x = Math.Tan(Math.Atan(direction.X / forward * gain.X) + degrees * Math.PI / 180);
        double y = direction.Y / forward * gain.Y;
        var r = eye.Rotation;
        double vx = r[0] * x + r[3] * y - r[6];
        double vy = r[1] * x + r[4] * y - r[7];
        double vz = r[2] * x + r[5] * y - r[8];
        if (!double.IsFinite(vx) || !double.IsFinite(vy) || !double.IsFinite(vz) || vz >= -1e-6) return false;
        var p = eye.Projection;
        double nx = 2 * (vx / -vz - p[0]) / (p[1] - p[0]) - 1;
        double ny = 2 * (vy / -vz) / (p[3] - p[2]) + (p[2] + p[3]) / (p[3] - p[2]);
        if (!double.IsFinite(nx) || !double.IsFinite(ny)) return false;
        location = new((float)Math.Clamp(nx, -1, 1), (float)Math.Clamp(ny, -1, 1));
        return true;
    }

    public static byte[] Encode(Vector4 locations, long qpc)
    {
        var bytes = new byte[44];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes, 0x3147564e);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), 1);
        // QPC is also a monotonic sequence, so restarting only this bridge does
        // not reset the provider's sequence to 1 while that provider is alive.
        BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(8), qpc);
        BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(16), qpc);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(24), locations.X);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(28), locations.Y);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(32), locations.Z);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(36), locations.W);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(40), 3);
        return bytes;
    }
}

internal sealed class GazeStream(EyeGeometry[] eyes, Calibration gain)
{
    private long epoch, sourceTime, lastArrival;

    public void ConfirmEpoch(long confirmedEpoch, bool newSubscription = false)
    {
        if (epoch == confirmedEpoch && !newSubscription) return;
        epoch = confirmedEpoch;
        sourceTime = 0;
        lastArrival = 0;
    }

    public byte[]? Accept(ReadOnlySpan<byte> bytes, long arrivalQpc, long frequency)
    {
        if (!EyePacket.TryDecode(bytes, out var frame)) return null;
        if (frame.Epoch != epoch)
        {
            // A new subscription/reboot can change epoch. Do not let a delayed
            // other-epoch datagram replace an actively updating source.
            if (epoch != 0 && arrivalQpc - lastArrival <= frequency * .15) return null;
            epoch = frame.Epoch;
            sourceTime = 0;
        }
        if (frame.SourceTime <= sourceTime) return null;
        sourceTime = frame.SourceTime;
        lastArrival = arrivalQpc;
        return GazeConversion.TryProject(frame, eyes, gain, out var locations)
            ? GazeConversion.Encode(locations, arrivalQpc) : null;
    }
}
