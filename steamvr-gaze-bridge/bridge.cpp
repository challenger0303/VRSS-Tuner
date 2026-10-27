#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "../third_party/openvr/openvr.h"
#include "../common-vrss-provider/config_io.h"
#include "projection.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib,"ws2_32.lib")
namespace fs=std::filesystem;
using namespace steamvr_gaze;
static std::atomic<bool> running{true};
static BOOL WINAPI stop(DWORD) {running=false;return TRUE;}
#pragma pack(push,1)
struct UdpGazeV1 {
    uint32_t magic=0x3147564e,version=1;
    uint64_t sequence=0,qpc=0;
    float leftX=0,leftY=0,rightX=0,rightY=0;
    uint32_t validMask=3;
};
#pragma pack(pop)
static_assert(sizeof(UdpGazeV1)==44);
static std::map<std::wstring,std::wstring> args(int argc,wchar_t** argv) {
    if((argc-1)%2)throw std::runtime_error("Arguments must be --name value pairs");
    std::map<std::wstring,std::wstring> result;
    for(int i=1;i<argc;i+=2) result[argv[i]]=argv[i+1];
    return result;
}
static std::vector<double> numbers(std::string value,size_t count) {
    std::replace(value.begin(),value.end(),',',' ');
    std::istringstream input(value);std::vector<double> result;double number;
    while(input>>number) {if(!std::isfinite(number))throw std::runtime_error("Nonfinite geometry");result.push_back(number);}
    if(result.size()!=count)throw std::runtime_error("Invalid geometry length");
    return result;
}
static std::array<Eye,2> geometry(const fs::path& file) {
    const auto config=ReadSharedConfig(file);std::array<Eye,2> eyes;
    for(int i=0;i<2;++i) {
        auto p=numbers(config.at("projection"+std::to_string(i)),4);
        auto r=numbers(config.at("eyeToHead"+std::to_string(i)),9);
        auto t=numbers(config.at("eyeTranslation"+std::to_string(i)),3);
        std::copy(p.begin(),p.end(),eyes[i].projection.begin());
        std::copy(r.begin(),r.end(),eyes[i].rotation.begin());
        std::copy(t.begin(),t.end(),eyes[i].translation.begin());
    }
    return eyes;
}
static Calibration calibration(const fs::path& file) {
    const auto config=ReadSharedConfig(file);Calibration result;
    result.x=std::stod(config.at("x_gain"));result.y=std::stod(config.at("y_gain"));
    result.leftDegrees=std::stod(config.at("left_deg"));result.rightDegrees=std::stod(config.at("right_deg"));
    if(!std::isfinite(result.x)||!std::isfinite(result.y)||
       result.x<.1||result.x>2||std::abs(result.y)<.1||std::abs(result.y)>2||
       !std::isfinite(result.leftDegrees)||!std::isfinite(result.rightDegrees)||
       std::abs(result.leftDegrees)>5||std::abs(result.rightDegrees)>5)
        throw std::runtime_error("Invalid calibration");
    return result;
}
static Mat3 rotation(const vr::HmdMatrix34_t& matrix) {
    Mat3 result{};
    for(int row=0;row<3;++row)for(int column=0;column<3;++column)
        result[row*3+column]=matrix.m[row][column];
    return result;
}
static Vec3 translation(const vr::HmdMatrix34_t& matrix) {
    return {matrix.m[0][3],matrix.m[1][3],matrix.m[2][3]};
}
static bool selfTest() {
    Eye left{{-.8,.8,-.8,.8},{1,0,0,0,1,0,0,0,1},{-.032,0,0}};
    Eye right=left;right.translation[0]=.032;
    const Mat3 identity{1,0,0,0,1,0,0,0,1};const Vec3 zero{0,0,0};
    const auto l=project({0,0,-2},identity,zero,left,{},0);
    const auto r=project({0,0,-2},identity,zero,right,{},1);
    const auto up=project({0,.2,-2},identity,zero,left,{},0);
    const auto shifted=project({.2,0,-2},identity,zero,left,{},0);
    const auto headMoved=project({.2,0,-2},identity,{.2,0,0},left,{},0);
    return l[0]>0&&r[0]<0&&up[1]>l[1]&&shifted[0]>l[0]&&
           std::abs(headMoved[0]-l[0])<1e-5;
}
int wmain(int argc,wchar_t** argv) {
    SOCKET socket=INVALID_SOCKET;bool winsock=false,steamvr=false;
    try {
        const auto options=args(argc,argv);
        if(options.count(L"--self-test")) {
            if(!selfTest())throw std::runtime_error("Projection self-test failed");
            std::cout<<"STEAMVR_GAZE_PROJECTION_PASS\n";return 0;
        }
        const fs::path manifest=options.at(L"--manifest");
        const fs::path config=options.at(L"--config");
        const fs::path stopFile=options.at(L"--stop-file");
        const auto eyes=geometry(options.at(L"--geometry"));
        Calibration gain=calibration(config);
        if(!fs::is_regular_file(manifest))throw std::runtime_error("Eye action manifest missing");
        const int port=options.count(L"--port")?std::stoi(options.at(L"--port")):5556;
        const int seconds=options.count(L"--seconds")?std::stoi(options.at(L"--seconds")):3600;
        if(port<1||port>65535||seconds<1||seconds>86400)throw std::runtime_error("Invalid port or duration");
        vr::EVRInitError init=vr::VRInitError_None;
        auto* system=vr::VR_Init(&init,vr::VRApplication_Background);
        if(!system||init!=vr::VRInitError_None)throw std::runtime_error("SteamVR is unavailable");
        steamvr=true;
        auto* input=vr::VRInput();if(!input)throw std::runtime_error("OpenVR input unavailable");
        const auto utf8=fs::absolute(manifest).u8string();
        auto error=input->SetActionManifestPath(utf8.c_str());
        if(error!=vr::VRInputError_None)throw std::runtime_error("Eye action manifest rejected");
        vr::VRActionSetHandle_t set=vr::k_ulInvalidActionSetHandle;
        vr::VRActionHandle_t action=vr::k_ulInvalidActionHandle;
        error=input->GetActionSetHandle("/actions/vrss_probe",&set);
        if(error==vr::VRInputError_None)
            error=input->GetActionHandle("/actions/vrss_probe/in/gaze",&action);
        if(error!=vr::VRInputError_None)throw std::runtime_error("Eye action unavailable");
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock unavailable");winsock=true;
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(socket==INVALID_SOCKET)throw std::runtime_error("UDP socket unavailable");
        sockaddr_in destination{};destination.sin_family=AF_INET;
        destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);destination.sin_port=htons(static_cast<u_short>(port));
        SetConsoleCtrlHandler(stop,TRUE);
        uint64_t sequence=0, valid=0, invalid=0;vr::VRActiveActionSet_t active{};active.ulActionSet=set;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        auto nextConfig=std::chrono::steady_clock::now();
        while(running&&!fs::exists(stopFile)&&std::chrono::steady_clock::now()<end) {
            const auto now=std::chrono::steady_clock::now();
            if(now>=nextConfig) {try {gain=calibration(config);}catch(...){}nextConfig=now+std::chrono::milliseconds(250);}
            error=input->UpdateActionState(&active,sizeof(active),1);
            vr::VREyeTrackingData_t gaze{};
            if(error==vr::VRInputError_None)
                error=input->GetEyeTrackingDataRelativeToNow(action,vr::TrackingUniverseStanding,0,&gaze,sizeof(gaze));
            if(error!=vr::VRInputError_None||!gaze.bActive||!gaze.bValid||!gaze.bTracked) {
                ++invalid;std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;
            }
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
            system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
            const auto& hmd=poses[vr::k_unTrackedDeviceIndex_Hmd];
            if(!hmd.bPoseIsValid||!hmd.bDeviceIsConnected) {++invalid;continue;}
            try {
                const Vec3 origin{gaze.vGazeOrigin.v[0],gaze.vGazeOrigin.v[1],gaze.vGazeOrigin.v[2]};
                const Vec3 target{gaze.vGazeTarget.v[0],gaze.vGazeTarget.v[1],gaze.vGazeTarget.v[2]};
                const double length=std::hypot(target[0]-origin[0],target[1]-origin[1],target[2]-origin[2]);
                if(!std::isfinite(length)||length<.05||length>100)throw std::runtime_error("Invalid gaze distance");
                const auto headR=rotation(hmd.mDeviceToAbsoluteTracking);
                const auto headT=translation(hmd.mDeviceToAbsoluteTracking);
                const auto l=project(target,headR,headT,eyes[0],gain,0);
                const auto r=project(target,headR,headT,eyes[1],gain,1);
                LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);
                UdpGazeV1 packet{};packet.sequence=++sequence;packet.qpc=uint64_t(qpc.QuadPart);
                packet.leftX=l[0];packet.leftY=l[1];packet.rightX=r[0];packet.rightY=r[1];
                if(sendto(socket,reinterpret_cast<const char*>(&packet),sizeof(packet),0,
                    reinterpret_cast<const sockaddr*>(&destination),sizeof(destination))!=sizeof(packet))
                    throw std::runtime_error("UDP send failed");
                ++valid;
            } catch(const std::runtime_error&) {++invalid;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout<<"STEAMVR_GAZE_BRIDGE_STOP sent="<<valid<<" invalid="<<invalid<<'\n';
        closesocket(socket);WSACleanup();vr::VR_Shutdown();return 0;
    } catch(const std::exception& error) {
        if(socket!=INVALID_SOCKET)closesocket(socket);if(winsock)WSACleanup();if(steamvr)vr::VR_Shutdown();
        std::cerr<<"STEAMVR_GAZE_BRIDGE_ERROR: "<<error.what()<<'\n';return 2;
    }
}
