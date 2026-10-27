#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "private_contract.h"
#include "driver_contract_file.h"
#include "projection.h"
#include "gaze_state.h"
#include "GazeInput.h"
#include "config_io.h"
#include "OpenVrFrameSource.h"
#include "ProviderEntry.h"
#include "../vrss-tuner/BaballoniaRelay.h"
#include <atomic>
#include <cwchar>
#include <filesystem>
#include <map>
#include <memory>
#include <thread>
#include <psapi.h>
#pragma comment(lib,"ws2_32.lib")
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static std::atomic<bool> running{true};
static BOOL WINAPI Control(DWORD) { running=false; return TRUE; }
struct ProviderOwnership {
    HANDLE mutex{};
    ~ProviderOwnership() { if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);} }
};
static bool OtherGazeProvider() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return true;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    bool other=false;
    if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);return true;}
    do {
        if(entry.th32ProcessID!=GetCurrentProcessId() &&
           (_wcsicmp(entry.szExeFile,L"common_vrss_provider.exe")==0 ||
            _wcsicmp(entry.szExeFile,L"vrss_gaze_provider.exe")==0))other=true;
    }while(Process32NextW(snapshot,&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES)other=true;
    CloseHandle(snapshot);return other;
}
static std::map<std::string,std::string> ReadFields(const fs::path& path) {
    return ReadSharedConfig(path);
}
static std::vector<double> Numbers(std::string text,size_t expected) {
    std::replace(text.begin(),text.end(),',',' '); std::istringstream in(text);
    std::vector<double> out; double value;
    while(in>>value) { if(!std::isfinite(value)) throw std::runtime_error("nonfinite config"); out.push_back(value); }
    if(out.size()!=expected) throw std::runtime_error("Wrong geometry field length");
    return out;
}
static Gain ReadGain(const fs::path& path) {
    auto config=ReadFields(path); Gain gain;
    gain.x=std::stod(config.at("x_gain")); gain.y=std::stod(config.at("y_gain"));
    gain.leftDegrees=std::stod(config.at("left_deg"));gain.rightDegrees=std::stod(config.at("right_deg"));
    if(!std::isfinite(gain.x)||!std::isfinite(gain.y)||gain.x<.1||gain.x>2||
       std::abs(gain.y)<.1||std::abs(gain.y)>2||!std::isfinite(gain.leftDegrees)||
       !std::isfinite(gain.rightDegrees)||std::abs(gain.leftDegrees)>5||std::abs(gain.rightDegrees)>5)
        throw std::runtime_error("Invalid input gain config");
    return gain;
}
struct NvidiaSession {
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr; ID3DNvGazeHandler* gaze=nullptr;
    std::optional<driver::Identity> additional;
    const driver::Identity* identity=nullptr;
    UnregisterProvider unregisterFn=nullptr;bool registered=false,initialized=false;
    ~NvidiaSession() {
        if(registered && unregisterFn) std::cout<<"unregister="<<unregisterFn(gaze,context)<<'\n';
        if(gaze)gaze->Release();if(context)context->Release();if(device)device->Release();
        if(initialized)NvAPI_Unload();
    }
};
static void InitializeNvidiaRuntime(NvidiaSession& nv) {
    D3D_FEATURE_LEVEL level{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&nv.device,&level,&nv.context);
    if(FAILED(hr))throw std::runtime_error("D3D initialization failed");
    const int status=NvAPI_Initialize();
    if(status)throw std::runtime_error("NVIDIA initialization failed: "+std::to_string(status));
    nv.initialized=true;
}
static std::optional<driver::Identity> LoadExperimentalContract() {
    wchar_t path[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"VRSS_EXPERIMENTAL_DRIVER_CONTRACT",path,32768);
    if(!length)return std::nullopt;
    if(length>=32768)throw std::runtime_error("Experimental contract path is too long");
    const fs::path filePath(path);
    if(!filePath.is_absolute())throw std::runtime_error("Experimental contract path must be absolute");
    std::ifstream file(filePath,std::ios::binary|std::ios::ate);
    if(!file||file.tellg()<=0||file.tellg()>4096)
        throw std::runtime_error("Experimental contract file is missing or too large");
    file.seekg(0);
    std::string body((std::istreambuf_iterator<char>(file)),{});
    auto parsed=driver::ParseExactContract(body);
    if(!parsed)throw std::runtime_error("Experimental contract format is invalid");
    return parsed;
}
static void InitializeVerifiedDriver(NvidiaSession& nv) {
    InitializeNvidiaRuntime(nv);
    const auto api=HashModule(L"nvapi64.dll"),impl=HashModule(L"nvapi64_impl.dll"),
        umd=HashModule(L"nvwgf2umx.dll");
    nv.identity=driver::Select(api,impl,umd);
    if(!nv.identity) {
        nv.additional=LoadExperimentalContract();
        if(nv.additional) {
            if(!driver::MatchesExactImages(*nv.additional,api,impl,umd))
                throw std::runtime_error("Experimental contract does not match installed NVAPI and UMD images");
            nv.identity=&*nv.additional;
        }
    }
    if(!nv.identity)throw std::runtime_error("Unsupported driver image: private ABI refused");
    std::cout<<"driverTarget="<<nv.identity->version<<'\n';
    std::cout<<"driverContract="<<(nv.additional?"experimental":"audited")<<'\n';
}
static void CreateGazeHandler(NvidiaSession& nv,float horizontalFov,float verticalFov) {
    NV_GAZE_HANDLER_INIT_PARAMS init{};init.version=NV_GAZE_HANDLER_INIT_PARAMS_VER;
    init.GazeDataDeviceId=0;init.GazeDataType=NV_GAZE_DATA_STEREO;
    init.fHorizontalFOV=horizontalFov;init.fVericalFOV=verticalFov;init.ppNvGazeHandler=&nv.gaze;
    const int status=NvAPI_D3D_InitializeNvGazeHandler(nv.device,&init);
    if(status||!nv.gaze)throw std::runtime_error("Gaze handler initialization failed: "+std::to_string(status));
}
static bool ExecutableAddress(uintptr_t address) {
    MEMORY_BASIC_INFORMATION page{};
    if(!VirtualQuery(reinterpret_cast<const void*>(address),&page,sizeof(page)) || page.State!=MEM_COMMIT)
        return false;
    const DWORD protection=page.Protect&0xff;
    return protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
static void AuditDriverWithoutRegistration() {
    NvidiaSession nv;
    InitializeNvidiaRuntime(nv);
    const auto api=HashModule(L"nvapi64.dll"),impl=HashModule(L"nvapi64_impl.dll"),
        umd=HashModule(L"nvwgf2umx.dll");
    std::cout<<"nvapi64Sha256="<<api<<'\n'
             <<"nvapi64ImplSha256="<<impl<<'\n'
             <<"nvwgf2umxSha256="<<umd<<'\n';
    const auto known=driver::Select(api,impl,umd);
    std::cout<<"knownContract="<<(known?known->version:"none")<<'\n';
    // Initialize only the public NVAPI gaze handler. Do not call the private
    // challenge, registration, listener, consumer-query, or submission methods.
    CreateGazeHandler(nv,90.0f,90.0f);
    uintptr_t table=0;
    std::memcpy(&table,nv.gaze,sizeof(table));
    for(const wchar_t* name : {L"nvapi64.dll",L"nvapi64_impl.dll"}) {
        const auto module=GetModuleHandleW(name);
        MODULEINFO image{};
        if(!module||!GetModuleInformation(GetCurrentProcess(),module,&image,sizeof(image)))continue;
        const auto base=reinterpret_cast<uintptr_t>(module);
        constexpr auto bytes=sizeof(uintptr_t)*8;
        if(!base||image.SizeOfImage<bytes||table<base||table-base>image.SizeOfImage-bytes)continue;
        std::array<uintptr_t,8> methods{};
        std::memcpy(methods.data(),reinterpret_cast<const void*>(table),sizeof(methods));
        std::cout<<"vtableModule="<<(std::wcscmp(name,L"nvapi64.dll")==0?"nvapi64.dll":"nvapi64_impl.dll")<<'\n'
                 <<"vtableRva=0x"<<std::hex<<(table-base)<<std::dec<<'\n';
        std::cout<<"methodRvas=";
        bool allInImage=true,allExecutable=true;
        for(size_t i=0;i<methods.size();++i) {
            if(i)std::cout<<',';
            if(methods[i]<base||methods[i]-base>=image.SizeOfImage) {
                std::cout<<"OUTSIDE_IMAGE";allInImage=false;
            } else std::cout<<"0x"<<std::hex<<(methods[i]-base)<<std::dec;
            if(!ExecutableAddress(methods[i]))allExecutable=false;
        }
        std::cout<<"\nmethodsInImage="<<allInImage<<'\n'
                 <<"methodsExecutable="<<allExecutable<<'\n';
        if(!allInImage||!allExecutable)
            throw std::runtime_error("Gaze methods are outside executable NVAPI code");
        std::cout<<"DRIVER_AUDIT_PASS no_private_calls\n";
        return;
    }
    throw std::runtime_error("Gaze vtable is outside loaded NVAPI modules; no contract candidate emitted");
}
static std::array<uintptr_t,8> VerifiedGazeMethods(const NvidiaSession& nv) {
    const auto module=GetModuleHandleW(nv.identity->module.c_str());
    const auto base=reinterpret_cast<uintptr_t>(module);
    MODULEINFO image{};
    uintptr_t table=0;
    if(!nv.gaze || !GetModuleInformation(GetCurrentProcess(),module,&image,sizeof(image)))
        throw std::runtime_error("Cannot verify gaze module bounds");
    std::memcpy(&table,nv.gaze,sizeof(table));
    if(!driver::TableAddressMatches(*nv.identity,base,image.SizeOfImage,table))
        throw std::runtime_error("Gaze vtable address mismatch: private calls refused");
    std::array<uintptr_t,8> methods{};
    std::memcpy(methods.data(),reinterpret_cast<const void*>(table),sizeof(methods));
    if(!driver::MethodsMatch(*nv.identity,base,methods))
        throw std::runtime_error("Gaze vtable method mismatch: private calls refused");
    for(auto address:methods)if(!ExecutableAddress(address))
        throw std::runtime_error("Gaze vtable method is not executable: private calls refused");
    return methods;
}
struct ConsumerQuery {uint32_t version=0x10028,pad=0;uint64_t count=0,a=0,b=0,c=0;};
static_assert(sizeof(ConsumerQuery)==40);
using QueryConsumer=int(__fastcall*)(void*,void*,ConsumerQuery*);
using SetupListener=int(__fastcall*)(void*,void*,HANDLE*);
int RunVrssProvider(int argc,wchar_t** argv,const std::function<bool()>& keepRunning,
                    const std::function<void()>& prepareProfile) {
    std::cout<<std::unitbuf;
    try {
        std::map<std::wstring,std::wstring> args;
        for(int i=1;i+1<argc;i+=2) args[argv[i]]=argv[i+1];
        if(args.count(L"--driver-audit") && args.at(L"--driver-audit")==L"1") {
            AuditDriverWithoutRegistration();return 0;
        }
        if(args.count(L"--driver-check-only") && args.at(L"--driver-check-only")==L"1") {
            NvidiaSession nv;InitializeVerifiedDriver(nv);
            CreateGazeHandler(nv,90.0f,90.0f);
            VerifiedGazeMethods(nv);
            std::cout<<"DRIVER_CHECK_PASS exact images and method table\n";return 0;
        }
        const bool frameInput=args.count(L"--source") && args.at(L"--source")==L"openvr";
        const bool frameSession=args.count(L"--frame-session") && args.at(L"--frame-session")==L"1";
        const auto profile=hmd::ParseProfile(args.count(L"--hmd-profile")?args.at(L"--hmd-profile"):L"standard");
        if(!profile || (!frameInput && *profile!=hmd::Profile::Standard))
            throw std::runtime_error("Invalid HMD profile for this input source");
        if(frameSession && (!frameInput || *profile!=hmd::Profile::SteamFrame || !keepRunning || !prepareProfile))
            throw std::runtime_error("Frame session requires an owned worker and recoverable profile transaction");
        if(frameInput && BusyVr(false))throw std::runtime_error("Close VRChat and Unity before starting the provider");
        if(keepRunning && !keepRunning())return 0;
        OpenVrFrameSource frameSource;
        EyeGeometry eyes[2];
        uint32_t vendor=0,product=0;
        bool unspecifiedIdentity=false;
        if(frameInput) {
            auto identity=frameSource.Open(eyes,*profile);
            if(frameSession) {
                const auto testIdentity=hmd::SelectSessionIdentity(*profile,identity,bool(keepRunning),bool(prepareProfile));
                if(!testIdentity)throw std::runtime_error("Frame registration requires a verified Steam Frame with both EDID properties missing");
                identity=*testIdentity;
                std::cout<<"registrationIdentity=frame-session-key vendor="<<identity.vendor
                         <<" product="<<identity.product<<" hardwareIdentity=0\n";
            }
            vendor=identity.vendor;product=identity.product;unspecifiedIdentity=identity.unspecified;
        }
        else {
            const auto geometry=ReadFields(fs::path(args.at(L"--geometry")));
            for(int eye=0;eye<2;++eye) {
                auto p=Numbers(geometry.at("projection"+std::to_string(eye)),4);
                auto r=Numbers(geometry.at("eyeToHead"+std::to_string(eye)),9);
                std::copy(p.begin(),p.end(),eyes[eye].projection.begin());std::copy(r.begin(),r.end(),eyes[eye].eyeToHead.begin());
                double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
                if(std::abs(det-1)>.01)throw std::runtime_error("Invalid eye rotation");
            }
            vendor=std::stoul(geometry.at("vendor"));product=std::stoul(geometry.at("product"));
        }
        if(!unspecifiedIdentity && (!vendor||!product||vendor>65535||product>65535))throw std::runtime_error("Invalid HMD EDID");
        const fs::path output=args.at(L"--output"),config=args.at(L"--config");
        const bool checkOnly=args.count(L"--check-only") && args.at(L"--check-only")==L"1";
        Gain gain=ReadGain(config);
        if(checkOnly) { std::cout<<"GEOMETRY_CONFIG_PASS vendor="<<vendor<<" product="<<product<<'\n';return 0; }
        if(!hmd::CanRegister({vendor,product,unspecifiedIdentity}))
            throw std::runtime_error("Steam Frame provides no EDID. NVIDIA 596.02 rejects unspecified vendor/product IDs (-5). "
                                     "Registration stopped before NVIDIA initialization; no profile settings changed.");
        if(!AdminEnabled())throw std::runtime_error("Enabled Administrators token required");
        std::wstring event=L"Global\\nfdv8Dup8UqK_JnJG2fkew:"+std::to_wstring(vendor)+L":"+std::to_wstring(product)+L"-evt";
        ProviderOwnership ownership;
        bool retainedConsumerEvent=false;
        if(!frameInput) {
            const auto lockName=L"Global\\VRSS-Tuner-Provider-"+std::to_wstring(vendor)+L"-"+std::to_wstring(product);
            ownership.mutex=CreateMutexW(nullptr,TRUE,lockName.c_str());
            if(!ownership.mutex || GetLastError()==ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Another Tuner provider owns this HMD route");
        }
        if(!EventAbsent(event)) {
            const bool reconnect=!frameInput && args.count(L"--allow-reconnect") && args.at(L"--allow-reconnect")==L"1";
            if(!reconnect || OtherGazeProvider())
                throw std::runtime_error("An existing provider bridge is present or inaccessible");
            HANDLE existing=OpenEventW(SYNCHRONIZE,FALSE,event.c_str());
            if(!existing)throw std::runtime_error("The existing provider event is inaccessible; reconnect refused");
            CloseHandle(existing);
            retainedConsumerEvent=true;
            // A consumer can retain this event after successful unregister.
            // Its existence does not prove a live sender. Keep NVIDIA's actual
            // registration result authoritative; never change the event/ACL.
            std::cout<<"RECONNECT retained consumer event; no other gaze sender is running\n";
        }
        fs::create_directories(output);
        std::ofstream csv;
        if(!frameInput) {
            csv.open(output/"gaze.csv");
            csv<<"qpc,sourceSequence,fresh,ageSeconds,consumerCount,queryStatus,submitted,submitStatus,leftX,leftY,rightX,rightY\n";
        }
        bool osc=args.count(L"--source")&&args.at(L"--source")==L"osc";
        bool udp=osc || (args.count(L"--source")&&args.at(L"--source")==L"udp");
        int port=args.count(L"--port")?std::stoi(args.at(L"--port")):(osc?9020:udp?5556:5555);
        int oscForward=args.count(L"--osc-forward")?std::stoi(args.at(L"--osc-forward")):8888;
        std::string inputRoute;
        if(args.count(L"--input-route"))inputRoute=std::string(args.at(L"--input-route").begin(),args.at(L"--input-route").end());
        else inputRoute=osc?"Baballonia":udp?"":"SRanibro";
        std::unique_ptr<Input> input;
        std::unique_ptr<babble::Relay> oscInput;
        auto configureInput=[&](const std::string& route,int nextPort,int forward) {
            const bool nextOsc=route=="Baballonia";
            const bool nextUdp=nextOsc || route!="SRanibro";
            if(route!="Baballonia" && route!="SRanibro" && route!="PSVR2Toolkit" && route!="ViveProEye" &&
               route!="ViveStreaming" && route!="SteamVREye" && route!="Varjo" && !route.empty())
                throw std::runtime_error("Unknown gaze input route");
            if(nextPort<1 || nextPort>65535 || forward<0 || forward>65535 ||
               (nextOsc && (forward==nextPort || nextPort==5556 || forward==5556)))
                throw std::runtime_error("Invalid gaze input ports");
            if(nextOsc) {
                if(oscInput && nextPort==port)oscInput->SetForwardPort(forward);
                else {
                    auto replacement=std::make_unique<babble::Relay>();
                    if(!replacement->Start(nextPort,float(gain.x),float(gain.y),float(gain.leftDegrees),float(gain.rightDegrees),
                                           []{return false;},5556,forward))
                        throw std::runtime_error("Cannot bind Baballonia OSC receive port "+std::to_string(nextPort)+
                            "; socket error="+std::to_string(replacement->SocketError())+". Use a free port and direct Baballonia output to it.");
                    oscInput=std::move(replacement);
                }
                input.reset();
            } else {
                if(input && !osc && udp==nextUdp && port==nextPort) {
                    if(nextUdp && route!=inputRoute)input->ResetUdp();
                } else {
                    auto replacement=std::make_unique<Input>(nextUdp,nextPort,running);
                    input=std::move(replacement);
                }
                oscInput.reset();
            }
            osc=nextOsc;udp=nextUdp;port=nextPort;oscForward=forward;inputRoute=route;
        };
        if(!frameInput)configureInput(inputRoute,port,oscForward);
        PrintToken();NvidiaSession nv;InitializeVerifiedDriver(nv);
        if(frameSession && nv.identity!=&driver::k59602)
            throw std::runtime_error("EDID-free Steam Frame sessions are verified only on NVIDIA 596.02");
        const auto& p=eyes[0].projection;
        CreateGazeHandler(nv,float((std::atan(-p[0])+std::atan(p[1]))*180/3.141592653589793),
            float((std::atan(-p[2])+std::atan(p[3]))*180/3.141592653589793));
        const auto table=VerifiedGazeMethods(nv);
        if(keepRunning && !keepRunning())return 0;
        if(frameInput && BusyVr(false))throw std::runtime_error("VRChat or Unity started during preparation; registration refused");
        Challenge challenge{};auto get=reinterpret_cast<GetChallenge>(table[3]);
        int status=get(nv.gaze,nv.context,&challenge);if(status)throw std::runtime_error("Challenge failed: "+std::to_string(status));
        Registration request{};request.vendor=vendor;request.product=product;
        for(int i=0;i<16;++i)request.response[i]=challenge.bytes[i]^kMask[i];
        nv.unregisterFn=reinterpret_cast<UnregisterProvider>(table[7]);
        status=reinterpret_cast<RegisterProvider>(table[4])(nv.gaze,nv.context,&request);
        std::cout<<"register="<<status<<" vendor="<<vendor<<" product="<<product<<'\n';
        if(status) {
            if(status==-175 && retainedConsumerEvent)
                throw std::runtime_error("NVIDIA rejected reconnect while VRChat retains the previous connection. The provider is fully stopped. Close VRChat normally, start the gaze path, then launch VRChat again.");
            throw std::runtime_error("Provider registration failed: "+std::to_string(status));
        }
        nv.registered=true;
        HANDLE listener=nullptr;
        status=reinterpret_cast<SetupListener>(table[6])(nv.gaze,nv.context,&listener);
        std::cout<<"listener="<<status<<" handlePresent="<<(listener!=nullptr)<<'\n';
        if(status||!listener)throw std::runtime_error("Consumer listener setup failed");
        if(prepareProfile && (!keepRunning || keepRunning()))prepareProfile();
        auto query=reinterpret_cast<QueryConsumer>(table[5]);
        SetConsoleCtrlHandler(Control,TRUE);
        const double seconds=args.count(L"--seconds")?std::stod(args.at(L"--seconds")):1200;
        if(seconds<=0||seconds>86400)throw std::runtime_error("Duration must be 1..86400 seconds");
        const auto started=Clock::now();auto next=started,nextStatus=started,nextLog=started,nextConfig=started;
        uint64_t sent=0,freshCount=0,activeCount=0,failed=0,lastSequence=0,lastQpc=0;
        const bool ownerLifetime=frameInput && bool(keepRunning);
        while(running && (!keepRunning || keepRunning()) &&
              (ownerLifetime || std::chrono::duration<double>(Clock::now()-started).count()<seconds)) {
            if(fs::exists(output/"stop.request"))break;
            auto now=Clock::now();
            if(now>=nextConfig) {
                try { gain=ReadGain(config); }
                catch(const std::exception& e) { std::cerr<<"CONFIG_RELOAD_REJECTED retaining_last_good: "<<e.what()<<'\n'; }
                nextConfig=now+std::chrono::milliseconds(250);
                if(oscInput)oscInput->SetCalibration(float(gain.x),float(gain.y),float(gain.leftDegrees),float(gain.rightDegrees));
                if(!frameInput && fs::exists(output/"input-source.txt")) {
                    try {
                        const auto request=ReadFields(output/"input-source.txt");
                        const auto& route=request.at("route");
                        const int nextPort=std::stoi(request.at("port")),forward=std::stoi(request.at("forward"));
                        if(route!=inputRoute || nextPort!=port || forward!=oscForward) {
                            configureInput(route,nextPort,forward);lastSequence=lastQpc=0;
                        }
                        std::error_code ignored;fs::remove(output/"input-error.txt",ignored);
                    } catch(const std::exception& error) {
                        std::ofstream(output/"input-error.txt")<<error.what();
                    }
                }
            }
            ConsumerQuery consumer{};const int queried=query(nv.gaze,nv.context,&consumer);
            StereoGaze raw{};std::array<float,4> location{};uint64_t qpc=0,seq=0;double age=-1;
            bool fresh=false;
            if(frameInput) {
                fresh=frameSource.Read(gain,location);
                if(fresh) {
                    LARGE_INTEGER sampleTime{};QueryPerformanceCounter(&sampleTime);
                    qpc=uint64_t(sampleTime.QuadPart);seq=lastSequence+1;age=0;
                }
            } else if(oscInput) {
                babble::UdpGazeV1 packet{};fresh=oscInput->Read(packet,age);
                qpc=packet.qpc;seq=packet.sequence;
                if(fresh)location={packet.leftX,packet.leftY,packet.rightX,packet.rightY};
            } else fresh=input->Read(raw,location,qpc,seq,age);
            if(fresh&&!udp&&!frameInput) {
                auto left=ProjectHeadTangent(raw.left.tan_x,raw.left.tan_y,eyes[0],gain,0);
                auto right=ProjectHeadTangent(raw.right.tan_x,raw.right.tan_y,eyes[1],gain,1);
                location={left[0],left[1],right[0],right[1]};
            }
            if(fresh)++freshCount;
            if(consumer.count)++activeCount;
            bool submitted=false;int updated=0;
            if(frame::ShouldSubmit(fresh,queried,consumer.count,seq,lastSequence,qpc,lastQpc)) {
                NV_FOVEATED_RENDERING_UPDATE_GAZE_DATA_PARAMS sample{};
                sample.version=NV_FOVEATED_RENDERING_UPDATE_GAZE_DATA_PARAMS_VER;sample.Timestamp=qpc;
                auto& l=sample.sStereoData.sLeftEye;auto& r=sample.sStereoData.sRightEye;
                l.version=r.version=NV_FOVEATED_RENDERING_GAZE_DATA_PER_EYE_VER;
                l.GazeDataValidityFlags=r.GazeDataValidityFlags=NV_GAZE_LOCATION_VALID;
                l.fGazeNormalizedLocation[0]=location[0];l.fGazeNormalizedLocation[1]=location[1];
                r.fGazeNormalizedLocation[0]=location[2];r.fGazeNormalizedLocation[1]=location[3];
                updated=nv.gaze->UpdateGazeData(nv.context,&sample);submitted=true;++sent;
                if(updated)++failed;lastSequence=seq;lastQpc=qpc;
            }
            if(csv.is_open()) {
                LARGE_INTEGER host{};QueryPerformanceCounter(&host);
                csv<<host.QuadPart<<','<<seq<<','<<fresh<<','<<std::setprecision(9)<<age<<','<<consumer.count<<','<<queried<<','<<submitted<<','<<updated;
                for(float f:location)csv<<','<<f;csv<<'\n';
            }
            if(now>=nextStatus) {
                std::ofstream state(output/"status.tmp");
                state<<"{\"pid\":"<<GetCurrentProcessId()<<",\"registered\":true,\"fresh\":"<<(fresh?"true":"false")
                     <<",\"consumerCount\":"<<consumer.count<<",\"queryStatus\":"<<queried<<",\"sent\":"<<sent
                     <<",\"submitFailures\":"<<failed<<",\"sourceSequence\":"<<seq
                     <<",\"identityMode\":\""<<(frameSession?"frame-experimental":"edid")
                     <<"\",\"deliveryState\":\""<<frame::DeliveryState(fresh,queried,consumer.count,updated)<<"\",\"locations\":[";
                for(int i=0;i<4;++i)state<<(i?",":"")<<location[i];
                state<<"],\"xGain\":"<<gain.x<<",\"yGain\":"<<gain.y<<",\"source\":\""<<(frameInput?"steam-frame-openvr":osc?"baballonia-osc":udp?"normalized-udp":"brokeneye-tcp")<<"\"";
                if(oscInput)state<<",\"oscDatagrams\":"<<oscInput->Datagrams()<<",\"oscReadings\":"<<oscInput->Received()<<",\"oscPort\":"<<port;
                if(!frameInput)state<<",\"inputRoute\":\""<<inputRoute<<"\",\"inputPort\":"<<port<<",\"oscForward\":"<<oscForward;
                state<<"}";
                state.close();MoveFileExW((output/"status.tmp").c_str(),(output/"status.json").c_str(),MOVEFILE_REPLACE_EXISTING);
                nextStatus=now+std::chrono::milliseconds(50);
            }
            if(now>=nextLog) {
                if(csv.is_open())csv.flush();
                std::cout<<"fresh="<<fresh<<" consumer="<<consumer.count<<" sent="<<sent<<" failures="<<failed<<'\n';
                nextLog=now+std::chrono::seconds(1);
            }
            next+=std::chrono::milliseconds(10);if(next<Clock::now())next=Clock::now();std::this_thread::sleep_until(next);
        }
        std::cout<<"STOP sent="<<sent<<" freshPolls="<<freshCount<<" consumerPolls="<<activeCount<<" failures="<<failed<<'\n';
        const int unregistered=nv.unregisterFn(nv.gaze,nv.context);if(!unregistered)nv.registered=false;
        std::cout<<"unregister="<<unregistered<<'\n';
        return failed?12:unregistered?13:0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n';return 1; }
}
#ifndef VRSS_PROVIDER_EMBEDDED
int wmain(int argc,wchar_t** argv) { return RunVrssProvider(argc,argv); }
#endif
