#include "gaze_parse.h"
#include <cassert>
#include <cmath>

int main() {
  ParsedEye left, right;
  constexpr auto good = R"({"left":{"gaze_direction_is_valid":true,"gaze_direction":[-0.10000,0.20000],"openness":1},"right":{"gaze_direction_is_valid":true,"gaze_direction":[0.05000,-0.10000]}})";
  assert(parse_eye(good, "left", left));
  assert(parse_eye(good, "right", right));
  assert(left.tan_x > 0.10f && left.tan_y > 0.20f);
  assert(right.tan_x < -0.05f && right.tan_y < -0.10f);
  constexpr auto invalid = R"({"left":{"gaze_direction_is_valid":false,"gaze_direction":[0,0]}})";
  assert(!parse_eye(invalid, "left", left));
  constexpr auto malformed = R"({"left":{"gaze_direction_is_valid":true,"gaze_direction":[1e99,0]}})";
  assert(!parse_eye(malformed, "left", left));
  constexpr auto truncated = R"({"left":{"gaze_direction_is_valid":true,"gaze_direction":[0.1,0.2)";
  assert(!parse_eye(truncated, "left", left));
}
