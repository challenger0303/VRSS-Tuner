#include "../third_party/openvr/openvr.h"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>

// Read-only SteamVR eye-action probe. It never starts SteamVR or touches VRChat.
int wmain(int argc, wchar_t** argv) {
    const auto manifest = std::filesystem::absolute(argc > 1 ? std::filesystem::path(argv[1]) :
        std::filesystem::path(argv[0]).parent_path() / L"eye_actions.json");
    if (!std::filesystem::is_regular_file(manifest)) {
        std::cerr << "ACTION_MANIFEST_MISSING\n";
        return 2;
    }
    vr::EVRInitError init = vr::VRInitError_None;
    vr::VR_Init(&init, vr::VRApplication_Background);
    if (init != vr::VRInitError_None) {
        std::cout << "STEAMVR_UNAVAILABLE init=" << int(init) << '\n';
        return 3;
    }
    struct Shutdown { ~Shutdown() { vr::VR_Shutdown(); } } shutdown;
    auto* input = vr::VRInput();
    if (!input) { std::cout << "INPUT_UNAVAILABLE\n"; return 4; }
    const auto path = manifest.u8string();
    auto error = input->SetActionManifestPath(path.c_str());
    if (error != vr::VRInputError_None) {
        std::cout << "ACTION_MANIFEST_REJECTED error=" << int(error) << '\n';
        return 5;
    }
    vr::VRActionSetHandle_t actionSet = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t gazeAction = vr::k_ulInvalidActionHandle;
    error = input->GetActionSetHandle("/actions/vrss_probe", &actionSet);
    if (error == vr::VRInputError_None)
        error = input->GetActionHandle("/actions/vrss_probe/in/gaze", &gazeAction);
    if (error != vr::VRInputError_None) {
        std::cout << "EYE_ACTION_UNAVAILABLE error=" << int(error) << '\n';
        return 6;
    }
    vr::VRActiveActionSet_t active{};
    active.ulActionSet = actionSet;
    std::cout << "sample,update_error,gaze_error,active,valid,tracked,origin_x,origin_y,origin_z,target_x,target_y,target_z\n";
    for (int sample = 0; sample < 50; ++sample) {
        const auto update = input->UpdateActionState(&active, sizeof(active), 1);
        vr::VREyeTrackingData_t gaze{};
        const auto read = update == vr::VRInputError_None ?
            input->GetEyeTrackingDataRelativeToNow(gazeAction, vr::TrackingUniverseStanding,
                0.0f, &gaze, sizeof(gaze)) : update;
        std::cout << sample << ',' << int(update) << ',' << int(read) << ','
            << int(gaze.bActive) << ',' << int(gaze.bValid) << ',' << int(gaze.bTracked);
        std::cout << std::fixed << std::setprecision(6);
        for (float value : gaze.vGazeOrigin.v) std::cout << ',' << value;
        for (float value : gaze.vGazeTarget.v) std::cout << ',' << value;
        std::cout << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
