#include "common-vrss-provider/hmd_candidate_identity.h"
#include <iostream>

int main() {
    using hmd_identity::Derive;
    static_assert(hmd_identity::kCandidateVendor==0xFEEE &&
                  hmd_identity::kCommonSessionProduct==1);
    const auto first=Derive("wireless-runtime","headset-001");
    const auto again=Derive("wireless-runtime","headset-001");
    const auto otherSerial=Derive("wireless-runtime","headset-002");
    const auto otherRuntime=Derive("other-runtime","headset-001");
    if(!first.valid || first.vendor!=0xFEEE || first.product==0 ||
       first.product!=again.product || first.fingerprint!=again.fingerprint ||
       first.fingerprint==otherSerial.fingerprint ||
       first.fingerprint==otherRuntime.fingerprint ||
       Derive("","headset-001").valid || Derive("wireless-runtime","").valid) return 1;
    std::cout<<"HMD_CANDIDATE_ID_PASS deterministic serial_required\n";
}
