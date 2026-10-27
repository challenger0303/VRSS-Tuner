#include "gaze_state.h"
#include <cassert>
#include <chrono>

int main() {
  using namespace std::chrono_literals;
  const auto t0 = GazeClock::time_point{} + 1s;
  constexpr auto left_only = R"({"left":{"gaze_direction_is_valid":true,"gaze_direction":[-0.10,0.20]},"right":{"gaze_direction_is_valid":false,"gaze_direction":[0,0]}})";
  constexpr auto right_only = R"({"left":{"gaze_direction_is_valid":false,"gaze_direction":[0,0]},"right":{"gaze_direction_is_valid":true,"gaze_direction":[0.05,-0.10]}})";
  constexpr auto both = R"({"left":{"gaze_direction_is_valid":true,"gaze_direction":[-0.10,0.20]},"right":{"gaze_direction_is_valid":true,"gaze_direction":[0.05,-0.10]}})";
  constexpr auto neither = R"({"left":{"gaze_direction_is_valid":false,"gaze_direction":[0,0]},"right":{"gaze_direction_is_valid":false,"gaze_direction":[0,0]}})";
  GazeState state;
  StereoGaze out;
  state.publish_frame(left_only, t0);
  assert(state.read_fresh(t0 + 10ms, out));
  assert(out.monocular && out.left.tan_x == out.right.tan_x && out.left.tan_y == out.right.tan_y);
  state.publish_frame(right_only, t0 + 50ms);
  assert(state.read_fresh(t0 + 60ms, out));
  assert(out.monocular && out.left.tan_x == out.right.tan_x && out.right.tan_x < -0.05f);
  assert(out.max_age_seconds > 0.009 && out.max_age_seconds < 0.011);
  state.publish_frame(both, t0 + 70ms);
  assert(state.read_fresh(t0 + 80ms, out));
  assert(!out.monocular && out.left.tan_x > 0.10f && out.right.tan_x < -0.05f);
  state.publish_frame(left_only, t0 + 90ms);
  assert(state.read_fresh(t0 + 100ms, out) && out.monocular);
  state.publish_frame(neither, t0 + 110ms);
  assert(!state.read_fresh(t0 + 111ms, out));
  state.publish_frame(left_only, t0 + 170ms);
  assert(state.read_fresh(t0 + 180ms, out));
  assert(!state.read_fresh(t0 + 321ms, out));
  state.clear();
  assert(!state.read_fresh(t0 + 180ms, out));
}
