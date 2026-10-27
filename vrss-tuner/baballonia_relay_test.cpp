#include "BaballoniaRelay.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#pragma comment(lib, "ws2_32.lib")

static std::vector<char> Osc(const char* address, float value) {
    std::vector<char> bytes(address, address + std::strlen(address));
    bytes.push_back(0);
    while (bytes.size() % 4) bytes.push_back(0);
    bytes.insert(bytes.end(), {',', 'f', 0, 0});
    uint32_t raw{}; std::memcpy(&raw, &value, 4);
    bytes.push_back(char(raw >> 24)); bytes.push_back(char(raw >> 16));
    bytes.push_back(char(raw >> 8)); bytes.push_back(char(raw));
    return bytes;
}
int main() {
    auto probe = Osc("/LeftEyeX", .5f);
    auto parsed = babble::ParseOsc(probe.data(), probe.size());
    assert(parsed.eyeAxis == 0 && parsed.value == .5f);
    assert(babble::ParseOsc(probe.data(), 3).eyeAxis == -1);
    auto prefixed = Osc("/avatar/parameters/RightEyeY", -.25f);
    assert(babble::ParseOsc(prefixed.data(), prefixed.size()).eyeAxis == 3);
    std::vector<char> bundle{'#','b','u','n','d','l','e',0, 0,0,0,0,0,0,0,1};
    for (auto frame : {probe, prefixed}) {
        const auto size = uint32_t(frame.size());
        bundle.insert(bundle.end(), {char(size >> 24), char(size >> 16), char(size >> 8), char(size)});
        bundle.insert(bundle.end(), frame.begin(), frame.end());
    }
    const auto bundled = babble::ParsePacket(bundle.data(), bundle.size());
    assert(bundled.count == 2 && bundled.items[0].eyeAxis == 0 && bundled.items[1].eyeAxis == 3);
    auto custom=Osc("/custom/eyes/LeftEyeX",.25f);
    assert(babble::ParsePacket(custom.data(),custom.size()).items[0].eyeAxis==0);
    auto nested=bundle;
    const auto nestedSize=uint32_t(bundle.size());
    nested.resize(16);
    nested.insert(nested.end(),{char(nestedSize>>24),char(nestedSize>>16),char(nestedSize>>8),char(nestedSize)});
    nested.insert(nested.end(),bundle.begin(),bundle.end());
    assert(babble::ParsePacket(nested.data(),nested.size()).count==2);
    WSADATA data{}; assert(WSAStartup(MAKEWORD(2,2), &data) == 0);
    SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in endpoint{}; endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(39021); endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(receiver, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == 0);
    DWORD timeout = 250;
    setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    SOCKET forwarded = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    endpoint.sin_port = htons(39022);
    assert(bind(forwarded, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == 0);
    setsockopt(forwarded, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::atomic<bool> allow{false};
    babble::Relay relay;
    assert(relay.Start(39020, .5f, -.75f, 0, 0, [&] { return allow.load(); }, 39021, 39022));
    babble::Relay conflict;
    assert(!conflict.Start(39020,1,1,0,0,[]{return true;},39021));
    assert(conflict.SocketError()!=0 && relay.Running());
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    endpoint.sin_port = htons(39020);
    auto sendSet = [&] {
        for (auto [name, value] : {
            std::pair{"/LeftEyeX", .8f}, {"/LeftEyeY", .4f},
            {"/RightEyeX", -.6f}, {"/RightEyeY", -.2f}}) {
            auto bytes = Osc(name, value);
            assert(sendto(sender, bytes.data(), int(bytes.size()), 0,
                reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == int(bytes.size()));
        }
    };
    sendSet(); Sleep(40);
    assert(relay.Received() >= 4 && relay.Sent() == 0);
    babble::UdpGazeV1 captured{};double age=-1;
    assert(relay.Read(captured,age) && captured.sequence && captured.qpc && relay.Datagrams()>=4);
    assert(std::abs(captured.leftX-.4f)<.001f);
    Sleep(180);
    assert(!relay.Read(captured,age));
    char forwardedBytes[128]{};
    const int forwardSize = recv(forwarded, forwardedBytes, sizeof(forwardedBytes), 0);
    assert(forwardSize > 0 && babble::ParseOsc(forwardedBytes, forwardSize).eyeAxis >= 0);
    allow = true; Sleep(520); sendSet();
    babble::UdpGazeV1 packet{};
    assert(recv(receiver, reinterpret_cast<char*>(&packet), sizeof(packet), 0) == sizeof(packet));
    assert(packet.magic == 0x3147564e && packet.version == 1 && packet.validMask == 3);
    assert(std::abs(packet.leftX - .4f) < .001f && std::abs(packet.leftY + .3f) < .001f);
    assert(std::abs(packet.rightX + .3f) < .001f && std::abs(packet.rightY - .15f) < .001f);
    assert(packet.qpc > 0 && packet.sequence > 0);
    relay.Stop(); closesocket(sender); closesocket(receiver); closesocket(forwarded); WSACleanup();
    std::cout << "BABALLONIA_RELAY_PASS parser nested_bundle custom_prefix port_conflict input_snapshot stale_rejected osc_forward stereo_udp gain inversion\n";
}
