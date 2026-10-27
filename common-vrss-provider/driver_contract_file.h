#pragma once
#include "driver_identity.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <sstream>

namespace driver {
inline bool LowerSha256(std::string_view value) {
    if(value.size()!=64)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline std::optional<uintptr_t> HexRva(std::string_view value) {
    if(value.size()<3||value.substr(0,2)!="0x")return std::nullopt;
    uintptr_t parsed=0;
    const auto first=value.data()+2,last=value.data()+value.size();
    const auto result=std::from_chars(first,last,parsed,16);
    if(result.ec!=std::errc{}||result.ptr!=last||parsed==0)return std::nullopt;
    return parsed;
}
inline std::optional<Identity> ParseExactContract(std::string_view text) {
    if(text.empty()||text.size()>4096)return std::nullopt;
    std::map<std::string,std::string> fields;
    std::istringstream lines{std::string(text)};
    std::string line;
    while(std::getline(lines,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        const auto equal=line.find('=');
        if(equal==std::string::npos||equal==0||equal==line.size()-1||line.find('=',equal+1)!=std::string::npos)
            return std::nullopt;
        if(!fields.emplace(line.substr(0,equal),line.substr(equal+1)).second)return std::nullopt;
    }
    if(fields.size()!=7)return std::nullopt;
    for(const char* key:{"format","version","module","api_sha256","umd_sha256","vtable_rva","method_rvas"})
        if(fields.find(key)==fields.end())return std::nullopt;
    if(fields.at("format")!="1")return std::nullopt;
    Identity identity{};
    identity.version=fields["version"];
    if(identity.version.empty()||identity.version.size()>32)return std::nullopt;
    for(char c:identity.version)if(!((c>='0'&&c<='9')||c=='.'))return std::nullopt;
    const auto module=fields["module"];
    if(module=="nvapi64.dll")identity.module=L"nvapi64.dll";
    else if(module=="nvapi64_impl.dll")identity.module=L"nvapi64_impl.dll";
    else return std::nullopt;
    identity.apiHash=fields["api_sha256"];
    identity.umdHash=fields["umd_sha256"];
    if(!LowerSha256(identity.apiHash)||!LowerSha256(identity.umdHash))return std::nullopt;
    const auto table=HexRva(fields["vtable_rva"]);
    if(!table)return std::nullopt;
    identity.vtableRva=*table;
    if(std::count(fields["method_rvas"].begin(),fields["method_rvas"].end(),',')!=7)
        return std::nullopt;
    std::istringstream methods(fields["method_rvas"]);
    for(auto& rva:identity.methodRvas) {
        std::string field;
        if(!std::getline(methods,field,','))return std::nullopt;
        const auto value=HexRva(field);
        if(!value)return std::nullopt;
        rva=*value;
    }
    if(methods.rdbuf()->in_avail()!=0)return std::nullopt;
    return identity;
}
inline bool MatchesExactImages(const Identity& contract,std::string_view api,
                               std::string_view impl,std::string_view umd) {
    return umd==contract.umdHash &&
        (contract.module==L"nvapi64.dll" ? api==contract.apiHash :
         contract.module==L"nvapi64_impl.dll" && impl==contract.apiHash);
}
}
