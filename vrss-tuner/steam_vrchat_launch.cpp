#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "third_party/openvr/openvr.h"
#include "common-vrss-provider/hmd_candidate_identity.h"

namespace fs = std::filesystem;

static bool Running(const wchar_t* name) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    bool found=false;
    if(Process32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szExeFile,name)==0){found=true;break;}
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}

static fs::path LocalData() {
    wchar_t value[32768]{};
    DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",value,32768);
    return size && size<32768 ? fs::path(value)/L"VRSS Tuner" : fs::path();
}

static std::map<std::string,std::string> Fields(const fs::path& path) {
    std::map<std::string,std::string> fields;
    std::ifstream stream(path,std::ios::binary);
    std::string line;
    while(std::getline(stream,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        auto equal=line.find('=');
        if(equal!=std::string::npos)fields[line.substr(0,equal)]=line.substr(equal+1);
    }
    return fields;
}

static std::string Property(vr::IVRSystem* system,vr::ETrackedDeviceProperty prop) {
    char value[512]{};vr::ETrackedPropertyError error{};
    uint32_t size=system->GetStringTrackedDeviceProperty(0,prop,value,sizeof(value),&error);
    return error==vr::TrackedProp_Success && size>1 && size<=sizeof(value) && value[size-1]=='\0'
        ? std::string(value,size-1):std::string();
}

struct Identity {int vendor{},product{};std::string reason;};

static bool ActiveProviderMatches(const fs::path& data,const Identity& identity) {
    std::ifstream active(data/L"active-run.txt",std::ios::binary);
    std::string runText;std::getline(active,runText);
    if(runText.size()>=3 && runText.substr(0,3)=="\xEF\xBB\xBF")runText.erase(0,3);
    if(!runText.empty() && runText.back()=='\r')runText.pop_back();
    if(runText.empty())return false;
    std::error_code error;
    const auto root=fs::weakly_canonical(data/L"runs",error);
    if(error)return false;
    const auto run=fs::weakly_canonical(fs::path(runText),error);
    if(error || run.parent_path()!=root)return false;
    const auto fields=Fields(run/L"geometry.txt");
    auto vendor=fields.find("vendor"),product=fields.find("product"),
         source=fields.find("identitySource");
    if(vendor==fields.end() || product==fields.end() || source==fields.end())return false;
    return vendor->second==std::to_string(identity.vendor) &&
           product->second==std::to_string(identity.product) && source->second==identity.reason;
}

static Identity CurrentIdentity(const fs::path& data) {
    vr::EVRInitError error{};
    auto* system=vr::VR_Init(&error,vr::VRApplication_Background);
    if(!system || error!=vr::VRInitError_None)return {0,0,"openvr-unavailable"};
    if(!system->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd)) {
        vr::VR_Shutdown();return {0,0,"hmd-disconnected"};
    }
    const auto model=Property(system,vr::Prop_ModelNumber_String);
    const auto maker=Property(system,vr::Prop_ManufacturerName_String);
    if((model=="Steam Frame" || model=="Deckard MP") && maker=="Valve") {
        vr::VR_Shutdown();return {0,0,"steam-frame-dedicated-path"};
    }
    vr::ETrackedPropertyError a{},b{};
    int vendor=system->GetInt32TrackedDeviceProperty(0,vr::Prop_EdidVendorID_Int32,&a);
    int product=system->GetInt32TrackedDeviceProperty(0,vr::Prop_EdidProductID_Int32,&b);
    const bool edid=a==vr::TrackedProp_Success && b==vr::TrackedProp_Success &&
        vendor>0 && vendor<=65535 && product>0 && product<=65535;
    auto candidate=hmd_identity::Derive(Property(system,vr::Prop_TrackingSystemName_String),
                                        Property(system,vr::Prop_SerialNumber_String));
    vr::VR_Shutdown();
    const auto saved=Fields(data/L"preflight"/L"geometry.txt");
    auto source=saved.find("identitySource");
    if(source!=saved.end() && source->second=="tuner-session") {
        auto fingerprint=saved.find("identityFingerprint");
        if(fingerprint==saved.end())return {0,0,"hmd-fingerprint-missing"};
        std::ostringstream current;current<<std::hex<<std::setw(16)<<std::setfill('0')
                                         <<candidate.fingerprint;
        if(!candidate.valid || current.str()!=fingerprint->second)
            return {0,0,"hmd-fingerprint-changed"};
        return {hmd_identity::kCandidateVendor,hmd_identity::kCommonSessionProduct,
                "tuner-session"};
    }
    if(!edid && !candidate.valid)return {0,0,"identity-unavailable"};
    if(!edid){vendor=candidate.vendor;product=candidate.product;}
    if(source!=saved.end() && source->second=="candidate-manual" && candidate.valid) {
        std::ostringstream fingerprint;fingerprint<<std::hex<<std::setw(16)<<std::setfill('0')
                                                   <<candidate.fingerprint;
        auto savedFingerprint=saved.find("identityFingerprint");
        auto savedVendor=saved.find("vendor"),savedProduct=saved.find("product");
        if(savedFingerprint!=saved.end() && savedFingerprint->second==fingerprint.str() &&
           savedVendor!=saved.end() && savedProduct!=saved.end()) {
            try {
                size_t usedV=0,usedP=0;
                int v=std::stoi(savedVendor->second,&usedV),p=std::stoi(savedProduct->second,&usedP);
                if(usedV==savedVendor->second.size() && usedP==savedProduct->second.size() &&
                   v>0 && v<=65535 && p>0 && p<=65535)
                    return {v,p,"candidate-manual"};
            }catch(...){}
        }
        return {0,0,"manual-candidate-mismatch"};
    }
    return {vendor,product,edid?"openvr-edid":"candidate-auto"};
}

static bool CommonSourceSelected(const fs::path& data) {
    std::ifstream input(data/L"last-source.txt",std::ios::binary);
    std::string selected;std::getline(input,selected);
    if(!selected.empty() && selected.back()=='\r')selected.pop_back();
    return selected.size()==1 && selected[0]>='1' && selected[0]<='8';
}

static std::wstring Quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t ch:value) {
        if(ch==L'\\'){++slashes;continue;}
        if(ch==L'"')result.append(slashes*2+1,L'\\');
        else result.append(slashes,L'\\');
        slashes=0;result.push_back(ch);
    }
    result.append(slashes*2,L'\\');result.push_back(L'"');return result;
}

static void Log(const fs::path& data,const std::string& message) {
    if(data.empty())return;
    std::error_code error;fs::create_directories(data,error);
    std::ofstream log(data/L"steam-vrchat-launch.log",std::ios::app);
    SYSTEMTIME now{};GetLocalTime(&now);
    log<<now.wYear<<'-'<<std::setw(2)<<std::setfill('0')<<now.wMonth<<'-'<<std::setw(2)
       <<now.wDay<<' '<<std::setw(2)<<now.wHour<<':'<<std::setw(2)<<now.wMinute<<':'
       <<std::setw(2)<<now.wSecond<<' '<<message<<'\n';
}

int wmain(int argc,wchar_t** argv) {
    if(argc<2)return 2;
    fs::path target=argv[1];
    const auto exe=target.filename().wstring();
    if(_wcsicmp(exe.c_str(),L"launch.exe")!=0 &&
       _wcsicmp(exe.c_str(),L"start_protected_game.exe")!=0 &&
       _wcsicmp(exe.c_str(),L"VRChat.exe")!=0)return 3;
    std::wstring command=Quote(target.wstring());
    for(int i=2;i<argc;++i){command.push_back(L' ');command+=Quote(argv[i]);}
    auto data=LocalData();
    bool keyed=false;
    if(CommonSourceSelected(data)) {
        auto identity=CurrentIdentity(data);
        const bool providerRunning=Running(L"common_vrss_provider.exe");
        const bool providerMatches=providerRunning && ActiveProviderMatches(data,identity);
        // A fixed session ID is chosen before VRChat starts. Once the HMD
        // fingerprint matches, the gaze provider can attach later without a
        // second game launch. Never reserve an arbitrary EDID this way.
        const bool reserved=(!providerRunning && identity.reason=="tuner-session");
        if(identity.vendor && identity.product && (providerMatches || reserved)) {
            std::wostringstream vendor,product;
            vendor<<L"0x"<<std::hex<<std::uppercase<<identity.vendor;
            product<<L"0x"<<std::hex<<std::uppercase<<identity.product;
            keyed=SetEnvironmentVariableW(L"TEST_VENDOR_ID",vendor.str().c_str()) &&
                  SetEnvironmentVariableW(L"TEST_PRODUCT_ID",product.str().c_str());
            Log(data,identity.reason+" vendor="+std::to_string(identity.vendor)+
                " product="+std::to_string(identity.product)+
                (reserved?" reserved-before-provider":" provider-ready")+
                (keyed?" applied":" set-failed"));
        }else Log(data,identity.reason+" or provider-identity-mismatch passthrough");
    }else Log(data,"native-path passthrough");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION child{};
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
    if(!CreateProcessW(target.c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,0,nullptr,
                       target.parent_path().c_str(),&startup,&child)) {
        Log(data,"CreateProcess failed="+std::to_string(GetLastError()));return 4;
    }
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess,INFINITE);
    DWORD result=0;GetExitCodeProcess(child.hProcess,&result);CloseHandle(child.hProcess);
    // Steam tracks the launch-option wrapper. Keep it alive while the protected
    // launcher hands off to the real game, without doing this for test fixtures.
    if((_wcsicmp(exe.c_str(),L"launch.exe")==0 ||
        _wcsicmp(exe.c_str(),L"start_protected_game.exe")==0) &&
       fs::exists(target.parent_path()/L"VRChat.exe")) {
        const auto deadline=GetTickCount64()+60000;
        while(!Running(L"VRChat.exe") && GetTickCount64()<deadline)Sleep(500);
        if(Running(L"VRChat.exe")) {
            Log(data,"VRChat process observed; waiting for game exit");
            while(Running(L"VRChat.exe"))Sleep(1000);
        }else Log(data,"VRChat process did not appear within 60 seconds");
    }
    Log(data,std::string(keyed?"keyed":"passthrough")+" launcher-exit="+std::to_string(result));
    return static_cast<int>(result);
}
