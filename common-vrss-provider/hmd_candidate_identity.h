#pragma once

#include <cstdint>
#include <string>

namespace hmd_identity {

// Experimental local namespace. This is not a real EDID vendor assignment.
constexpr uint16_t kCandidateVendor = 0xFEEE;
// Common-path session key. This is only an NVIDIA provider/consumer routing key;
// headset projection and gaze calibration still come from the connected HMD.
constexpr uint16_t kCommonSessionProduct = 0x0001;

struct Candidate {
    uint16_t vendor = 0;
    uint16_t product = 0;
    uint64_t fingerprint = 0;
    bool valid = false;
};

inline Candidate Derive(const std::string& trackingSystem, const std::string& serial) {
    if (trackingSystem.empty() || serial.empty()) return {};
    uint64_t hash = 14695981039346656037ull;
    const auto add = [&hash](unsigned char value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    for (unsigned char value : trackingSystem) add(value);
    add(0);
    for (unsigned char value : serial) add(value);
    const uint16_t folded = static_cast<uint16_t>(hash ^ (hash >> 16) ^
        (hash >> 32) ^ (hash >> 48));
    return {kCandidateVendor, static_cast<uint16_t>(folded ? folded : 1), hash, true};
}

} // namespace hmd_identity
