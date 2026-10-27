#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <stdexcept>
#include "gaze_state.h"
#pragma pack(push,1)
struct UdpGazeV1 {
    uint32_t magic,version;
    uint64_t sequence,qpc;
    float leftX,leftY,rightX,rightY;
    uint32_t validMask;
};
#pragma pack(pop)
static_assert(sizeof(UdpGazeV1)==44);
class Input {
    GazeState state_; std::mutex mutex_; UdpGazeV1 udp_{};
    std::thread reader_; std::atomic<bool> stop_{false};
    std::atomic<uint64_t> sequence_{0};
    std::atomic<bool>& running;
    bool udpMode_; int port_;
    SOCKET udpSocket_{INVALID_SOCKET};
    bool Connect(SOCKET socket,const sockaddr_in& address) {
        u_long nonblocking=1;
        if(ioctlsocket(socket,FIONBIO,&nonblocking))return false;
        int result=connect(socket,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
        bool connected=result==0;
        if(!connected && WSAGetLastError()==WSAEWOULDBLOCK) {
            fd_set writable,failed;FD_ZERO(&writable);FD_ZERO(&failed);
            FD_SET(socket,&writable);FD_SET(socket,&failed);timeval timeout{0,250000};
            if(select(0,nullptr,&writable,&failed,&timeout)>0 && FD_ISSET(socket,&writable)) {
                int error=0,length=sizeof(error);
                connected=getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)==0 && !error;
            }
        }
        nonblocking=0;
        return connected && !stop_ && running && !ioctlsocket(socket,FIONBIO,&nonblocking);
    }
    bool Exact(SOCKET socket,char* data,size_t size) {
        while(size && running && !stop_) { int n=recv(socket,data,int(size),0);if(n<=0)return false;data+=n;size-=n; }
        return size==0;
    }
    void Tcp() {
        while(!stop_ && running) {
            SOCKET socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(socket==INVALID_SOCKET) break;
            DWORD timeout=250;setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<u_short>(port_));
            address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            bool connected=Connect(socket,address);
            const char request=0;
            if(connected) connected=send(socket,&request,1,0)==1;
            while(connected && !stop_ && running) {
                char header[5];if(!Exact(socket,header,5)||header[0]!=0)break;
                uint32_t length=0;std::memcpy(&length,header+1,4);
                if(!length||length>4096)break;
                std::string body(length,'\0');if(!Exact(socket,body.data(),length))break;
                state_.publish_frame(body,GazeClock::now());++sequence_;
            }
            closesocket(socket);state_.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    void Udp() {
        SOCKET socket=udpSocket_;
        while(!stop_ && running) {
            UdpGazeV1 packet{};
            int n=recv(socket,(char*)&packet,sizeof(packet),0);
            if(n!=sizeof(packet)||packet.magic!=0x3147564e||packet.version!=1||packet.validMask!=3)continue;
            bool good=true;
            for(float f:{packet.leftX,packet.leftY,packet.rightX,packet.rightY}) if(!std::isfinite(f)||std::abs(f)>1)good=false;
            if(!good)continue;
            LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
            if(!packet.sequence || !packet.qpc || packet.qpc>uint64_t(now.QuadPart) ||
               double(uint64_t(now.QuadPart)-packet.qpc)/frequency.QuadPart>.15)continue;
            std::lock_guard<std::mutex> lock(mutex_);
            // A source restart resets its sequence number. The host clock is
            // monotonic across sources; reject old timestamps, not new epochs.
            if(packet.qpc>udp_.qpc) {udp_=packet;++sequence_;}
        }
    }
public:
    Input(bool udpMode,int port,std::atomic<bool>& ownerRunning):running(ownerRunning),udpMode_(udpMode),port_(port) {
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("WSAStartup failed");
        if(udpMode_) {
            udpSocket_=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
            DWORD timeout=100;BOOL exclusive=TRUE;
            if(udpSocket_!=INVALID_SOCKET) {
                setsockopt(udpSocket_,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
                setsockopt(udpSocket_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(char*)&exclusive,sizeof(exclusive));
            }
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<u_short>(port_));
            address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            if(udpSocket_==INVALID_SOCKET || bind(udpSocket_,(sockaddr*)&address,sizeof(address))) {
                const int error=WSAGetLastError();if(udpSocket_!=INVALID_SOCKET)closesocket(udpSocket_);WSACleanup();
                throw std::runtime_error("Cannot bind normalized gaze UDP port "+std::to_string(port_)+"; socket error="+std::to_string(error));
            }
        }
        reader_=std::thread([this]{if(udpMode_)Udp();else Tcp();});
    }
    ~Input(){stop_=true;if(reader_.joinable())reader_.join();if(udpSocket_!=INVALID_SOCKET)closesocket(udpSocket_);WSACleanup();}
    void ResetUdp() { std::lock_guard<std::mutex> lock(mutex_);udp_={};sequence_=0; }
    bool Read(StereoGaze& raw,std::array<float,4>& normalized,uint64_t& qpc,uint64_t& sequence,double& age) {
        LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        sequence=sequence_;
        if(udpMode_) {
            std::lock_guard<std::mutex> lock(mutex_);
            age=double(now.QuadPart-int64_t(udp_.qpc))/frequency.QuadPart;
            if(!udp_.qpc||age<0||age>.15)return false;
            normalized={udp_.leftX,udp_.leftY,udp_.rightX,udp_.rightY};qpc=udp_.qpc;sequence=sequence_;
            return true;
        }
        if(!state_.read_fresh(GazeClock::now(),raw))return false;
        age=raw.max_age_seconds;qpc=uint64_t(now.QuadPart-int64_t(age*frequency.QuadPart));
        return true;
    }
};
