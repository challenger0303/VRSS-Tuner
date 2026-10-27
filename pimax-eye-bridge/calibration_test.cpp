#include "gaze_calibration.h"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
void write_file(const wchar_t* path, const char* text) {
  FILE* file = nullptr;
  assert(_wfopen_s(&file, path, L"wb") == 0 && file);
  assert(std::fputs(text, file) >= 0);
  assert(std::fclose(file) == 0);
}
bool AlmostEqual(float a, double b) { return std::abs(a-b) < 1e-5; }
}
int main() {
  GazeCalibration::Values values;
  assert(GazeCalibration::Parse("left_deg=-2\nright_deg=2\n", values));
  assert(values.left_deg == -2 && values.right_deg == 2 && values.x_gain == 1 && values.y_gain == 1);
  assert(GazeCalibration::Parse("left_deg=-2\r\nright_deg=2\r\nx_gain=0.5\r\ny_gain=-1\r\n", values));
  assert(values.x_gain == .5f && values.y_gain == -1);
  for (const char* bad : {"left_deg=-20\nright_deg=2\n", "left_deg=nan\nright_deg=2\n",
       "left_deg=0\nright_deg=0\nx_gain=0.5\n", "left_deg=0\nright_deg=0\nx_gain=0\ny_gain=-1\n",
       "left_deg=0\nright_deg=0\nx_gain=3\ny_gain=-1\n", "left_deg=0\nright_deg=0\nx_gain=1\ny_gain=0\n",
       "left_deg=0\nright_deg=0\nx_gain=1\ny_gain=inf\n", "left_deg=0\nright_deg=0\nunknown=1\ny_gain=-1\n",
       "left_deg=0\nright_deg=0\nx_gain=1\ny_gain=-1\nextra"}) {
    auto before = values;
    assert(!GazeCalibration::Parse(bad, values));
    assert(values.x_gain == before.x_gain && values.y_gain == before.y_gain);
  }
  wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
  assert(GetTempPathW(MAX_PATH, directory) > 0);
  assert(GetTempFileNameW(directory, L"vrs", 0, path));
  assert(DeleteFileW(path));
  GazeCalibration calibration(path);
  const auto start = GazeClock::now();
  StereoGaze original{{.4f, .2f}, {-.2f, -.3f}, .05};
  auto sample = original;
  calibration.Apply(sample, start);
  assert(AlmostEqual(sample.left.tan_x, .4) && AlmostEqual(sample.right.tan_y, -.3));
  write_file(path, "left_deg=-2\nright_deg=2\nx_gain=0.5\ny_gain=-1\n");
  sample = original;
  calibration.Apply(sample, start + std::chrono::seconds(1));
  constexpr double rad = 3.14159265358979323846 / 180;
  assert(AlmostEqual(sample.left.tan_x, std::tan(std::atan(.2) - 2*rad)));
  assert(AlmostEqual(sample.right.tan_x, std::tan(std::atan(-.1) + 2*rad)));
  assert(AlmostEqual(sample.left.tan_y, -.2) && AlmostEqual(sample.right.tan_y, .3));
  assert(sample.max_age_seconds == original.max_age_seconds);
  write_file(path, "left_deg=-2\nright_deg=2\nx_gain=2\ny_gain=1\n");
  sample = original;
  calibration.Apply(sample, start + std::chrono::milliseconds(1100));
  assert(AlmostEqual(sample.left.tan_y, -.2));
  sample = original;
  calibration.Apply(sample, start + std::chrono::seconds(2));
  assert(AlmostEqual(sample.left.tan_x, std::tan(std::atan(.8) - 2*rad)) && AlmostEqual(sample.left.tan_y, .2));
  write_file(path, "partial write");
  sample = original;
  calibration.Apply(sample, start + std::chrono::seconds(3));
  assert(AlmostEqual(sample.left.tan_x, .4) && AlmostEqual(sample.right.tan_y, -.3));
  assert(DeleteFileW(path));
  std::puts("CALIBRATION_GAINS_RELOAD_AND_LEGACY_TEST_PASS");
}
