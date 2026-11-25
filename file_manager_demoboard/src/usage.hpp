#pragma once

// The demoboard's command line, printed for --help (Windows field report
// D7: --help started the program). Every main reads the same arguments.

#include <cstdio>
#include <string_view>

namespace file_manager_demoboard {

[[nodiscard]] inline bool is_help_argument(std::string_view argument) noexcept {
    return argument == "--help" || argument == "-h" || argument == "/?";
}

inline void print_usage(const char* program) {
    std::fprintf(stdout,
                 "usage: %s [--product-only] [--smoke-test] [--capture-state=<state>]\n"
                 "  --product-only           open the product window alone, without the Demoboard Controller\n"
                 "                           (the installed shortcut passes it)\n"
                 "  --smoke-test             present a frame, print the pass line and exit\n"
                 "  --capture-state=<state>  start in a named capture state\n"
                 "  --help                   print this text and exit\n",
                 program != nullptr && *program != '\0' ? program : "File Manager Demoboard");
    std::fflush(stdout);
}

}  // namespace file_manager_demoboard
