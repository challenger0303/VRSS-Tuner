#include <windows.h>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring(argv[1]) != L"--driver-check-only" || std::wstring(argv[2]) != L"1") return 3;
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"VRSS_EXPERIMENTAL_DRIVER_CONTRACT", path, 32768)) return 4;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return 5;
    std::wcout << L"contractPath=" << path << L'\n';
    wchar_t builtin[2]{};
    const bool useBuiltin = GetEnvironmentVariableW(L"VRSS_CONTRACT_FIXTURE_BUILTIN", builtin, 2) != 0;
    std::cout << "driverContract=" << (useBuiltin ? "audited" : "experimental") << '\n';
    wchar_t fail[2]{};
    if (GetEnvironmentVariableW(L"VRSS_CONTRACT_FIXTURE_FAIL", fail, 2)) return 6;
    std::cout << "DRIVER_CHECK_PASS fake_provider_no_nvidia\n";
}
