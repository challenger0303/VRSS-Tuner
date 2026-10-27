#include "../hmd2_gaze.h"
#include <cstdint>

extern "C" __declspec(dllexport) int psvr2_toolkit_init() { return 0; }
extern "C" __declspec(dllexport) void psvr2_toolkit_deinit() {}
extern "C" __declspec(dllexport) bool psvr2_toolkit_get_driver_active() { return true; }
extern "C" __declspec(dllexport) bool psvr2_toolkit_gaze_status(hmd2_gaze_status_t* out, uint32_t) {
    static uint32_t frame = 0;
    *out = {};
    out->magic[0] = 'G'; out->magic[1] = 'S';
    out->size = sizeof(*out);
    out->wearable.frame_counter = ++frame;
    out->wearable.left.is_gaze_dir_valid = HMD2_GAZE_BOOL_TRUE;
    out->wearable.right.is_gaze_dir_valid = HMD2_GAZE_BOOL_TRUE;
    out->wearable.left.gaze_dir_norm = {0.2f, 0.1f, 0.9746794f};
    out->wearable.right.gaze_dir_norm = {-0.2f, -0.1f, 0.9746794f};
    return true;
}
