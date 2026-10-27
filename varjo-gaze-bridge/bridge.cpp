#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <Varjo.h>
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
#include "../common-vrss-provider/config_io.h"
#include "../vrss-tuner/GazeCalibrationFormat.h"
#pragma comment(lib,"ws2_32.lib")
namespace fs=std::filesystem;
static std::atomic<bool> running{true};
static BOOL WINAPI Stop(DWORD) {running=false;return TRUE;}
struct EyeGeometry {std::array<double,4> projection{};std::array<double,9> rotation{};};
#pragma pack(push,1)
struct UdpGazeV1 {
    uint32_t magic=0x3147564e,version=1;
    uint64_t sequence=0,qpc=0;
    float leftX=0,leftY=0,rightX=0,rightY=0;
    uint32_t validMask=3;
};
#pragma pack(pop)
static_assert(sizeof(UdpGazeV1)==44);
static std::vector<double> Numbers(std::string text,size_t count) {
    std::replace(text.begin(),text.end(),',',' ');
    std::istringstream input(text);std::vector<double> out;double value;
    while(input>>value) {if(!std::isfinite(value))throw std::runtime_error("Nonfinite geometry");out.push_back(value);}
    if(out.size()!=count)throw std::runtime_error("Invalid geometry length");return out;
}
static std::array<EyeGeometry,2> Geometry(const fs::path& path) {
    const auto fields=ReadSharedConfig(path);std::array<EyeGeometry,2> eyes{};
    for(int i=0;i<2;++i) {
        auto p=Numbers(fields.at("projection"+std::to_string(i)),4);
        auto r=Numbers(fields.at("eyeToHead"+std::to_string(i)),9);
        std::copy(p.begin(),p.end(),eyes[i].projection.begin());
        std::copy(r.begin(),r.end(),eyes[i].rotation.begin());
        if(!(p[0]<0&&p[1]>0&&p[2]<0&&p[3]>0))throw std::runtime_error("Invalid OpenVR projection");
    }
    return eyes;
}
static GazeCalibration::Values Calibration(const fs::path& config) {
    const auto fields=ReadSharedConfig(config);
    std::string text;
    for(const auto* key:{"left_deg","right_deg","x_gain","y_gain"})text+=std::string(key)+"="+fields.at(key)+"\n";
    GazeCalibration::Values value;
    if(!GazeCalibration::Parse(text,value))throw std::runtime_error("Invalid calibration");
    return value;
}
static std::array<float,2> Project(const varjo_Ray& ray,const EyeGeometry& eye,double xGain,double yGain,double offset=0) {
    // Varjo gaze rays are head-relative +X right, +Y up, +Z forward.
    const auto& d=ray.forward;
    if(!std::isfinite(d[0])||!std::isfinite(d[1])||!std::isfinite(d[2])||d[2]<.1)
        throw std::runtime_error("Invalid gaze ray");
    const double length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(length<.8||length>1.2)throw std::runtime_error("Invalid direction length");
    const double h[3]={std::tan(std::atan(d[0]/d[2]*xGain)+offset*3.141592653589793/180),d[1]/d[2]*yGain,-1};
    double local[3]{};const auto& r=eye.rotation;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)local[i]+=r[j*3+i]*h[j];
    if(local[2]>=-1e-6)throw std::runtime_error("Eye-space ray behind camera");
    const auto& p=eye.projection;
    const double sx=2*(local[0]/-local[2]-p[0])/(p[1]-p[0])-1;
    const double sy=2*(local[1]/-local[2])/(p[3]-p[2])+(p[2]+p[3])/(p[3]-p[2]);
    if(!std::isfinite(sx)||!std::isfinite(sy))throw std::runtime_error("Invalid projected gaze");
    return {float(std::clamp(sx,-1.,1.)),float(std::clamp(sy,-1.,1.))};
}
static std::map<std::wstring,std::wstring> Args(int argc,wchar_t** argv) {
    std::map<std::wstring,std::wstring> out;
    if((argc-1)%2)throw std::runtime_error("Arguments must be --name value pairs");
    for(int i=1;i+1<argc;i+=2)out[argv[i]]=argv[i+1];return out;
}
int wmain(int argc,wchar_t** argv) {
    varjo_Session* session=nullptr;SOCKET socket=INVALID_SOCKET;bool winsock=false;
    try {
        const auto args=Args(argc,argv);
        const auto eyes=Geometry(args.at(L"--geometry"));
        double xGain=args.count(L"--x-gain")?std::stod(args.at(L"--x-gain")):1;
        double yGain=args.count(L"--y-gain")?std::stod(args.at(L"--y-gain")):1;
        GazeCalibration::Values gain{};
        const fs::path config=args.count(L"--config")?fs::path(args.at(L"--config")):fs::path();
        const fs::path stopFile=args.count(L"--stop-file")?fs::path(args.at(L"--stop-file")):fs::path();
        if(!config.empty()) {gain=Calibration(config);xGain=gain.x_gain;yGain=gain.y_gain;}
        if(!std::isfinite(xGain)||!std::isfinite(yGain)||std::abs(xGain)<.1||std::abs(xGain)>2||
           std::abs(yGain)<.1||std::abs(yGain)>2)throw std::runtime_error("Invalid gains");
        if(args.count(L"--self-test")) {
            varjo_Ray center{};center.forward[2]=1;
            auto a=Project(center,eyes[0],xGain,yGain);
            auto offset=Project(center,eyes[0],xGain,yGain,3);
            if(!(offset[0]>a[0]))throw std::runtime_error("Offset self-test failed");
            center.forward[0]=.2;
            auto b=Project(center,eyes[0],xGain,yGain);
            center.forward[0]=0;center.forward[1]=.2;
            auto c=Project(center,eyes[0],xGain,yGain);
            if(!(b[0]>a[0]&&c[1]>a[1]))throw std::runtime_error("Projection self-test failed");
            std::cout<<"VARJO_BRIDGE_SELF_TEST_PASS\n";return 0;
        }
        const int port=args.count(L"--port")?std::stoi(args.at(L"--port")):5556;
        const int seconds=args.count(L"--seconds")?std::stoi(args.at(L"--seconds")):3600;
        if(port<1||port>65535||seconds<1||seconds>86400)throw std::runtime_error("Invalid port or duration");
        if(!varjo_IsAvailable())throw std::runtime_error("Varjo runtime unavailable");
        session=varjo_SessionInit();if(!session)throw std::runtime_error("Varjo session unavailable");
        if(!varjo_IsGazeAllowed(session))throw std::runtime_error("Varjo Base eye permission disabled");
        varjo_GazeInit(session);
        if(const auto error=varjo_GetError(session);error!=varjo_NoError)
            throw std::runtime_error(std::string("Varjo gaze initialization failed: ")+varjo_GetErrorDesc(error));
        WSADATA wd{};if(WSAStartup(MAKEWORD(2,2),&wd))throw std::runtime_error("Winsock init failed");winsock=true;
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket==INVALID_SOCKET)throw std::runtime_error("UDP socket failed");
        sockaddr_in target{};target.sin_family=AF_INET;target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        target.sin_port=htons(static_cast<u_short>(port));SetConsoleCtrlHandler(Stop,TRUE);
        int64_t lastFrame=-1;uint64_t sequence=0;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        auto nextConfig=std::chrono::steady_clock::now();
        while(running&&std::chrono::steady_clock::now()<end) {
            if(!stopFile.empty()&&fs::exists(stopFile))break;
            const auto now=std::chrono::steady_clock::now();
            if(!config.empty()&&now>=nextConfig) {
                try {gain=Calibration(config);xGain=gain.x_gain;yGain=gain.y_gain;}
                catch(const std::exception&) { /* Preserve the last valid calibration. */ }
                nextConfig=now+std::chrono::milliseconds(250);
            }
            std::array<varjo_Gaze,32> frames{};
            const int count=varjo_GetGazeArray(session,frames.data(),static_cast<int32_t>(frames.size()));
            for(int i=0;i<count;++i) {
                const auto& gaze=frames[i];
                if(gaze.frameNumber<=lastFrame)continue;lastFrame=gaze.frameNumber;
                if(gaze.status!=varjo_GazeStatus_Valid||gaze.leftStatus<varjo_GazeEyeStatus_Compensated||
                   gaze.rightStatus<varjo_GazeEyeStatus_Compensated)continue;
                try {
                    auto l=Project(gaze.leftEye,eyes[0],xGain,yGain,gain.left_deg);
                    auto r=Project(gaze.rightEye,eyes[1],xGain,yGain,gain.right_deg);
                    LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);
                    UdpGazeV1 packet{};packet.sequence=++sequence;packet.qpc=uint64_t(qpc.QuadPart);
                    packet.leftX=l[0];packet.leftY=l[1];packet.rightX=r[0];packet.rightY=r[1];
                    sendto(socket,reinterpret_cast<const char*>(&packet),sizeof(packet),0,
                           reinterpret_cast<const sockaddr*>(&target),sizeof(target));
                } catch(const std::runtime_error&) { /* No stale or invalid gaze is forwarded. */ }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::cout<<"VARJO_BRIDGE_STOP sent="<<sequence<<"\n";
        closesocket(socket);WSACleanup();varjo_SessionShutDown(session);return 0;
    } catch(const std::exception& e) {
        if(socket!=INVALID_SOCKET)closesocket(socket);if(winsock)WSACleanup();
        if(session)varjo_SessionShutDown(session);
        std::cerr<<"VARJO_BRIDGE_ERROR: "<<e.what()<<'\n';return 2;
    }
}
