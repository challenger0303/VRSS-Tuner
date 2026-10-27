#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace driver {
struct Identity {
    std::string version;
    std::wstring module;
    std::string apiHash,umdHash;
    uintptr_t vtableRva;
    std::array<uintptr_t,8> methodRvas;
};
inline const Identity k59602{
    "596.02",L"nvapi64.dll",
    "a902fe11d59166285c264aea4695d1978eb6ce6cf0d4c477fcddcac1527ddfb2",
    "ec0cb8ade4bf81e6b6835832e7e4d665d59600c616967577fae162455edb505c",
    0x480280,{0x2fb260,0x3289d0,0x328a30,0x327dd0,0x328560,0x327510,0x327c70,0x3268d0}};
// Retain upstream's exact 610.88 contract for existing non-Frame providers.
inline const Identity k61088{
    "610.88",L"nvapi64_impl.dll",
    "df6455ccf83e43cfe68f405af1eec4e053c7f95da998bf358053b7583980c2f4",
    "04c01cfc849e5b58ad50da89dc83bff15f15847684de7964ff5900fedfab18fe",
    0,{0x2fcc80,0x328040,0x3280a0,0x327440,0x327bd0,0x326b80,0x3272e0,0x325f70}};
inline const Identity* Select(std::string_view api,std::string_view impl,std::string_view umd) {
    if(api==k59602.apiHash && umd==k59602.umdHash)return &k59602;
    if(impl==k61088.apiHash && umd==k61088.umdHash)return &k61088;
    return nullptr;
}
inline bool AddressMatches(uintptr_t base,uintptr_t rva,uintptr_t actual) {
    return base && base<=(std::numeric_limits<uintptr_t>::max)()-rva && actual==base+rva;
}
inline bool TableAddressMatches(const Identity& profile,uintptr_t base,uintptr_t size,uintptr_t table) {
    constexpr auto bytes=sizeof(uintptr_t)*8;
    return base && size>=bytes && table>=base && table-base<=size-bytes &&
        (!profile.vtableRva || AddressMatches(base,profile.vtableRva,table));
}
inline bool MethodsMatch(const Identity& profile,uintptr_t base,const std::array<uintptr_t,8>& methods) {
    for(size_t i=0;i<methods.size();++i)
        if(!AddressMatches(base,profile.methodRvas[i],methods[i]))return false;
    return true;
}
}
