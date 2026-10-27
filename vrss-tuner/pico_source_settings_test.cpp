#include <windows.h>
#include <iostream>
#include "PicoSourceSettings.h"

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    for(const auto* value:{"10.23.45.67","127.0.0.1","255.255.255.255","0.0.0.0"})
        if(!pico::ValidHeadsetIp(value))return 2;
    for(const auto* value:{"","10.1.1","10.1.1.256","10.1.1.01","10.1.1.1 ","host","::1","10.1.1.1\n","10.1.1.1;echo x"})
        if(pico::ValidHeadsetIp(value))return 3;
    const std::filesystem::path data=argv[1];
    if(!pico::ReadHeadsetIp(data).empty() || !pico::SaveHeadsetIp(data,"10.23.45.67") ||
       pico::ReadHeadsetIp(data)!="10.23.45.67" || pico::SaveHeadsetIp(data,"bad") ||
       pico::ReadHeadsetIp(data)!="10.23.45.67")return 4;
    std::cout<<"PICO_SETTINGS_PASS ipv4_bounds roundtrip invalid_preserves_saved_ip\n";
}
