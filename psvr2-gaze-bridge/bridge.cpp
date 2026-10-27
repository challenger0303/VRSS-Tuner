#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "hmd2_gaze.h"
#include "../common-vrss-provider/config_io.h"

#pragma comment(lib, "ws2_32.lib")
namespace fs = std::filesystem;
namespace {
struct EyeGeometry { std::array<double,4> projection{}; std::array<double,9> eyeToHead{}; };
struct Gain { double x=1, y=1, leftDegrees=0, rightDegrees=0; };
#pragma pack(push,1)
struct UdpGazeV1 {
    uint32_t magic=0x3147564e, version=1;
    uint64_t sequence=0, qpc=0;
    float leftX=0, leftY=0, rightX=0, rightY=0;
    uint32_t validMask=3;
};
#pragma pack(pop)
static_assert(sizeof(UdpGazeV1)==44);
using Init=int(*)();
using Deinit=void(*)();
using Active=bool(*)();
using GazeStatus=bool(*)(hmd2_gaze_status_t*,uint32_t);
std::atomic<bool> running{true};
BOOL WINAPI onConsole(DWORD) { running=false; return TRUE; }

std::vector<double> numbers(const std::string& raw,size_t count) {
    std::string text=raw;
    std::replace(text.begin(),text.end(),',',' ');
    std::istringstream in(text);
    std::vector<double> result;
    double n;
    while(in>>n) { if(!std::isfinite(n)) throw std::runtime_error("Nonfinite geometry"); result.push_back(n); }
    if(result.size()!=count) throw std::runtime_error("Invalid geometry field length");
    return result;
}
std::array<EyeGeometry,2> readGeometry(const fs::path& path) {
    const auto fields=ReadSharedConfig(path);
    std::array<EyeGeometry,2> eyes{};
    for(int i=0;i<2;++i) {
        const auto p=numbers(fields.at("projection"+std::to_string(i)),4);
        const auto e=numbers(fields.at("eyeToHead"+std::to_string(i)),9);
        std::copy(p.begin(),p.end(),eyes[i].projection.begin());
        std::copy(e.begin(),e.end(),eyes[i].eyeToHead.begin());
        const auto& v=eyes[i].projection;
        if(!(v[0]<0 && v[1]>0 && v[2]<0 && v[3]>0)) throw std::runtime_error("Invalid projection");
    }
    return eyes;
}
Gain readGain(const fs::path& path) {
    const auto values=ReadSharedConfig(path);
    Gain g;
    g.x=std::stod(values.at("x_gain"));g.y=std::stod(values.at("y_gain"));
    g.leftDegrees=std::stod(values.at("left_deg"));g.rightDegrees=std::stod(values.at("right_deg"));
    if(!std::isfinite(g.x)||!std::isfinite(g.y)||!std::isfinite(g.leftDegrees)||!std::isfinite(g.rightDegrees)||
       g.x<.1||g.x>2||std::abs(g.y)<.1||std::abs(g.y)>2||
       std::abs(g.leftDegrees)>5||std::abs(g.rightDegrees)>5) throw std::runtime_error("Invalid calibration");
    return g;
}
std::array<float,2> project(const hmd2_gaze_vec3_t& direction,const EyeGeometry& eye,const Gain& gain,int index) {
    const double x=-direction.x, y=direction.y, z=-direction.z; // PSVR2 Toolkit's own OpenVR transform.
    const double norm=std::sqrt(x*x+y*y+z*z);
    if(!std::isfinite(norm)||norm<.8||norm>1.2||z>=-.1) throw std::runtime_error("Invalid gaze direction");
    const double angle=(index==0?gain.leftDegrees:gain.rightDegrees)*3.141592653589793/180.;
    const double head[3]={std::tan(std::atan(x/-z*gain.x)+angle),y/-z*gain.y,-1};
    double local[3]{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)local[i]+=eye.eyeToHead[j*3+i]*head[j];
    if(!std::isfinite(local[0])||!std::isfinite(local[1])||!std::isfinite(local[2])||local[2]>=-1e-6)
        throw std::runtime_error("Invalid eye-space ray");
    const auto& p=eye.projection;
    const double sx=2*(local[0]/-local[2]-p[0])/(p[1]-p[0])-1;
    const double sy=2*(local[1]/-local[2])/(p[3]-p[2])+(p[2]+p[3])/(p[3]-p[2]);
    return {float(std::clamp(sx,-1.,1.)),float(std::clamp(sy,-1.,1.))};
}
std::map<std::wstring,std::wstring> arguments(int argc,wchar_t** argv) {
    std::map<std::wstring,std::wstring> args;
    for(int i=1;i+1<argc;i+=2) args[argv[i]]=argv[i+1];
    return args;
}
fs::path capiPath(const std::map<std::wstring,std::wstring>& args) {
    if(args.count(L"--capi")) return args.at(L"--capi");
    const auto file=fs::temp_directory_path()/L"psvr2tk_capi_path.txt";
    std::ifstream in(file);
    std::string directory;
    std::getline(in,directory);
    if(directory.empty()) throw std::runtime_error("PSVR2 Toolkit CAPI path not found");
    return fs::path(directory)/L"psvr2_toolkit_capi.dll";
}
}
int wmain(int argc,wchar_t** argv) {
    HMODULE library=nullptr;
    SOCKET socket=INVALID_SOCKET;
    bool initialized=false, winsock=false;
    Deinit deinit=nullptr;
    try {
        const auto args=arguments(argc,argv);
        const fs::path capi=capiPath(args);
        if(!capi.is_absolute()||capi.filename()!=L"psvr2_toolkit_capi.dll"||!fs::is_regular_file(capi))
            throw std::runtime_error("Invalid PSVR2 Toolkit CAPI path");
        library=LoadLibraryExW(capi.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!library) throw std::runtime_error("PSVR2 Toolkit CAPI could not be loaded");
        auto init=reinterpret_cast<Init>(GetProcAddress(library,"psvr2_toolkit_init"));
        deinit=reinterpret_cast<Deinit>(GetProcAddress(library,"psvr2_toolkit_deinit"));
        auto active=reinterpret_cast<Active>(GetProcAddress(library,"psvr2_toolkit_get_driver_active"));
        auto gaze=reinterpret_cast<GazeStatus>(GetProcAddress(library,"psvr2_toolkit_gaze_status"));
        if(!init||!deinit||!active||!gaze) throw std::runtime_error("PSVR2 Toolkit CAPI exports missing");
        if(args.count(L"--check-exports-only")) {
            std::cout<<"PSVR2TK_CAPI_EXPORTS_READY\n";
            FreeLibrary(library);
            return 0;
        }
        if(init()!=0) throw std::runtime_error("PSVR2 Toolkit CAPI initialization failed");
        initialized=true;
        if(!active()) throw std::runtime_error("PSVR2 Toolkit driver is inactive");
        if(args.count(L"--check-only")) { std::cout<<"PSVR2TK_CAPI_READY\n"; deinit();FreeLibrary(library);return 0; }
        if(!args.count(L"--geometry")||!args.count(L"--config")) throw std::runtime_error("Geometry and calibration are required");
        const auto eyes=readGeometry(args.at(L"--geometry"));
        const fs::path config=args.at(L"--config");
        const fs::path stopFile=args.count(L"--stop-file")?fs::path(args.at(L"--stop-file")):fs::path{};
        Gain gain=readGain(config);
        int port=args.count(L"--port")?std::stoi(args.at(L"--port")):5556;
        if(port<1||port>65535) throw std::runtime_error("Invalid port");
        const double seconds=args.count(L"--seconds")?std::stod(args.at(L"--seconds")):3600;
        if(!std::isfinite(seconds)||seconds<=0||seconds>86400) throw std::runtime_error("Invalid duration");
        WSADATA wd{};
        if(WSAStartup(MAKEWORD(2,2),&wd)) throw std::runtime_error("Winsock failed");
        winsock=true;
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(socket==INVALID_SOCKET) throw std::runtime_error("UDP socket failed");
        sockaddr_in destination{};destination.sin_family=AF_INET;
        destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);destination.sin_port=htons(static_cast<u_short>(port));
        SetConsoleCtrlHandler(onConsole,TRUE);
        LARGE_INTEGER qpc{};
        uint64_t sequence=0;
        uint32_t lastFrame=0;
        bool hadFrame=false;
        auto start=std::chrono::steady_clock::now();
        auto nextConfig=start;
        while(running.load()&&(stopFile.empty()||!fs::exists(stopFile))&&
              std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds) {
            const auto now=std::chrono::steady_clock::now();
            if(now>=nextConfig) { try {gain=readGain(config);}catch(...){} nextConfig=now+std::chrono::milliseconds(250); }
            hmd2_gaze_status_t data{};
            if(!gaze(&data,30)) {std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
            if(data.magic[0]!='G'||data.magic[1]!='S'||data.size!=sizeof(data)||
                (hadFrame&&data.wearable.frame_counter==lastFrame)) continue;
            hadFrame=true;lastFrame=data.wearable.frame_counter;
            if(data.wearable.left.is_gaze_dir_valid!=HMD2_GAZE_BOOL_TRUE||
               data.wearable.right.is_gaze_dir_valid!=HMD2_GAZE_BOOL_TRUE) continue;
            try {
                const auto left=project(data.wearable.left.gaze_dir_norm,eyes[0],gain,0);
                const auto right=project(data.wearable.right.gaze_dir_norm,eyes[1],gain,1);
                QueryPerformanceCounter(&qpc);
                UdpGazeV1 packet{};packet.sequence=++sequence;packet.qpc=uint64_t(qpc.QuadPart);
                packet.leftX=left[0];packet.leftY=left[1];packet.rightX=right[0];packet.rightY=right[1];
                sendto(socket,reinterpret_cast<const char*>(&packet),sizeof(packet),0,
                       reinterpret_cast<sockaddr*>(&destination),sizeof(destination));
            } catch(const std::exception&) { /* An invalid sample must not reach the provider. */ }
        }
        std::cout<<"sent="<<sequence<<"\n";
        closesocket(socket);WSACleanup();deinit();FreeLibrary(library);return 0;
    } catch(const std::exception& e) {
        if(socket!=INVALID_SOCKET) closesocket(socket);
        if(winsock) WSACleanup();
        if(initialized&&deinit) deinit();
        if(library) FreeLibrary(library);
        std::cerr<<"PSVR2_BRIDGE_ERROR: "<<e.what()<<'\n';return 2;
    }
}
