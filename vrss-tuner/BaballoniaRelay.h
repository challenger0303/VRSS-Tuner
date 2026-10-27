#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

// Baballonia OSC is received on localhost and forwarded only to the opt-in
// common provider's normalized UDP input. No game-process access is involved.
namespace babble {
struct Reading { int eyeAxis{-1}; float value{}; };
inline Reading ParseOsc(const char* bytes, size_t length) {
    if (!bytes || length < 12 || length > 4096) return {};
    size_t addressEnd = 0;
    while (addressEnd < length && bytes[addressEnd]) ++addressEnd;
    if (addressEnd == length) return {};
    const size_t tagsAt = (addressEnd + 4) & ~size_t(3);
    if (tagsAt + 4 > length || bytes[tagsAt] != ',' || bytes[tagsAt + 1] != 'f' || bytes[tagsAt + 2]) return {};
    const size_t valueAt = (tagsAt + 4) & ~size_t(3);
    if (valueAt + 4 > length) return {};
    const char* address = bytes;
    constexpr const char* prefix = "/avatar/parameters/";
    if (std::strncmp(address, prefix, std::strlen(prefix)) == 0) address += std::strlen(prefix);
    // Baballonia allows a custom OSC prefix. Match the final path segment.
    if (const char* slash = std::strrchr(address, '/')) address = slash + 1;
    int axis = -1;
    if (std::strcmp(address, "LeftEyeX") == 0) axis = 0;
    else if (std::strcmp(address, "LeftEyeY") == 0) axis = 1;
    else if (std::strcmp(address, "RightEyeX") == 0) axis = 2;
    else if (std::strcmp(address, "RightEyeY") == 0) axis = 3;
    uint32_t bits = (uint32_t(uint8_t(bytes[valueAt])) << 24) |
        (uint32_t(uint8_t(bytes[valueAt + 1])) << 16) |
        (uint32_t(uint8_t(bytes[valueAt + 2])) << 8) | uint8_t(bytes[valueAt + 3]);
    float value{}; std::memcpy(&value, &bits, sizeof(value));
    if (axis < 0 || !std::isfinite(value) || std::abs(value) > 1.0f) return {};
    return {axis, value};
}
struct PacketReadings { std::array<Reading, 64> items{}; size_t count{}; };
inline void AppendPacket(const char* bytes, size_t length, PacketReadings& result, unsigned depth=0) {
    if (!bytes || length < 12 || length > 65507 || depth > 4) return;
    if (length >= 16 && std::memcmp(bytes, "#bundle\0", 8) == 0) {
        size_t at = 16; // OSC bundle header and timetag.
        while (at + 4 <= length && result.count < result.items.size()) {
            const size_t size = (size_t(uint8_t(bytes[at])) << 24) |
                (size_t(uint8_t(bytes[at + 1])) << 16) |
                (size_t(uint8_t(bytes[at + 2])) << 8) | uint8_t(bytes[at + 3]);
            at += 4;
            if (!size || size % 4 || size > length - at) break;
            AppendPacket(bytes + at, size, result, depth+1);
            at += size;
        }
    } else {
        auto reading = ParseOsc(bytes, length);
        if (reading.eyeAxis >= 0 && result.count < result.items.size()) result.items[result.count++] = reading;
    }
}
inline PacketReadings ParsePacket(const char* bytes, size_t length) {
    PacketReadings result{};
    AppendPacket(bytes,length,result);
    return result;
}
#pragma pack(push, 1)
struct UdpGazeV1 {
    uint32_t magic{0x3147564e}, version{1};
    uint64_t sequence{}, qpc{};
    float leftX{}, leftY{}, rightX{}, rightY{};
    uint32_t validMask{3};
};
#pragma pack(pop)
static_assert(sizeof(UdpGazeV1) == 44);

class Relay {
    SOCKET input_{INVALID_SOCKET}, output_{INVALID_SOCKET};
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> received_{0}, sent_{0};
    std::atomic<uint64_t> datagrams_{0};
    std::mutex packetMutex_;
    UdpGazeV1 latest_{};
    std::atomic<int> socketError_{0};
    bool winsockReady_{};
    std::function<bool()> outputAllowed_;
    std::atomic<float> xGain_{1}, yGain_{1}, leftOffset_{0}, rightOffset_{0};
    int outputPort_{5556};
    std::atomic<int> forwardPort_{0};
    void Loop() {
        struct Slot { float value{}; uint64_t tick{}; };
        std::array<Slot, 4> slots{};
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(static_cast<u_short>(outputPort_));
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sockaddr_in forward = destination;
        uint64_t sequence = 0, checkedAt = 0;
        bool allowed = false;
        while (running_) {
            char bytes[65507];
            sockaddr_in sender{}; int senderSize = sizeof(sender);
            const int size = recvfrom(input_, bytes, sizeof(bytes), 0,
                reinterpret_cast<sockaddr*>(&sender), &senderSize);
            if (size <= 0 || sender.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;
            ++datagrams_;
            forward.sin_port = htons(static_cast<u_short>(forwardPort_.load()));
            if (forwardPort_.load())
                sendto(output_, bytes, size, 0, reinterpret_cast<const sockaddr*>(&forward), sizeof(forward));
            const auto readings = ParsePacket(bytes, size_t(size));
            for (size_t i = 0; i < readings.count; ++i) {
                const auto reading = readings.items[i];
                ++received_;
                const uint64_t now = GetTickCount64();
                slots[reading.eyeAxis] = {reading.value, now};
                bool complete = true;
                for (const auto& slot : slots) if (!slot.tick || now - slot.tick > 150) complete = false;
                if (!complete) continue;
                if (now - checkedAt >= 100) { allowed = outputAllowed_ && outputAllowed_(); checkedAt = now; }
                LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
                auto clamp = [](float v) { return std::fmax(-1.0f, std::fmin(1.0f, v)); };
                UdpGazeV1 packet{};
                packet.sequence = ++sequence;
                packet.qpc = uint64_t(qpc.QuadPart);
                packet.leftX = clamp(slots[0].value * xGain_.load() + leftOffset_.load() / 45.0f);
                packet.leftY = clamp(slots[1].value * yGain_.load());
                packet.rightX = clamp(slots[2].value * xGain_.load() + rightOffset_.load() / 45.0f);
                packet.rightY = clamp(slots[3].value * yGain_.load());
                { std::lock_guard<std::mutex> lock(packetMutex_); latest_ = packet; }
                if (!allowed) continue;
                if (sendto(output_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                    reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) == sizeof(packet)) ++sent_;
            }
        }
    }
public:
    ~Relay() { Stop(); }
    bool Start(int port, float xGain, float yGain, float leftOffset, float rightOffset,
               std::function<bool()> outputAllowed, int outputPort = 5556, int forwardPort = 0) {
        if (running_ || port < 1 || port > 65535 || !std::isfinite(xGain) || !std::isfinite(yGain) ||
            !std::isfinite(leftOffset) || !std::isfinite(rightOffset) || outputPort < 1 || outputPort > 65535 ||
            forwardPort < 0 || forwardPort > 65535 || forwardPort == port || outputPort == port) return false;
        socketError_ = 0;
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) return false;
        winsockReady_ = true;
        input_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        output_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        BOOL exclusive = TRUE;
        if (input_ != INVALID_SOCKET)
            setsockopt(input_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_port = htons(static_cast<u_short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        DWORD timeout = 100;
        bool ok = input_ != INVALID_SOCKET && output_ != INVALID_SOCKET &&
            setsockopt(input_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 &&
            bind(input_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
        if (!ok) { socketError_ = WSAGetLastError(); Stop(); return false; }
        SetCalibration(xGain, yGain, leftOffset, rightOffset);
        outputPort_ = outputPort;
        forwardPort_ = forwardPort;
        outputAllowed_ = std::move(outputAllowed);
        received_ = sent_ = datagrams_ = 0;
        { std::lock_guard<std::mutex> lock(packetMutex_); latest_ = {}; }
        running_ = true;
        worker_ = std::thread([this] { Loop(); });
        return true;
    }
    void Stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        if (input_ != INVALID_SOCKET) { closesocket(input_); input_ = INVALID_SOCKET; }
        if (output_ != INVALID_SOCKET) { closesocket(output_); output_ = INVALID_SOCKET; }
        if (winsockReady_) { WSACleanup(); winsockReady_ = false; }
    }
    bool Running() const { return running_; }
    uint64_t Received() const { return received_; }
    uint64_t Sent() const { return sent_; }
    uint64_t Datagrams() const { return datagrams_; }
    int SocketError() const { return socketError_; }
    void SetForwardPort(int port) { forwardPort_=port; }
    bool Read(UdpGazeV1& packet, double& age) {
        std::lock_guard<std::mutex> lock(packetMutex_);
        packet = latest_;
        LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
        age = packet.qpc ? double(now.QuadPart - int64_t(packet.qpc)) / frequency.QuadPart : -1;
        return packet.qpc && age >= 0 && age <= .15;
    }
    void SetCalibration(float xGain, float yGain, float leftOffset, float rightOffset) {
        xGain_ = xGain; yGain_ = yGain;
        leftOffset_ = leftOffset; rightOffset_ = rightOffset;
    }
};
} // namespace babble
