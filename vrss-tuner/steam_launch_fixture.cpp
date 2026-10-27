#include <windows.h>
#include <fstream>
#include <filesystem>

int wmain(int argc,wchar_t** argv) {
    wchar_t path[32768]{},vendor[64]{},product[64]{};
    if(!GetEnvironmentVariableW(L"VRSS_STEAM_LAUNCH_FIXTURE_OUTPUT",path,32768))return 2;
    GetEnvironmentVariableW(L"TEST_VENDOR_ID",vendor,64);
    GetEnvironmentVariableW(L"TEST_PRODUCT_ID",product,64);
    std::wofstream output{std::filesystem::path(path)};
    output<<L"vendor="<<vendor<<L"\nproduct="<<product<<L"\n";
    for(int i=1;i<argc;++i)output<<L"arg="<<argv[i]<<L"\n";
    return output.good()?0:3;
}
