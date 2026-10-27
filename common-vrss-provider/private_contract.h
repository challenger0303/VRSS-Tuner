#pragma once
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "nvapi.h"
#include "driver_identity.h"
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "nvapi64.lib")

// Private calls require an exact built-in or explicitly opted-in image pair
// and a matching method table. An experimental match is not an ABI guarantee.
// EDID-free Frame sessions are additionally restricted to the verified 596.02 path.
struct Challenge { uint32_t version = 0x10014; uint8_t bytes[16]{}; };
struct Registration {
    uint32_t version = 0x10020;
    uint32_t reserved = 0;
    uint8_t response[16]{};
    uint32_t vendor = 0;
    uint32_t product = 0;
};
static_assert(sizeof(Challenge) == 20);
static_assert(sizeof(Registration) == 32);
using GetChallenge = int(__fastcall*)(void*, void*, Challenge*);
using RegisterProvider = int(__fastcall*)(void*, void*, Registration*);
using UnregisterProvider = int(__fastcall*)(void*, void*);
static const uint8_t kMask[16] = {
    0x9d,0xf7,0x6f,0xf0,0x3b,0xa9,0xf1,0x4a,
    0x8a,0xfc,0x99,0xc9,0x1b,0x67,0xe4,0x7b
};

static std::string HashModule(const wchar_t* name) {
    auto mod = GetModuleHandleW(name);
    wchar_t path[32768]{};
    if (!mod || !GetModuleFileNameW(mod, path, 32768)) return "UNAVAILABLE";
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    BYTE hash[32]{}; DWORD size = sizeof(hash);
    if (bytes.empty() || !CryptHashCertificate2(L"SHA256", 0, nullptr, bytes.data(),
            static_cast<DWORD>(bytes.size()), hash, &size)) return "HASH_FAILED";
    std::ostringstream out;
    for (BYTE byte : hash) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
    return out.str();
}

static bool AdminEnabled() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr; BOOL member = FALSE;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &sid)) return false;
    const BOOL ok = CheckTokenMembership(nullptr, sid, &member);
    FreeSid(sid);
    return ok && member;
}

static void PrintToken() {
    DWORD session = ~DWORD(0); ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::cout << "pid=" << GetCurrentProcessId() << " session=" << session
              << " administratorsEnabled=" << AdminEnabled() << '\n';
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
    TOKEN_ELEVATION elevation{}; DWORD size = 0;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
        std::cout << "tokenElevated=" << elevation.TokenIsElevated << '\n';
    DWORD required = 0;
    GetTokenInformation(token, TokenPrivileges, nullptr, 0, &required);
    std::vector<BYTE> storage(required);
    bool globalPresent = false, globalEnabled = false;
    LUID global{}; LookupPrivilegeValueW(nullptr, L"SeCreateGlobalPrivilege", &global);
    if (GetTokenInformation(token, TokenPrivileges, storage.data(), required, &required)) {
        auto privileges = reinterpret_cast<TOKEN_PRIVILEGES*>(storage.data());
        for (DWORD i=0; i<privileges->PrivilegeCount; ++i) {
            const auto& item = privileges->Privileges[i];
            if (item.Luid.LowPart == global.LowPart && item.Luid.HighPart == global.HighPart) {
                globalPresent = true; globalEnabled = (item.Attributes & SE_PRIVILEGE_ENABLED) != 0;
            }
        }
    }
    std::cout << "seCreateGlobalPresent=" << globalPresent
              << " seCreateGlobalEnabled=" << globalEnabled << '\n';
    CloseHandle(token);
}

static bool BusyVr(bool includeRuntime=true) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool busy = false;
    if (!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); return true; }
    do {
        for (const wchar_t* name : {L"VRChat.exe", L"vrserver.exe", L"vrcompositor.exe", L"Unity.exe"})
            if (_wcsicmp(entry.szExeFile, name) == 0 &&
                (includeRuntime || _wcsicmp(name,L"VRChat.exe")==0 || _wcsicmp(name,L"Unity.exe")==0)) busy = true;
    } while (Process32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) busy = true;
    CloseHandle(snapshot);
    return busy;
}

static bool EventAbsent(const std::wstring& name) {
    SetLastError(ERROR_SUCCESS);
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    DWORD error = GetLastError();
    if (event) { CloseHandle(event); return false; }
    return error == ERROR_FILE_NOT_FOUND;
}
