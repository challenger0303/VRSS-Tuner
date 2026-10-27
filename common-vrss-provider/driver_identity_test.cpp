#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "driver_identity.h"
#include "driver_contract_file.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace driver;
    assert(Select(k59602.apiHash, "UNAVAILABLE", k59602.umdHash)==&k59602);
    assert(Select("unused", k61088.apiHash, k61088.umdHash)==&k61088);
    assert(!Select(k59602.apiHash, k61088.apiHash, "changed"));
    assert(!Select(k59602.apiHash, "UNAVAILABLE", k61088.umdHash));
    assert(!Select("UNAVAILABLE", "UNAVAILABLE", k59602.umdHash));
    for(const auto* profile : {&k59602,&k61088}) {
        for(uintptr_t base : {uintptr_t{0x180000000},uintptr_t{0x7ffb12340000}}) {
            auto methods=profile->methodRvas;
            for(auto& method:methods)method+=base;
            assert(MethodsMatch(*profile,base,methods));
            for(size_t i=0;i<methods.size();++i) {
                auto wrong=methods; ++wrong[i];
                assert(!MethodsMatch(*profile,base,wrong));
            }
            const auto table=base+(profile->vtableRva?profile->vtableRva:0x400000);
            assert(TableAddressMatches(*profile,base,0x1000000,table));
            assert(!TableAddressMatches(*profile,base,0x1000000,base-1));
            assert(!TableAddressMatches(*profile,base,0x1000000,base+0x1000000-1));
        }
    }
    assert(!TableAddressMatches(k59602,0x180000000,0x1000000,0x180480281));
    assert(!TableAddressMatches(k59602,0,0x1000000,0x480280));
    assert(!AddressMatches((std::numeric_limits<uintptr_t>::max)(),1,0));
    const std::string contract="format=1\nversion=610.88\nmodule=nvapi64_impl.dll\napi_sha256="+
        k61088.apiHash+"\numd_sha256="+k61088.umdHash+
        "\nvtable_rva=0x489df0\nmethod_rvas=0x2fcc80,0x328040,0x3280a0,0x327440,0x327bd0,0x326b80,0x3272e0,0x325f70\n";
    const auto parsed=ParseExactContract(contract);
    assert(parsed && parsed->version=="610.88" && parsed->vtableRva==0x489df0);
    assert(MethodsMatch(*parsed,0x180000000,{0x1802fcc80,0x180328040,0x1803280a0,0x180327440,
        0x180327bd0,0x180326b80,0x1803272e0,0x180325f70}));
    assert(MatchesExactImages(*parsed,"UNAVAILABLE",k61088.apiHash,k61088.umdHash));
    assert(!MatchesExactImages(*parsed,k61088.apiHash,"UNAVAILABLE",k61088.umdHash));
    assert(!MatchesExactImages(*parsed,"UNAVAILABLE",k61088.apiHash,k59602.umdHash));
    assert(!ParseExactContract(contract+"version=610.88\n"));
    assert(!ParseExactContract(contract.substr(0,contract.find("method_rvas="))));
    assert(!ParseExactContract(contract+"unexpected=1\n"));
    assert(!ParseExactContract(std::string(4097,'x')));
    auto changed=contract;changed.replace(changed.find("nvapi64_impl.dll"),16,"other.dll");
    assert(!ParseExactContract(changed));
    changed=contract;changed.replace(changed.find("0x489df0"),8,"0x0");
    assert(!ParseExactContract(changed));
    changed=contract;changed.replace(changed.find(k61088.apiHash),64,std::string(64,'z'));
    assert(!ParseExactContract(changed));
    assert(!ParseExactContract(contract+","));
    std::cout<<"DRIVER_IDENTITY_PASS 59602 61088 missing_images mixed_versions every_slot table_bounds overflow exact_contract_parser\n";
}
