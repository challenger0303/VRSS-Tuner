#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include "nvapi.h"
#include "GazeSettings.h"
#include "RegionEstimate.h"
#include "BaballoniaRelay.h"
#include "FrameRuntime.h"
#include "FrameTestGate.h"
#include "../common-vrss-provider/ProviderEntry.h"
#include "Localization.h"
#include "ProviderTelemetry.h"
#include "PimaxGazeMonitor.h"
#include "NvidiaQueueMonitor.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "nvapi64.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {
#ifdef VRSS_OFFLINE_UI_TEST
constexpr bool kOfflineUiTest=true;
#else
constexpr bool kOfflineUiTest=false;
#endif
constexpr NvU32 kHmdGate = 0x00524252u;
constexpr NvU32 kVrLayout = 0x00524253u;
constexpr NvU32 kArea = 0x00524254u;
constexpr NvU32 kAspect = 0x00524255u;
constexpr NvU32 kAllow = 0x00D5E9C6u;
constexpr NvU32 kMode = 0x00D5E9C7u;
constexpr std::array<NvU32, 4> kEditable{kMode, kArea, kAspect, kVrLayout};

using QueryInterface = void* (__cdecl*)(NvU32);
using DrsSetEx = NvAPI_Status (__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*, NvU32, NvU32);
using DrsGetEx = NvAPI_Status (__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING*, NvU32*);
using DrsDeleteEx = NvAPI_Status (__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32);

struct Hidden {
    DrsSetEx set{};
    DrsGetEx get{};
    DrsDeleteEx remove{};
};

struct Value {
    bool found{};
    bool custom{};
    NvU32 raw{};
};

struct ProfileValues {
    Value mode, area, aspect, layout, gate, allow;
};

struct Session {
    NvDRSSessionHandle session{};
    NvDRSProfileHandle profile{};
    Hidden hidden{};
    bool initialized{};

    ~Session() {
        if (session) NvAPI_DRS_DestroySession(session);
        if (initialized) NvAPI_Unload();
    }
    NvAPI_Status open() {
        NvAPI_Status status = NvAPI_Initialize();
        if (status != NVAPI_OK) return status;
        initialized = true;
        auto* module = GetModuleHandleW(L"nvapi64.dll");
        auto query = module ? reinterpret_cast<QueryInterface>(GetProcAddress(module, "nvapi_QueryInterface")) : nullptr;
        if (!query) return NVAPI_NO_IMPLEMENTATION;
        hidden.set = reinterpret_cast<DrsSetEx>(query(0x8A2CF5F5u));
        if (!hidden.set) hidden.set = reinterpret_cast<DrsSetEx>(query(0x577DD202u));
        hidden.get = reinterpret_cast<DrsGetEx>(query(0xEA99498Du));
        if (!hidden.get) hidden.get = reinterpret_cast<DrsGetEx>(query(0x73BF8338u));
        hidden.remove = reinterpret_cast<DrsDeleteEx>(query(0xD20D29DFu));
        if (!hidden.remove) hidden.remove = reinterpret_cast<DrsDeleteEx>(query(0xE4A26362u));
        if (!hidden.get) return NVAPI_NO_IMPLEMENTATION;
        status = NvAPI_DRS_CreateSession(&session);
        if (status != NVAPI_OK) return status;
        status = NvAPI_DRS_LoadSettings(session);
        if (status != NVAPI_OK) return status;
        NvAPI_UnicodeString name{};
        constexpr wchar_t profileName[] = L"VRChat";
        for (size_t i = 0; i < std::size(profileName); ++i) name[i] = static_cast<NvU16>(profileName[i]);
        status = NvAPI_DRS_FindProfileByName(session, name, &profile);
        if (status == NVAPI_OK) return status;
        // A user-created VRChat.exe profile can shadow the NVIDIA VRChat profile.
        // Use an application lookup only when it has the VRSS allow gate enabled.
        NvAPI_UnicodeString applicationName{};
        constexpr wchar_t executable[] = L"VRChat.exe";
        for (size_t i = 0; i < std::size(executable); ++i)
            applicationName[i] = static_cast<NvU16>(executable[i]);
        NVDRS_APPLICATION application{};
        application.version = NVDRS_APPLICATION_VER;
        NvDRSProfileHandle candidate{};
        if (NvAPI_DRS_FindApplicationByName(session, applicationName, &candidate, &application) != NVAPI_OK)
            return status;
        NVDRS_SETTING allow{};
        allow.version = NVDRS_SETTING_VER;
        NvU32 extra{};
        if (hidden.get(session, candidate, kAllow, &allow, &extra) != NVAPI_OK ||
            allow.u32CurrentValue != 1) return status;
        profile = candidate;
        return NVAPI_OK;
    }
    NvAPI_Status read(NvU32 id, Value& value) const {
        NVDRS_SETTING setting{};
        setting.version = NVDRS_SETTING_VER;
        NvU32 extra{};
        NvAPI_Status status = hidden.get(session, profile, id, &setting, &extra);
        if (status == NVAPI_SETTING_NOT_FOUND) {
            value = {};
            return NVAPI_OK;
        }
        if (status == NVAPI_OK) {
            value = {true, setting.isCurrentPredefined == 0, setting.u32CurrentValue};
        }
        return status;
    }
    NvAPI_Status readAll(ProfileValues& values) const {
        for (auto [id, destination] : std::array<std::pair<NvU32, Value*>, 6>{
                 std::pair{kMode, &values.mode}, {kArea, &values.area},
                 {kAspect, &values.aspect}, {kVrLayout, &values.layout},
                 {kHmdGate, &values.gate}, {kAllow, &values.allow}}) {
            NvAPI_Status status = read(id, *destination);
            if (status != NVAPI_OK) return status;
        }
        return NVAPI_OK;
    }
    NvAPI_Status write(NvU32 id, NvU32 raw) const {
        if (!hidden.set) return NVAPI_NO_IMPLEMENTATION;
        NVDRS_SETTING setting{};
        setting.version = NVDRS_SETTING_VER;
        setting.settingId = id;
        setting.settingType = NVDRS_DWORD_TYPE;
        setting.u32CurrentValue = raw;
        return hidden.set(session, profile, &setting, 0, 0);
    }
    NvAPI_Status erase(NvU32 id) const {
        if (!hidden.remove) return NVAPI_NO_IMPLEMENTATION;
        NvAPI_Status status = hidden.remove(session, profile, id);
        return status == NVAPI_SETTING_NOT_FOUND ? NVAPI_OK : status;
    }
};

NvU32 floatBits(float value) {
    NvU32 bits{};
    static_assert(sizeof bits == sizeof value);
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
float bitsFloat(NvU32 bits) {
    float value{};
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
bool parseNumber(const std::wstring& input, float& output) {
    std::wstring normalized = input;
    for (auto& ch : normalized) if (ch == L',') ch = L'.';
    std::wistringstream stream(normalized);
    stream.imbue(std::locale::classic());
    stream >> output;
    if (stream.fail()) return false;
    stream >> std::ws;
    return stream.eof() && std::isfinite(output);
}
std::wstring numberText(float value) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(8) << value;
    return out.str();
}
std::wstring gazeNumberText(float value) {
    std::wostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(6) << value;
    auto text = out.str();
    while (!text.empty() && text.back() == L'0') text.pop_back();
    if (!text.empty() && text.back() == L'.') text.pop_back();
    return text == L"-0" ? L"0" : text;
}
std::filesystem::path backupFolder() {
    if constexpr(kOfflineUiTest) {
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
        return std::filesystem::path(module).parent_path()/L"offline-ui-data";
    }
    wchar_t buffer[32768]{};
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (!size || size >= std::size(buffer)) return {};
    return std::filesystem::path(buffer) / L"VRSS Tuner";
}
std::filesystem::path languageFile() { return backupFolder() / L"language.txt"; }
void loadLanguage() {
    std::ifstream in(languageFile(), std::ios::binary);
    std::string value;
    if (in >> value && (value == "ja" || value == "en")) language::english = value == "en";
    else language::english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_JAPANESE;
}
void saveLanguage() {
    std::error_code error;
    std::filesystem::create_directories(backupFolder(), error);
    if (!error) {
        std::ofstream out(languageFile(), std::ios::binary | std::ios::trunc);
        out << (language::english ? "en\n" : "ja\n");
    }
}
std::filesystem::path lastBackup() {
    auto folder = backupFolder();
    return folder.empty() ? std::filesystem::path{} : folder / L"last-backup.txt";
}

bool saveBackup(const ProfileValues& values) {
    auto folder = backupFolder();
    if (folder.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) return false;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wostringstream name;
    name << L"backup-" << std::setfill(L'0') << std::setw(4) << now.wYear
         << std::setw(2) << now.wMonth << std::setw(2) << now.wDay << L'-'
         << std::setw(2) << now.wHour << std::setw(2) << now.wMinute
         << std::setw(2) << now.wSecond << L'-' << std::setw(3) << now.wMilliseconds
         << L'-' << GetCurrentProcessId() << L".txt";
    std::ostringstream content;
    content << "VRSS-TUNER-BACKUP-1\n";
    for (auto [id, value] : std::array<std::pair<NvU32, const Value*>, 4>{
             std::pair{kMode, &values.mode}, {kArea, &values.area},
             {kAspect, &values.aspect}, {kVrLayout, &values.layout}}) {
        content << id << ' ' << (value->custom ? 1 : 0) << ' ' << value->raw << '\n';
    }
    auto writeFile = [&](const std::filesystem::path& path) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << content.str();
        out.flush();
        return out.good();
    };
    if (!writeFile(folder / name.str())) return false;
    auto pending = folder / L"last-backup.pending";
    if (!writeFile(pending)) return false;
    return MoveFileExW(pending.c_str(), lastBackup().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
bool loadBackup(std::array<Value, 4>& values) {
    auto path = lastBackup();
    if (path.empty()) return false;
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    if (!in || !std::getline(in, magic) || magic != "VRSS-TUNER-BACKUP-1") return false;
    for (size_t i = 0; i < kEditable.size(); ++i) {
        unsigned long long id{}, custom{}, raw{};
        if (!(in >> id >> custom >> raw) || id != kEditable[i] || custom > 1 || raw > 0xffffffffULL) return false;
        values[i] = {true, custom == 1, static_cast<NvU32>(raw)};
    }
    std::string extra;
    if (in >> extra) return false;
    if (values[0].custom && values[0].raw > 3) return false;
    if (values[1].custom && (!std::isfinite(bitsFloat(values[1].raw)) || bitsFloat(values[1].raw) < 1 || bitsFloat(values[1].raw) > 100)) return false;
    if (values[2].custom && (!std::isfinite(bitsFloat(values[2].raw)) || bitsFloat(values[2].raw) < 0.5f || bitsFloat(values[2].raw) > 2.0f)) return false;
    if (values[3].custom && values[3].raw > 7) return false;
    return true;
}

int applyCommand(int argc, wchar_t** argv) {
    if (argc != 6) return 10;
    unsigned mode{}, flags{};
    float area{}, aspect{};
    try {
        size_t end{};
        mode = static_cast<unsigned>(std::stoul(argv[2], &end));
        if (end != wcslen(argv[2])) return 11;
        flags = static_cast<unsigned>(std::stoul(argv[5], &end));
        if (end != wcslen(argv[5])) return 11;
    } catch (...) { return 11; }
    if (mode > 3 || flags > 5 || (flags & ~5u) || !parseNumber(argv[3], area) || area < 1 || area > 100) return 11;
    bool defaultAspect = wcscmp(argv[4], L"default") == 0;
    if (!defaultAspect && (!parseNumber(argv[4], aspect) || aspect < 0.5f || aspect > 2.0f)) return 11;
    Session session;
    NvAPI_Status status = session.open();
    if (status != NVAPI_OK) return 12;
    ProfileValues original{};
    status = session.readAll(original);
    if (status != NVAPI_OK) return 13;
    if (!saveBackup(original)) return 14;
    NvU32 originalFlags = original.layout.found ? original.layout.raw : 4u;
    NvU32 newFlags = (originalFlags & ~5u) | flags;
    status = session.write(kMode, mode);
    if (status != NVAPI_OK) return 15;
    status = session.write(kArea, floatBits(area));
    if (status != NVAPI_OK) return 16;
    status = defaultAspect ? session.erase(kAspect) : session.write(kAspect, floatBits(aspect));
    if (status != NVAPI_OK) return 17;
    status = session.write(kVrLayout, newFlags);
    if (status != NVAPI_OK) return 18;
    return NvAPI_DRS_SaveSettings(session.session) == NVAPI_OK ? 0 : 19;
}
int restoreCommand() {
    std::array<Value, 4> saved{};
    if (!loadBackup(saved)) return 20;
    Session session;
    if (session.open() != NVAPI_OK) return 21;
    for (size_t i = 0; i < saved.size(); ++i) {
        NvAPI_Status status = saved[i].custom
            ? session.write(kEditable[i], saved[i].raw)
            : session.erase(kEditable[i]);
        if (status != NVAPI_OK) return 22 + static_cast<int>(i);
    }
    return NvAPI_DRS_SaveSettings(session.session) == NVAPI_OK ? 0 : 26;
}

std::filesystem::path gResearch;
live::Store gGazeStore;
void initializeGazePaths() {
    wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, DWORD(std::size(module)));
    auto executableFolder = std::filesystem::path(module).parent_path();
    std::error_code localError;
    if (std::filesystem::is_directory(executableFolder / L"common-vrss-provider", localError))
        gResearch = executableFolder;
    auto path = executableFolder;
    for (int i = 0; gResearch.empty() && i < 5 && !path.empty(); ++i, path = path.parent_path()) {
        if (_wcsicmp(path.filename().c_str(), L"vrss-tuner") == 0) { gResearch = path.parent_path(); break; }
    }
    if (gResearch.empty()) {
        for (const auto& candidate : {executableFolder, executableFolder.parent_path()}) {
            std::error_code error;
            if (std::filesystem::is_directory(candidate / L"common-vrss-provider", error)) {
                gResearch = candidate;
                break;
            }
        }
    }
    auto backup = backupFolder();
    gGazeStore.pimax = L"C:\\ProgramData\\CodexVRSS\\gaze-calibration.txt";
    if (!backup.empty()) {
        gGazeStore.common = backup / L"common-calibration.txt";
        gGazeStore.backups = backup / L"gaze-backups";
        if (GetFileAttributesW(gGazeStore.common.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::string initial;
            const auto bundled = gResearch / L"common-vrss-provider" / L"calibration.txt";
            GazeCalibration::Values values{};
            if (live::ReadBytes(bundled, initial) && GazeCalibration::Parse(initial, values) &&
                live::Valid(values)) {
                std::error_code error;
                std::filesystem::create_directories(backup, error);
                DWORD winError{};
                if (!error) live::AtomicWrite(gGazeStore.common, initial, winError);
            }
        }
    }
}
bool compatiblePimaxDriver() {
    std::string manifest, dll;
    if (!live::ReadBytes(gResearch / L"pvr-sranibro-eye-driver/gain-build-manifest.json", manifest, 65536) ||
        !live::ReadBytes(L"C:\\Program Files\\Pimax\\Runtime\\drivers\\sranibro_eye\\bin\\win64\\driver_sranibro_eye.dll", dll, 16 * 1024 * 1024)) return false;
    auto hash = live::Sha(dll); std::string normalized = manifest;
    for (auto& ch : normalized) if (ch >= 'A' && ch <= 'F') ch += 'a' - 'A';
    return hash.size() == 64 && normalized.find(hash) != std::string::npos;
}
bool saveStartupState() {
    Session session; ProfileValues values{};
    if (session.open() != NVAPI_OK || session.readAll(values) != NVAPI_OK) return false;
    live::Snapshot gaze;
    const bool legacyLayout = !gResearch.empty() &&
        std::filesystem::is_directory(gResearch / L"vrss-tuner") &&
        gGazeStore.Read(live::Target::Pimax, gaze);
    if (!legacyLayout) {
        auto folder = backupFolder();
        if (folder.empty()) return false;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) return false;
        const bool common = gGazeStore.Read(live::Target::Common, gaze);
        std::ostringstream state;
        state << "format=2\nmode=" << (values.mode.found ? values.mode.raw : 0)
              << "\narea=" << (values.area.found ? values.area.raw : floatBits(50))
              << "\nlayout=" << (values.layout.found ? values.layout.raw : 0)
              << "\ncalibration_target=" << (common ? "common" : "none")
              << "\ncalibration_sha256=" << (common ? gaze.hash : "none") << '\n';
        DWORD winError{};
        return live::AtomicWrite(folder / L"startup-state-portable.ini", state.str(), winError);
    }
    auto root = gResearch / L"vrss-tuner";
    auto snapshot = root / (L"startup-pimax-calibration-" + live::Wide(gaze.hash) + L".txt");
    std::string retained; DWORD error{};
    if (std::filesystem::exists(snapshot)) {
        if (!live::ReadBytes(snapshot, retained) || retained != gaze.bytes) return false;
    } else if (!live::WriteNew(snapshot, gaze.bytes, error)) return false;
    std::ostringstream state;
    state << "format=1\nmode=" << (values.mode.found ? values.mode.raw : 0)
          << "\narea=" << (values.area.found ? values.area.raw : floatBits(50))
          << "\nlayout=" << (values.layout.found ? values.layout.raw : 0)
          << "\ncalibration_sha256=" << gaze.hash << '\n';
    return live::AtomicWrite(root / L"startup-state.ini", state.str(), error);
}
live::Target parseTarget(const wchar_t* text) {
    if (wcscmp(text, L"pimax") == 0) return live::Target::Pimax;
    if (wcscmp(text, L"common") == 0) return live::Target::Common;
    throw std::runtime_error("Unknown gaze target");
}
int gazeCommand(int argc, wchar_t** argv, bool restore) {
    if (argc != (restore ? 4 : 8)) return 60;
    try {
        auto target = parseTarget(argv[2]);
        if (target == live::Target::Pimax && !compatiblePimaxDriver()) return 68;
        std::wstring hash = argv[restore ? 3 : 7];
        if (hash.size() != 64 || hash.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) return 60;
        std::string expected; for (wchar_t ch : hash) expected.push_back(static_cast<char>(ch));
        live::Result result;
        if (restore) result = gGazeStore.Restore(target, expected);
        else {
            live::Values values;
            if (!parseNumber(argv[3], values.x_gain) || !parseNumber(argv[4], values.y_gain) ||
                !parseNumber(argv[5], values.left_deg) || !parseNumber(argv[6], values.right_deg)) return 60;
            result = gGazeStore.Apply(target, expected, values);
        }
        if (result.code) return result.code;
        return saveStartupState() ? 0 : 69;
    } catch (...) { return 60; }
}

enum ControlId {
    kRefreshButton = 100, kApplyButton, kRestoreButton,
    kModeCombo, kAreaEdit, kAspectCheck, kAspectEdit, kLayoutCombo, kInvertCheck, kStatusEdit,
    kDriverTab, kGazeTab, kGazeSource, kGazeX, kGazeY, kGazeLeft, kGazeRight,
    kGazeLeftMinus, kGazeLeftPlus, kGazeRightMinus, kGazeRightPlus, kGazeInvert,
    kGazeRefresh, kGazeApply, kGazeRestore, kGazeReset,
    kAreaSlider, kEstimateMsaa, kProfileName, kProfileSave, kProfileSwitch,
    kInputTab, kInputPort, kInputForward, kInputStart, kInputStop, kInputStatus, kPreviewZoom,
    kLanguage, kFrameTab, kFrameStart, kFrameStop, kFrameLaunch
};
struct Ui {
    HWND window{}, mode{}, area{}, aspectCheck{}, aspect{}, layout{}, invert{}, status{}, restore{};
    ProfileValues loaded{};
    bool loadedOk{};
    HWND driverTab{}, gazeTab{}, gazeSourceLabel{}, gazePathNote{}, gazeX{}, gazeY{}, gazeLeft{}, gazeRight{}, gazeInvert{}, gazeStatus{}, gazeApply{}, gazeRestore{};
    HWND profileName{}, profileSave{}, profileSwitch{}, inputTab{}, gazeSource{}, sourceNote{}, inputPort{}, inputForward{}, inputStart{}, inputStop{}, inputStatus{}, liveMonitor{}, previewZoom{}, language{};
    live::Target target{live::Target::Pimax};
    live::Snapshot gazeLoaded{};
    bool gazeLoadedOk{};
    HWND frameTab{},frameStart{},frameStop{},frameLaunch{},frameStatus{};
    int panel{};
    std::vector<HWND> driverControls, gazeControls, inputControls, frameControls, restartBadges;
    HWND areaSlider{}, estimateMsaa{}, regionPreview{}, regionArea{}, regionCost{}, regionNote{}, installInfo{};
    region::Estimate estimate;
};
Ui gUi;
frame::Runtime gFrameRuntime;
std::atomic<int> gSelectedSource{0}; // 0=Pimax, 1=Baballonia, 2=PSVR2, 3=SRanibro, 4=VIVE Pro Eye, 5=VIVE Streaming, 6=SteamVR eye action, 7=Varjo
struct LiveMonitor {
    int source{}, selectedSource{};
    bool vendor{}, common{}, routeVerified{}, inputRunning{}, relayFresh{}, relaySent{}, providerReady{}, consumerReady{}, accepted{}, failure{};
    telemetry::Snapshot provider{};
    uint64_t lastReceived{}, lastRelaySent{}, lastAccepted{}, lastFailures{};
    uint64_t receivedAt{}, relaySentAt{}, acceptedAt{}, failureAt{};
    uint64_t lastProviderSequence{}, providerSequenceAt{};
    uint32_t providerPid{};
    uint64_t receivedPerSecond{}, relayPerSecond{}, acceptedPerSecond{};
};
LiveMonitor gLive;
HANDLE gRouteOperation{};
bool gRouteStopping{};
const wchar_t* gazeSourceName(int source, bool english) {
    switch(source) {
    case 0: return english ? L"Pimax" : L"Pimax";
    case 1: return english ? L"Baballonia" : L"Baballonia";
    case 2: return english ? L"PSVR2 Toolkit" : L"PSVR2 Toolkit";
    case 3: return english ? L"SRanibro" : L"SRanibro";
    case 4: return english ? L"VIVE Pro Eye" : L"VIVE Pro Eye";
    case 5: return english ? L"VIVE Streaming" : L"VIVE Streaming";
    case 6: return english ? L"SteamVR eye action" : L"SteamVR視線アクション";
    case 7: return english ? L"Varjo Native SDK" : L"Varjo Native SDK";
    default: return english ? L"unknown" : L"不明";
    }
}
int gPreviewZoom=3; // Visualization only. Never change submitted gaze or calibration.
PimaxGazeMonitor gPimaxGaze;
std::array<float, 4> gPimaxLocation{};
uint64_t gPimaxFreshAt{};
NvidiaQueueMonitor gNvidiaQueue;
std::array<float, 4> gNvidiaLocation{};
uint64_t gNvidiaFreshAt{};
HFONT gFont{};
HFONT gBoldFont{};
bool gBusy{};
int gCreatingPanel = -1;
std::vector<std::pair<HWND, std::wstring>> gLabels;
std::unordered_map<HWND, std::wstring> gOriginalText;

HWND control(const wchar_t* cls, const wchar_t* title, DWORD style, int x, int y, int w, int h, int id = 0) {
    std::wstring translated = language::Text(title);
    HWND handle = CreateWindowExW(0, cls, translated.c_str(), style | WS_CHILD | WS_VISIBLE,
                                 x, y, w, h, gUi.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
    if (gCreatingPanel == 0) gUi.driverControls.push_back(handle);
    if (gCreatingPanel == 1) gUi.gazeControls.push_back(handle);
    if (gCreatingPanel == 2) gUi.inputControls.push_back(handle);
    if (gCreatingPanel == 3) gUi.frameControls.push_back(handle);
    if (*title && (_wcsicmp(cls, L"STATIC") == 0 || _wcsicmp(cls, L"BUTTON") == 0))
        gLabels.emplace_back(handle, title);
    return handle;
}
void setText(HWND target, const std::wstring& value) {
    gOriginalText[target] = value;
    auto translated = language::Text(value);
    SetWindowTextW(target, translated.c_str());
}
int localizedMessageBox(HWND owner, const wchar_t* message, const wchar_t* title, UINT flags) {
    const auto body = language::Text(message ? message : L"");
    const auto caption = language::Text(title ? title : L"");
    return ::MessageBoxW(owner, body.c_str(), caption.c_str(), flags);
}
std::wstring frameLaunchError(const std::string& error) {
    if (language::english) return {error.begin(), error.end()};
    if (error == "Close VRChat and Unity before launching")
        return L"起動前にVRChatとUnityを終了してください。";
    if (error == "The current Frame session is not ready for a game launch")
        return L"Steam Frameの接続準備が完了していません。視線の状態を確認してください。";
    if (error == "Start the Tuner UI without administrator privileges before launching VRChat")
        return L"Tunerを管理者権限なしで起動し直してから、VRChatを起動してください。";
    if (error == "The installed VRChat EAC launcher could not be verified" ||
        error == "Steam App 438100 installation is not registered in Windows")
        return L"VRChatのインストール先を確認できません。SteamでVRChatの導入状態を確認してください。";
    return L"VRChatを起動できませんでした。Steam Frameの接続状態と実行ログを確認してください。";
}
#define MessageBoxW localizedMessageBox
std::wstring getText(HWND target) {
    wchar_t buffer[4096]{};
    GetWindowTextW(target, buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}
void applyLanguage() {
    for (const auto& [handle, source] : gLabels) {
        const auto translated = language::Text(source);
        SetWindowTextW(handle, translated.c_str());
    }
    auto repopulate = [](HWND combo, const wchar_t* first, const wchar_t* second) {
        int selection = int(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        const auto a = language::Text(first), b = language::Text(second);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(a.c_str()));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(b.c_str()));
        SendMessageW(combo, CB_SETCURSEL, selection, 0);
    };
    repopulate(gUi.mode, L"オフ", L"オン");
    int source = int(SendMessageW(gUi.gazeSource, CB_GETCURSEL, 0, 0));
    SendMessageW(gUi.gazeSource, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* name : {L"Pimax経路（純正優先）", L"Baballonia OSC", L"PSVR2 Toolkit（直接）", L"SRanibro（共通）", L"VIVE Pro Eye（SRanipal・実機未検証）", L"VIVE Streaming（実機未検証）", L"SteamVR視線アクション（実機未検証）", L"Varjo Native SDK（実機未検証）"}) {
        const auto translated=language::Text(name);
        SendMessageW(gUi.gazeSource,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(translated.c_str()));
    }
    SendMessageW(gUi.gazeSource,CB_SETCURSEL,source,0);
    for (HWND handle : {gUi.status, gUi.gazeStatus, gUi.inputStatus,gUi.inputStart,gUi.frameStatus,gUi.gazeSourceLabel,gUi.gazePathNote,gUi.sourceNote,
                        gUi.regionArea, gUi.regionCost, gUi.regionNote, gUi.installInfo}) {
        auto found = gOriginalText.find(handle);
        if (found != gOriginalText.end()) {
            const auto translated = language::Text(found->second);
            SetWindowTextW(handle, translated.c_str());
        }
    }
    InvalidateRect(gUi.regionPreview, nullptr, TRUE);
    InvalidateRect(gUi.liveMonitor, nullptr, TRUE);
}
bool isChecked(HWND target) { return SendMessageW(target, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void check(HWND target, bool on) { SendMessageW(target, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
std::wstring decimal(double value, int digits=1) {
    std::wostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(digits) << value; return out.str();
}
void updateEstimate() {
    if (!gUi.regionPreview || !gUi.estimateMsaa) return;
    float area{}, aspect=1;
    int index = int(SendMessageW(gUi.estimateMsaa, CB_GETCURSEL, 0, 0));
    int samples = index == 0 ? 2 : index == 2 ? 8 : 4;
    int mode = int(SendMessageW(gUi.mode, CB_GETCURSEL, 0, 0));
    int layout = 0;
    bool valid = parseNumber(getText(gUi.area), area) &&
        (!isChecked(gUi.aspectCheck) || parseNumber(getText(gUi.aspect), aspect));
    gUi.estimate = valid ? region::Calculate(area,aspect,samples,mode,layout) : region::Estimate{};
    auto& e = gUi.estimate;
    if (!e.valid) {
        setText(gUi.regionArea,L"領域・縦横比の入力値を確認してください。");
        setText(gUi.regionCost,L"色計算の増加：—");
        setText(gUi.regionNote,L"領域1～100、縦横比0.5～2で試算できます。");
    } else if (!e.supported) {
        setText(gUi.regionArea,L"この実験用モードは試算対象外です。");
        setText(gUi.regionCost,L"色計算の増加：—");
        setText(gUi.regionNote,L"2領域・Force/Adaptive/Offでプレビューできます。");
    } else {
        SendMessageW(gUi.areaSlider,TBM_SETPOS,TRUE,LPARAM(std::lround(area)));
        setText(gUi.regionArea,(e.active ? L"追加SSの面積：約" : L"設定領域の面積：約") + decimal(e.visibleFraction*100) + L"% / 各目");
        setText(gUi.regionCost,L"色計算：" + decimal(e.shadingRatio,2) + L"倍（＋" + decimal((e.shadingRatio-1)*100) + L"%）");
        std::wstring note = L"横幅 " + decimal(e.radiusX*200) + L"% × 高さ " + decimal(e.radiusY*200) + L"%";
        if (!e.active) note += L"\r\nVRSS Off：追加SSはありません。";
        else if (e.adaptive) note += L"\r\n入力サイズでの仮定。Adaptiveの実領域は変動。";
        else note += L"\r\nMSAA単独比の色計算。GPU全体・FPSの比ではありません。";
        setText(gUi.regionNote,note);
    }
    InvalidateRect(gUi.regionPreview,nullptr,FALSE);
}

LRESULT CALLBACK regionPreviewProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window,&paint); RECT client{}; GetClientRect(window,&client);
        HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc,client.right,client.bottom);
        auto oldBitmap = SelectObject(buffer,bitmap);
        FillRect(buffer,&client,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetBkMode(buffer,TRANSPARENT); auto oldFont=SelectObject(buffer,gFont);
        SetTextColor(buffer,RGB(70,85,105));
        const auto& e = gUi.estimate;
        if (e.valid && e.supported) {
            int side=std::min(126,int(client.bottom)-25);
            for(int eye=0;eye<2;++eye) {
                int x=eye*(side+24),y=22;
                const auto label = language::Text(eye==0?L"左目":L"右目");
                TextOutW(buffer,x,0,label.c_str(),int(label.size()));
                RECT bounds{x,y,x+side,y+side};
                HBRUSH background=CreateSolidBrush(RGB(241,245,251));FillRect(buffer,&bounds,background);DeleteObject(background);
                int saved=SaveDC(buffer);IntersectClipRect(buffer,bounds.left,bounds.top,bounds.right,bounds.bottom);
                HBRUSH fill=CreateSolidBrush(e.active?RGB(192,219,253):RGB(233,236,240));
                HPEN pen=CreatePen(e.active?PS_SOLID:PS_DASH,1,e.active?RGB(36,107,205):RGB(125,136,151));
                auto oldBrush=SelectObject(buffer,fill);auto oldPen=SelectObject(buffer,pen);
                int cx=x+side/2,cy=y+side/2;
                int rx=int(std::lround(e.radiusX*side)),ry=int(std::lround(e.radiusY*side));
                Ellipse(buffer,cx-rx,cy-ry,cx+rx,cy+ry);
                MoveToEx(buffer,cx-4,cy,nullptr);LineTo(buffer,cx+5,cy);MoveToEx(buffer,cx,cy-4,nullptr);LineTo(buffer,cx,cy+5);
                SelectObject(buffer,oldBrush);SelectObject(buffer,oldPen);DeleteObject(fill);DeleteObject(pen);RestoreDC(buffer,saved);
                FrameRect(buffer,&bounds,static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            }
        } else {
            RECT messageRect{4,35,client.right-4,client.bottom};
            const auto previewMessage = language::Text(e.valid?L"領域配置は未検証です。":L"入力値を確認してください。");
            DrawTextW(buffer,previewMessage.c_str(),-1,&messageRect,DT_WORDBREAK);
        }
        SelectObject(buffer,oldFont);BitBlt(dc,0,0,client.right,client.bottom,buffer,0,0,SRCCOPY);
        SelectObject(buffer,oldBitmap);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
LRESULT CALLBACK liveMonitorProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message != WM_PAINT) return DefWindowProcW(window,message,wParam,lParam);
    PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint); RECT client{}; GetClientRect(window,&client);
    HDC buffer=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,client.right,client.bottom);
    auto oldBitmap=SelectObject(buffer,bitmap);
    FillRect(buffer,&client,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(buffer,&client,static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
    SetBkMode(buffer,TRANSPARENT); auto oldFont=SelectObject(buffer,gFont);
    auto put=[&](int x,int y,const std::wstring& value,COLORREF color=RGB(48,61,76)) {
        SetTextColor(buffer,color); TextOutW(buffer,x,y,value.c_str(),int(value.size()));
    };
    const bool en=language::english;
    put(12,8,en?L"Live gaze path":L"視線経路の動作状況");
    const bool activeRoute=gLive.vendor || gLive.routeVerified;
    std::wstring routeLabel=(activeRoute?(en?L"Running: ":L"稼働: "):(en?L"Selected: ":L"選択: "))+
        std::wstring(gazeSourceName(gLive.source,en));
    if (activeRoute && gLive.source!=gLive.selectedSource)
        routeLabel+=(en?L" (selected: ":L"（選択: ")+std::wstring(gazeSourceName(gLive.selectedSource,en))+
            (en?L")":L"）");
    put(220,8,routeLabel,activeRoute && gLive.source!=gLive.selectedSource?RGB(151,93,15):RGB(84,97,112));
    auto stage=[&](int y,bool active,bool warning,const std::wstring& label,const std::wstring& detail) {
        HBRUSH dot=CreateSolidBrush(active?RGB(34,157,88):warning?RGB(225,154,34):RGB(156,164,176));
        HBRUSH old=static_cast<HBRUSH>(SelectObject(buffer,dot));
        Ellipse(buffer,14,y+3,26,y+15); SelectObject(buffer,old); DeleteObject(dot);
        put(34,y,label); put(200,y,detail,active?RGB(28,114,68):warning?RGB(151,93,15):RGB(104,115,128));
    };
    const uint64_t tick = GetTickCount64();
    const bool pimaxFresh = gLive.vendor && gPimaxFreshAt && tick >= gPimaxFreshAt && tick-gPimaxFreshAt < 1500;
    const bool queueFresh = gLive.vendor && gNvidiaFreshAt && tick >= gNvidiaFreshAt && tick-gNvidiaFreshAt < 1500;
    const bool commonDotsFresh = gLive.providerReady && gLive.provider.fresh &&
        gLive.providerSequenceAt && tick >= gLive.providerSequenceAt && tick-gLive.providerSequenceAt < 1500 &&
        (gLive.source==1?gLive.relaySent:gLive.relayFresh);
    const std::wstring inputDetail=gLive.source==0?(pimaxFresh?
        (en?L"Pimax gaze moving":L"Pimax視線を受信中"):
        (en?L"Pimax gaze waiting":L"Pimax視線待ち")):
        gLive.source==1?(gLive.relayFresh?
            (en?L"OSC receiving ":L"OSC受信中 ")+std::to_wstring(gLive.receivedPerSecond)+(en?L" /s":L" 件/秒"):
            gLive.inputRunning?(en?L"listener running; waiting for OSC":L"受信待機中・OSC待ち"):
                               (en?L"OSC listener stopped":L"OSC受信停止中")):
        gLive.inputRunning?(en?L"source app / bridge running":L"視線アプリ・ブリッジ起動中"):
                           (en?L"source app / bridge stopped":L"視線アプリ・ブリッジ停止中");
    stage(30,gLive.source==0?pimaxFresh:gLive.source==1?gLive.relayFresh:gLive.inputRunning,
        gLive.source==0?gLive.vendor&&!pimaxFresh:gLive.source==1?gLive.inputRunning&&!gLive.relayFresh:
        gLive.common&&!gLive.inputRunning,en?L"1  Eye source":L"1  視線入力元",inputDetail);
    stage(53,gLive.source==0?queueFresh:gLive.relaySent,
        gLive.source==0?gLive.vendor&&!queueFresh:gLive.source==1?gLive.relayFresh&&!gLive.relaySent:
        gLive.inputRunning&&!gLive.relaySent,
        (en?L"2  Gaze handoff":L"2  視線の受け渡し"),
        gLive.source==0?(queueFresh?(en?L"NVIDIA queue updating":L"NVIDIAへの視線が更新中"):
            (en?L"waiting for NVIDIA gaze":L"NVIDIAへの視線待ち")):
        gLive.relaySent?(gLive.source>=1?(en?L"fresh gaze reached provider":L"新しい視線がプロバイダーへ到達"):
            (en?L"sent ":L"送信中 ")+std::to_wstring(gLive.relayPerSecond)+(en?L" /s":L" 件/秒")):
        gLive.vendor?(en?L"Pimax provider has priority":L"Pimax経路を優先中"):
        !gLive.common?(en?L"waiting for common provider":L"共通プロバイダー待ち"):
                      (en?L"waiting for fresh gaze":L"新しい視線データ待ち"));
    if (gLive.source==0 || gLive.vendor) {
        stage(76,gLive.vendor&&gNvidiaQueue.Present(),gLive.vendor&&!gNvidiaQueue.Present(),en?L"3  Provider":L"3  プロバイダー",gLive.vendor?
            (gNvidiaQueue.Present()?(en?L"Pimax registered":L"Pimax経路を検出"):
                (en?L"Pimax running; gaze unverified":L"Pimax起動中・視線未確認")):
            (en?L"Pimax path stopped":L"Pimax経路は停止中"));
        stage(99,queueFresh,gLive.vendor&&gNvidiaQueue.Present()&&!queueFresh,en?L"4  NVIDIA gaze":L"4  NVIDIA視線",gLive.vendor?
            (queueFresh?(en?L"valid stereo packets updating":L"有効な両目視線が更新中"):
                (en?L"waiting for valid packets":L"有効な視線データ待ち")):
            (en?L"waiting for provider":L"プロバイダー待ち"));
    } else {
        stage(76,gLive.providerReady,gLive.common,en?L"3  Provider":L"3  プロバイダー",
            gLive.providerReady?(en?L"registered":L"登録済み"):
            gLive.common?(en?L"running; registration unconfirmed":L"起動中・登録未確認"):
                         (en?L"stopped":L"停止中"));
        std::wstring detail=gLive.accepted?
            (en?L"gaze accepted ":L"視線受理 ")+std::to_wstring(gLive.acceptedPerSecond)+(en?L" /s":L" 件/秒"):
            gLive.failure?(en?L"submission/query error":L"送信・照会エラー"):
            !gLive.providerReady?(en?L"waiting for provider":L"プロバイダー待ち"):
            !gLive.consumerReady?(en?L"waiting for VR consumer":L"VRアプリの照会待ち"):
                                 (en?L"waiting for fresh gaze":L"新しい視線データ待ち");
        stage(99,gLive.accepted,gLive.failure||gLive.providerReady,en?L"4  NVIDIA gaze":L"4  NVIDIA視線",detail);
    }
    put(12,126,gLive.source==0?
        (queueFresh?(en?L"NVIDIA gaze position (left / right eye)":L"NVIDIAへ届いた視線（左目／右目）"):
            (en?L"Pimax input gaze (left / right eye)":L"Pimax入力視線（左目／右目）")):
        gLive.accepted?(en?L"NVIDIA-accepted gaze (left / right)":L"NVIDIAが受理した視線（左目／右目）"):
        commonDotsFresh?(en?L"Provider input gaze (left / right)":L"プロバイダー入力視線（左目／右目）"):
        (en?L"Waiting for provider gaze (left / right)":L"プロバイダーの視線待ち（左目／右目）"),RGB(84,97,112));
    for (int eye=0;eye<2;++eye) {
        const int x=12+eye*165, y=148, width=145, height=103;
        RECT square{x,y,x+width,y+height}; HBRUSH pale=CreateSolidBrush(RGB(243,246,250));
        FillRect(buffer,&square,pale); DeleteObject(pale);
        FrameRect(buffer,&square,static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
        HPEN cross=CreatePen(PS_SOLID,1,RGB(205,212,220)); auto oldPen=SelectObject(buffer,cross);
        MoveToEx(buffer,x+width/2,y,nullptr);LineTo(buffer,x+width/2,y+height);
        MoveToEx(buffer,x,y+height/2,nullptr);LineTo(buffer,x+width,y+height/2);
        SelectObject(buffer,oldPen);DeleteObject(cross);
        if ((gLive.source==0 && (queueFresh||pimaxFresh)) || (gLive.source!=0 && commonDotsFresh)) {
            const float gx=gLive.source==0?(queueFresh?gNvidiaLocation[eye*2]:gPimaxLocation[eye*2]):gLive.provider.locations[eye*2];
            const float gy=gLive.source==0?(queueFresh?gNvidiaLocation[eye*2+1]:gPimaxLocation[eye*2+1]):gLive.provider.locations[eye*2+1];
            const float screenY=gLive.source==0?gy:-gy;
            auto point=[&](float px,float py) {
                return POINT{x+width/2+int(std::lround(std::clamp(px,-1.0f,1.0f)*(width/2-7))),
                             y+height/2+int(std::lround(std::clamp(py,-1.0f,1.0f)*(height/2-7)))};
            };
            const POINT actual=point(gx,screenY);
            const POINT enlarged=point(gx*gPreviewZoom,screenY*gPreviewZoom);
            HPEN ring=CreatePen(PS_SOLID,2,RGB(59,105,164));
            auto priorPen=SelectObject(buffer,ring);
            auto priorBrush=SelectObject(buffer,GetStockObject(NULL_BRUSH));
            Ellipse(buffer,actual.x-5,actual.y-5,actual.x+6,actual.y+6);
            SelectObject(buffer,priorPen);SelectObject(buffer,priorBrush);DeleteObject(ring);
            HBRUSH dot=CreateSolidBrush(RGB(34,157,88));auto old=SelectObject(buffer,dot);
            Ellipse(buffer,enlarged.x-4,enlarged.y-4,enlarged.x+5,enlarged.y+5);
            SelectObject(buffer,old);DeleteObject(dot);
        }
        put(x+6,y+5,eye==0?(en?L"L":L"左"):(en?L"R":L"右"));
    }
    RECT note{350,151,client.right-10,client.bottom-8};
    const wchar_t* noteText=gLive.source==0?
        (queueFresh?(en?L"Dots use NVIDIA's final coordinates. Calibration is already included. The VRSS image must be checked in the headset.":
            L"点はNVIDIAへ届いた最終座標です。補正値は反映済み。VRSSの領域はHMD内で確認してください。"):
            (en?L"Dots show Pimax input; NVIDIA reception is not yet confirmed.":
                L"点はPimax入力です。NVIDIA側への到達は未確認です。")):
        (gLive.accepted?(en?L"NVIDIA accepted this gaze. The VRSS image still needs an in-headset check.":
            L"この視線はNVIDIAが受理しました。VRSSの描画はHMD内で確認してください。"):
        commonDotsFresh?(en?L"Source gaze is updating; NVIDIA acceptance is not yet confirmed.":
            L"入力視線は更新中。NVIDIA側の受理はまだ確認できません。"):
        (en?L"Waiting for live gaze samples. Dots do not represent the VRSS area.":
            L"新しい視線データ待ち。点はVRSS領域を表しません。"));
    const std::wstring legend=(en?L"Green: observed / amber: check / gray: waiting.\nBlue ring: actual gaze. Green dot: display zoom x":
        L"緑：確認済み／橙：要確認／灰：待機。\n青い輪：実際の視線。緑の点：表示だけ")+
        std::to_wstring(gPreviewZoom)+(en?L". VRSS is unchanged.\n":L"倍。VRSSは変更しません。\n");
    const std::wstring explanation=legend+noteText;
    SetTextColor(buffer,RGB(85,97,111));DrawTextW(buffer,explanation.c_str(),-1,&note,DT_WORDBREAK);
    SelectObject(buffer,oldFont);BitBlt(dc,0,0,client.right,client.bottom,buffer,0,0,SRCCOPY);
    SelectObject(buffer,oldBitmap);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);return 0;
}
bool vrchatRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"VRChat.exe") == 0) { found = true; break; }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found;
}
unsigned processCount(const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof(entry)}; unsigned count{};
    if (Process32FirstW(snapshot, &entry)) do { if (_wcsicmp(entry.szExeFile, name) == 0) ++count; } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot); return count;
}
struct PimaxServiceState { bool found{}, running{}, disabled{}; };
constexpr wchar_t kPimaxEyeService[] = L"Tobii VR4PIMAXP3B Platform Runtime";
PimaxServiceState pimaxServiceState() {
    PimaxServiceState result{};
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if (!manager) return result;
    SC_HANDLE service=OpenServiceW(manager,kPimaxEyeService,SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG);
    if (service) {
        result.found=true;
        SERVICE_STATUS status{};
        if (QueryServiceStatus(service,&status)) result.running=status.dwCurrentState==SERVICE_RUNNING;
        std::array<BYTE,8192> storage{}; DWORD needed{};
        if (QueryServiceConfigW(service,reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(storage.data()),
                DWORD(storage.size()),&needed))
            result.disabled=reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(storage.data())->dwStartType==SERVICE_DISABLED;
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return result;
}
int restorePimaxService() {
    if (processCount(L"SRanibro.exe")) return 81; // Never take the EyeChip from active custom capture.
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if (!manager) return 82;
    SC_HANDLE service=OpenServiceW(manager,kPimaxEyeService,
        SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG|SERVICE_CHANGE_CONFIG|SERVICE_START);
    if (!service) { CloseServiceHandle(manager); return 83; }
    std::array<BYTE,8192> storage{}; DWORD needed{};
    int result=0;
    if (!QueryServiceConfigW(service,reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(storage.data()),
            DWORD(storage.size()),&needed)) result=84;
    else if (reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(storage.data())->dwStartType==SERVICE_DISABLED &&
             !ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,
                 nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)) result=85;
    if (!result && processCount(L"SRanibro.exe")) result=81;
    if (!result && !StartServiceW(service,0,nullptr) && GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING) result=86;
    if (!result) {
        SERVICE_STATUS status{};
        for (int i=0;i<20;++i) {
            if (QueryServiceStatus(service,&status) && status.dwCurrentState==SERVICE_RUNNING) break;
            Sleep(250);
        }
        if (status.dwCurrentState!=SERVICE_RUNNING) result=87;
    }
    CloseServiceHandle(service); CloseServiceHandle(manager);
    return result;
}
void showPanel(int panel) {
    gUi.panel = panel;
    for(HWND handle:gUi.frameControls)ShowWindow(handle,panel==3?SW_SHOW:SW_HIDE);
    check(gUi.frameTab,panel==3);
    for (HWND item : gUi.driverControls) ShowWindow(item, panel == 0 ? SW_SHOW : SW_HIDE);
    for (HWND item : gUi.gazeControls) ShowWindow(item, panel == 1 ? SW_SHOW : SW_HIDE);
    for (HWND item : gUi.inputControls) ShowWindow(item, panel == 2 ? SW_SHOW : SW_HIDE);
    SendMessageW(gUi.driverTab, BM_SETCHECK, panel == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gUi.gazeTab, BM_SETCHECK, panel == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gUi.inputTab, BM_SETCHECK, panel == 2 ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(gUi.window, nullptr, TRUE);
}
void refreshGaze() {
    int source = int(SendMessageW(gUi.gazeSource, CB_GETCURSEL, 0, 0));
    if (source < 0 || source > 7) source = 0;
    gUi.target = source == 0 ? live::Target::Pimax : live::Target::Common;
    if (gUi.gazeSourceLabel) {
        const wchar_t* selected=source==0?L"選択中: Pimax経路（純正優先）":
            source==1?L"選択中: Baballonia OSC → 共通プロバイダー":
            source==2?L"選択中: PSVR2 Toolkit → 共通プロバイダー（実機未検証）":
            source==3?L"選択中: SRanibro → 共通プロバイダー（HMD別に要検証）":
            source==4?L"選択中: VIVE Pro Eye → 共通プロバイダー（実機未検証）":
            source==5?L"選択中: VIVE Streaming → 共通プロバイダー（実機未検証）":
            source==6?L"選択中: SteamVR視線アクション → 共通プロバイダー（実機未検証）":
                      L"選択中: Varjo Native SDK → 共通プロバイダー（実機未検証）";
        setText(gUi.gazeSourceLabel,selected);
    }
    if (gUi.gazePathNote) setText(gUi.gazePathNote,source==0?
        L"Pimax経路の補正値を編集します。":
        L"共通経路は同じ補正ファイルを使用。HMD別の設定はプロファイルで保存できます。");
    if (gUi.sourceNote) setText(gUi.sourceNote,source==0?
        L"純正XR5視線を優先。SRanibro使用時はPimaxブリッジへ切替。TunerのOSC転送は不要です。":
        source==1?L"Baballonia → Tuner → 共通プロバイダー":
        source==2?L"PSVR2 Toolkit → 直接ブリッジ → 共通プロバイダー（実機未確認）":
        source==3?L"SRanibro → 共通プロバイダー（接続中HMDの投影情報を使用）":
        source==4?L"SRanipal → Pro Eyeブリッジ → 共通プロバイダー（実機未確認）":
        source==5?L"VIVE Streaming SDK → ブリッジ → 共通プロバイダー（実機未確認）":
        source==6?L"SteamVR視線アクション → 左右投影 → 共通プロバイダー（実機未確認）":
                  L"Varjo Native SDK → 両目視線ブリッジ → 共通プロバイダー（実機未確認）");
    gUi.gazeLoadedOk = gGazeStore.Read(gUi.target, gUi.gazeLoaded);
    if (gUi.gazeLoadedOk) {
        const auto& v = gUi.gazeLoaded.values;
        setText(gUi.gazeX, gazeNumberText(v.x_gain)); setText(gUi.gazeY, gazeNumberText(std::abs(v.y_gain)));
        setText(gUi.gazeLeft, gazeNumberText(v.left_deg)); setText(gUi.gazeRight, gazeNumberText(v.right_deg));
        check(gUi.gazeInvert, v.y_gain < 0);
    }
    std::wstring info = L"Pimax: " + std::to_wstring(processCount(L"vrss_gaze_provider.exe")) +
        L" / 共通: " + std::to_wstring(processCount(L"common_vrss_provider.exe")) + L" プロセス\r\n";
    info += gUi.gazeLoadedOk ? L"補正を読込済み。保存後は実行中の経路へ反映します。" :
        L"この経路の補正ファイルを読み取れません。";
    setText(gUi.gazeStatus, info);
    EnableWindow(gUi.gazeApply, gUi.gazeLoadedOk);
    EnableWindow(gUi.gazeRestore, gUi.gazeLoadedOk);
}
void refresh() {
    Session session;
    NvAPI_Status result = session.open();
    ProfileValues values{};
    if (result == NVAPI_OK) result = session.readAll(values);
    gUi.loadedOk = result == NVAPI_OK;
    if (gUi.installInfo) {
        std::wstring install;
        if (result != NVAPI_OK) install = L"導入状態: NVIDIAのVRChatプロファイルを読み取れません。";
        else if (!values.allow.found || values.allow.raw != 1)
            install = L"導入状態: このVRChatプロファイルではVRSS許可が有効ではありません。";
        else if (gResearch.empty()) install = L"導入状態: 実行ファイル単体です。視線補正と共通プロバイダーは未接続です。";
        else if (compatiblePimaxDriver()) install = processCount(L"vrss_gaze_provider.exe")
            ? L"導入状態: Pimax経路を検出。実機での追従は別途確認してください。"
            : L"導入状態: Pimax補正ドライバーを確認。視線プロバイダーは停止中です。";
        else if (std::filesystem::is_regular_file(gResearch / L"common-vrss-provider/common_vrss_provider.exe"))
            install = L"導入状態: 共通プロバイダーを検出。登録・起動・視線入力は別途必要です。";
        else install = L"導入状態: 画質設定のみ使用可能。視線経路は未準備です。";
        setText(gUi.installInfo, install);
    }
    if (!gUi.loadedOk) {
        setText(gUi.status, L"VRChatプロファイルを読み取れません。NVAPI状態: " + std::to_wstring(result));
        EnableWindow(GetDlgItem(gUi.window, kApplyButton), FALSE);
        return;
    }
    gUi.loaded = values;
    SendMessageW(gUi.mode, CB_SETCURSEL, values.mode.found && values.mode.raw != 0 ? 1 : 0, 0);
    setText(gUi.area, numberText(values.area.found ? bitsFloat(values.area.raw) : 50.0f));
    bool customAspect = values.aspect.custom;
    check(gUi.aspectCheck, customAspect);
    setText(gUi.aspect, numberText(values.aspect.found ? bitsFloat(values.aspect.raw) : 1.0f));
    EnableWindow(gUi.aspect, customAspect);
    std::wostringstream info;
    info << L"VRChat: " << (vrchatRunning() ? L"起動中。変更後は再起動が必要。" : L"停止中。次回起動から反映。") << L"\r\n"
         << L"保存値: " << (values.mode.found && values.mode.raw ? L"オン" : L"オフ")
         << L" / 大きさ " << numberText(values.area.found ? bitsFloat(values.area.raw) : 50.0f)
         << L" / 縦横比 " << numberText(values.aspect.found ? bitsFloat(values.aspect.raw) : 1.0f)
         << L"\r\n既存の目別処理: " << (values.layout.found && (values.layout.raw & 1u) ? L"選択目" : L"両目")
         << L" / ドライバー縦反転: " << (values.layout.found && (values.layout.raw & 4u) ? L"オン" : L"オフ")
         << L"（保持）";
    setText(gUi.status, info.str());
    EnableWindow(GetDlgItem(gUi.window, kApplyButton), TRUE);
    EnableWindow(gUi.restore, TRUE);
}

int runElevated(const std::wstring& arguments) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return -1;
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = path;
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return GetLastError() == ERROR_CANCELLED ? -2 : -3;
    DWORD code = 99;
    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(1, &info.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_OBJECT_0 + 1) break;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}
bool applyFromUi(bool confirm = true) {
    if (!gUi.loadedOk) return false;
    if (!gUi.loaded.allow.found || gUi.loaded.allow.raw != 1) {
        MessageBoxW(gUi.window, L"このVRChatプロファイルではVRSS許可が有効ではありません。設定を保存しても効果がないため変更しません。",
            L"VRSS設定", MB_ICONWARNING);
        return false;
    }
    int mode = static_cast<int>(SendMessageW(gUi.mode, CB_GETCURSEL, 0, 0));
    float area{}, aspect{};
    bool defaultAspect = !isChecked(gUi.aspectCheck);
    if (mode < 0 || mode > 1 ||
        !parseNumber(getText(gUi.area), area) || area < 1 || area > 100 ||
        (!defaultAspect && (!parseNumber(getText(gUi.aspect), aspect) || aspect < 0.5f || aspect > 2))) {
        MessageBoxW(gUi.window, L"領域は1～100、縦横比は0.5～2の数値を指定してください。", L"入力エラー", MB_ICONWARNING);
        return false;
    }
    // Preserve the existing driver layout bits. The UI does not expose these
    // experimental per-eye/Y-axis switches because they are not gaze calibration.
    unsigned flags = gUi.loaded.layout.found ? (gUi.loaded.layout.raw & 5u) : 0u;
    if (confirm && MessageBoxW(gUi.window,
        L"この設定は、保存後にVRChatを再起動して反映します。\n起動中のVRChatには、その場で反映される保証がありません。\n\n今のワールドからは移動せず、自動再起動もしません。\n設定を保存しますか？",
        L"VRChatの再起動が必要です", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return false;
    std::wstring args = L"--apply " + std::to_wstring(mode) + L" " + numberText(area) + L" " +
                        (defaultAspect ? L"default" : numberText(aspect)) + L" " + std::to_wstring(flags);
    EnableWindow(GetDlgItem(gUi.window, kApplyButton), FALSE);
    EnableWindow(gUi.restore, FALSE);
    gBusy = true;
    int result = runElevated(args);
    gBusy = false;
    if (result != -2) refresh();
    else {
        EnableWindow(GetDlgItem(gUi.window, kApplyButton), TRUE);
        EnableWindow(gUi.restore, TRUE);
    }
    if (result == 0 && confirm) MessageBoxW(gUi.window, L"設定を保存し、読戻しました。VRChatを通常の方法で再起動すると反映されます。",
                                L"完了", MB_OK | MB_ICONINFORMATION);
    else if (result == 69) MessageBoxW(gUi.window, L"設定は保存されましたが、次回起動用の記録を更新できませんでした。", L"起動設定の記録", MB_ICONWARNING);
    else if (result != -2) MessageBoxW(gUi.window, (L"保存できませんでした。エラー番号: " + std::to_wstring(result) +
                                         L"\n現在値を再読込しました。バックアップは残っています。").c_str(),
                                       L"VRSS設定", MB_OK | MB_ICONERROR);
    return result == 0 || result == 69;
}
void restoreFromUi() {
    if (!gUi.loadedOk) return;
    const auto& v = gUi.loaded;
    SendMessageW(gUi.mode, CB_SETCURSEL, v.mode.found && v.mode.raw ? 1 : 0, 0);
    setText(gUi.area, numberText(v.area.found ? bitsFloat(v.area.raw) : 50.0f));
    check(gUi.aspectCheck, v.aspect.custom);
    setText(gUi.aspect, numberText(v.aspect.found ? bitsFloat(v.aspect.raw) : 1.0f));
    EnableWindow(gUi.aspect, v.aspect.custom);
    updateEstimate();
}

bool applyGazeFromUi(bool restore) {
    if (!gUi.gazeLoadedOk) return false;
    if (restore) {
        const auto& v = gUi.gazeLoaded.values;
        setText(gUi.gazeX, gazeNumberText(v.x_gain));
        setText(gUi.gazeY, gazeNumberText(std::abs(v.y_gain)));
        setText(gUi.gazeLeft, gazeNumberText(v.left_deg));
        setText(gUi.gazeRight, gazeNumberText(v.right_deg));
        check(gUi.gazeInvert, v.y_gain < 0);
        return true;
    }
    live::Values values{};
    if (!restore && (!parseNumber(getText(gUi.gazeX), values.x_gain) || !parseNumber(getText(gUi.gazeY), values.y_gain) ||
        values.y_gain < .1f || !parseNumber(getText(gUi.gazeLeft), values.left_deg) ||
        !parseNumber(getText(gUi.gazeRight), values.right_deg))) {
        MessageBoxW(gUi.window, L"倍率は0.1～2、左右の角度は-5～+5度で指定してください。", L"入力エラー", MB_ICONWARNING); return false;
    }
    if (!restore && isChecked(gUi.gazeInvert)) values.y_gain = -values.y_gain;
    if (!restore && !live::Valid(values)) {
        MessageBoxW(gUi.window, L"倍率は0.1～2、左右の角度は-5～+5度で指定してください。", L"入力エラー", MB_ICONWARNING); return false;
    }
    if (gUi.target == live::Target::Pimax && !compatiblePimaxDriver()) {
        MessageBoxW(gUi.window, L"Pimax用の補正対応ドライバーを確認できません。補正は変更していません。", L"対応ドライバーの確認", MB_ICONWARNING); return false;
    }
    gBusy = true;
    EnableWindow(gUi.gazeApply, FALSE); EnableWindow(gUi.gazeRestore, FALSE);
    auto result = gGazeStore.Apply(gUi.target, gUi.gazeLoaded.hash, values);
    int code = result.code;
    if (code && !result.changed && result.winError == ERROR_ACCESS_DENIED) {
        std::wstring args = L"--apply-gaze ";
        args += live::Name(gUi.target);
        args += L" " + numberText(values.x_gain) + L" " + numberText(values.y_gain) + L" " +
            numberText(values.left_deg) + L" " + numberText(values.right_deg);
        args += L" " + live::Wide(gUi.gazeLoaded.hash);
        code = runElevated(args);
    } else if (code == 0 && !saveStartupState()) code = 69;
    gBusy = false;
    refreshGaze();
    if (code == 0) {
        setText(gUi.gazeStatus, gOriginalText[gUi.gazeStatus] + L"\r\n補正を保存・読戻し済み。VRChatの再起動は不要です。");
    } else if (code == 61) MessageBoxW(gUi.window, L"読込後に補正が別の操作で変更されています。現在値を読み直しました。内容を確認してから再度操作してください。", L"変更の競合", MB_ICONWARNING);
    else if (code == 69) MessageBoxW(gUi.window, L"補正は保存されましたが、次回起動用の記録を更新できませんでした。", L"起動設定の記録", MB_ICONWARNING);
    else if (code != -2) MessageBoxW(gUi.window, (L"補正の保存・復元を完了できませんでした。現在値を再読込しました。\nエラー番号: " + std::to_wstring(code)).c_str(), L"視線補正", MB_ICONERROR);
    return code == 0 || code == 69;
}

void adjustGazeOffset(HWND edit, int direction) {
    float value{};
    if (!parseNumber(getText(edit), value) || value < -5.0f || value > 5.0f) {
        MessageBoxW(gUi.window, L"左右の角度は-5～+5度で指定してください。", L"入力エラー", MB_ICONWARNING);
        return;
    }
    const double next = std::max(-5.0, std::min(5.0,
        std::round((static_cast<double>(value) + direction * 0.1) * 1000000.0) / 1000000.0));
    setText(edit, gazeNumberText(static_cast<float>(next)));
}

struct NamedProfile {
    int mode{}, source{};
    float area{}, aspect{}, x{}, y{}, left{}, right{};
    bool customAspect{};
};
std::filesystem::path profileFolder() { return backupFolder() / L"profiles"; }
std::filesystem::path lastProfileFile() { return backupFolder() / L"last-profile.txt"; }
std::filesystem::path lastSourceFile() { return backupFolder() / L"last-source.txt"; }
std::filesystem::path previewZoomFile() { return backupFolder() / L"preview-zoom.txt"; }
bool savePreviewZoom(int zoom) {
    if (zoom<1 || zoom>4) return false;
    std::error_code ec;
    std::filesystem::create_directories(backupFolder(),ec);
    DWORD error{};
    return !ec && live::AtomicWrite(previewZoomFile(),std::to_string(zoom)+"\n",error);
}
int readPreviewZoom() {
    std::string bytes;
    if (!live::ReadBytes(previewZoomFile(),bytes,4) || bytes.size()!=2 ||
        bytes[0]<'1' || bytes[0]>'4' || bytes[1]!='\n') return 3;
    return bytes[0]-'0';
}
bool validProfileName(const std::wstring& name) {
    return !name.empty() && name.size() <= 40 && name != L"." && name != L".." &&
        name.front() != L' ' && name.back() != L' ' &&
        name.find_first_of(L"<>:\"/\\|?*\r\n\t") == std::wstring::npos;
}
bool saveLastProfile(const std::wstring& name) {
    if (!validProfileName(name)) return false;
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), int(name.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return false;
    std::string bytes(size_t(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), int(name.size()),
                            bytes.data(), length, nullptr, nullptr) != length) return false;
    std::error_code ec;
    std::filesystem::create_directories(backupFolder(), ec);
    DWORD error{};
    return !ec && live::AtomicWrite(lastProfileFile(), bytes + "\n", error);
}
bool readLastProfile(std::wstring& name) {
    std::string bytes;
    if (!live::ReadBytes(lastProfileFile(), bytes, 256)) return false;
    while (!bytes.empty() && (bytes.back() == '\r' || bytes.back() == '\n')) bytes.pop_back();
    if (bytes.empty()) return false;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()),
                                           nullptr, 0);
    if (length <= 0 || length > 40) return false;
    std::wstring candidate(size_t(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()),
                            candidate.data(), length) != length || !validProfileName(candidate)) return false;
    name = std::move(candidate);
    return true;
}
bool saveLastSource(int source) {
    if (source < 0 || source > 7) return false;
    std::error_code ec;
    std::filesystem::create_directories(backupFolder(), ec);
    DWORD error{};
    return !ec && live::AtomicWrite(lastSourceFile(), std::to_string(source) + "\n", error);
}
bool readLastSource(int& source) {
    std::string bytes;
    if (!live::ReadBytes(lastSourceFile(), bytes, 8) || bytes.size() < 2 || bytes.size() > 3 ||
        bytes[0] < '0' || bytes[0] > '7' ||
        (bytes.size() == 2 && bytes[1] != '\n') ||
        (bytes.size() == 3 && (bytes[1] != '\r' || bytes[2] != '\n'))) return false;
    source = bytes[0] - '0';
    return true;
}
void listProfiles() {
    auto name = getText(gUi.profileName);
    SendMessageW(gUi.profileName, CB_RESETCONTENT, 0, 0);
    std::error_code ec;
    auto folder = profileFolder();
    if (std::filesystem::exists(folder, ec)) {
        for (const auto& item : std::filesystem::directory_iterator(folder, ec)) {
            if (item.is_regular_file() && item.path().extension() == L".ini") {
                auto stem = item.path().stem().wstring();
                SendMessageW(gUi.profileName, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(stem.c_str()));
            }
        }
    }
    setText(gUi.profileName, name);
}
bool readProfile(const std::wstring& name, NamedProfile& p);
void restoreLastProfileSelection() {
    std::wstring name;
    if (!readLastProfile(name)) return;
    NamedProfile profile{};
    if (!readProfile(name, profile)) return;
    const LRESULT index = SendMessageW(gUi.profileName, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                      reinterpret_cast<LPARAM>(name.c_str()));
    if (index == CB_ERR) return;
    SendMessageW(gUi.profileName, CB_SETCURSEL, index, 0);
    SendMessageW(gUi.gazeSource, CB_SETCURSEL, profile.source, 0);
    gSelectedSource = profile.source;
}
void restoreLastSourceSelection() {
    int source{};
    if (!readLastSource(source)) return;
    SendMessageW(gUi.gazeSource, CB_SETCURSEL, source, 0);
    gSelectedSource = source;
}
bool readProfile(const std::wstring& name, NamedProfile& p) {
    if (!validProfileName(name)) return false;
    std::ifstream in(profileFolder() / (name + L".ini"), std::ios::binary);
    std::string magic;
    if (!std::getline(in, magic) || magic != "VRSS-TUNER-PROFILE-1") return false;
    int custom{};
    if (!(in >> p.mode >> p.area >> p.aspect >> custom >> p.source >> p.x >> p.y >> p.left >> p.right)) return false;
    std::string extra;
    if (in >> extra) return false;
    p.customAspect = custom == 1;
    live::Values gaze{p.left, p.right, p.x, p.y};
    return (p.mode == 0 || p.mode == 1) && p.area >= 1 && p.area <= 100 &&
        p.aspect >= .5f && p.aspect <= 2 && (custom == 0 || custom == 1) &&
        (p.source >= 0 && p.source <= 7) && live::Valid(gaze);
}
bool profileFromUi(NamedProfile& p) {
    p.mode = int(SendMessageW(gUi.mode, CB_GETCURSEL, 0, 0));
    p.source = int(SendMessageW(gUi.gazeSource, CB_GETCURSEL, 0, 0));
    p.customAspect = isChecked(gUi.aspectCheck);
    if (!parseNumber(getText(gUi.area), p.area) ||
        !parseNumber(getText(gUi.aspect), p.aspect) ||
        !parseNumber(getText(gUi.gazeX), p.x) ||
        !parseNumber(getText(gUi.gazeY), p.y) ||
        !parseNumber(getText(gUi.gazeLeft), p.left) ||
        !parseNumber(getText(gUi.gazeRight), p.right)) return false;
    if (isChecked(gUi.gazeInvert)) p.y = -p.y;
    live::Values gaze{p.left, p.right, p.x, p.y};
    return (p.mode == 0 || p.mode == 1) && (p.source >= 0 && p.source <= 7) &&
        p.area >= 1 && p.area <= 100 && p.aspect >= .5f && p.aspect <= 2 && live::Valid(gaze);
}
bool writeProfile(const std::wstring& name, const NamedProfile& p) {
    if (!validProfileName(name)) return false;
    std::error_code ec;
    auto folder = profileFolder();
    std::filesystem::create_directories(folder, ec);
    if (ec) return false;
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9);
    out << "VRSS-TUNER-PROFILE-1\n" << p.mode << ' ' << p.area << ' ' << p.aspect << ' '
        << (p.customAspect ? 1 : 0) << ' ' << p.source << ' ' << p.x << ' ' << p.y
        << ' ' << p.left << ' ' << p.right << '\n';
    DWORD error{};
    return live::AtomicWrite(folder / (name + L".ini"), out.str(), error);
}
void saveProfileFromUi() {
    const auto name = getText(gUi.profileName);
    NamedProfile p{};
    if (!validProfileName(name) || !profileFromUi(p)) {
        MessageBoxW(gUi.window, L"プロファイル名または設定値を確認してください。", L"プロファイル", MB_ICONWARNING);
        return;
    }
    if (!writeProfile(name, p)) {
        MessageBoxW(gUi.window, L"プロファイルを保存できません。", L"プロファイル", MB_ICONERROR); return;
    }
    listProfiles();
    setText(gUi.status, L"プロファイルを保存しました。現在のドライバー設定は変更していません。");
}
bool loadProfileToUi() {
    NamedProfile p{};
    if (!readProfile(getText(gUi.profileName), p)) {
        MessageBoxW(gUi.window, L"保存済みプロファイルを読み取れません。", L"プロファイル", MB_ICONWARNING);
        return false;
    }
    SendMessageW(gUi.mode, CB_SETCURSEL, p.mode, 0);
    setText(gUi.area, numberText(p.area));
    check(gUi.aspectCheck, p.customAspect);
    setText(gUi.aspect, numberText(p.aspect));
    EnableWindow(gUi.aspect, p.customAspect);
    SendMessageW(gUi.gazeSource, CB_SETCURSEL, p.source, 0);
    gSelectedSource = p.source;
    refreshGaze();
    setText(gUi.gazeX, gazeNumberText(p.x));
    setText(gUi.gazeY, gazeNumberText(std::abs(p.y)));
    setText(gUi.gazeLeft, gazeNumberText(p.left));
    setText(gUi.gazeRight, gazeNumberText(p.right));
    check(gUi.gazeInvert, p.y < 0);
    updateEstimate();
    return true;
}
void switchProfile() {
    const auto name = getText(gUi.profileName);
    NamedProfile selected{};
    if (!readProfile(name, selected)) {
        MessageBoxW(gUi.window, L"保存済みプロファイルを読み取れません。", L"プロファイル", MB_ICONWARNING);
        return;
    }
    if (selected.source != 0 && processCount(L"vrss_gaze_provider.exe")) {
        MessageBoxW(gUi.window,
            L"Pimax経路が現在動作中です。共通経路のプロファイルは適用できません。\n"
            L"共通経路へ切り替える場合は、先にVRChatとPimax側プロバイダーを通常終了してください。",
            L"視線経路の競合", MB_ICONWARNING);
        return;
    }
    if (selected.source == 0 && processCount(L"common_vrss_provider.exe")) {
        MessageBoxW(gUi.window,
            L"共通プロバイダーが現在動作中です。Pimax経路のプロファイルは適用できません。\n"
            L"先に共通プロバイダーを停止してください。",
            L"視線経路の競合", MB_ICONWARNING);
        return;
    }
    live::Snapshot available{};
    if (!gGazeStore.Read(selected.source == 0 ? live::Target::Pimax : live::Target::Common, available)) {
        MessageBoxW(gUi.window, L"この経路の視線補正ファイルがありません。適用していません。", L"プロファイル", MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(gUi.window, L"このプロファイルを画質と視線補正へ適用します。画質設定はVRChatの再起動後に反映します。続けますか？",
        L"プロファイルを適用", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return;
    if (!loadProfileToUi()) return;
    NamedProfile wanted{};
    if (!profileFromUi(wanted)) return;
    const auto& current = gUi.loaded;
    const bool driverChanged = !current.mode.found || current.mode.raw != unsigned(wanted.mode) ||
        !current.area.found || std::abs(bitsFloat(current.area.raw) - wanted.area) > .00001f ||
        current.aspect.custom != wanted.customAspect ||
        (wanted.customAspect && (!current.aspect.found || std::abs(bitsFloat(current.aspect.raw) - wanted.aspect) > .00001f));
    const auto& gaze = gUi.gazeLoaded.values;
    const bool gazeChanged = std::abs(gaze.x_gain-wanted.x) > .00001f ||
        std::abs(gaze.y_gain-wanted.y) > .00001f ||
        std::abs(gaze.left_deg-wanted.left) > .00001f ||
        std::abs(gaze.right_deg-wanted.right) > .00001f;
    if (driverChanged && !applyFromUi(false)) return;
    if (gazeChanged && !applyGazeFromUi(false)) {
        MessageBoxW(gUi.window, driverChanged ? L"画質設定のみ保存されました。視線補正は適用できていません。" :
            L"視線補正を適用できませんでした。", L"一部だけ反映", MB_ICONWARNING);
    } else {
        const bool profileSaved = saveLastProfile(name);
        const bool sourceSaved = saveLastSource(wanted.source);
        if (!profileSaved || !sourceSaved) {
            MessageBoxW(gUi.window, L"プロファイルは適用されましたが、次回起動用の選択を保存できませんでした。",
                        L"プロファイル", MB_ICONWARNING);
        } else {
            MessageBoxW(gUi.window, driverChanged ? L"プロファイルを適用しました。画質への反映にはVRChatの再起動が必要です。" : L"プロファイルを適用しました。",
                        L"適用完了", MB_ICONINFORMATION);
        }
    }
}

bool recent(uint64_t tick, uint64_t now) { return tick && now >= tick && now - tick < 3000; }
bool currentFile(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) return false;
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER now{}, written{};
    now.LowPart = time.dwLowDateTime; now.HighPart = time.dwHighDateTime;
    written.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    written.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    return now.QuadPart >= written.QuadPart && now.QuadPart - written.QuadPart < 30000000ULL;
}
int sourceFromRunRoute(const std::string& route) {
    if (route == "Baballonia") return 1;
    if (route == "PSVR2Toolkit") return 2;
    if (route == "SRanibro") return 3;
    if (route == "ViveProEye") return 4;
    if (route == "ViveStreaming") return 5;
    if (route == "SteamVREye") return 6;
    if (route == "Varjo") return 7;
    return -1;
}
bool readRecordedProviderTelemetry(telemetry::Snapshot& result, int& routeSource) {
    routeSource = -1;
    const auto root = backupFolder();
    if (root.empty()) return false;
    std::string raw;
    if (!live::ReadBytes(root / L"active-run.txt", raw, 4096)) return false;
    if (raw.size() >= 3 && raw.compare(0, 3, "\xef\xbb\xbf") == 0) raw.erase(0, 3);
    while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n' || raw.back() == ' ')) raw.pop_back();
    if (raw.empty()) return false;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), nullptr, 0);
    if (count <= 0) return false;
    std::wstring wide(size_t(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), wide.data(), count)) return false;
    std::error_code error;
    const auto runs = std::filesystem::weakly_canonical(root / L"runs", error);
    if (error) return false;
    const auto run = std::filesystem::weakly_canonical(std::filesystem::path(wide), error);
    if (error) return false;
    const auto prefix = runs.wstring() + L"\\";
    const auto runText = run.wstring();
    if (runText.size() <= prefix.size() || _wcsnicmp(runText.c_str(), prefix.c_str(), prefix.size()) != 0) return false;
    const auto status = run / L"status.json";
    if (!currentFile(status)) return false;
    std::string json;
    if (!live::ReadBytes(status, json, 2048) || !telemetry::Parse(json, result)) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, result.pid);
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    if (running) {
        std::string route;
        if (live::ReadBytes(run / L"input-route.txt", route, 64)) {
            while (!route.empty() && (route.back() == '\r' || route.back() == '\n' || route.back() == ' ')) route.pop_back();
            routeSource = sourceFromRunRoute(route);
        }
    }
    return running;
}
bool readProviderTelemetry(telemetry::Snapshot& result, int& routeSource) {
    if(readRecordedProviderTelemetry(result,routeSource))return true;
    if(!processCount(L"common_vrss_provider.exe"))return false;
    static uint64_t searchedAt=0;
    const auto now=GetTickCount64();
    if(now-searchedAt<1000)return false;
    searchedAt=now;
    std::error_code error;
    const auto runs=backupFolder()/L"runs";
    for(std::filesystem::directory_iterator item(runs,error),end;!error && item!=end;item.increment(error)) {
        const auto run=item->path();
        if(!item->is_directory(error) || item->is_symlink(error) || error || !currentFile(run/L"status.json"))continue;
        std::string json,route;
        telemetry::Snapshot candidate{};
        if(!live::ReadBytes(run/L"status.json",json,2048) || !telemetry::Parse(json,candidate) ||
           !live::ReadBytes(run/L"input-route.txt",route,64))continue;
        while(!route.empty() && (route.back()=='\r'||route.back()=='\n'||route.back()==' '))route.pop_back();
        const int source=sourceFromRunRoute(route);
        if(source<1)continue;
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,candidate.pid);
        if(!process)continue;
        wchar_t executable[32768]{};DWORD length=DWORD(std::size(executable));
        const bool matches=WaitForSingleObject(process,0)==WAIT_TIMEOUT &&
            QueryFullProcessImageNameW(process,0,executable,&length) &&
            _wcsicmp(std::filesystem::path(executable).filename().c_str(),L"common_vrss_provider.exe")==0;
        CloseHandle(process);
        if(!matches)continue;
        DWORD writeError{};
        live::AtomicWrite(backupFolder()/L"active-run.txt",run.u8string()+"\n",writeError);
        result=candidate;routeSource=source;return true;
    }
    return false;
}
void updateLiveMonitor(bool vendor, bool common) {
    const uint64_t now = GetTickCount64();
    gLive.selectedSource = gSelectedSource.load();
    gLive.common = common;
    telemetry::Snapshot snapshot{};
    int activeRoute=-1;
    const bool telemetryReady = common && readProviderTelemetry(snapshot, activeRoute);
    gLive.vendor = vendor && !telemetryReady;
    gLive.routeVerified = telemetryReady && activeRoute>=1;
    gLive.source = gLive.routeVerified ? activeRoute : vendor ? 0 : gLive.selectedSource;
    gLive.inputRunning = gLive.source==0 ? vendor :
        gLive.source==1 ? telemetryReady :
        gLive.source==2 ? processCount(L"psvr2_gaze_bridge.exe")!=0 :
        gLive.source==3 ? processCount(L"SRanibro.exe")!=0 :
        (gLive.source==4||gLive.source==5) ? processCount(L"ViveStreamingGazeBridge.exe")!=0 :
        gLive.source==6 ? processCount(L"steamvr_gaze_bridge.exe")!=0 :
        processCount(L"varjo_gaze_bridge.exe")!=0;
    gLive.relayFresh = gLive.relaySent = false;
    gLive.receivedPerSecond = gLive.relayPerSecond = 0;
    gLive.providerReady = gLive.source!=0 && telemetryReady && snapshot.registered;
    if (gLive.source >= 1) {
        if (gLive.providerReady && snapshot.pid != gLive.providerPid) {
            gLive.lastProviderSequence = 0;
            gLive.providerSequenceAt = 0;
            gLive.lastReceived = snapshot.oscReadings;
        }
        if (gLive.providerReady && snapshot.sourceSequence != gLive.lastProviderSequence) {
            gLive.providerSequenceAt = now;
            gLive.lastProviderSequence = snapshot.sourceSequence;
        }
        gLive.relayFresh = gLive.providerReady && snapshot.fresh && recent(gLive.providerSequenceAt, now);
        gLive.relaySent = gLive.relayFresh;
        gLive.receivedPerSecond = gLive.relayFresh ? 1 : 0;
        if(gLive.source==1) {
            gLive.receivedPerSecond=snapshot.oscReadings>=gLive.lastReceived?snapshot.oscReadings-gLive.lastReceived:0;
            gLive.lastReceived=snapshot.oscReadings;
        }
        gLive.relayPerSecond = gLive.receivedPerSecond;
    }
    gLive.consumerReady = gLive.accepted = gLive.failure = false;
    gLive.acceptedPerSecond = 0;
    if (gLive.providerReady) {
        gLive.provider = snapshot;
        if (snapshot.pid != gLive.providerPid) {
            gLive.providerPid = snapshot.pid;
            gLive.lastAccepted = snapshot.accepted();
            gLive.lastFailures = snapshot.submitFailures;
            gLive.acceptedAt = gLive.failureAt = 0;
        } else {
            const uint64_t accepted = snapshot.accepted();
            gLive.acceptedPerSecond = accepted >= gLive.lastAccepted ? accepted - gLive.lastAccepted : 0;
            if (accepted > gLive.lastAccepted) gLive.acceptedAt = now;
            if (snapshot.submitFailures > gLive.lastFailures) gLive.failureAt = now;
            gLive.lastAccepted = accepted;
            gLive.lastFailures = snapshot.submitFailures;
        }
        gLive.consumerReady = snapshot.consumerCount > 0 && snapshot.queryStatus == 0;
        gLive.failure = snapshot.queryStatus != 0 || recent(gLive.failureAt, now);
        gLive.accepted = gLive.consumerReady && snapshot.fresh && recent(gLive.acceptedAt, now) && !gLive.failure;
    } else {
        gLive.providerPid = 0;
        gLive.acceptedAt = gLive.failureAt = 0;
    }
    if (gUi.liveMonitor) InvalidateRect(gUi.liveMonitor, nullptr, FALSE);
}
void updateGazeMap() {
    if (!gUi.liveMonitor || gUi.panel != 2) return;
    if (gLive.vendor) {
        std::array<float, 4> location{};
        if (gPimaxGaze.Read(location)) { gPimaxLocation=location; gPimaxFreshAt=GetTickCount64(); }
        if (gNvidiaQueue.Read(location)) { gNvidiaLocation=location; gNvidiaFreshAt=GetTickCount64(); }
    } else if (gLive.common) {
        telemetry::Snapshot snapshot{};
        int route=-1;
        if (readProviderTelemetry(snapshot,route) && snapshot.registered && snapshot.pid==gLive.providerPid) {
            const auto now=GetTickCount64();
            if (snapshot.sourceSequence!=gLive.lastProviderSequence) {
                gLive.providerSequenceAt=now;
                gLive.lastProviderSequence=snapshot.sourceSequence;
            }
            gLive.provider=snapshot;
            if (gLive.source>=1) gLive.relayFresh=gLive.relaySent=
                snapshot.fresh && recent(gLive.providerSequenceAt,now);
        }
    }
    InvalidateRect(gUi.liveMonitor,nullptr,FALSE);
}
void updateInputStatus() {
    if (!gUi.inputStatus) return;
    const int source = gSelectedSource.load();
    const bool vendor = processCount(L"vrss_gaze_provider.exe") != 0;
    const bool common = processCount(L"common_vrss_provider.exe") != 0;
    const bool sranibro = processCount(L"SRanibro.exe") != 0;
    const bool xr5 = processCount(L"platform_runtime_XR5EYECHIP_WIN10_x64.exe") != 0;
    const bool bridgeInstalled=GetFileAttributesW(
        L"C:\\Program Files\\Pimax\\Runtime\\drivers\\sranibro_eye\\bin\\win64\\driver_sranibro_eye.dll")!=INVALID_FILE_ATTRIBUTES;
    const PimaxServiceState pimaxService=pimaxServiceState();
    std::wstring status;
    if (source == 0) {
        status = vendor ? L"Pimax経路: NVIDIAプロバイダー起動中" : L"Pimax経路: NVIDIAプロバイダー停止中";
        status += xr5 ? L"\r\n入力候補: Pimax純正XR5ランタイム（視線値は未計測）" :
            sranibro && bridgeInstalled ? L"\r\n入力候補: SRanibro → Pimaxブリッジ（視線値は未計測）" :
            sranibro ? L"\r\nSRanibro起動中・Pimaxブリッジ未検出" :
            pimaxService.disabled ? L"\r\n純正視線サービスは無効です。復帰が必要です。" :
            pimaxService.running ? L"\r\n純正サービス起動中。視線が戻らなければPimaxサービスを再起動してください。" :
            L"\r\nPimax視線入力を待っています。";
        status += L"\r\nTunerのOSC転送は使用しません。";
    } else if (source == 1) {
        telemetry::Snapshot osc{}; int route=-1;
        const bool receiving=common && readProviderTelemetry(osc,route) && route==1;
        status = receiving ? L"Baballonia OSC: バックグラウンドで受信中" : L"Baballonia OSC: 停止中";
        if(receiving) {
            status+=L"\r\nOSCパケット " + std::to_wstring(osc.oscDatagrams) + L" / 視線値 " + std::to_wstring(osc.oscReadings);
            if(!osc.oscDatagrams)status+=L"\r\n未受信: Baballoniaの送信先を127.0.0.1と下記の受信ポートに設定してください。";
            else if(!osc.oscReadings)status+=L"\r\nパケットは受信済みですが、対応する視線値がありません。Eye出力を確認してください。";
            else if(!osc.fresh)status+=L"\r\n新しい左右4軸の視線値を待っています。";
        }
        status+=L"\r\nBaballonia → Tuner受信ポート → VRCFaceTracking転送先。カメラの再接続は不要です。";
    } else {
        const bool toolkit = source == 2;
        const bool bridge = processCount(L"psvr2_gaze_bridge.exe") != 0;
        const bool vive = source == 4 || source == 5;
        const bool viveBridge = processCount(L"ViveStreamingGazeBridge.exe") != 0;
        const bool steamBridge = processCount(L"steamvr_gaze_bridge.exe") != 0;
        const bool varjoBridge = processCount(L"varjo_gaze_bridge.exe") != 0;
        status = toolkit ?
            (bridge ? L"PSVR2 Toolkit CAPIブリッジ起動中" : L"PSVR2 Toolkit CAPIブリッジ停止中") :
            vive ? (viveBridge ? L"VIVE視線ブリッジ起動中（実機未検証）" : L"VIVE視線ブリッジ停止中") :
            source==6 ? (steamBridge ? L"SteamVR視線ブリッジ起動中（実機未検証）" : L"SteamVR視線ブリッジ停止中") :
            source==7 ? (varjoBridge ? L"Varjo視線ブリッジ起動中（実機未検証）" : L"Varjo視線ブリッジ停止中") :
            (sranibro ? L"SRanibro起動中" : L"SRanibro停止中");
        status += common ? L"\r\n共通プロバイダー起動中。視線の受理は下の状態で確認。" :
                  toolkit ? L"\r\n共通プロバイダー停止中。Start-PSVR2-Toolkit.cmdを実行。" :
                  vive ? L"\r\n共通プロバイダー停止中。VIVE起動スクリプトを実行。" :
                  source==6 ? L"\r\n共通プロバイダー停止中。Start-SteamVR-Eye.cmdを実行。" :
                  source==7 ? L"\r\n共通プロバイダー停止中。Start-Varjo.cmdを実行。" :
                         L"\r\n共通プロバイダー停止中。Start-SRanibro.cmdを実行。";
        if (vendor) status += L"\r\nPimax視線プロバイダーを検出。選択した経路へ切り替えると送信だけを停止します。";
        status += L"\r\nTunerのOSC転送は使用しません。";
    }
    updateLiveMonitor(vendor, common);
    if (gLive.routeVerified && source!=gLive.source)
        status += L"\r\n選択中の経路と稼働中の経路が異なります。稼働: " +
            std::wstring(gazeSourceName(gLive.source,false));
    setText(gUi.inputStatus, status);
    const bool changing=gRouteOperation && WaitForSingleObject(gRouteOperation,0)==WAIT_TIMEOUT;
    if(changing)status+=gRouteStopping?L"\r\n視線経路をすべて停止しています。":L"\r\n選択した視線経路へ切り替えています。";
    setText(gUi.inputStatus,status);
    setText(gUi.inputStart,(vendor || common)?L"選択した経路に切替":L"視線経路を起動");
    EnableWindow(gUi.inputPort, source == 1 && !changing);
    EnableWindow(gUi.inputForward, source == 1 && !changing);
    EnableWindow(gUi.inputStart,!changing);
    EnableWindow(gUi.inputStop,TRUE);
}
void startRouteOperation(bool stop) {
    if(stop && gRouteStopping && gRouteOperation && WaitForSingleObject(gRouteOperation,0)==WAIT_TIMEOUT)return;
    if(!stop && gRouteOperation && WaitForSingleObject(gRouteOperation,0)==WAIT_TIMEOUT)return;
    const int source=gSelectedSource.load();
    if(source<0 || source>7)return;
    float port=9020,forward=8888;
    if(!stop && source==1 &&
       (!parseNumber(getText(gUi.inputPort),port) || port!=std::floor(port) || port<1 || port>65535 ||
        !parseNumber(getText(gUi.inputForward),forward) || forward!=std::floor(forward) || forward<0 || forward>65535 ||
        port==forward || port==5556 || forward==5556)) {
        MessageBoxW(gUi.window,L"ポート番号を確認してください。転送先は0で無効にできます。",L"視線入力",MB_ICONWARNING);return;
    }
    auto script=gResearch/L"Set-Gaze-Route.ps1";
    if(!std::filesystem::is_regular_file(script))script=gResearch/L"vrss-tuner/portable/Set-Gaze-Route.ps1";
    if(!std::filesystem::is_regular_file(script)) {
        MessageBoxW(gUi.window,L"起動スクリプトを開けませんでした。",L"視線入力",MB_ICONWARNING);return;
    }
    if(stop) {
        gFrameRuntime.Stop();
        std::ofstream paused(backupFolder()/L"gaze-paused.txt");paused<<"User requested stop\n";
    }
    const wchar_t* routes[]={L"Pimax",L"Baballonia",L"PSVR2Toolkit",L"SRanibro",L"ViveProEye",L"ViveStreaming",L"SteamVREye",L"Varjo"};
    std::wstring args=L"-NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+L"\" ";
    args+=stop?L"-Stop":L"-InputRoute "+std::wstring(routes[source])+L" -OscPort "+std::to_wstring(int(port))+L" -OscForward "+std::to_wstring(int(forward));
    wchar_t system[32768]{};GetSystemDirectoryW(system,DWORD(std::size(system)));
    const auto shell=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
    SHELLEXECUTEINFOW launch{sizeof(launch)};launch.hwnd=gUi.window;launch.fMask=SEE_MASK_NOCLOSEPROCESS;
    launch.lpFile=shell.c_str();launch.lpParameters=args.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch) || !launch.hProcess) {
        MessageBoxW(gUi.window,L"起動スクリプトを開けませんでした。",L"視線入力",MB_ICONWARNING);return;
    }
    if(gRouteOperation)CloseHandle(gRouteOperation);
    gRouteOperation=launch.hProcess;gRouteStopping=stop;
    updateInputStatus();
}
void pollRouteOperation() {
    if(!gRouteOperation || WaitForSingleObject(gRouteOperation,0)==WAIT_TIMEOUT)return;
    DWORD result{};GetExitCodeProcess(gRouteOperation,&result);CloseHandle(gRouteOperation);gRouteOperation=nullptr;
    if(result) {
        const auto detail=frame::LogTail(backupFolder()/L"route-error.txt");
        MessageBoxW(gUi.window,(std::wstring(L"視線経路の操作を完了できませんでした。\r\n")+detail).c_str(),L"視線入力",MB_ICONWARNING);
    }
}

void updateFrameStatus() {
    setText(gUi.frameStatus,gFrameRuntime.Message());
    EnableWindow(gUi.frameStart,!gFrameRuntime.WorkerAlive());
    EnableWindow(gUi.frameLaunch,gFrameRuntime.CanLaunchGame() && !vrchatRunning() && !processCount(L"Unity.exe"));
}
void prepareFrameProfile() {
    if(vrchatRunning())throw std::runtime_error("Close VRChat before the initial Frame provider connection");
    Session session;ProfileValues original{};
    if(session.open()!=NVAPI_OK || session.readAll(original)!=NVAPI_OK)
        throw std::runtime_error("NVIDIA VRChat profile is unavailable");
    if(!original.allow.found || original.allow.raw!=1)
        throw std::runtime_error("The VRChat profile does not permit VRSS; no allow flag or profile was fabricated");
    if(original.mode.found && original.mode.raw!=0)return;
    if(!saveBackup(original))throw std::runtime_error("Cannot back up VRSS settings");
    if(session.write(kMode,1)!=NVAPI_OK || NvAPI_DRS_SaveSettings(session.session)!=NVAPI_OK)
        throw std::runtime_error("Cannot enable VRSS; see the saved profile backup");
    Session verify;ProfileValues applied{};
    if(verify.open()!=NVAPI_OK || verify.readAll(applied)!=NVAPI_OK || !applied.mode.found || applied.mode.raw!=1)
        throw std::runtime_error("VRSS mode read-back failed; see the saved profile backup");
}
struct FrameGateStore {
    std::filesystem::path output;
    std::filesystem::path pending;
    bool ownsPending{};
    frame::GateValue Read() {
        Session session;Value value{};
        if(session.open()!=NVAPI_OK || session.read(kHmdGate,value)!=NVAPI_OK)
            throw std::runtime_error("Cannot read the VRChat HMD gate");
        return {value.found,value.custom,value.raw};
    }
    void Backup(frame::GateValue value) {
        DWORD error{};
        if(!live::WriteNew(output/L"gate-before.txt",frame::SerializeGateBackup(value),error))
            throw std::runtime_error("Cannot preserve the original HMD gate: "+std::to_string(error));
        if(!pending.empty()) {
            if(!live::WriteNew(pending,frame::SerializeGateBackup(value),error))
                throw std::runtime_error("Cannot preserve the pending gate recovery record: "+std::to_string(error));
            ownsPending=true;
        }
        std::cout<<"gateBefore found="<<value.found<<" custom="<<value.custom<<" raw="<<value.raw<<'\n';
    }
    void Save(frame::GateValue value) {
        Session session;
        if(session.open()!=NVAPI_OK)throw std::runtime_error("Cannot open the VRChat profile");
        const auto status=value.custom?session.write(kHmdGate,value.raw):session.erase(kHmdGate);
        if(status!=NVAPI_OK || NvAPI_DRS_SaveSettings(session.session)!=NVAPI_OK)
            throw std::runtime_error("Cannot save the VRChat HMD gate");
    }
    void WriteTest() { Save({true,true,0});std::cout<<"gateTestApplied=0\n"; }
    void Restore(frame::GateValue value) { Save(value); }
    std::optional<frame::GateValue> Pending() {
        if(pending.empty() || !std::filesystem::exists(pending))return {};
        const auto attributes=GetFileAttributesW(pending.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
           std::filesystem::file_size(pending)>256)throw std::runtime_error("Unsafe gate recovery record");
        std::ifstream in(pending,std::ios::binary);
        const auto value=frame::ReadGateBackup(in);
        if(!value)throw std::runtime_error("Invalid gate recovery record; refusing new settings");
        return value;
    }
    void ClearPending() {
        if(!pending.empty() && !DeleteFileW(pending.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Gate restored but recovery record could not be cleared");
        ownsPending=false;
    }
    void RequireExistingVrss() {
        if(vrchatRunning() || processCount(L"Unity.exe"))throw std::runtime_error("Close VRChat and Unity before applying the test gate");
        Session session;ProfileValues current{};
        if(session.open()!=NVAPI_OK || session.readAll(current)!=NVAPI_OK ||
           !current.allow.found || current.allow.raw!=1 || !current.mode.found || !current.mode.raw || current.mode.raw>2 || !current.layout.found || current.layout.raw!=4)
            throw std::runtime_error("Steam Frame requires a permitted VRChat VRSS profile, mode 1/2 and layout 4");
        std::cout<<"vrssMode="<<current.mode.raw<<" vrssAllow="<<current.allow.raw
                 <<" vrLayout="<<current.layout.raw<<" profileScope=VRChat gateOnly=1\n";
    }
};
int frameWorker(int argc,wchar_t** argv) {
    if(argc!=6 || !frame::ValidRunId(argv[4]))return 80;
    const auto profile=hmd::ParseProfile(argv[5]);if(!profile || *profile!=hmd::Profile::SteamFrame)return 80;
    try {
        size_t used=0;
        const auto pid=std::stoul(argv[2],&used);
        if(used!=wcslen(argv[2]))return 80;
        const auto created=std::stoull(argv[3],&used);
        if(used!=wcslen(argv[3]))return 80;
        auto owner=frame::OpenOwner(DWORD(pid),created);
        if(!owner.get())return 81;
        const auto root=backupFolder();if(root.empty())return 82;
        const auto run=root/L"frame-runs"/argv[4];frame::RequirePlainDirectory(run);
        frame::Handle singleton(CreateMutexW(nullptr,FALSE,frame::kWorkerMutex));
        if(!singleton.get() || GetLastError()==ERROR_ALREADY_EXISTS)return 83;
        if(processCount(L"common_vrss_provider.exe") || processCount(L"vrss_gaze_provider.exe"))return 84;
        if(vrchatRunning() || processCount(L"Unity.exe"))return 87;
        std::ofstream log(run/L"worker.log",std::ios::binary);
        if(!log)return 85;
        frame::LogRedirect redirect(log);std::cout<<std::unitbuf;
        const bool experimental=*profile==hmd::Profile::SteamFrame;
        FrameGateStore store{run,root/L"frame-gate-pending.txt"};
        frame::TestGateOverride<FrameGateStore> gate(store);
        std::vector<std::wstring> arguments{L"provider",L"--source",L"openvr",L"--hmd-profile",hmd::ProfileName(*profile),L"--config",
            gGazeStore.common.wstring(),L"--output",run.wstring()};
        if(experimental) {arguments.push_back(L"--frame-session");arguments.push_back(L"1");}
        std::vector<wchar_t*> raw;for(auto& argument:arguments)raw.push_back(argument.data());
        uint64_t nextLogCheck=0;
        int result=RunVrssProvider(int(raw.size()),raw.data(),[&] {
            if(WaitForSingleObject(owner.get(),0)!=WAIT_TIMEOUT)return false;
            const auto now=GetTickCount64();
            if(now>=nextLogCheck) {
                if(log.tellp()>std::streamoff(4*1024*1024)) {
                    log.close();log.open(run/L"worker.log",std::ios::binary|std::ios::trunc);
                }
                nextLogCheck=now+60000;
            }
            return true;
        },[&] {
            frame::RecoverPendingGate(store);
            prepareFrameProfile();
            if(experimental) {store.RequireExistingVrss();gate.Apply();}
        });
        if(experimental || std::filesystem::exists(store.pending)) {
            try {
                gate.Recover();
                if(store.ownsPending)store.ClearPending();
                if(store.Pending())throw std::runtime_error("A previous gate recovery remains pending; no new session was applied");
                std::cout<<"gateRestored=1\n";
            } catch(const std::exception& error) {std::cerr<<"RECOVERY_REQUIRED: "<<error.what()<<'\n';result=90;}
        }
        std::ofstream(run/L"exit-code.txt")<<result<<'\n';
        return result;
    } catch(...) { return 86; }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gUi.window = window;
        gFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        control(L"STATIC", L"VRSS Tuner", SS_LEFT, 20, 14, 205, 25);
        control(L"STATIC", L"プロファイル", SS_LEFT, 230, 14, 95, 25);
        gUi.profileName = control(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 328, 10, 165, 250, kProfileName);
        gUi.profileSave = control(L"BUTTON", L"保存", WS_TABSTOP | BS_PUSHBUTTON, 505, 10, 72, 27, kProfileSave);
        gUi.profileSwitch = control(L"BUTTON", L"適用", WS_TABSTOP | BS_PUSHBUTTON, 585, 10, 85, 27, kProfileSwitch);
        gUi.driverTab = control(L"BUTTON", L"画質", BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_TABSTOP, 20, 47, 110, 30, kDriverTab);
        gUi.gazeTab = control(L"BUTTON", L"視線補正", BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_TABSTOP, 140, 47, 110, 30, kGazeTab);
        gUi.inputTab = control(L"BUTTON", L"視線入力", BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_TABSTOP, 260, 47, 110, 30, kInputTab);
        gUi.frameTab=control(L"BUTTON",L"Steam Frame",BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_TABSTOP,380,47,130,30,kFrameTab);
        gUi.language = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                               520, 47, 150, 100, kLanguage);
        SendMessageW(gUi.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"日本語"));
        SendMessageW(gUi.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(gUi.language, CB_SETCURSEL, language::english ? 1 : 0, 0);
        gCreatingPanel = 0;
        control(L"STATIC", L"VRSSモード", SS_LEFT, 20, 102, 170, 23);
        gUi.mode = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 205, 98, 340, 100, kModeCombo);
        for (const wchar_t* name : {L"オフ", L"オン"}) {
            const auto translated = language::Text(name);
            SendMessageW(gUi.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(translated.c_str()));
        }
        gUi.restartBadges.push_back(control(L"STATIC", L"● 要再起動", SS_LEFT, 565, 102, 110, 24));
        gUi.aspectCheck = control(L"BUTTON", L"縦横比を指定", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 145, 185, 26, kAspectCheck);
        gUi.aspect = control(L"EDIT", L"1", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 225, 144, 95, 27, kAspectEdit);
        control(L"STATIC", L"0.5～2", SS_LEFT, 335, 148, 100, 25);
        gUi.restartBadges.push_back(control(L"STATIC", L"● 要再起動", SS_LEFT, 565, 148, 110, 24));
        control(L"STATIC", L"領域プレビュー", SS_LEFT,20,194,210,23);
        control(L"STATIC", L"大きさ（1～100）", SS_LEFT,20,226,180,23);
        gUi.area = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 205, 222, 90, 27, kAreaEdit);
        gUi.areaSlider = control(TRACKBAR_CLASSW,L"",TBS_NOTICKS | WS_TABSTOP,305,220,245,30,kAreaSlider);
        SendMessageW(gUi.areaSlider,TBM_SETRANGE,TRUE,MAKELPARAM(1,100));
        SendMessageW(gUi.areaSlider,TBM_SETPAGESIZE,0,5);
        gUi.restartBadges.push_back(control(L"STATIC", L"● 要再起動", SS_LEFT, 565, 226, 110, 24));
        control(L"STATIC", L"試算用 MSAA", SS_LEFT,350,264,110,23);
        gUi.estimateMsaa=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,470,260,160,100,kEstimateMsaa);
        for(const wchar_t* name:{L"MSAA x2",L"MSAA x4",L"MSAA x8"}) SendMessageW(gUi.estimateMsaa,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(gUi.estimateMsaa,CB_SETCURSEL,1,0);
        gUi.regionPreview=control(L"VrssRegionPreview",L"",0,20,290,310,134);
        gUi.regionArea=control(L"STATIC",L"",SS_LEFT,350,304,325,24);
        gUi.regionCost=control(L"STATIC",L"",SS_LEFT,350,334,325,24);
        LOGFONTW bold{}; GetObjectW(gFont,sizeof(bold),&bold); bold.lfWeight=FW_BOLD; gBoldFont=CreateFontIndirectW(&bold);
        SendMessageW(gUi.regionCost,WM_SETFONT,reinterpret_cast<WPARAM>(gBoldFont),TRUE);
        gUi.regionNote=control(L"STATIC",L"",SS_LEFT,350,366,325,64);
        control(L"STATIC",L"視線は中央で固定。負荷は均等な場合の概算です。試算用MSAAはゲーム設定を変えません。",SS_LEFT,20,450,655,24);
        gUi.installInfo = control(L"STATIC", L"", SS_LEFT, 20, 488, 655, 44);
        control(L"BUTTON", L"画質設定を保存", WS_TABSTOP | BS_PUSHBUTTON, 20, 550, 160, 32, kApplyButton);
        gUi.restore = control(L"BUTTON", L"操作を戻す", WS_TABSTOP | BS_PUSHBUTTON, 195, 550, 145, 32, kRestoreButton);
        control(L"BUTTON", L"現在値を読込", WS_TABSTOP | BS_PUSHBUTTON, 505, 550, 165, 32, kRefreshButton);
        gUi.status = control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                             20, 595, 650, 55, kStatusEdit);
        control(L"STATIC", L"● 要再起動 = 対象アプリのみ。保存時に確認します。", SS_LEFT, 20, 664, 655, 23);
        gCreatingPanel = 1;
        control(L"STATIC", L"視線入力タブで選択中のソース", SS_LEFT, 20, 110, 230, 24);
        gUi.gazeSourceLabel=control(L"STATIC",L"",SS_LEFT,255,110,415,28);
        control(L"STATIC", L"横方向の倍率", SS_LEFT, 20, 155, 170, 24);
        gUi.gazeX = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 205, 151, 115, 27, kGazeX);
        control(L"STATIC", L"0.1～2", SS_LEFT, 335, 155, 200, 24);
        control(L"STATIC", L"縦方向の倍率", SS_LEFT, 20, 200, 170, 24);
        gUi.gazeY = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 205, 196, 115, 27, kGazeY);
        gUi.gazeInvert = control(L"BUTTON", L"上下方向を反転", BS_AUTOCHECKBOX | WS_TABSTOP, 350, 197, 220, 27, kGazeInvert);
        control(L"STATIC", L"左目の横角度オフセット", SS_LEFT, 20, 245, 180, 24);
        gUi.gazeLeft = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 205, 241, 115, 27, kGazeLeft);
        control(L"BUTTON", L"−", WS_TABSTOP | BS_PUSHBUTTON, 335, 241, 30, 27, kGazeLeftMinus);
        control(L"BUTTON", L"+", WS_TABSTOP | BS_PUSHBUTTON, 373, 241, 30, 27, kGazeLeftPlus);
        control(L"STATIC", L"0.1度刻み（-5～+5）", SS_LEFT, 415, 245, 250, 24);
        control(L"STATIC", L"右目の横角度オフセット", SS_LEFT, 20, 290, 180, 24);
        gUi.gazeRight = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 205, 286, 115, 27, kGazeRight);
        control(L"BUTTON", L"−", WS_TABSTOP | BS_PUSHBUTTON, 335, 286, 30, 27, kGazeRightMinus);
        control(L"BUTTON", L"+", WS_TABSTOP | BS_PUSHBUTTON, 373, 286, 30, 27, kGazeRightPlus);
        control(L"STATIC", L"0.1度刻み（-5～+5）", SS_LEFT, 415, 290, 250, 24);
        gUi.gazePathNote=control(L"STATIC",L"",SS_LEFT,20,328,650,40);
        gUi.gazeApply = control(L"BUTTON", L"視線補正を反映", WS_TABSTOP | BS_PUSHBUTTON, 20, 386, 150, 32, kGazeApply);
        gUi.gazeRestore = control(L"BUTTON", L"操作を戻す", WS_TABSTOP | BS_PUSHBUTTON, 180, 386, 145, 32, kGazeRestore);
        control(L"BUTTON", L"現在値を読込", WS_TABSTOP | BS_PUSHBUTTON, 505, 550, 165, 32, kGazeRefresh);
        gUi.gazeStatus = control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 20, 435, 650, 100);
        control(L"STATIC", L"VRChat再起動不要。保存前の補正は経路別にバックアップします。", SS_LEFT, 20, 664, 655, 23);
        gCreatingPanel = 2;
        control(L"STATIC",L"視線の入力元",SS_LEFT,20,105,180,23);
        gUi.gazeSource=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,205,101,450,260,kGazeSource);
        for (const wchar_t* name : {L"Pimax経路（純正優先）",L"Baballonia OSC",L"PSVR2 Toolkit（直接）",L"SRanibro（共通）",L"VIVE Pro Eye（SRanipal・実機未検証）",L"VIVE Streaming（実機未検証）",L"SteamVR視線アクション（実機未検証）",L"Varjo Native SDK（実機未検証）"}) {
            const auto translated=language::Text(name);
            SendMessageW(gUi.gazeSource,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(translated.c_str()));
        }
        const bool preferCommon = !processCount(L"vrss_gaze_provider.exe") &&
            (processCount(L"common_vrss_provider.exe") ||
             (!compatiblePimaxDriver() && !gResearch.empty() &&
              std::filesystem::is_regular_file(gResearch / L"common-vrss-provider/common_vrss_provider.exe")));
        telemetry::Snapshot startupProvider{};int startupRoute=-1;
        const bool knownRoute=readProviderTelemetry(startupProvider,startupRoute) && startupRoute>=1;
        gSelectedSource=knownRoute?startupRoute:preferCommon?1:0;
        SendMessageW(gUi.gazeSource,CB_SETCURSEL,gSelectedSource.load(),0);
        control(L"STATIC",L"受信ポート",SS_LEFT,20,150,180,23);
        gUi.inputPort=control(L"EDIT",L"9020",WS_BORDER | ES_NUMBER | WS_TABSTOP,205,146,110,27,kInputPort);
        control(L"STATIC",L"OSC転送先（0=なし）",SS_LEFT,20,190,180,23);
        gUi.inputForward=control(L"EDIT",L"8888",WS_BORDER | ES_NUMBER | WS_TABSTOP,205,186,110,27,kInputForward);
        control(L"STATIC",L"VRCFaceTrackingへ転送する場合は8888",SS_LEFT,335,190,320,23);
        { std::ifstream ports(backupFolder()/L"osc-settings.txt");int input=0,forward=-1;
          if(ports>>input>>forward && input>=1 && input<=65535 && forward>=0 && forward<=65535 && input!=forward) {
              setText(gUi.inputPort,std::to_wstring(input));setText(gUi.inputForward,std::to_wstring(forward));
          }
        }
        gUi.inputStart=control(L"BUTTON",L"視線経路を起動",WS_TABSTOP | BS_PUSHBUTTON,20,240,180,32,kInputStart);
        gUi.inputStop=control(L"BUTTON",L"停止",WS_TABSTOP | BS_PUSHBUTTON,215,240,105,32,kInputStop);
        control(L"STATIC",L"表示倍率",SS_LEFT,350,245,110,23);
        gUi.previewZoom=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,470,240,100,110,kPreviewZoom);
        for(const wchar_t* label:{L"1x",L"2x",L"3x",L"4x"})
            SendMessageW(gUi.previewZoom,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        gPreviewZoom=readPreviewZoom();
        SendMessageW(gUi.previewZoom,CB_SETCURSEL,gPreviewZoom-1,0);
        gUi.inputStatus=control(L"EDIT",L"停止中",WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,20,285,650,64,kInputStatus);
        gUi.liveMonitor=control(L"VrssLiveMonitor",L"",0,20,358,650,290);
        gUi.sourceNote=control(L"STATIC",L"",SS_LEFT,20,655,650,25);
        gCreatingPanel=3;
        control(L"STATIC",L"Steam Frame / NVIDIA 596.02",SS_LEFT,20,105,650,28);
        control(L"STATIC",L"SteamVRのSteamリンク設定で、他のPCアプリへの視線共有を有効にしてください。",SS_LEFT,20,150,650,48);
        control(L"STATIC",L"接続前にVRChatとUnityを終了してください。管理者確認はワーカーだけに表示されます。",SS_LEFT,20,205,650,48);
        gUi.frameStart=control(L"BUTTON",L"接続 / 再試行",WS_TABSTOP | BS_PUSHBUTTON,20,270,160,32,kFrameStart);
        gUi.frameStop=control(L"BUTTON",L"停止",WS_TABSTOP | BS_PUSHBUTTON,190,270,100,32,kFrameStop);
        gUi.frameLaunch=control(L"BUTTON",L"VRChatを起動（Frame用）",WS_TABSTOP | BS_PUSHBUTTON,300,270,330,32,kFrameLaunch);
        gUi.frameStatus=control(L"EDIT",L"",WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,20,325,650,155);
        control(L"STATIC",L"VRレイアウト4を使用。上下補正はFrame入力に内蔵しています。\r\n追加の補正は共通プロバイダーで調整できます。\r\n終了時に一時設定を復元します。ゲームは強制終了しません。",SS_LEFT,20,500,650,95);
        gCreatingPanel = -1;
        listProfiles();
        restoreLastProfileSelection();
        restoreLastSourceSelection();
        if constexpr(!kOfflineUiTest)refresh();
        refreshGaze();
        updateEstimate();
        updateInputStatus();
        updateFrameStatus();
        SetTimer(window, 1, 1000, nullptr);
        SetTimer(window, 2, 50, nullptr);
        showPanel(0);
        return 0;
    }
    case WM_COMMAND:
        if (gBusy) return 0;
        if constexpr(kOfflineUiTest) {
            switch(LOWORD(wParam)) {
            case kDriverTab:showPanel(0);return 0;
            case kGazeTab:showPanel(1);return 0;
            case kInputTab:showPanel(2);return 0;
            case kFrameTab:showPanel(3);return 0;
            }
            return 0;
        }
        if (LOWORD(wParam) == kLanguage && HIWORD(wParam) == CBN_SELCHANGE) {
            language::english = SendMessageW(gUi.language, CB_GETCURSEL, 0, 0) == 1;
            saveLanguage();
            applyLanguage();
            refreshGaze();
            updateInputStatus();
            return 0;
        }
        if ((HIWORD(wParam)==EN_CHANGE && (LOWORD(wParam)==kAreaEdit || LOWORD(wParam)==kAspectEdit)) ||
            (HIWORD(wParam)==CBN_SELCHANGE && (LOWORD(wParam)==kModeCombo || LOWORD(wParam)==kLayoutCombo || LOWORD(wParam)==kEstimateMsaa))) {
            updateEstimate();return 0;
        }
        if (LOWORD(wParam) == kGazeSource && HIWORD(wParam) == CBN_SELCHANGE) {
            const int source=int(SendMessageW(gUi.gazeSource,CB_GETCURSEL,0,0));
            if (source<0 || source>7) return 0;
            gSelectedSource=source;
            if (!saveLastSource(source))
                MessageBoxW(gUi.window,L"視線入力元の選択を次回起動用に保存できませんでした。",
                            L"視線入力",MB_ICONWARNING);
            refreshGaze(); updateInputStatus(); return 0;
        }
        if (LOWORD(wParam)==kPreviewZoom && HIWORD(wParam)==CBN_SELCHANGE) {
            const int selected=int(SendMessageW(gUi.previewZoom,CB_GETCURSEL,0,0));
            if (selected>=0 && selected<4) {
                gPreviewZoom=selected+1;
                savePreviewZoom(gPreviewZoom);
                InvalidateRect(gUi.liveMonitor,nullptr,FALSE);
            }
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case kRefreshButton: refresh(); return 0;
            case kApplyButton: applyFromUi(); return 0;
            case kRestoreButton: restoreFromUi(); return 0;
            case kAspectCheck: EnableWindow(gUi.aspect, isChecked(gUi.aspectCheck)); updateEstimate();return 0;
            case kDriverTab: showPanel(0); return 0;
            case kGazeTab: showPanel(1); return 0;
            case kGazeRefresh: refreshGaze(); return 0;
            case kGazeLeftMinus: adjustGazeOffset(gUi.gazeLeft, -1); return 0;
            case kGazeLeftPlus: adjustGazeOffset(gUi.gazeLeft, 1); return 0;
            case kGazeRightMinus: adjustGazeOffset(gUi.gazeRight, -1); return 0;
            case kGazeRightPlus: adjustGazeOffset(gUi.gazeRight, 1); return 0;
            case kGazeApply: applyGazeFromUi(false); return 0;
            case kGazeRestore: applyGazeFromUi(true); return 0;
            case kGazeReset:
                return 0;
            case kProfileSave: saveProfileFromUi(); return 0;
            case kProfileSwitch: switchProfile(); return 0;
            case kInputTab: showPanel(2); return 0;
            case kFrameTab: showPanel(3); return 0;
            case kFrameStart: gFrameRuntime.Retry();updateFrameStatus();return 0;
            case kFrameStop: gFrameRuntime.Stop();updateFrameStatus();return 0;
            case kFrameLaunch:
                try {
                    if(vrchatRunning() || processCount(L"Unity.exe"))
                        throw std::runtime_error("Close VRChat and Unity before launching");
                    gFrameRuntime.LaunchGame(window);
                } catch(const std::exception& e) {
                    const auto detail = frameLaunchError(e.what());
                    ::MessageBoxW(window, detail.c_str(), language::english ? L"Frame launch stopped" : L"Frameでの起動を中止しました", MB_ICONWARNING);
                }
                updateFrameStatus();return 0;
            case kInputStart: startRouteOperation(false); return 0;
            case kInputStop: startRouteOperation(true); return 0;
            }
        }
        break;
    case WM_HSCROLL:
        if (!gBusy && reinterpret_cast<HWND>(lParam)==gUi.areaSlider) {
            setText(gUi.area,std::to_wstring(SendMessageW(gUi.areaSlider,TBM_GETPOS,0,0)));updateEstimate();return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == 1) {
            if constexpr(kOfflineUiTest) return 0;
            if(!gBusy) gFrameRuntime.Tick({processCount(L"vrserver.exe")!=0,
                vrchatRunning() || processCount(L"Unity.exe")!=0,
                processCount(L"common_vrss_provider.exe")!=0 ||
                processCount(L"vrss_gaze_provider.exe")!=0,false},backupFolder(),window);
            pollRouteOperation(); updateFrameStatus(); updateInputStatus(); return 0; }
        if (wParam == 2) {
            updateGazeMap();
            if (!gLive.vendor) { gPimaxGaze.Close(); gNvidiaQueue.Close(); gPimaxFreshAt=0; gNvidiaFreshAt=0; }
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam); HWND item = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(25, 25, 25));
        for (HWND badge : gUi.restartBadges) if (item == badge) SetTextColor(dc, RGB(164, 90, 0));
        if (item==gUi.regionCost) SetTextColor(dc,RGB(27,100,194));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_CLOSE: if (gBusy) return 0; break;
    case WM_DESTROY: KillTimer(window, 1); KillTimer(window, 2); gPimaxGaze.Close(); gNvidiaQueue.Close(); gFrameRuntime.Stop(); if(gRouteOperation)CloseHandle(gRouteOperation); if(gBoldFont) DeleteObject(gBoldFont);PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc{};
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 30;
    if constexpr(kOfflineUiTest) {if(argc>1){LocalFree(argv);return 31;}}
    initializeGazePaths();
    loadLanguage();
    if (argc > 1) {
        int result = 31;
        if (wcscmp(argv[1],L"--frame-worker")==0)result=frameWorker(argc,argv);
        else if (wcscmp(argv[1], L"--readiness") == 0 && argc == 3) {
            Session session; ProfileValues values{};
            NvAPI_Status status = session.open();
            if (status == NVAPI_OK) status = session.readAll(values);
            live::Snapshot pimax{}, common{};
            const bool pimaxCalibration = gGazeStore.Read(live::Target::Pimax, pimax);
            const bool commonCalibration = gGazeStore.Read(live::Target::Common, common);
            const bool compatiblePimax = !gResearch.empty() && compatiblePimaxDriver();
            const PimaxServiceState pimaxService=pimaxServiceState();
            const bool bridgeInstalled=GetFileAttributesW(
                L"C:\\Program Files\\Pimax\\Runtime\\drivers\\sranibro_eye\\bin\\win64\\driver_sranibro_eye.dll")!=INVALID_FILE_ATTRIBUTES;
            const bool commonBinary = !gResearch.empty() &&
                std::filesystem::is_regular_file(gResearch / L"common-vrss-provider/common_vrss_provider.exe");
            std::ofstream report(argv[2], std::ios::binary | std::ios::trunc);
            if (report) {
                report << "VRSS-TUNER-READINESS-1\n"
                       << "research_root=" << (gResearch.empty() ? "missing" : "found") << '\n'
                       << "nvapi_vrchat_profile=" << (status == NVAPI_OK ? "readable" : "unavailable") << '\n'
                       << "nvapi_status=" << status << '\n'
                       << "graphics_write_entry=" << (status == NVAPI_OK && session.hidden.set ? "available" : "unavailable") << '\n'
                       << "vrss_allow=" << (status == NVAPI_OK && values.allow.found && values.allow.raw == 1 ? "enabled" : "not_enabled") << '\n'
                       << "hmd_test_branch=" << (status == NVAPI_OK && values.gate.found && values.gate.raw == 0 ? "enabled" : "unavailable") << '\n'
                       << "vrss_mode=" << (status != NVAPI_OK ? "unknown" : values.mode.raw ? "on" : "off") << '\n'
                       << "pimax_calibration=" << (pimaxCalibration ? "readable" : "missing") << '\n'
                       << "pimax_compatible_driver=" << (compatiblePimax ? "verified" : "unverified") << '\n'
                       << "pimax_provider_running=" << (processCount(L"vrss_gaze_provider.exe") ? "yes" : "no") << '\n'
                       << "pimax_xr5_runtime_running=" << (processCount(L"platform_runtime_XR5EYECHIP_WIN10_x64.exe") ? "yes" : "no") << '\n'
                       << "pimax_service_running=" << (pimaxService.running ? "yes" : "no") << '\n'
                       << "pimax_service_disabled=" << (pimaxService.disabled ? "yes" : "no") << '\n'
                       << "sranibro_running=" << (processCount(L"SRanibro.exe") ? "yes" : "no") << '\n'
                       << "pimax_bridge_present=" << (bridgeInstalled ? "yes" : "no") << '\n'
                       << "common_provider_binary=" << (commonBinary ? "present" : "missing") << '\n'
                       << "common_calibration=" << (commonCalibration ? "readable" : "missing") << '\n'
                       << "common_provider_running=" << (processCount(L"common_vrss_provider.exe") ? "yes" : "no") << '\n'
                       << "live_gaze_mapping=not_verified_by_this_check\n";
                report.flush();
                result = report.good() ? 0 : 72;
            } else result = 72;
        }
        else if (wcscmp(argv[1], L"--restore-pimax-service") == 0 && argc == 2) result=restorePimaxService();
        else if (wcscmp(argv[1], L"--apply") == 0) { result = applyCommand(argc, argv); if (!result && !saveStartupState()) result = 69; }
        else if (wcscmp(argv[1], L"--restore") == 0 && argc == 2) { result = restoreCommand(); if (!result && !saveStartupState()) result = 69; }
        else if (wcscmp(argv[1], L"--apply-gaze") == 0) result = gazeCommand(argc, argv, false);
        else if (wcscmp(argv[1], L"--restore-gaze") == 0) result = gazeCommand(argc, argv, true);
        else if (wcscmp(argv[1], L"--check") == 0 && argc == 2) {
            Session session;
            ProfileValues values{};
            NvAPI_Status status = session.open();
            if (status == NVAPI_OK) status = session.readAll(values);
            result = status == NVAPI_OK ? 0 : 32;
        }
        else if (wcscmp(argv[1], L"--self-test") == 0 && argc == 2) {
            float value{};
            result = parseNumber(L"5", value) && value == 5.0f &&
                     parseNumber(L"0.75", value) && value == 0.75f &&
                     !parseNumber(L"5junk", value) && !parseNumber(L"NaN", value) &&
                     bitsFloat(floatBits(1.25f)) == 1.25f &&
                     sourceFromRunRoute("Baballonia") == 1 &&
                     sourceFromRunRoute("PSVR2Toolkit") == 2 &&
                     sourceFromRunRoute("SRanibro") == 3 &&
                     sourceFromRunRoute("ViveProEye") == 4 &&
                     sourceFromRunRoute("ViveStreaming") == 5 &&
                     sourceFromRunRoute("SteamVREye") == 6 &&
                     sourceFromRunRoute("Varjo") == 7 &&
                     sourceFromRunRoute("unknown") == -1 ? 0 : 35;
        }
        else if (wcscmp(argv[1], L"--profile-self-test") == 0 && argc == 3) {
            SetEnvironmentVariableW(L"LOCALAPPDATA", argv[2]);
            NamedProfile original{};
            original.mode = 1; original.source = 2; original.area = 35;
            original.aspect = 1.25f; original.customAspect = true;
            original.x = .5f; original.y = -.55f; original.left = -2; original.right = 2;
            NamedProfile actual{};
            bool pass = writeProfile(L"CodexProfileTest", original) &&
                readProfile(L"CodexProfileTest", actual) &&
                actual.mode == original.mode && actual.source == original.source &&
                actual.area == original.area && actual.aspect == original.aspect &&
                actual.customAspect == original.customAspect && actual.x == original.x &&
                actual.y == original.y && actual.left == original.left && actual.right == original.right &&
                !validProfileName(L"../escape") && !validProfileName(L"bad:name");
            for (int legacySource : {0,1,3,4,5}) {
                original.source=legacySource;
                pass=pass && writeProfile(L"LegacyProfileTest",original) &&
                    readProfile(L"LegacyProfileTest",actual) && actual.source==legacySource;
            }
            original.source=8;
            pass=pass && writeProfile(L"InvalidSourceTest",original) &&
                !readProfile(L"InvalidSourceTest",actual);
             std::wstring restored;
             pass=pass && saveLastProfile(L"日本語 Profile") &&
                 readLastProfile(restored) && restored==L"日本語 Profile" &&
                 !saveLastProfile(L"../escape");
             int restoredSource=-1;
             pass=pass && saveLastSource(7) && readLastSource(restoredSource) &&
                 restoredSource==7 && !saveLastSource(8);
            result = pass ? 0 : 37;
        }
        else if (wcscmp(argv[1], L"--startup-self-test") == 0 && argc == 3) {
            SetEnvironmentVariableW(L"LOCALAPPDATA", argv[2]);
            result = saveStartupState() ? 0 : 69;
        }
        else if (wcscmp(argv[1], L"--status") == 0 && argc == 2) {
            Session session;
            ProfileValues values{};
            NvAPI_Status status = session.open();
            if (status == NVAPI_OK) status = session.readAll(values);
            if (status == NVAPI_OK) {
                std::wostringstream line;
                line << L"mode=" << values.mode.raw << L" area=" << bitsFloat(values.area.raw)
                     << L" aspect=" << (values.aspect.found ? bitsFloat(values.aspect.raw) : 1.0f)
                     << L" layout=" << values.layout.raw << L" hmd_gate=" << values.gate.raw;
                MessageBoxW(nullptr, line.str().c_str(), L"VRSS Tuner / 読取専用", MB_OK);
                result = 0;
            } else result = 32;
        }
        LocalFree(argv);
        return result;
    }
    LocalFree(argv);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"VrssTunerMainWindow";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&cls)) return 33;
    WNDCLASSW preview{}; preview.lpfnWndProc=regionPreviewProc;preview.hInstance=instance;
    preview.lpszClassName=L"VrssRegionPreview"; preview.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&preview)) return 36;
    WNDCLASSW liveMonitor{}; liveMonitor.lpfnWndProc=liveMonitorProc; liveMonitor.hInstance=instance;
    liveMonitor.lpszClassName=L"VrssLiveMonitor"; liveMonitor.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&liveMonitor)) return 37;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int x=work.left+std::max(0,(int(work.right-work.left)-710)/2),y=work.top+std::max(0,(int(work.bottom-work.top)-730)/2);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"VRSS Tuner",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  x, y, 710, 730, nullptr, nullptr, instance, nullptr);
    if (!window) return 34;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
