#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

// Optional, read-only monitor for the audited NVIDIA VRSS2 stereo gaze queue.
// An unknown layout stays unverified; it is never treated as an error.
class NvidiaQueueMonitor {
public:
    ~NvidiaQueueMonitor() { Close(); }
    bool Present() const { return view_ != nullptr; }
    bool Read(std::array<float, 4>& location) {
        if (!view_ && GetTickCount64() >= nextSearch_) Find();
        if (!view_) return false;
        std::uint64_t timestamp{};
        if (!Decode(view_, location, timestamp)) {
            if (++invalidReads_ > 20) Close();
            return false;
        }
        invalidReads_ = 0;
        if (timestamp == previousTimestamp_) {
            if (GetTickCount64() - lastChange_ > 3000) Close();
            return false;
        }
        previousTimestamp_ = timestamp;
        lastChange_ = GetTickCount64();
        return true;
    }
    void Close() {
        if (view_) UnmapViewOfFile(view_);
        if (section_) CloseHandle(section_);
        view_ = nullptr;
        section_ = nullptr;
        previousTimestamp_ = 0;
        invalidReads_ = 0;
        nextSearch_ = GetTickCount64() + 1000;
    }
private:
    struct UnicodeString { std::uint16_t length, maximumLength; wchar_t* buffer; };
    struct ObjectAttributes {
        std::uint32_t length; HANDLE rootDirectory; UnicodeString* objectName;
        std::uint32_t attributes; void* securityDescriptor; void* securityQualityOfService;
    };
    struct DirectoryEntry { UnicodeString name, typeName; };
    using OpenDirectory = LONG(NTAPI*)(HANDLE*, ACCESS_MASK, ObjectAttributes*);
    using QueryDirectory = LONG(NTAPI*)(HANDLE, void*, std::uint32_t, BOOLEAN,
                                         BOOLEAN, std::uint32_t*, std::uint32_t*);
    template<class T> static T At(const std::uint8_t* bytes, std::size_t offset) {
        T value{}; std::memcpy(&value, bytes + offset, sizeof(value)); return value;
    }
    static bool Decode(const std::uint8_t* bytes, std::array<float, 4>& location,
                       std::uint64_t& timestamp) {
        constexpr std::size_t kHeader = 8, kSlot = 0xC8, kSlots = 5;
        if (At<std::uint32_t>(bytes, 0) != 1 || At<std::uint32_t>(bytes, 4) >= kSlots) return false;
        std::uint64_t newest = 0;
        std::array<float, 4> candidate{};
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            const std::size_t base = kHeader + slot * kSlot;
            if (At<std::uint32_t>(bytes, base) != 0x10088 ||
                At<std::uint32_t>(bytes, base + 16) != 2) return false;
            for (std::size_t eye = 0; eye < 2; ++eye) {
                const std::size_t record = base + (eye ? 0x8C : 0x50);
                if (At<std::uint32_t>(bytes, record) != 0x1003C ||
                    !(At<std::uint32_t>(bytes, record + 4) & 4)) return false;
            }
            const std::uint64_t stamp = At<std::uint64_t>(bytes, base + 8);
            if (stamp > newest) {
                newest = stamp;
                for (std::size_t eye = 0; eye < 2; ++eye) {
                    const std::size_t record = base + (eye ? 0x8C : 0x50);
                    const float x = At<float>(bytes, record + 32);
                    const float y = At<float>(bytes, record + 36);
                    if (!std::isfinite(x) || !std::isfinite(y) ||
                        std::abs(x) > 2 || std::abs(y) > 2) return false;
                    candidate[eye * 2] = x;
                    candidate[eye * 2 + 1] = y;
                }
            }
        }
        if (!newest) return false;
        timestamp = newest; location = candidate; return true;
    }
    static std::wstring Text(const UnicodeString& item) {
        if (!item.buffer || item.length % sizeof(wchar_t)) return {};
        return {item.buffer, item.length / sizeof(wchar_t)};
    }
    void Find() {
        nextSearch_ = GetTickCount64() + 3000;
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto open = reinterpret_cast<OpenDirectory>(GetProcAddress(ntdll, "NtOpenDirectoryObject"));
        auto query = reinterpret_cast<QueryDirectory>(GetProcAddress(ntdll, "NtQueryDirectoryObject"));
        if (!open || !query) return;
        wchar_t path[] = L"\\BaseNamedObjects";
        UnicodeString name{static_cast<std::uint16_t>(wcslen(path) * sizeof(wchar_t)),
                           static_cast<std::uint16_t>(sizeof(path)), path};
        ObjectAttributes attrs{sizeof(attrs), nullptr, &name, 0x40, nullptr, nullptr};
        HANDLE directory{};
        if (open(&directory, 0x0001, &attrs) < 0 || !directory) return;
        std::uint32_t context{}, returned{};
        alignas(16) std::uint8_t buffer[8192]{};
        for (unsigned count = 0; count < 10000; ++count) {
            const LONG result = query(directory, buffer, sizeof(buffer), TRUE, FALSE, &context, &returned);
            if (static_cast<std::uint32_t>(result) == 0x8000001A || result < 0) break;
            const auto* entry = reinterpret_cast<const DirectoryEntry*>(buffer);
            const auto object = Text(entry->name);
            if (Text(entry->typeName) != L"Section" || object.size() < 5 ||
                object.compare(object.size() - 4, 4, L"-mem") != 0) continue;
            const auto fullName = L"Global\\" + object;
            HANDLE section = OpenFileMappingW(FILE_MAP_READ, FALSE, fullName.c_str());
            if (!section) continue;
            const auto* view = static_cast<const std::uint8_t*>(MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0));
            MEMORY_BASIC_INFORMATION info{};
            const bool largeEnough = view && VirtualQuery(view, &info, sizeof(info)) && info.RegionSize >= 1008;
            std::array<float, 4> locations{}; std::uint64_t timestamp{};
            if (largeEnough && Decode(view, locations, timestamp)) {
                view_ = view; section_ = section; previousTimestamp_ = timestamp;
                lastChange_ = GetTickCount64(); break;
            }
            if (view) UnmapViewOfFile(view);
            CloseHandle(section);
        }
        CloseHandle(directory);
    }
    HANDLE section_{};
    const std::uint8_t* view_{};
    std::uint64_t previousTimestamp_{}, lastChange_{}, nextSearch_{};
    unsigned invalidReads_{};
};
