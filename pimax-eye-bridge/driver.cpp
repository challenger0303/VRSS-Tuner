#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <pvr_driver.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "gaze_state.h"
#include "gaze_calibration.h"

#ifndef SRANIBRO_BRIDGE_PORT
#define SRANIBRO_BRIDGE_PORT 5555
#endif
static_assert(SRANIBRO_BRIDGE_PORT > 0 && SRANIBRO_BRIDGE_PORT <= 65535);

namespace {
using namespace pvr::driver;
using Clock = GazeClock;

bool receive_exact(SOCKET socket, char* destination, size_t length) {
  while (length) {
    const int received = recv(socket, destination, static_cast<int>(length), 0);
    if (received <= 0) return false;
    destination += received;
    length -= static_cast<size_t>(received);
  }
  return true;
}

bool xr5_platform_runtime_present() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return true;  // Fail closed.
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) {
    CloseHandle(snapshot);
    return true;  // Fail closed if process enumeration is unavailable.
  }
  bool found = false;
  do {
    if (_wcsicmp(entry.szExeFile, L"platform_runtime_XR5EYECHIP_WIN10_x64.exe") == 0) {
      found = true;
      break;
    }
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);
  return found;
}

class Driver;

class EyeDevice final : public IPVRDevice, public IPVREyeTrackingComponent {
 public:
  EyeDevice(IPVRDriverContext* context, GazeState* state, Driver* owner)
      : context_(context), state_(state), owner_(owner) {}

  void* getComponent(const char* name) override {
    if (name && std::strcmp(name, IPVREyeTrackingComponent_Version) == 0)
      return static_cast<IPVREyeTrackingComponent*>(this);
    return nullptr;
  }
  void destroy() override;

  pvrEyeTrackingInfo getEyeTrackingInfoAtTime(double) override {
    pvrEyeTrackingInfo result{};
    StereoGaze sample;
    if (!state_->read_fresh(Clock::now(), sample)) return result;
    calibration_.Apply(sample, Clock::now());
    result.GazeTan[pvrEye_Left].x = sample.left.tan_x;
    result.GazeTan[pvrEye_Left].y = sample.left.tan_y;
    result.GazeTan[pvrEye_Right].x = sample.right.tan_x;
    result.GazeTan[pvrEye_Right].y = sample.right.tan_y;
    result.TimeInSeconds = context_->getTimeSeconds() - sample.max_age_seconds;
    return result;
  }

 private:
  IPVRDriverContext* context_;
  GazeState* state_;
  Driver* owner_;
  GazeCalibration calibration_;
};

class Driver final : public IPVRDriver {
 public:
  bool init(IPVRDriverContext* context) override {
    std::lock_guard<std::recursive_mutex> lock(device_mutex_);
    if (!context || device_) return false;
    context_ = context;
    running_ = true;
    worker_ = std::thread([this] { read_loop(); });
    context_->log(pvrInfo, "SRanibro eye bridge initialized; waiting for fresh gaze with XR5 runtime absent");
    return true;
  }

  void cleanUp() override {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::recursive_mutex> lock(device_mutex_);
    release_device();
    context_ = nullptr;
  }
  void enterStandby() override {}
  void leaveStandby() override {}
  void update() override {
    std::lock_guard<std::recursive_mutex> lock(device_mutex_);
    if (!context_) return;
    const auto now = Clock::now();
    if (now - last_runtime_check_ > std::chrono::milliseconds(250)) {
      runtime_present_ = xr5_platform_runtime_present();
      last_runtime_check_ = now;
    }
    StereoGaze sample;
    const bool fresh = state_.read_fresh(now, sample);
    if (runtime_present_) {
      release_device();
      return;
    }
    if (fresh) last_fresh_at_ = now;
    if (!fresh) {
      if (device_ && now - last_fresh_at_ > std::chrono::seconds(2)) release_device();
      return;
    }
    if (device_ || now - last_add_attempt_ < std::chrono::seconds(1)) return;
    last_add_attempt_ = now;
    device_ = new EyeDevice(context_, &state_, this);
    context_->addDevice(device_, "sranibro_vrss_eye_01", PVRDeviceClass_EyeTracking);
    if (!device_) {
      context_->log(pvrInfo, "SRanibro eye bridge: existing PVR eye device retained priority");
    } else {
      context_->log(pvrInfo, "SRanibro eye bridge: PVR eye device added during CUSTOM gaze");
    }
  }

  void on_device_destroyed(EyeDevice* device) {
    std::lock_guard<std::recursive_mutex> lock(device_mutex_);
    if (device_ == device) device_ = nullptr;
  }

 private:
  void release_device() {
    std::lock_guard<std::recursive_mutex> lock(device_mutex_);
    if (!device_) return;
    EyeDevice* device = device_;
    device_ = nullptr;
    if (context_) context_->removeDevice(device);
  }

  void read_loop() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    while (running_) {
      SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (socket == INVALID_SOCKET) break;
      DWORD timeout_ms = 250;
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_port = htons(SRANIBRO_BRIDGE_PORT);
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      bool connected = connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
      if (connected) {
        const char request = 0;
        connected = send(socket, &request, 1, 0) == 1;
      }
      while (running_ && connected) {
        char header[5];
        if (!receive_exact(socket, header, sizeof(header)) || header[0] != 0) break;
        const auto* bytes = reinterpret_cast<const unsigned char*>(header + 1);
        const std::uint32_t size = static_cast<std::uint32_t>(bytes[0]) |
                                   static_cast<std::uint32_t>(bytes[1]) << 8 |
                                   static_cast<std::uint32_t>(bytes[2]) << 16 |
                                   static_cast<std::uint32_t>(bytes[3]) << 24;
        if (size == 0 || size > 4096) break;
        std::string json(size, '\0');
        if (!receive_exact(socket, json.data(), size)) break;
        state_.publish_frame(json, Clock::now());
      }
      closesocket(socket);
      state_.clear();
      if (running_) std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    WSACleanup();
  }

  IPVRDriverContext* context_ = nullptr;
  GazeState state_;
  std::recursive_mutex device_mutex_;
  EyeDevice* device_ = nullptr;
  std::atomic<bool> running_{false};
  std::thread worker_;
  Clock::time_point last_runtime_check_{};
  Clock::time_point last_add_attempt_{};
  Clock::time_point last_fresh_at_{};
  bool runtime_present_ = true;
};

void EyeDevice::destroy() {
  owner_->on_device_destroyed(this);
  delete this;
}

Driver driver;
}  // namespace

DECLAR_PVRDRIVER_INTERFACE_VERSIONS();
PVRDRIVER_CREATE_ENTRY(interface_name) {
  if (interface_name && std::strcmp(interface_name, pvr::driver::IPVRDriver_Version) == 0)
    return &driver;
  return nullptr;
}
