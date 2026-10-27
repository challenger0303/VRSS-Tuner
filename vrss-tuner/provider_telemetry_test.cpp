#include "ProviderTelemetry.h"
#include <cassert>
#include <iostream>

int main() {
    constexpr auto valid = R"({"pid":123,"registered":true,"fresh":true,"consumerCount":1,"queryStatus":0,"sent":80,"submitFailures":2,"sourceSequence":99,"locations":[-0.5,0.75,0.25,-1],"source":"normalized-udp"})";
    telemetry::Snapshot parsed{};
    assert(telemetry::Parse(valid, parsed));
    assert(parsed.pid == 123 && parsed.registered && parsed.fresh);
    assert(parsed.accepted() == 78 && parsed.locations[1] == 0.75f);
    assert(!telemetry::Parse(R"({"pid":123,"registered":true})", parsed));
    const std::string overflow = std::string(valid).replace(std::string(valid).find("\"sent\":80"), 9, "\"sent\":18446744073709551616");
    assert(!telemetry::Parse(overflow, parsed));
    const std::string badLocation = std::string(valid).replace(std::string(valid).find("-0.5"), 4, "1.5");
    assert(!telemetry::Parse(badLocation, parsed));
    const std::string failures = std::string(valid).replace(std::string(valid).find("\"submitFailures\":2"), 18, "\"submitFailures\":81");
    assert(!telemetry::Parse(failures, parsed));
    std::cout << "PROVIDER_TELEMETRY_PASS valid missing overflow range failure_count\n";
}
