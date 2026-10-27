using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Numerics;
using PicoGaze;

internal static partial class Tests
{
    private static int count;
    private static readonly double[] Identity = [1, 0, 0, 0, 1, 0, 0, 0, 1];
    private static EyeGeometry[] Eyes(double[]? p = null) => [new(p ?? [-1, 1, -1, 1], Identity), new(p ?? [-1, 1, -1, 1], Identity)];
    private static void Check(bool value, string message)
    {
        if (!value) throw new Exception(message);
        count++;
    }
    private static bool Near(float a, float b) => Math.Abs(a - b) < 1e-5;
    private static byte[] Packet(long time = 100, long epoch = 42, uint valid = 2, Vector3? gaze = null)
    {
        var bytes = new byte[83]; bytes[0] = (byte)'E'; bytes[1] = 1;
        BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(3), epoch);
        BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(11), time);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(27), valid);
        var vector = gaze ?? new(0, 0, -1);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(55), vector.X);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(59), vector.Y);
        BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(63), vector.Z);
        return bytes;
    }
    private static EyeFrame Decode(byte[] bytes)
    {
        Check(EyePacket.TryDecode(bytes, out var frame), "E packet must decode");
        return frame;
    }
    private static int Main(string[] args)
    {
        try
        {
            var actual = args.Length == 1 ? File.ReadAllBytes(args[0]) : Packet();
            var frame = Decode(actual);
            Check(actual[2] == 0 && frame.CombinedValid, "Sender's zero morphology flags preserve native combined gaze");
            Check(!frame.LeftValid && !frame.RightValid, "Combined-only is not independent binocular tracking");
            foreach (var bytes in new[] { Packet()[..82], Packet(time: 0), Packet(epoch: 0), Packet(valid: 0x1000) })
                Check(!EyePacket.TryDecode(bytes, out _), "Reject malformed packet/header/status");
            foreach (int at in new[] { 31, 43, 55, 67, 75 })
            {
                var bytes = Packet(); BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(at), float.NaN);
                Check(!EyePacket.TryDecode(bytes, out _), "Reject nonfinite native fields");
            }
            var morphology = Packet(); morphology[2] = 1;
            Check(!EyePacket.TryDecode(morphology, out _), "E carries no morphology validity");
            var both = Packet(); both[1] = 3;
            Check(EyePacket.TryDecode(both, out _), "Eye plus face subscription still carries E");
            Check(GazeConversion.TryProject(frame, Eyes(), new(), out var center) && center == Vector4.Zero, "Center projects to both eyes");
            foreach (var (direction, expected) in new[] {
                (new Vector3(.2f, 0, -1), new Vector4(.2f, 0, .2f, 0)),
                (new Vector3(-.2f, 0, -1), new Vector4(-.2f, 0, -.2f, 0)),
                (new Vector3(0, .2f, -1), new Vector4(0, .2f, 0, .2f)),
                (new Vector3(0, -.2f, -1), new Vector4(0, -.2f, 0, -.2f)) })
            {
                Check(GazeConversion.TryProject(Decode(Packet(gaze: direction)), Eyes(), new(), out var location) &&
                    Vector4.Distance(location, expected) < 1e-5, "Horizontal and vertical directions");
            }
            Check(GazeConversion.TryProject(frame, Eyes([-.8, 1.2, -1, 1]), new(), out var asymmetric) &&
                Near(asymmetric.X, -.2f), "Use the connected HMD's asymmetric projection");
            Check(GazeConversion.TryProject(Decode(Packet(gaze: new(.2f, .2f, -1))), Eyes(), new(2, -1), out var gain) &&
                Near(gain.X, .4f) && Near(gain.Y, -.2f), "Calibration applies once");
            Check(!GazeConversion.TryProject(Decode(Packet(valid: 0)), Eyes(), new(), out _), "Invalid combined gaze is not replayed");
            Check(!GazeConversion.TryProject(Decode(Packet(gaze: new(0, 0, 1))), Eyes(), new(), out _), "Reject a backwards ray");
            var stream = new GazeStream(Eyes(), new());
            var output = stream.Accept(actual, 1000, 1000);
            Check(output is { Length: 44 }, "Emit existing normalized provider packet");
            Check(BinaryPrimitives.ReadUInt32LittleEndian(output!) == 0x3147564e &&
                BinaryPrimitives.ReadUInt32LittleEndian(output!.AsSpan(4)) == 1 &&
                BinaryPrimitives.ReadInt64LittleEndian(output!.AsSpan(8)) == 1000 &&
                BinaryPrimitives.ReadInt64LittleEndian(output!.AsSpan(16)) == 1000 &&
                BinaryPrimitives.ReadUInt32LittleEndian(output!.AsSpan(40)) == 3, "Exact provider wire layout and PC QPC domain");
            Check(stream.Accept(actual, 1010, 1000) is null, "Duplicate source cannot refresh gaze");
            Check(stream.Accept(Packet(time: 99), 1020, 1000) is null, "Backward source cannot refresh gaze");
            Check(stream.Accept(Packet(time: 101, valid: 0), 1030, 1000) is null, "New invalid sample stops output");
            Check(stream.Accept(Packet(time: 101), 1040, 1000) is null, "Invalid sample still consumes source identity");
            Check(stream.Accept(Packet(time: 102), 1050, 1000) is not null, "New valid sample recovers");
            Check(stream.Accept(Packet(time: 1, epoch: 43), 1060, 1000) is null, "Other epoch cannot replace an active source");
            Check(stream.Accept(Packet(time: 1, epoch: 43), 1250, 1000) is not null, "New epoch allowed after a quiet interval");
            using var receiver = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
            receiver.Client.ReceiveTimeout = 1000;
            using var sender = new UdpClient(AddressFamily.InterNetwork);
            sender.Send(output!, (IPEndPoint)receiver.Client.LocalEndPoint!);
            var peer = new IPEndPoint(IPAddress.Any, 0);
            Check(receiver.Receive(ref peer).AsSpan().SequenceEqual(output), "Loopback UDP preserves the 44-byte payload");
            TestDirectSubscription();
            TestDirectCli();
            Console.WriteLine($"PICO_GAZE_TEST_PASS assertions={count} network=ephemeral-loopback-only");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
