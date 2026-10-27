#pragma once

#include <array>
#include <chrono>
#include <cmath>

#ifdef VRSS_TUNER_PVR_SDK
#include <PVR_API.h>
#endif

// Read-only Pimax input preview. NVIDIA acceptance is checked separately.
class PimaxGazeMonitor {
public:
    ~PimaxGazeMonitor() { Close(); }
    bool Read(std::array<float, 4>& location) {
#ifdef VRSS_TUNER_PVR_SDK
        const auto now = std::chrono::steady_clock::now();
        if (!session_) {
            if (now - lastAttempt_ < std::chrono::seconds(3)) return false;
            lastAttempt_ = now;
            if (pvr_initialise(&environment_) != pvr_success) return false;
            if (pvr_createSession(environment_, &session_) != pvr_success) { Close(); return false; }
            for (int eye = 0; eye < 2; ++eye) {
                if (pvr_getEyeRenderInfo(session_, static_cast<pvrEyeType>(eye), &render_[eye]) != pvr_success) {
                    Close(); return false;
                }
            }
        }
        const double query = pvr_getTimeSeconds(environment_);
        pvrEyeTrackingInfo gaze{};
        if (pvr_getEyeTrackingInfo(session_, query, &gaze) != pvr_success ||
            gaze.TimeInSeconds <= 0 || query - gaze.TimeInSeconds < -0.05 ||
            query - gaze.TimeInSeconds > 0.5) return false;
        for (int eye = 0; eye < 2; ++eye) {
            const auto& fov = render_[eye].Fov;
            const double width = double(fov.LeftTan) + fov.RightTan;
            const double height = double(fov.UpTan) + fov.DownTan;
            if (width <= 0 || height <= 0) return false;
            const double x = gaze.GazeTan[eye].x, y = -gaze.GazeTan[eye].y, z = 1.0;
            const auto& q = render_[eye].HmdToEyePose.Orientation;
            const double tx = 2.0 * (q.y * z - q.z * y);
            const double ty = 2.0 * (q.z * x - q.x * z);
            const double tz = 2.0 * (q.x * y - q.y * x);
            const double rx = x + q.w * tx + q.y * tz - q.z * ty;
            const double ry = y + q.w * ty + q.z * tx - q.x * tz;
            const double rz = z + q.w * tz + q.x * ty - q.y * tx;
            if (std::abs(rz) < 1e-9) return false;
            const double nx = 2.0 * (rx / rz) / width + (fov.LeftTan - fov.RightTan) / width;
            const double ny = 2.0 * (ry / rz) / height + (fov.UpTan - fov.DownTan) / height;
            if (!std::isfinite(nx) || !std::isfinite(ny) || std::abs(nx) > 2 || std::abs(ny) > 2) return false;
            location[eye * 2] = static_cast<float>(nx);
            location[eye * 2 + 1] = static_cast<float>(ny);
        }
        return true;
#else
        (void)location;
        return false;
#endif
    }
    void Close() {
#ifdef VRSS_TUNER_PVR_SDK
        if (session_) { pvr_destroySession(session_); session_ = nullptr; }
        if (environment_) { pvr_shutdown(environment_); environment_ = nullptr; }
#endif
    }
private:
#ifdef VRSS_TUNER_PVR_SDK
    pvrEnvHandle environment_{};
    pvrSessionHandle session_{};
    std::array<pvrEyeRenderInfo, 2> render_{};
    std::chrono::steady_clock::time_point lastAttempt_{};
#endif
};
