using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using PicoGaze;

internal static partial class Tests
{
    private const string SubscriptionId = "0123456789abcdef";
    private static byte[] Ack(long epoch = 42, bool enabled = true, string id = SubscriptionId) =>
        Encoding.ASCII.GetBytes($"PXR_GAZE_ACK id={id} epoch={epoch:x16} enabled={(enabled ? 1 : 0)}");

    private static void TestDirectSubscription()
    {
        var headset = new IPEndPoint(IPAddress.Loopback, 41234);
        GazeSubscription Client() => new(headset, Eyes(), new(), 1000, SubscriptionId);
        var client = Client();
        Check(Encoding.ASCII.GetString(client.Renew(0)!) == $"PXR_GAZE_SUB id={SubscriptionId}", "Independent exact ASCII subscription");
        Check(client.Renew(999) is null, "No renewal before 1000 ms");
        Check(Encoding.ASCII.GetString(client.Renew(1000)!) == $"PXR_GAZE_SUB id={SubscriptionId}", "Idempotent renewal uses the same id");
        Check(client.Receive(Packet(), headset, 1001) is null && client.Epoch == 0, "E cannot authorize its own epoch");

        foreach (var peer in new[] { new IPEndPoint(IPAddress.Parse("127.0.0.2"), headset.Port), new IPEndPoint(headset.Address, headset.Port + 1) })
        {
            client.Receive(Ack(), peer, 1002);
            Check(client.Epoch == 0 && client.Receive(Packet(), headset, 1003) is null, "Wrong IP or port cannot authorize an epoch");
        }
        foreach (var invalid in new[] {
            "PXR_GAZE_ACK id=fedcba9876543210 epoch=000000000000002a enabled=1",
            "PXR_GAZE_ACK id=0123456789ABCDEF epoch=000000000000002a enabled=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=000000000000002A enabled=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=2a enabled=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=0000000000000000 enabled=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=8000000000000000 enabled=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=ffffffffffffffff enabled=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=000000000000002a enabled=2",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=000000000000002a enabled=1 extra=1",
            "PXR_GAZE_ACK id=0123456789abcdef epoch=000000000000002a enabled=1\n",
            "PXR_GAZE_ACK  id=0123456789abcdef epoch=000000000000002a enabled=1",
            "POLO id=0123456789abcdef epoch=000000000000002a enabled=1" })
        {
            client.Receive(Encoding.ASCII.GetBytes(invalid), headset, 1004);
            Check(client.Epoch == 0 && client.Receive(Packet(), headset, 1005) is null, "Malformed, legacy, or wrong-id ACK cannot authorize gaze");
        }
        var nonAscii = Ack(); nonAscii[^1] = 0xb1;
        client.Receive(nonAscii, headset, 1006);
        Check(client.Epoch == 0, "Non-ASCII ACK is rejected");

        client.Receive(Ack(), headset, 1100);
        var output = client.Receive(Packet(), headset, 1101);
        Check(client.Epoch == 42 && output is { Length: 44 }, "Current id and peer ACK authorizes matching E");
        Check(BinaryPrimitives.ReadInt64LittleEndian(output!.AsSpan(8)) == 1101 &&
            BinaryPrimitives.ReadInt64LittleEndian(output.AsSpan(16)) == 1101, "Provider timestamp and sequence use PC QPC, not native time 100");
        client.Receive(Ack(), headset, 1102);
        Check(client.Receive(Packet(), headset, 1103) is null, "Repeated same-epoch ACK does not reset native source");
        Check(client.Receive(Packet(time: 99), headset, 1104) is null, "Backward source is not replayed after ACK");
        Check(client.Receive(Packet(time: 101), new(IPAddress.Parse("127.0.0.2"), headset.Port), 1105) is null &&
            client.Receive(Packet(time: 101), new(headset.Address, headset.Port + 1), 1105) is null &&
            client.Receive(Packet(time: 101), headset, 1106) is not null, "Wrong-peer E neither forwards nor consumes the source");
        Check(client.Receive(Packet(time: 1, epoch: 43), headset, 1107) is null, "Unconfirmed epoch cannot replace current source");
        client.Receive(Ack(epoch: 43), headset, 1110);
        Check(client.Receive(Packet(time: 1, epoch: 43), headset, 1111) is not null, "ACK epoch changes reset source immediately, inside the old 150 ms quiet gate");
        Check(client.Receive(Packet(time: 102), headset, 1112) is null, "Old epoch is rejected after authoritative switch");
        client.Receive(Ack(epoch: 43, enabled: false), headset, 1120);
        Check(client.Receive(Packet(time: 2, epoch: 43), headset, 1121) is null, "Disabled ACK stops gaze output immediately");
        Check(client.Renew(2000) is not null, "Disabled subscriptions continue renewing");
        client.Receive(Ack(epoch: 43), headset, 2130);
        Check(client.Receive(Packet(time: 1, epoch: 43), headset, 2131) is null, "Re-enabling same epoch cannot replay the last native frame");
        Check(client.Receive(Packet(time: 2, epoch: 43), headset, 2132) is not null, "New combined gaze recovers after re-enable");
        Check(client.Receive(Packet(time: 3, epoch: 43, valid: 0), headset, 2133) is null &&
            client.Receive(Packet(time: 3, epoch: 43), headset, 2134) is null, "Invalid combined sample consumes its source identity without replay");
        var faceMask = Packet(time: 4, epoch: 43); faceMask[1] = 3;
        var faceFlags = Packet(time: 4, epoch: 43); faceFlags[2] = 1;
        Check(client.Receive(faceMask, headset, 2135) is null && client.Receive(faceFlags, headset, 2136) is null, "Direct gaze requires mask=1 and flags=0");
        Check(client.Receive(Packet(time: 4, epoch: 43), headset, 2137) is not null, "Rejected headers do not consume a valid source");
        Check(client.Renew(5000) is not null, "Renewal is independent from ACK freshness");
        client.Receive(Ack(epoch: 99, id: "aaaaaaaaaaaaaaaa"), headset, 5100);
        Check(client.Epoch == 43, "Wrong-id ACK cannot change an active epoch");
        Check(client.Receive(Packet(time: 5, epoch: 43), headset, 5129) is not null, "ACK remains valid until the 3000 ms boundary");
        Check(client.Receive(Packet(time: 6, epoch: 43), headset, 5130) is null, "ACK expires at exactly 3000 ms even while SUB and E continue");
        client.Receive(Ack(epoch: 43), headset, 5140);
        Check(client.Receive(Packet(time: 5, epoch: 43), headset, 5141) is null, "Old identity's ACK cannot recover an expired subscription");
        client.Receive(Ack(epoch: 44, id: client.Id), headset, 5142);
        Check(client.Receive(Packet(time: 1, epoch: 44), headset, 5143) is not null, "New identity's ACK recovers output with a confirmed source epoch");
        client.Receive(Ack(epoch: 44, id: client.Id), headset, 8000);
        Check(client.Receive(Packet(time: 1, epoch: 44), headset, 8001) is null &&
            client.Receive(Packet(time: 2, epoch: 44), headset, 10999) is not null, "Duplicate ACK renews only ACK freshness, never gaze freshness");
        Check(client.Receive(Packet(time: 3, epoch: 44), headset, 11000) is null, "Renewed ACK itself still times out");
        client.Receive(Ack(epoch: 45, enabled: false, id: client.Id), headset, 11001);
        Check(client.Epoch == 45 && client.Receive(Packet(time: 1, epoch: 45), headset, 11002) is null, "Disabled authoritative ACK can switch epoch but cannot forward");
        client.Receive(Ack(epoch: 45, id: client.Id), headset, 11003);
        Check(client.Receive(Packet(time: 1, epoch: 45), headset, 11004) is not null, "New epoch is ready when its enabled ACK arrives");
        string closingId = client.Id;
        Check(Encoding.ASCII.GetString(client.Close()!) == $"PXR_GAZE_STOP id={closingId}", "Exit sends only the dedicated gaze STOP for the current identity");
        client.Receive(Ack(epoch: 46, id: closingId), headset, 11005);
        Check(client.Close() is null && client.Renew(12000) is null && client.Epoch == 45 && client.Id == closingId &&
            client.Receive(Packet(time: 2, epoch: 45), headset, 12001) is null, "Close is final and idempotent for ACK, E, renew, and stop");
        var maxEpoch = Client(); maxEpoch.Receive(Ack(epoch: long.MaxValue), headset, 0);
        Check(maxEpoch.Receive(Packet(time: 1, epoch: long.MaxValue), headset, 1) is not null, "Largest positive signed int64 epoch is valid");
        var random1 = new GazeSubscription(headset, Eyes(), new(), 1000);
        var random2 = new GazeSubscription(headset, Eyes(), new(), 1000);
        Check(random1.Id.Length == 16 && random1.Id.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f') &&
            random1.Id != random2.Id, "Each run gets a random 16-digit lowercase hex id");
        TestAckOrdering();
        TestTimeoutIdentityRotation();
    }

    private static void TestAckOrdering()
    {
        var peer = new IPEndPoint(IPAddress.Loopback, 41234);
        var client = new GazeSubscription(peer, Eyes(), new(), 1000, SubscriptionId);
        client.Receive(Ack(epoch: 42), peer, 0);
        Check(client.Receive(Packet(time: 100, epoch: 42), peer, 1) is not null, "Ordering test starts with authorized native gaze");
        client.Receive(Ack(epoch: 43, enabled: false), peer, 10);
        Check(client.Epoch == 43 && client.Receive(Packet(time: 1, epoch: 43), peer, 11) is null,
            "Newer disabled epoch immediately stops output");
        client.Receive(Ack(epoch: 42), peer, 20);
        Check(client.Epoch == 43 && client.Receive(Packet(time: 101, epoch: 42), peer, 21) is null &&
            client.Receive(Packet(time: 1, epoch: 43), peer, 22) is null, "Delayed old enabled ACK cannot roll back or undo newer disabled state");
        client.Receive(Ack(epoch: 43), peer, 30);
        Check(client.Receive(Packet(time: 1, epoch: 43), peer, 31) is not null, "Same-epoch enable keeps its existing protocol behavior");
        client.Receive(Ack(epoch: 42), peer, 40);
        Check(client.Epoch == 43 && client.Receive(Packet(time: 1, epoch: 43), peer, 41) is null,
            "Rejected older ACK cannot reset the native dedup watermark");
        client.Receive(Ack(epoch: 42, enabled: false), peer, 2990);
        Check(client.Receive(Packet(time: 2, epoch: 43), peer, 3029) is not null,
            "Older disabled ACK cannot change the active enabled state");
        Check(client.Receive(Packet(time: 3, epoch: 43), peer, 3030) is null,
            "Rejected older ACK never extends the current ACK's 3000 ms freshness");
        client.Receive(Ack(epoch: 42), peer, 3031);
        Check(client.Epoch == 0 && client.Id != SubscriptionId && client.Receive(Packet(time: 100, epoch: 42), peer, 3032) is null,
            "After ACK timeout the old identity cannot reauthorize or replay previously output gaze");
        client.Receive(Ack(epoch: 42, id: client.Id), peer, 3033);
        Check(client.Epoch == 42 && client.Receive(Packet(time: 1, epoch: 42), peer, 3034) is not null,
            "Lower reboot epoch recovers only after the new identity's ACK");

        var boundary = new GazeSubscription(peer, Eyes(), new(), 1000, SubscriptionId);
        boundary.Receive(Ack(epoch: 43, enabled: false), peer, 10);
        boundary.Receive(Ack(epoch: 42), peer, 3009);
        Check(boundary.Epoch == 43 && boundary.Receive(Packet(time: 1, epoch: 42), peer, 3009) is null,
            "Lower epoch stays rejected immediately before ACK expiration");
        boundary.Receive(Ack(epoch: 42), peer, 3010);
        Check(boundary.Epoch == 0 && boundary.Id != SubscriptionId && boundary.Receive(Packet(time: 1, epoch: 42), peer, 3011) is null,
            "Receive itself rotates identity and rejects the expired identity's ACK at exactly 3000 ms");
        boundary.Receive(Ack(epoch: 42, id: boundary.Id), peer, 3012);
        Check(boundary.Epoch == 42 && boundary.Receive(Packet(time: 1, epoch: 42), peer, 3013) is not null,
            "New identity handshake permits lower epoch after the exact timeout boundary");
    }

    private static void TestTimeoutIdentityRotation()
    {
        var peer = new IPEndPoint(IPAddress.Loopback, 41234);
        var client = new GazeSubscription(peer, Eyes(), new(), 1000, SubscriptionId);
        client.Renew(0);
        client.Receive(Ack(epoch: 42), peer, 0);
        var alreadyOutput = Packet(time: 100, epoch: 42);
        Check(client.Receive(alreadyOutput, peer, 1) is not null, "Replay regression starts with an actually output old E frame");
        client.Receive(Ack(epoch: 43, enabled: false), peer, 10);
        client.Renew(3000);
        client.Receive(Ack(epoch: 42), peer, 3010);
        string recoveryId = client.Id;
        Check(recoveryId != SubscriptionId && recoveryId.Length == 16 && recoveryId.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f') &&
            client.Epoch == 0, "Confirmed disabled session expires into exactly one new random lowercase identity");
        Check(client.Receive(alreadyOutput, peer, 3011) is null, "Delayed old enabled ACK followed by an already output E cannot replay after timeout");
        string recoverySub = $"PXR_GAZE_SUB id={recoveryId}";
        Check(Encoding.ASCII.GetString(client.Renew(3011)!) == recoverySub,
            "New identity SUB is immediate even though the previous SUB was only 11 ms ago");
        Check(client.Renew(3012) is null && client.Renew(4010) is null, "Recovery SUB still uses the 1000 ms renewal interval");
        foreach (bool enabled in new[] { false, true })
        {
            client.Receive(Ack(epoch: 43, enabled: enabled), peer, 4010);
            client.Receive(Ack(epoch: 42, enabled: enabled), peer, 4010);
            Check(client.Id == recoveryId && client.Epoch == 0 && client.Receive(alreadyOutput, peer, 4010) is null,
                "Old disabled and enabled ACKs cannot authorize the new identity");
        }
        Check(Encoding.ASCII.GetString(client.Renew(4011)!) == recoverySub, "Unacknowledged recovery renews the same new identity");
        client.Receive(Ack(epoch: 99), peer, 9000);
        Check(Encoding.ASCII.GetString(client.Renew(10000)!) == recoverySub && client.Id == recoveryId && client.Epoch == 0,
            "Long unacknowledged waits and old ACKs never rotate identity repeatedly");
        client.Receive(Ack(epoch: 41, id: recoveryId), new(peer.Address, peer.Port + 1), 10001);
        Check(client.Epoch == 0 && client.Receive(Packet(time: 1, epoch: 41), peer, 10002) is null,
            "Recovery still requires a new-identity ACK from the configured peer");
        client.Receive(Ack(epoch: 41, id: recoveryId), peer, 10010);
        Check(client.Epoch == 41 && client.Receive(Packet(time: 1, epoch: 41), peer, 10011) is not null,
            "Correct new identity ACK authorizes a lower reboot epoch and new source watermark");
        Check(client.Receive(alreadyOutput, peer, 10012) is null, "Old E epoch stays rejected after the lower-epoch handshake");
        client.Receive(Ack(epoch: 41, id: recoveryId), peer, 10020);
        Check(client.Receive(Packet(time: 1, epoch: 41), peer, 10021) is null,
            "Repeated ACK of the recovery identity does not reset native dedup");
        client.Receive(Ack(epoch: 42, enabled: false), peer, 10022);
        Check(client.Receive(Packet(time: 2, epoch: 41), peer, 10023) is not null,
            "Old disabled ACK cannot disable the confirmed recovery identity");
        client.Renew(13019);
        var secondRecovery = client.Renew(13020);
        string secondId = client.Id;
        Check(secondId != recoveryId && Encoding.ASCII.GetString(secondRecovery!) == $"PXR_GAZE_SUB id={secondId}" && client.Epoch == 0,
            "A later confirmed-session timeout rotates once and sends SUB directly from Renew");
        Check(Encoding.ASCII.GetString(client.Renew(20000)!) == $"PXR_GAZE_SUB id={secondId}" && client.Id == secondId,
            "Second recovery remains stable while no ACK arrives");
        Check(Encoding.ASCII.GetString(client.Close()!) == $"PXR_GAZE_STOP id={secondId}" && client.Renew(30000) is null && client.Id == secondId,
            "Close unsubscribes the current unacknowledged recovery identity without further rotations");

        var initial = new GazeSubscription(peer, Eyes(), new(), 1000, SubscriptionId);
        foreach (long now in new long[] { 0, 1000, 3000, 9000 })
            Check(Encoding.ASCII.GetString(initial.Renew(now)!) == $"PXR_GAZE_SUB id={SubscriptionId}" && initial.Id == SubscriptionId,
                "A never-acknowledged initial subscription does not rotate on elapsed time");

        var sameEpoch = new GazeSubscription(peer, Eyes(), new(), 1000, SubscriptionId);
        sameEpoch.Receive(Ack(), peer, 0);
        sameEpoch.Receive(Packet(time: 100), peer, 1);
        sameEpoch.Renew(3000);
        sameEpoch.Receive(Ack(id: sameEpoch.Id), peer, 3001);
        Check(sameEpoch.Receive(Packet(time: 1), peer, 3002) is not null,
            "First confirmed ACK of a fresh identity owns its source watermark even if a reboot reuses an epoch number");
        sameEpoch.Receive(Ack(id: sameEpoch.Id), peer, 3003);
        Check(sameEpoch.Receive(Packet(time: 1), peer, 3004) is null,
            "Only the first ACK of a fresh identity resets a reused epoch's watermark");
    }

    private static void TestDirectCli()
    {
        var defaults = BridgeOptions.Parse(["--headset-ip", "127.0.0.1", "--geometry", "geometry", "--config", "config"]);
        Check(defaults.Headset.Equals(new IPEndPoint(IPAddress.Loopback, 9030)) && defaults.Port == 5556,
            "CLI accepts loopback IPv4 and defaults headset=9030, provider=5556");
        foreach (string[] invalid in new[] {
            new[] { "--geometry", "geometry", "--config", "config" },
            new[] { "--headset-ip", "::1", "--geometry", "geometry", "--config", "config" },
            new[] { "--headset-ip", "localhost", "--geometry", "geometry", "--config", "config" },
            new[] { "--headset-ip", "127.0.0.1", "--geometry", "geometry", "--config", "config", "--listen-port", "9031" } })
        {
            bool rejected = false;
            try { BridgeOptions.Parse(invalid); } catch (ArgumentException) { rejected = true; }
            Check(rejected, "CLI requires IPv4 and rejects the removed localhost relay option");
        }

        string root = Path.Combine(Environment.GetEnvironmentVariable("PICO_GAZE_TEST_ARTIFACTS") ??
            Path.Combine(".local", "direct-client-validation"), "fixtures-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        string geometry = Path.Combine(root, "geometry.txt"), config = Path.Combine(root, "config.txt"), stop = Path.Combine(root, "stop.flag");
        File.WriteAllText(geometry, "projection0=-1,1,-1,1\nprojection1=-1,1,-1,1\neyeToHead0=1,0,0,0,1,0,0,0,1\neyeToHead1=1,0,0,0,1,0,0,0,1\n");
        File.WriteAllText(config, "x_gain=1\ny_gain=1\nleft_deg=0\nright_deg=0\n");
        using var headset = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        using var provider = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        using var otherPeer = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        headset.Client.ReceiveTimeout = 4000;
        provider.Client.ReceiveTimeout = 250;
        var server = (IPEndPoint)headset.Client.LocalEndPoint!;
        var target = (IPEndPoint)provider.Client.LocalEndPoint!;
        string[] arguments = ["--headset-ip", "127.0.0.1", "--headset-port", server.Port.ToString(),
            "--geometry", geometry, "--config", config, "--port", target.Port.ToString(), "--seconds", "15", "--stop-file", stop];
        var run = Task.Run(() => BridgeProgram.Run(arguments));
        try
        {
            var peer = new IPEndPoint(IPAddress.Any, 0);
            string command = Encoding.ASCII.GetString(headset.Receive(ref peer));
            Check(command.StartsWith("PXR_GAZE_SUB id=", StringComparison.Ordinal) && command.Length == 32,
                "Actual CLI sends only dedicated SUB from its independent socket");
            Check(peer.Address.Equals(IPAddress.Loopback) && peer.Port != 9030 && peer.Port != server.Port,
                "CLI receives on a system-assigned port, never the PC 9030 owner");
            string id = command[16..];
            headset.Send(Packet(), peer);
            Check(NoOutput(provider), "Actual CLI rejects E before ACK");
            headset.Send(Ack(id: "aaaaaaaaaaaaaaaa"), peer);
            headset.Send(Packet(), peer);
            Check(NoOutput(provider), "Actual CLI rejects wrong-id ACK and following E");
            otherPeer.Send(Ack(id: id), peer); otherPeer.Send(Packet(), peer);
            Check(NoOutput(provider), "Actual CLI rejects ACK and E from another UDP port");
            headset.Send(Ack(id: id), peer);
            long before = Stopwatch.GetTimestamp();
            headset.Send(Packet(gaze: new(.2f, 0, -1)), peer);
            var outputPeer = new IPEndPoint(IPAddress.Any, 0);
            var data = provider.Receive(ref outputPeer);
            long after = Stopwatch.GetTimestamp();
            long qpc = BinaryPrimitives.ReadInt64LittleEndian(data.AsSpan(16));
            Check(data.Length == 44 && outputPeer.Address.Equals(IPAddress.Loopback) &&
                BinaryPrimitives.ReadUInt32LittleEndian(data) == 0x3147564e &&
                BinaryPrimitives.ReadInt64LittleEndian(data.AsSpan(8)) == qpc && qpc >= before && qpc <= after &&
                Near(BinaryPrimitives.ReadSingleLittleEndian(data.AsSpan(24)), .2f), "Actual 44-byte provider output uses projected combined gaze and PC QPC");
            headset.Send(Ack(id: id), peer); headset.Send(Packet(), peer);
            headset.Send(Packet(time: 101, valid: 0), peer); headset.Send(Packet(time: 101), peer);
            headset.Send(Packet(time: 1, epoch: 43), peer);
            Check(NoOutput(provider), "Actual CLI never replays duplicate, invalid, or unacknowledged-epoch gaze");
            headset.Send(Ack(epoch: 43, id: id), peer); headset.Send(Packet(time: 1, epoch: 43), peer);
            Check(provider.Receive(ref outputPeer).Length == 44, "Actual CLI immediately accepts authoritative new epoch");
            headset.Send(Ack(epoch: 43, enabled: false, id: id), peer); headset.Send(Packet(time: 2, epoch: 43), peer);
            Check(NoOutput(provider), "Actual CLI stops output while disabled");
            headset.Send(Ack(epoch: 43, id: id), peer); headset.Send(Packet(time: 1, epoch: 43), peer);
            Check(NoOutput(provider), "Actual CLI repeated enabled ACK cannot reset dedup");
            headset.Send(Packet(time: 2, epoch: 43), peer);
            Check(provider.Receive(ref outputPeer).Length == 44, "Actual CLI resumes for a new native frame");
            var renewalPeer = new IPEndPoint(IPAddress.Any, 0);
            Check(Encoding.ASCII.GetString(headset.Receive(ref renewalPeer)) == command && renewalPeer.Equals(peer),
                "Actual CLI renews same subscription from the same ephemeral socket");
            File.WriteAllText(stop, "stop");
            Check(ReceiveStop(headset, peer, id) && run.Wait(4000) && run.Result == 0,
                "Stop-file exit sends exact dedicated STOP and terminates normally");
            Check(NoOutput(provider), "Exit does not replay the last provider packet");
        }
        finally
        {
            File.WriteAllText(stop, "stop");
            if (!run.Wait(4000)) throw new Exception("CLI did not stop after its stop file");
        }

        var timedArguments = arguments[..^2];
        timedArguments[^1] = "1";
        var timedRun = Task.Run(() => BridgeProgram.Run(timedArguments));
        try
        {
            var peer = new IPEndPoint(IPAddress.Any, 0);
            string command = Encoding.ASCII.GetString(headset.Receive(ref peer));
            Check(command.StartsWith("PXR_GAZE_SUB id=", StringComparison.Ordinal) &&
                ReceiveStop(headset, peer, command[16..]) && timedRun.Wait(4000) && timedRun.Result == 0,
                "Normal duration exit also sends only dedicated gaze STOP without requiring an ACK");
        }
        finally
        {
            if (!timedRun.Wait(4000)) throw new Exception("Timed CLI did not terminate");
        }
    }

    private static bool NoOutput(UdpClient receiver)
    {
        var peer = new IPEndPoint(IPAddress.Any, 0);
        try { receiver.Receive(ref peer); return false; }
        catch (SocketException error) when (error.SocketErrorCode == SocketError.TimedOut) { return true; }
    }

    private static bool ReceiveStop(UdpClient headset, IPEndPoint expectedPeer, string id)
    {
        while (true)
        {
            var peer = new IPEndPoint(IPAddress.Any, 0);
            string text = Encoding.ASCII.GetString(headset.Receive(ref peer));
            if (!peer.Equals(expectedPeer)) return false;
            if (text == $"PXR_GAZE_SUB id={id}") continue;
            return text == $"PXR_GAZE_STOP id={id}";
        }
    }
}
