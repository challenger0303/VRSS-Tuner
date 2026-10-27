#pragma once

#include <chrono>
#include <mutex>
#include <string_view>

#include "gaze_parse.h"

using GazeClock = std::chrono::steady_clock;

struct StereoGaze {
  ParsedEye left;
  ParsedEye right;
  double max_age_seconds = 0.0;
  bool monocular = false;
};

// BrokenEye reports validity independently. If only one eye is tracked, use
// its current ray for both eye inputs; per-eye calibration is applied later.
// Never reuse the last ray of a closed eye or invent gaze when both are lost.
class GazeState {
 public:
  void publish_frame(std::string_view json, GazeClock::time_point received_at) {
    ParsedEye left, right;
    const bool left_valid = parse_eye(json, "left", left);
    const bool right_valid = parse_eye(json, "right", right);
    std::lock_guard<std::mutex> lock(mutex_);
    left_ = left_valid ? Eye{left, received_at, true} : Eye{};
    right_ = right_valid ? Eye{right, received_at, true} : Eye{};
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    left_ = {};
    right_ = {};
  }

  bool read_fresh(GazeClock::time_point now, StereoGaze& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const double left_age = std::chrono::duration<double>(now - left_.received_at).count();
    const double right_age = std::chrono::duration<double>(now - right_.received_at).count();
    const bool left_fresh = left_.valid && left_age >= 0.0 && left_age <= kMaxAgeSeconds;
    const bool right_fresh = right_.valid && right_age >= 0.0 && right_age <= kMaxAgeSeconds;
    if (!left_fresh && !right_fresh) return false;
    out = {left_fresh ? left_.gaze : right_.gaze,
           right_fresh ? right_.gaze : left_.gaze,
           left_fresh && right_fresh ? (left_age > right_age ? left_age : right_age)
                                     : (left_fresh ? left_age : right_age),
           left_fresh != right_fresh};
    return true;
  }

 private:
  struct Eye {
    ParsedEye gaze;
    GazeClock::time_point received_at{};
    bool valid = false;
  };
  static constexpr double kMaxAgeSeconds = 0.150;
  mutable std::mutex mutex_;
  Eye left_;
  Eye right_;
};
