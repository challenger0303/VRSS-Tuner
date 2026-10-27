using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using PicoGaze;

// This independent Bridge gaze subscription never owns the face-tracking
// session. It does not register NVIDIA or launch any VR application.
internal static class BridgeProgram
{
    private static int Main(string[] args) => Run(args);

    internal static int Run(string[] args)
    {
        try
        {
            var options = BridgeOptions.Parse(args);
            var subscription = new GazeSubscription(options.Headset,
                GazeConversion.ReadGeometry(options.Geometry), GazeConversion.ReadCalibration(options.Config), Stopwatch.Frequency);
            using var receiver = new UdpClient(AddressFamily.InterNetwork);
            receiver.ExclusiveAddressUse = true;
            receiver.Client.ReceiveTimeout = 100;
            receiver.Client.Bind(new IPEndPoint(IPAddress.Any, 0));
            using var sender = new UdpClient(AddressFamily.InterNetwork);
            var destination = new IPEndPoint(IPAddress.Loopback, options.Port);
            using var cancellation = new CancellationTokenSource();
            ConsoleCancelEventHandler cancel = (_, e) => { e.Cancel = true; cancellation.Cancel(); };
            Console.CancelKeyPress += cancel;
            long sent = 0, skipped = 0, connectionResets = 0;
            var end = Stopwatch.GetTimestamp() + options.Seconds * Stopwatch.Frequency;
            bool Running() => !cancellation.IsCancellationRequested && Stopwatch.GetTimestamp() < end &&
                !(options.StopFile is not null && File.Exists(options.StopFile));
            Console.WriteLine($"PICO_GAZE_BRIDGE bind={receiver.Client.LocalEndPoint} headset={options.Headset} output={destination} id={subscription.Id} source=combined-direction");
            try
            {
                while (Running())
                {
                    var renewal = subscription.Renew(Stopwatch.GetTimestamp());
                    if (renewal is not null) receiver.Send(renewal, options.Headset);
                    var peer = new IPEndPoint(IPAddress.Any, 0);
                    byte[] data;
                    try
                    {
                        data = receiver.Receive(ref peer);
                    }
                    catch (SocketException error) when (error.SocketErrorCode == SocketError.TimedOut) { continue; }
                    catch (SocketException error) when (error.SocketErrorCode == SocketError.ConnectionReset)
                    {
                        // Windows reports an earlier UDP send's ICMP error on
                        // Receive. Keep renewing, without authorizing any source.
                        connectionResets++;
                        continue;
                    }
                    if (!Running()) break;
                    var packet = subscription.Receive(data, peer, Stopwatch.GetTimestamp());
                    if (packet is null) { skipped++; continue; }
                    sender.Send(packet, destination);
                    sent++;
                }
            }
            finally
            {
                Console.CancelKeyPress -= cancel;
                var stop = subscription.Close();
                if (stop is not null) receiver.Send(stop, options.Headset);
            }
            Console.WriteLine($"PICO_GAZE_BRIDGE_STOP sent={sent} skipped={skipped} connectionResets={connectionResets}");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine($"PICO_GAZE_BRIDGE_ERROR: {error.Message}"); return 2; }
    }
}

internal sealed record BridgeOptions(IPEndPoint Headset, string Geometry, string Config, int Port, int Seconds, string? StopFile)
{
    private const string Usage = "Usage: --headset-ip IPv4 --geometry FILE --config FILE [--headset-port 9030] [--port 5556] [--seconds 1200] [--stop-file FILE]";

    public static BridgeOptions Parse(string[] args)
    {
        if (args.Length == 0 || args.Length % 2 != 0) throw new ArgumentException(Usage);
        var fields = new Dictionary<string, string>(StringComparer.Ordinal);
        for (int index = 0; index < args.Length; index += 2)
        {
            if (args[index] is not ("--headset-ip" or "--headset-port" or "--geometry" or "--config" or "--port" or "--seconds" or "--stop-file"))
                throw new ArgumentException($"Unknown option {args[index]}. {Usage}");
            fields.Add(args[index], args[index + 1]);
        }
        string Required(string key) => fields.TryGetValue(key, out var value) ? value : throw new ArgumentException($"Missing {key}. {Usage}");
        int Number(string key, int fallback) => fields.TryGetValue(key, out var value) ? int.Parse(value, System.Globalization.CultureInfo.InvariantCulture) : fallback;
        if (!IPAddress.TryParse(Required("--headset-ip"), out var address) || address.AddressFamily != AddressFamily.InterNetwork)
            throw new ArgumentException("--headset-ip must be an IPv4 address");
        int headsetPort = Number("--headset-port", 9030), port = Number("--port", 5556), seconds = Number("--seconds", 1200);
        if (headsetPort is < 1 or > 65535 || port is < 1 or > 65535 || seconds is < 1 or > 86400)
            throw new ArgumentException("Invalid port/duration");
        return new(new(address, headsetPort), Required("--geometry"), Required("--config"), port, seconds, fields.GetValueOrDefault("--stop-file"));
    }
}
