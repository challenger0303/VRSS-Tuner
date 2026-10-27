#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

struct ParsedEye {
  float tan_x = 0.0f;
  float tan_y = 0.0f;
};

inline bool parse_eye(std::string_view json, std::string_view name, ParsedEye& out) {
  const std::string key = std::string("\"") + std::string(name) + "\":";
  const auto begin = json.find(key);
  if (begin == std::string_view::npos) return false;
  const auto object_start = json.find('{', begin + key.size());
  const auto object_end = json.find('}', object_start);
  if (object_start == std::string_view::npos || object_end == std::string_view::npos) return false;
  const auto eye = json.substr(object_start, object_end - object_start + 1);
  const auto valid = eye.find("\"gaze_direction_is_valid\":true");
  if (valid == std::string_view::npos) return false;
  const auto field = eye.find("\"gaze_direction\":[");
  if (field == std::string_view::npos) return false;
  const auto array_start = field + sizeof("\"gaze_direction\":[") - 1;
  const std::string tail(eye.substr(array_start));
  char* end = nullptr;
  const float x = std::strtof(tail.c_str(), &end);
  if (end == tail.c_str() || *end != ',') return false;
  const float y = std::strtof(end + 1, &end);
  if (*end != ']' || !std::isfinite(x) || !std::isfinite(y)) return false;
  if (std::abs(x) > 0.95f || std::abs(y) > 0.95f) return false;
  const float z2 = 1.0f - x * x - y * y;
  if (z2 < 0.05f) return false;
  const float z = std::sqrt(z2);
  // SRanibro's BrokenEye JSON negates source X once. Restore source X here.
  out.tan_x = -x / z;
  out.tan_y = y / z;
  return true;
}
