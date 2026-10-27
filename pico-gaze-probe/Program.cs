using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using PicoGaze;

// Diagnostic for PicoFacialBridge's native E datagram. This program never sends
// gaze to NVIDIA. BridgeSplit is single-client; live capture is explicit opt-in.
internal static class Program
{
    private static readonly byte[] Discover = Encoding.ASCII.GetBytes("DISCOVER_DAEMON");
    private static readonly byte[] Polo = Encoding.ASCII.GetBytes("POLO");
    private static readonly byte[] Stop = Encoding.ASCII.GetBytes("STOP");
    private static readonly Regex Ack = new(@"^PXR_SUB_ACK id=([0-9a-f]{16}) mask=([0-3]) epoch=([0-9a-f]{16})$",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);
    private static EyeFrame? DecodeEye(ReadOnlySpan<byte> data, long epoch) =>
        EyePacket.TryDecode(data, out var frame) && frame.Epoch == epoch ? frame : null;
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 1 && args[0] == "--self-test")
            {
                var packet = new byte[83]; packet[0] = (byte)'E'; packet[1] = 1;
                BinaryPrimitives.WriteInt64LittleEndian(packet.AsSpan(3), 42);
                BinaryPrimitives.WriteInt64LittleEndian(packet.AsSpan(11), 100);
                BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(19), 0x100);
                BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(23), 0x100);
                BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(27), 2);
                BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(31), BitConverter.SingleToInt32Bits(.25f));
                BinaryPrimitives.WriteSingleLittleEndian(packet.AsSpan(63), -1f);
                if (DecodeEye(packet, 42) is not { Left.X: .25f, CombinedValid: true } || DecodeEye(packet, 43) is not null)
                    throw new Exception("PICO packet parser failed");
                Console.WriteLine("PICO_GAZE_PROBE_SELF_TEST_PASS"); return 0;
            }
            if (args.Length is not (3 or 5) || args[0] != "--headset-ip" ||
                !args.Contains("--exclusive-test", StringComparer.Ordinal) ||
                (args.Length == 5 && (args[2] != "--seconds" || args[4] != "--exclusive-test")) ||
                (args.Length == 3 && args[2] != "--exclusive-test"))
                throw new ArgumentException("Usage: --headset-ip A.B.C.D [--seconds 15] --exclusive-test (requires VRCFT PICO module stopped)");
            if (!IPAddress.TryParse(args[1], out var ip) || ip.AddressFamily != AddressFamily.InterNetwork ||
                IPAddress.IsLoopback(ip) || ip.IsIPv6Multicast)
                throw new ArgumentException("An IPv4 headset address is required");
            var seconds = args.Length == 5 ? int.Parse(args[3]) : 15;
            if (seconds is < 1 or > 60) throw new ArgumentException("Duration must be 1..60 seconds");
            if (Process.GetProcessesByName("VRCFaceTracking").Length != 0 ||
                Process.GetProcessesByName("VRCFaceTracking.ModuleProcess").Length != 0)
                throw new InvalidOperationException("VRCFaceTracking is running; the PICO bridge has one client and this probe would interrupt face tracking");
            using var udp = new UdpClient(new IPEndPoint(IPAddress.Any, 9030));
            udp.Client.ReceiveTimeout = 250;
            var peer = new IPEndPoint(ip, 9030);
            var id = Convert.ToHexString(Guid.NewGuid().ToByteArray().AsSpan(0, 8)).ToLowerInvariant();
            var subscription = Encoding.ASCII.GetBytes($"PXR_SUB id={id} mask=1");
            long epoch = 0, lastTime = 0;
            var end = DateTime.UtcNow.AddSeconds(seconds);
            var nextDiscover = DateTime.MinValue;
            var nextSubscribe = DateTime.MinValue;
            var announced = false;
            var locked = false;
            Console.WriteLine("PICO_BRIDGE_PROBE start; the PICO VRCFT module must not occupy UDP 9030");
            try
            {
                while (DateTime.UtcNow < end)
                {
                    var now = DateTime.UtcNow;
                    if (now >= nextDiscover && !locked) { udp.Send(Discover, peer); nextDiscover = now.AddSeconds(2); }
                    if (announced && epoch == 0 && now >= nextSubscribe)
                    { udp.Send(subscription, peer); nextSubscribe = now.AddSeconds(1); }
                    try
                    {
                        var remote = new IPEndPoint(IPAddress.Any, 0);
                        var data = udp.Receive(ref remote);
                        if (!remote.Address.Equals(ip) || remote.Port != 9030) continue;
                        locked = true;
                        if (data.AsSpan().SequenceEqual("MARCO\0"u8)) { udp.Send(Polo, peer); continue; }
                        if (data.Length >= 10 && data.AsSpan().StartsWith("PXR_MODE "u8))
                        { announced = true; nextSubscribe = DateTime.MinValue; continue; }
                        if (data.AsSpan().StartsWith("PXR_SUB_ACK "u8))
                        {
                            var match = Ack.Match(Encoding.ASCII.GetString(data));
                            if (match.Success && match.Groups[1].Value == id &&
                                int.Parse(match.Groups[2].Value) == 1 &&
                                long.TryParse(match.Groups[3].Value, System.Globalization.NumberStyles.HexNumber,
                                    System.Globalization.CultureInfo.InvariantCulture, out var parsed) && parsed > 0)
                            { epoch = parsed; lastTime = 0; Console.WriteLine("PICO_BRIDGE_PROBE subscribed"); }
                            continue;
                        }
                        if (epoch == 0) continue;
                        var eye = DecodeEye(data, epoch);
                        if (eye is not { } e || e.SourceTime <= lastTime) continue;
                        lastTime = e.SourceTime;
                        Console.WriteLine($"eye leftValid={e.LeftValid} rightValid={e.RightValid} combinedValid={e.CombinedValid} " +
                            $"L={e.Left} R={e.Right} combined={e.Combined}");
                    }
                    catch (SocketException ex) when (ex.SocketErrorCode == SocketError.TimedOut) { }
                }
            }
            finally { if (locked) udp.Send(Stop, peer); }
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine($"PICO_GAZE_PROBE_ERROR: {error.Message}"); return 2; }
    }
}
