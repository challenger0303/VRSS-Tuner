using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;

// VIVE Streaming SDK -> the existing normalized stereo UDP input. No vendor DLL is redistributed.
// Based on the public callback/data contract in HTC's ViveStreamingFaceTrackingModule.
internal static class Program
{
    private const uint Magic = 0x3147564e;
    private static readonly ConcurrentQueue<string> EyeFrames = new();
    private static volatile bool connected;
    private static volatile bool running = true;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void StatusCallback([MarshalAs(UnmanagedType.LPWStr)] string status,
        [MarshalAs(UnmanagedType.LPWStr)] string value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void TextCallback([MarshalAs(UnmanagedType.LPWStr)] string value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void SetCallbacks(StatusCallback status, TextCallback setting, TextCallback log);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Init();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Release();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate bool StartFaceTracking();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate bool StopFaceTracking();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ProEyeInit(int eyeType, IntPtr config);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetEyeDataV2(IntPtr data);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate bool IsViveProEye();
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string path, IntPtr reserved, uint flags);
    [DllImport("kernel32", SetLastError = true)] private static extern bool FreeLibrary(IntPtr module);

    private static void OnStatus(string status, string value)
    {
        if (status is "5001" or "2498")
        {
            if (connected && value.Length is > 0 and < 4096) EyeFrames.Enqueue(value);
        }
        else if (status == "2105") connected = value == "0";
    }
    private static void Ignore(string _) { }

    private static T Export<T>(IntPtr module, string name) where T : Delegate =>
        Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(module, name));

    private static Dictionary<string, string> Config(string path) => File.ReadAllLines(path)
        .Where(line => line.Contains('=') && !line.TrimStart().StartsWith('#'))
        .Select(line => line.Split('=', 2))
        .ToDictionary(parts => parts[0].Trim(), parts => parts[1].Trim(), StringComparer.Ordinal);

    private static double[] Values(Dictionary<string, string> fields, string key, int count)
    {
        var parts = fields[key].Split(',');
        if (parts.Length != count) throw new FormatException($"{key} length must be {count}");
        var numbers = parts.Select(part => double.Parse(part, CultureInfo.InvariantCulture)).ToArray();
        if (numbers.Any(number => !double.IsFinite(number))) throw new FormatException($"{key} is nonfinite");
        return numbers;
    }

    private sealed record EyeGeometry(double[] Projection, double[] Rotation);
    private sealed record Calibration(double X, double Y, double LeftDeg, double RightDeg);
    private static Calibration ReadCalibration(string path)
    {
        var fields = Config(path);
        double N(string key) => double.Parse(fields[key], CultureInfo.InvariantCulture);
        var result = new Calibration(N("x_gain"), N("y_gain"), N("left_deg"), N("right_deg"));
        if (!double.IsFinite(result.X) || !double.IsFinite(result.Y) ||
            !double.IsFinite(result.LeftDeg) || !double.IsFinite(result.RightDeg) ||
            result.X is < .1 or > 2 || Math.Abs(result.Y) is < .1 or > 2 ||
            Math.Abs(result.LeftDeg) > 5 || Math.Abs(result.RightDeg) > 5)
            throw new FormatException("Invalid calibration");
        return result;
    }
    private static EyeGeometry[] ReadGeometry(string path)
    {
        var fields = Config(path);
        var result = Enumerable.Range(0, 2).Select(i =>
            new EyeGeometry(Values(fields, $"projection{i}", 4), Values(fields, $"eyeToHead{i}", 9))).ToArray();
        foreach (var eye in result)
        {
            var p = eye.Projection;
            if (!(p[0] < 0 && p[1] > 0 && p[2] < 0 && p[3] > 0))
                throw new FormatException("Invalid OpenVR projection");
        }
        return result;
    }

    private static float[] Project(double x, double y, double z, EyeGeometry eye, Calibration gain, int index, bool positiveZ)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y) || !double.IsFinite(z)) throw new FormatException("Nonfinite gaze");
        var norm = Math.Sqrt(x * x + y * y + z * z);
        if (norm < .8 || norm > 1.2 || (positiveZ ? z < .1 : z > -.1))
            throw new FormatException("Invalid gaze direction");
        var forward = positiveZ ? z : -z;
        var angle = (index == 0 ? gain.LeftDeg : gain.RightDeg) * Math.PI / 180;
        var h = new[] { Math.Tan(Math.Atan(x / forward * gain.X) + angle), y / forward * gain.Y, -1.0 };
        var r = eye.Rotation;
        var local = new double[3];
        for (var i = 0; i < 3; ++i) for (var j = 0; j < 3; ++j) local[i] += r[j * 3 + i] * h[j];
        if (local[2] >= -1e-6) throw new FormatException("Ray points behind eye");
        var p = eye.Projection;
        var sx = 2 * (local[0] / -local[2] - p[0]) / (p[1] - p[0]) - 1;
        var sy = 2 * (local[1] / -local[2]) / (p[3] - p[2]) + (p[2] + p[3]) / (p[3] - p[2]);
        if (!double.IsFinite(sx) || !double.IsFinite(sy)) throw new FormatException("Invalid projected gaze");
        return [(float)Math.Clamp(sx, -1, 1), (float)Math.Clamp(sy, -1, 1)];
    }

    private static float[] ParseFrame(string raw, EyeGeometry[] eyes, Calibration gain, bool positiveZ)
    {
        var values = raw.Split(',');
        if (values.Length is not (21 or 23 or 37)) throw new FormatException("Unknown VIVE eye frame length");
        double D(int index) => double.Parse(values[index], CultureInfo.InvariantCulture);
        var left = Project(D(9), D(10), D(11), eyes[0], gain, 0, positiveZ);
        var right = Project(D(15), D(16), D(17), eyes[1], gain, 1, positiveZ);
        return [left[0], left[1], right[0], right[1]];
    }

    private static byte[] Packet(ulong sequence, ulong qpc, float[] xy)
    {
        var bytes = new byte[44];
        using var stream = new MemoryStream(bytes);
        using var writer = new BinaryWriter(stream);
        writer.Write(Magic); writer.Write(1u); writer.Write(sequence); writer.Write(qpc);
        foreach (var n in xy) writer.Write(n);
        writer.Write(3u);
        return bytes;
    }

    private static string Required(Dictionary<string, string> args, string key) =>
        args.TryGetValue(key, out var value) ? value : throw new ArgumentException($"Missing {key}");

    // SRanipal EyeData_v2 starts with no_user, frame_sequence, timestamp and
    // the left/right SingleEyeData blocks. Offsets are from HTC's public SDK
    // structures; only the first two blocks are read. No camera access occurs.
    private static float[] ParseProEye(IntPtr data, EyeGeometry[] eyes, Calibration gain, bool positiveZ)
    {
        if (Marshal.ReadByte(data) != 0) throw new FormatException("HMD not worn");
        const int verboseOffset = 16, eyeSize = 48, gazeMask = 1 << 1, directionOffset = 20;
        (double x, double y, double z)? ReadEye(int index)
        {
            var eye = IntPtr.Add(data, verboseOffset + index * eyeSize);
            if ((Marshal.ReadInt64(eye) & gazeMask) == 0) return null;
            var direction = IntPtr.Add(eye, directionOffset);
            var x = BitConverter.Int32BitsToSingle(Marshal.ReadInt32(direction, 0));
            var y = BitConverter.Int32BitsToSingle(Marshal.ReadInt32(direction, 4));
            var z = BitConverter.Int32BitsToSingle(Marshal.ReadInt32(direction, 8));
            return (x, y, z);
        }
        var leftRay = ReadEye(0);
        var rightRay = ReadEye(1);
        if (leftRay is null && rightRay is null) throw new FormatException("Both eyes invalid");
        var l = leftRay ?? rightRay!.Value;
        var r = rightRay ?? leftRay!.Value;
        var left = Project(l.x, l.y, l.z, eyes[0], gain, 0, positiveZ);
        var right = Project(r.x, r.y, r.z, eyes[1], gain, 1, positiveZ);
        return [left[0], left[1], right[0], right[1]];
    }

    private static ulong RunProEye(IntPtr module, EyeGeometry[] eyes, Calibration gain,
        bool positiveZ, int port, int seconds, string? config, string? stopFile)
    {
        var isProEye = Export<IsViveProEye>(module, "IsViveProEye");
        var initial = Export<ProEyeInit>(module, "Initial");
        var get = Export<GetEyeDataV2>(module, "GetEyeData_v2");
        if (initial(2, IntPtr.Zero) != 0) throw new InvalidOperationException("SRanipal eye initialization failed");
        if (!isProEye()) throw new InvalidOperationException("Connected HMD is not VIVE Pro Eye");
        using var udp = new UdpClient(AddressFamily.InterNetwork);
        var target = new IPEndPoint(IPAddress.Loopback, port);
        var data = Marshal.AllocHGlobal(4096); // Larger than HTC EyeData_v2; never read beyond known fields.
        try
        {
            var end = Stopwatch.GetTimestamp() + seconds * Stopwatch.Frequency;
            ulong sequence = 0;
            var lastFrame = int.MinValue;
            var nextConfig = 0L;
            while (running && Stopwatch.GetTimestamp() < end)
            {
                if (stopFile is not null && File.Exists(stopFile)) break;
                if (config is not null && Stopwatch.GetTimestamp() >= nextConfig)
                {
                    try { gain = ReadCalibration(config); } catch (Exception) { /* Keep last valid setting. */ }
                    nextConfig = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 4;
                }
                if (get(data) == 0)
                {
                    var frame = Marshal.ReadInt32(data, 4);
                    if (frame != lastFrame)
                    {
                        lastFrame = frame;
                        try
                        {
                            var xy = ParseProEye(data, eyes, gain, positiveZ);
                            var packet = Packet(++sequence, (ulong)Stopwatch.GetTimestamp(), xy);
                            udp.Send(packet, packet.Length, target);
                        }
                        catch (FormatException) { /* Invalid gaze must not reach the provider. */ }
                    }
                }
                Thread.Sleep(5);
            }
            return sequence;
        }
        finally { Marshal.FreeHGlobal(data); }
    }

    private static int Main(string[] argv)
    {
        try
        {
            var args = new Dictionary<string, string>(StringComparer.Ordinal);
            for (var i = 0; i < argv.Length; i += 2)
            {
                if (i + 1 >= argv.Length || !argv[i].StartsWith("--", StringComparison.Ordinal))
                    throw new ArgumentException("Arguments must be --name value pairs");
                args.Add(argv[i], argv[i + 1]);
            }
            var eyes = ReadGeometry(Required(args, "--geometry"));
            var xGain = double.Parse(args.GetValueOrDefault("--x-gain", "1"), CultureInfo.InvariantCulture);
            var yGain = double.Parse(args.GetValueOrDefault("--y-gain", "1"), CultureInfo.InvariantCulture);
            if (!double.IsFinite(xGain) || !double.IsFinite(yGain) || Math.Abs(xGain) is < .1 or > 2 || Math.Abs(yGain) is < .1 or > 2)
                throw new ArgumentException("Gains must be finite and between .1 and 2 in magnitude");
            var source = args.GetValueOrDefault("--source", "vive-streaming");
            if (source is not ("vive-streaming" or "pro-eye")) throw new ArgumentException("Invalid --source");
            var positiveZ = args.GetValueOrDefault("--forward-z", source == "pro-eye" ? "negative" : "positive") switch
            {
                "positive" => true, "negative" => false, _ => throw new ArgumentException("Invalid --forward-z")
            };
            var config = args.GetValueOrDefault("--config");
            var gain = config is null ? new Calibration(xGain, yGain, 0, 0) : ReadCalibration(config);
            var stopFile = args.GetValueOrDefault("--stop-file");
            if (args.ContainsKey("--self-test"))
            {
                var fields = Enumerable.Repeat("0", 21).ToArray();
                fields[11] = fields[17] = positiveZ ? "1" : "-1";
                var center = ParseFrame(string.Join(',', fields), eyes, gain, positiveZ);
                var packet = Packet(1, 1, center);
                if (packet.Length != 44 || BitConverter.ToUInt32(packet) != Magic || center.Any(n => !float.IsFinite(n)))
                    throw new Exception("Self-test failed");
                fields[9] = fields[15] = ".2";
                var right = ParseFrame(string.Join(',', fields), eyes, gain, positiveZ);
                if (right[0] <= center[0] || right[2] <= center[2]) throw new Exception("X mapping failed");
                fields[9] = fields[15] = "0"; fields[10] = fields[16] = ".2";
                var up = ParseFrame(string.Join(',', fields), eyes, gain, positiveZ);
                if (up[1] <= center[1] || up[3] <= center[3]) throw new Exception("Y mapping failed");
                var fake = Marshal.AllocHGlobal(4096);
                try
                {
                    for (var offset = 0; offset < 256; offset += 4) Marshal.WriteInt32(fake, offset, 0);
                    for (var eye = 0; eye < 2; ++eye)
                    {
                        var baseOffset = 16 + eye * 48;
                        Marshal.WriteInt64(fake, baseOffset, 2);
                        Marshal.WriteInt32(fake, baseOffset + 20, BitConverter.SingleToInt32Bits(.2f));
                        Marshal.WriteInt32(fake, baseOffset + 28,
                            BitConverter.SingleToInt32Bits(positiveZ ? 1f : -1f));
                    }
                    var proEye = ParseProEye(fake, eyes, gain, positiveZ);
                    if (proEye[0] <= center[0] || proEye[2] <= center[2])
                        throw new Exception("Pro Eye mapping failed");
                    Marshal.WriteInt64(fake, 16, 0);
                    var oneEye = ParseProEye(fake, eyes, gain, positiveZ);
                    if (oneEye[0] <= center[0] || oneEye[2] <= center[2])
                        throw new Exception("Pro Eye monocular fallback failed");
                    Marshal.WriteInt64(fake, 64, 0);
                    try { ParseProEye(fake, eyes, gain, positiveZ); throw new Exception("Invalid eye accepted"); }
                    catch (FormatException) { }
                }
                finally { Marshal.FreeHGlobal(fake); }
                Console.WriteLine(source == "pro-eye" ? "PRO_EYE_BRIDGE_SELF_TEST_PASS" : "VIVE_STREAMING_BRIDGE_SELF_TEST_PASS");
                return 0;
            }
            var sdk = Path.GetFullPath(Required(args, "--sdk"));
            var expectedDll = source == "pro-eye" ? "SRanipal.dll" : "VS_PC_SDK.dll";
            if (!Path.GetFileName(sdk).Equals(expectedDll, StringComparison.OrdinalIgnoreCase) || !File.Exists(sdk))
                throw new ArgumentException($"--sdk must point to an installed {expectedDll}");
            var port = int.Parse(args.GetValueOrDefault("--port", "5556"), CultureInfo.InvariantCulture);
            if (port is < 1 or > 65535) throw new ArgumentException("Invalid port");
            var seconds = int.Parse(args.GetValueOrDefault("--seconds", "3600"), CultureInfo.InvariantCulture);
            if (seconds is < 1 or > 86400) throw new ArgumentException("Invalid duration");
            var module = LoadLibraryExW(sdk, IntPtr.Zero, 0x8);
            if (module == IntPtr.Zero) throw new InvalidOperationException($"VIVE SDK load failed: {Marshal.GetLastWin32Error()}");
            try
            {
                Console.CancelKeyPress += (_, e) => { e.Cancel = true; running = false; };
                if (source == "pro-eye")
                {
                    Console.WriteLine($"PRO_EYE_BRIDGE_STOP sent={RunProEye(module, eyes, gain, positiveZ, port, seconds, config, stopFile)}");
                    return 0;
                }
                var setCallbacks = Export<SetCallbacks>(module, "VS_SetCallbackFunction");
                var init = Export<Init>(module, "VS_Init");
                var release = Export<Release>(module, "VS_Release");
                var startTracking = Export<StartFaceTracking>(module, "VS_StartFaceTracking");
                var stopTracking = Export<StopFaceTracking>(module, "VS_StopFaceTracking");
                StatusCallback status = OnStatus;
                TextCallback ignore = Ignore;
                setCallbacks(status, ignore, ignore);
                if (init() != 0) throw new InvalidOperationException("VIVE SDK init failed");
                try
                {
                    using var udp = new UdpClient(AddressFamily.InterNetwork);
                    var target = new IPEndPoint(IPAddress.Loopback, port);
                    var end = Stopwatch.GetTimestamp() + seconds * Stopwatch.Frequency;
                    ulong sequence = 0;
                    var trackingStarted = false;
                    var nextStart = 0L;
                    var nextConfig = 0L;
                    while (running && Stopwatch.GetTimestamp() < end)
                    {
                        if (stopFile is not null && File.Exists(stopFile)) break;
                        if (config is not null && Stopwatch.GetTimestamp() >= nextConfig)
                        {
                            try { gain = ReadCalibration(config); } catch (Exception) { /* Keep last valid setting. */ }
                            nextConfig = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 4;
                        }
                        if (connected && !trackingStarted && Stopwatch.GetTimestamp() >= nextStart)
                        {
                            trackingStarted = startTracking();
                            nextStart = Stopwatch.GetTimestamp() + Stopwatch.Frequency;
                        }
                        if (!connected && trackingStarted) { stopTracking(); trackingStarted = false; }
                        while (EyeFrames.TryDequeue(out var raw))
                        {
                            if (!connected || !trackingStarted) continue;
                            try
                            {
                                var xy = ParseFrame(raw, eyes, gain, positiveZ);
                                var bytes = Packet(++sequence, (ulong)Stopwatch.GetTimestamp(), xy);
                                udp.Send(bytes, bytes.Length, target);
                            }
                            catch (FormatException) { /* Invalid gaze must not reach the provider. */ }
                        }
                        Thread.Sleep(2);
                    }
                    if (trackingStarted) stopTracking();
                    Console.WriteLine($"VIVE_BRIDGE_STOP sent={sequence}");
                }
                finally { release(); GC.KeepAlive(status); GC.KeepAlive(ignore); }
            }
            finally { FreeLibrary(module); }
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"VIVE_BRIDGE_ERROR: {error.Message}");
            return 2;
        }
    }
}
