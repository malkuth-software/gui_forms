#include "lab.hpp"

#include <cstdio>
#include <cstring>

// present_lab [all|env|gdi|dxgi|dcomp|pm|winrt]...
int main(int argument_count, char** arguments) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool all = argument_count < 2;
    for (int index = 1; index < argument_count; ++index) {
        if (std::strcmp(arguments[index], "all") == 0) all = true;
    }
    lab::report_environment();
    for (int index = all ? 0 : 1; index < (all ? 1 : argument_count); ++index) {
        const char* mode = all ? "all" : arguments[index];
        const bool every = std::strcmp(mode, "all") == 0;
        if (every || std::strcmp(mode, "gdi") == 0) lab::run_gdi_routes();
        if (every || std::strcmp(mode, "dxgi") == 0) lab::run_dxgi_routes();
        if (every || std::strcmp(mode, "dcomp") == 0) lab::run_dcomp_routes();
        if (every || std::strcmp(mode, "pm") == 0) lab::run_presentation_manager_route();
        if (every || std::strcmp(mode, "winrt") == 0) lab::run_winrt_route();
    }
    CoUninitialize();
    std::printf("lab.done=1\n");
    return 0;
}
