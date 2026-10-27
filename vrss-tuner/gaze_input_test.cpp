#include "../common-vrss-provider/GazeInput.h"
#include <cassert>
#include <iostream>
#include <memory>
#pragma comment(lib,"ws2_32.lib")
int main() {
    std::atomic<bool> running{true};
    WSADATA data{};assert(!WSAStartup(MAKEWORD(2,2),&data));
    SOCKET sender=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in target{};target.sin_family=AF_INET;target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    target.sin_port=htons(39033);
    Input input(true,39033,running);
    auto send=[&](uint64_t sequence,int future=0) {
        LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        UdpGazeV1 packet{0x3147564e,1,sequence,uint64_t(now.QuadPart+int64_t(future)*frequency.QuadPart),.1f,.2f,.3f,.4f,3};
        assert(sendto(sender,reinterpret_cast<char*>(&packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&target),sizeof(target))==sizeof(packet));
        Sleep(30);
    };
    StereoGaze raw{};std::array<float,4> xy{};uint64_t qpc{},seq{};double age{};
    send(10000);assert(input.Read(raw,xy,qpc,seq,age));const auto first=seq;
    send(1);assert(input.Read(raw,xy,qpc,seq,age)&&seq>first);const auto second=seq;
    send(2,3600);assert(input.Read(raw,xy,qpc,seq,age)&&seq==second);
    send(3);assert(input.Read(raw,xy,qpc,seq,age)&&seq>second);
    Sleep(180);assert(!input.Read(raw,xy,qpc,seq,age));
    // A TCP peer that drip-feeds bytes must not hold input switching hostage.
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    target.sin_port=htons(39034);assert(!bind(listener,reinterpret_cast<sockaddr*>(&target),sizeof(target)));
    assert(!listen(listener,1));std::atomic<bool> feed{true},connected{false};
    std::thread server([&] {
        SOCKET client=accept(listener,nullptr,nullptr);assert(client!=INVALID_SOCKET);
        char request{};assert(recv(client,&request,1,0)==1);
        const char header[5]={0,0,16,0,0};assert(::send(client,header,5,0)==5);
        connected=true;
        while(feed) { char byte=' ';if(::send(client,&byte,1,0)!=1)break;Sleep(20); }
        closesocket(client);
    });
    auto tcp=std::make_unique<Input>(false,39034,running);
    const auto wait=GazeClock::now();
    while(!connected && GazeClock::now()-wait<std::chrono::seconds(2))Sleep(5);
    assert(connected);const auto start=GazeClock::now();tcp.reset();
    assert(GazeClock::now()-start<std::chrono::seconds(1));
    feed=false;server.join();closesocket(listener);closesocket(sender);WSACleanup();
    std::cout<<"GAZE_INPUT_PASS udp_source_restart future_timestamp_rejected stale_rejected tcp_partial_frame_stop_bounded\n";
}
