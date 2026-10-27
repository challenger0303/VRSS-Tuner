#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

// Layout documented by VirtualDesktop.VRCFaceTracking (MIT): FaceState.cs.
// This observer only maps the shared state as read-only. It does not wait on or
// reset VirtualDesktop.BodyStateEvent, which another consumer may depend on.
struct Quaternion { float x, y, z, w; };
struct Pose { Quaternion orientation; float position[3]; };
struct FaceState {
    uint8_t faceFlags, eyeFollowingValid;
    float expressionWeights[70], expressionConfidences[2];
    uint8_t leftValid, rightValid;
    Pose left, right;
    float leftConfidence, rightConfidence;
};
static_assert(sizeof(FaceState) == 360);
static_assert(offsetof(FaceState, leftValid) == 292);
static_assert(offsetof(FaceState, left) == 296);

static bool FiniteUnit(const Quaternion& q) {
    const float a[] = {q.x, q.y, q.z, q.w};
    float n = 0;
    for (float v : a) { if (!std::isfinite(v)) return false; n += v * v; }
    return n > 0.8f && n < 1.2f;
}

int main() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"VirtualDesktop.BodyState");
    if (!mapping) {
        std::cout << "VD_SHARED_STATE_UNAVAILABLE error=" << GetLastError() << '\n';
        return 2;
    }
    const auto* view = static_cast<const FaceState*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(FaceState)));
    if (!view) {
        std::cout << "VD_SHARED_VIEW_UNAVAILABLE error=" << GetLastError() << '\n';
        CloseHandle(mapping);
        return 3;
    }
    std::cout << "sample,left_valid,right_valid,left_confidence,right_confidence,left_qx,left_qy,left_qz,left_qw,right_qx,right_qy,right_qz,right_qw,finite_unit\n";
    for (int sample = 0; sample < 50; ++sample) {
        FaceState state{};
        std::memcpy(&state, view, sizeof(state));
        const bool valid = FiniteUnit(state.left.orientation) && FiniteUnit(state.right.orientation);
        const auto& l = state.left.orientation;
        const auto& r = state.right.orientation;
        std::cout << std::fixed << std::setprecision(6) << sample << ','
            << int(state.leftValid) << ',' << int(state.rightValid) << ','
            << state.leftConfidence << ',' << state.rightConfidence << ','
            << l.x << ',' << l.y << ',' << l.z << ',' << l.w << ','
            << r.x << ',' << r.y << ',' << r.z << ',' << r.w << ',' << int(valid) << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return 0;
}
