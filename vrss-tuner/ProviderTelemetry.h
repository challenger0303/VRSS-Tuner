#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace telemetry {
struct Snapshot {
    uint32_t pid{};
    bool registered{};
    bool fresh{};
    uint64_t consumerCount{};
    int queryStatus{};
    uint64_t sent{};
    uint64_t submitFailures{};
    uint64_t sourceSequence{};
    uint64_t oscDatagrams{}, oscReadings{};
    std::array<float, 4> locations{};
    uint64_t accepted() const { return sent >= submitFailures ? sent - submitFailures : 0; }
};

inline bool ValueAt(std::string_view json, std::string_view key, size_t& at) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto found = json.find(needle);
    if (found == std::string_view::npos || json.find(needle, found + needle.size()) != std::string_view::npos)
        return false;
    at = found + needle.size();
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t')) ++at;
    if (at == json.size() || json[at++] != ':') return false;
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t')) ++at;
    return at < json.size();
}
template<class T> inline bool Number(std::string_view json, std::string_view key, T& out) {
    size_t at{};
    if (!ValueAt(json, key, at)) return false;
    const char* first = json.data() + at;
    const char* last = json.data() + json.size();
    auto parsed = std::from_chars(first, last, out);
    if (parsed.ec != std::errc{} || parsed.ptr == first || parsed.ptr == last) return false;
    return *parsed.ptr == ',' || *parsed.ptr == '}' || *parsed.ptr == ' ';
}
inline bool Boolean(std::string_view json, std::string_view key, bool& out) {
    size_t at{};
    if (!ValueAt(json, key, at)) return false;
    if (json.substr(at, 4) == "true") { out = true; return true; }
    if (json.substr(at, 5) == "false") { out = false; return true; }
    return false;
}
inline bool Locations(std::string_view json, std::array<float, 4>& out) {
    size_t at{};
    if (!ValueAt(json, "locations", at) || json[at++] != '[') return false;
    for (size_t i = 0; i < out.size(); ++i) {
        while (at < json.size() && json[at] == ' ') ++at;
        if (at == json.size()) return false;
        std::string remaining(json.substr(at));
        char* end = nullptr;
        const float value = std::strtof(remaining.c_str(), &end);
        if (end == remaining.c_str() || !std::isfinite(value) || value < -1 || value > 1) return false;
        at += size_t(end - remaining.c_str());
        while (at < json.size() && json[at] == ' ') ++at;
        if (at == json.size() || json[at++] != (i + 1 == out.size() ? ']' : ',')) return false;
        out[i] = value;
    }
    return true;
}
inline bool Parse(std::string_view json, Snapshot& out) {
    if (json.size() < 40 || json.size() > 2048) return false;
    Snapshot parsed{};
    if (!Number(json, "pid", parsed.pid) || !parsed.pid ||
        !Boolean(json, "registered", parsed.registered) ||
        !Boolean(json, "fresh", parsed.fresh) ||
        !Number(json, "consumerCount", parsed.consumerCount) ||
        !Number(json, "queryStatus", parsed.queryStatus) ||
        !Number(json, "sent", parsed.sent) ||
        !Number(json, "submitFailures", parsed.submitFailures) ||
        parsed.submitFailures > parsed.sent ||
        !Number(json, "sourceSequence", parsed.sourceSequence) ||
        !Locations(json, parsed.locations)) return false;
    Number(json,"oscDatagrams",parsed.oscDatagrams);
    Number(json,"oscReadings",parsed.oscReadings);
    out = parsed;
    return true;
}
}
