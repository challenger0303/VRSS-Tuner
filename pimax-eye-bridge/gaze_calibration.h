#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

#include "gaze_state.h"

// Experimental correction at the raw eye-component boundary. The Pimax getter
// rotates this ray by the eye pose, and the official VRSS provider rotates its
// returned ray again. Zero/default reproduces the previous deployed behavior.
class GazeCalibration {
 public:
  struct Values {
    float left_deg = 0.0f;
    float right_deg = 0.0f;
    float x_gain = 1.0f;
    float y_gain = 1.0f;
  };

  explicit GazeCalibration(const wchar_t* path =
      L"C:\\ProgramData\\CodexVRSS\\gaze-calibration.txt") : path_(path) {}

  static bool Parse(std::string_view text, Values& result) {
    std::string_view lines[4];
    size_t count = 0;
    while (!text.empty()) {
      if (count == 4) return false;
      const size_t end = text.find('\n');
      auto line = text.substr(0, end);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      lines[count++] = line;
      if (end == std::string_view::npos) break;
      text.remove_prefix(end + 1);
    }
    Values parsed;
    if ((count != 2 && count != 4) ||
        !ParseField(lines[0], "left_deg=", parsed.left_deg) ||
        !ParseField(lines[1], "right_deg=", parsed.right_deg) ||
        std::abs(parsed.left_deg) > 5.0f || std::abs(parsed.right_deg) > 5.0f) return false;
    if (count == 4 &&
        (!ParseField(lines[2], "x_gain=", parsed.x_gain) ||
         !ParseField(lines[3], "y_gain=", parsed.y_gain) ||
         parsed.x_gain < 0.1f || parsed.x_gain > 2.0f ||
         std::abs(parsed.y_gain) < 0.1f || std::abs(parsed.y_gain) > 2.0f)) return false;
    result = parsed;
    return true;
  }

  void Apply(StereoGaze& gaze, GazeClock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (now >= next_read_) {
      next_read_ = now + std::chrono::milliseconds(250);
      values_ = Values{};
      char data[257]{};
      HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file != INVALID_HANDLE_VALUE) {
        DWORD count = 0;
        const bool good = ReadFile(file, data, sizeof(data), &count, nullptr) && count > 0 && count < sizeof(data);
        CloseHandle(file);
        Values parsed;
        if (good && Parse(std::string_view(data, count), parsed)) values_ = parsed;
      }
    }
    gaze.left.tan_x = RotateTangent(gaze.left.tan_x * values_.x_gain, values_.left_deg);
    gaze.right.tan_x = RotateTangent(gaze.right.tan_x * values_.x_gain, values_.right_deg);
    gaze.left.tan_y *= values_.y_gain;
    gaze.right.tan_y *= values_.y_gain;
  }

 private:
  static bool ParseField(std::string_view line, std::string_view key, float& result) {
    if (line.substr(0, key.size()) != key) return false;
    std::string value(line.substr(key.size()));
    if (value.empty()) return false;
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    result = parsed;
    return true;
  }

  static float RotateTangent(float tangent, float degrees) {
    constexpr double kPi = 3.14159265358979323846;
    return static_cast<float>(std::tan(std::atan(static_cast<double>(tangent)) +
                                       static_cast<double>(degrees) * kPi / 180.0));
  }

  std::wstring path_;
  std::mutex mutex_;
  GazeClock::time_point next_read_{};
  Values values_;
};
