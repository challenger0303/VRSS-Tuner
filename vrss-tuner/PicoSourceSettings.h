#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include "GazeSettings.h"

namespace pico {
inline bool ValidHeadsetIp(const std::string& value) {
    size_t position=0;
    for(int part=0;part<4;++part) {
        const size_t begin=position;
        unsigned number=0;
        while(position<value.size() && value[position]>='0' && value[position]<='9') {
            number=number*10+unsigned(value[position++]-'0');
            if(position-begin>3 || number>255)return false;
        }
        if(position==begin || (position-begin>1 && value[begin]=='0'))return false;
        if(part<3 && (position>=value.size() || value[position++]!='.'))return false;
    }
    return position==value.size();
}
inline std::filesystem::path HeadsetIpFile(const std::filesystem::path& data) {
    return data/L"pico-headset-ip.txt";
}
inline std::string ReadHeadsetIp(const std::filesystem::path& data) {
    std::ifstream input(HeadsetIpFile(data),std::ios::binary);
    std::string value;std::getline(input,value);
    if(!value.empty() && value.back()=='\r')value.pop_back();
    return ValidHeadsetIp(value)?value:std::string();
}
inline bool SaveHeadsetIp(const std::filesystem::path& data,const std::string& value) {
    if(!ValidHeadsetIp(value))return false;
    std::error_code ec;std::filesystem::create_directories(data,ec);
    DWORD error{};
    return !ec && live::AtomicWrite(HeadsetIpFile(data),value+"\n",error);
}
}
