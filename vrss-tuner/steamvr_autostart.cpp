#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <array>
#include <cstring>
#include "third_party/openvr/openvr.h"

namespace fs=std::filesystem;
constexpr const char* kAppKey="vrss.tuner.autostart";

static bool Running(const wchar_t* name) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W item{};item.dwSize=sizeof(item);
    bool found=false;
    if(Process32FirstW(snapshot,&item))do {
        if(_wcsicmp(item.szExeFile,name)==0){found=true;break;}
    }while(Process32NextW(snapshot,&item));
    CloseHandle(snapshot);return found;
}

static fs::path Bundle() {
    wchar_t path[32768]{};
    DWORD size=GetModuleFileNameW(nullptr,path,DWORD(std::size(path)));
    return size && size<std::size(path)?fs::path(path).parent_path():fs::path();
}

static std::string Utf8(const std::wstring& value) {
    int size=WideCharToMultiByte(CP_UTF8,0,value.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(size<=0)return {};
    std::string bytes(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.c_str(),-1,bytes.data(),size,nullptr,nullptr);
    bytes.pop_back();return bytes;
}

static int Configure(bool enable) {
    const auto manifest=Bundle()/L"vrss-tuner.vrmanifest";
    if(!fs::is_regular_file(manifest))return 10;
    // VR_Init can launch a runtime on its own. Registration must never choose
    // a SteamVR startup route on behalf of the user.
    if(!Running(L"vrserver.exe"))return 16;
    vr::EVRInitError init=vr::VRInitError_None;
    vr::VR_Init(&init,vr::VRApplication_Utility);
    if(init!=vr::VRInitError_None)return 11;
    auto* apps=vr::VRApplications();
    if(!apps){vr::VR_Shutdown();return 12;}
    const auto path=Utf8(manifest.wstring());
    const auto result=enable?apps->AddApplicationManifest(path.c_str(),false):
        apps->SetApplicationAutoLaunch(kAppKey,false);
    if(result!=vr::VRApplicationError_None &&
       !( !enable && result==vr::VRApplicationError_UnknownApplication )){
        std::cerr<<apps->GetApplicationsErrorNameFromEnum(result)<<'\n';
        vr::VR_Shutdown();return 13;
    }
    const auto setting=enable?apps->SetApplicationAutoLaunch(kAppKey,true):
        apps->RemoveApplicationManifest(path.c_str());
    if(setting!=vr::VRApplicationError_None &&
       !( !enable && setting==vr::VRApplicationError_UnknownApplication )){
        std::cerr<<apps->GetApplicationsErrorNameFromEnum(setting)<<'\n';
        vr::VR_Shutdown();return 14;
    }
    if(enable && !apps->GetApplicationAutoLaunch(kAppKey)){
        vr::VR_Shutdown();return 15;
    }
    vr::VR_Shutdown();return 0;
}

static int LastSource() {
    wchar_t local[32768]{};
    DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));
    if(!size || size>=std::size(local))return -1;
    std::ifstream input(fs::path(local)/L"VRSS Tuner"/L"last-source.txt",std::ios::binary);
    std::string source;std::getline(input,source);
    return source.size()==1 && source[0]>='0' && source[0]<='8'?source[0]-'0':-1;
}

static bool SameBytes(const fs::path& first,const fs::path& second) {
    std::error_code error;
    if(!fs::is_regular_file(second,error) ||
       fs::file_size(first,error)!=fs::file_size(second,error) || error)return false;
    std::ifstream a(first,std::ios::binary),b(second,std::ios::binary);
    if(!a || !b)return false;
    std::array<char,65536> x{},y{};
    do {
        a.read(x.data(),x.size());b.read(y.data(),y.size());
        if(a.gcount()!=b.gcount() || std::memcmp(x.data(),y.data(),size_t(a.gcount()))!=0)return false;
    } while(a.gcount()>0);
    return a.eof() && b.eof();
}

static void RepairSteamBridge(const fs::path& bundle) {
    // VRChat holds the launch wrapper open until the game exits. Installer
    // upgrades leave that copy alone; the next SteamVR start repairs it.
    if(Running(L"VRChat.exe") || Running(L"steam_vrchat_launch.exe"))return;
    wchar_t local[32768]{};
    DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));
    if(!size || size>=std::size(local))return;
    const auto destination=fs::path(local)/L"VRSS Tuner"/L"SteamLaunch";
    if(!fs::is_directory(destination))return;
    for(const auto* name:{L"steam_vrchat_launch.exe",L"openvr_api.dll"}) {
        const auto source=bundle/name,target=destination/name;
        if(fs::is_regular_file(source) && !SameBytes(source,target) &&
           !CopyFileW(source.c_str(),target.c_str(),FALSE))
            std::cerr<<"Steam launch bridge update deferred: "<<GetLastError()<<'\n';
    }
}

int wmain(int argc,wchar_t** argv) {
    if(argc==2 && wcscmp(argv[1],L"--install")==0)return Configure(true);
    if(argc==2 && wcscmp(argv[1],L"--remove")==0)return Configure(false);
    if(argc>1)return 2;
    const auto bundle=Bundle();
    if(bundle.empty())return 3;
    const auto tuner=bundle/L"VRSS Tuner.exe";
    if(!fs::is_regular_file(tuner))return 5;
    RepairSteamBridge(bundle);
    if(!Running(L"VRSS Tuner.exe") &&
       reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",tuner.c_str(),nullptr,bundle.c_str(),SW_SHOWNORMAL))<=32)
        return 6;
    wchar_t local[32768]{};
    if(GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local))) &&
       fs::is_regular_file(fs::path(local)/L"VRSS Tuner/gaze-paused.txt"))return 0;
    const int source=LastSource();
    if(source<=0 || Running(L"common_vrss_provider.exe") || Running(L"vrss_gaze_provider.exe"))return 0;
    static const wchar_t* routes[]={L"",L"Baballonia",L"PSVR2Toolkit",L"SRanibro",
        L"ViveProEye",L"ViveStreaming",L"SteamVREye",L"Varjo",L"PICO"};
    const auto launcher=bundle/L"Start-Provider-Only.cmd";
    if(!fs::is_regular_file(launcher))return 7;
    const std::wstring arguments=std::wstring(routes[source])+L" wait existing-steamvr";
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",launcher.c_str(),arguments.c_str(),bundle.c_str(),SW_SHOWNORMAL))>32?0:8;
}
