#include "support/normative_trace.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

// The normative trace's reference (release-1.0 condition 2;
// tests/support/normative_trace.hpp): the script dispatched straight into a
// Window must replay byte for byte and keep the order every host must
// reproduce. With --write <directory> it writes the references the host
// drivers are compared with: <directory>/normative-1.00.trace and
// normative-2.00.trace (tools/normative_trace.py compare).

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Whether `trace` holds `first` and, after it, `second`.
bool ordered(const std::string& trace, std::string_view first, std::string_view second) {
    const std::size_t at = trace.find(first);
    if (at == std::string::npos) return false;
    return trace.find(second, at + first.size()) != std::string::npos;
}

void write_trace(const std::string& directory, const char* const name, const std::string& trace) {
    const std::string path = directory + "/" + name;
    std::ofstream file(path, std::ios::binary);
    file << trace;
    require(file.good(), "the reference trace could not be written");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string first = gui_forms::tests::normative_headless_trace(1.0);
        const std::string second = gui_forms::tests::normative_headless_trace(1.0);
        require(first == second, "the normative trace replays byte for byte");
        const std::string doubled = gui_forms::tests::normative_headless_trace(2.0);
        require(ordered(first, "step 2 click left", "focus id=normative.left state=on"),
                "a click on left focuses it");
        require(ordered(first, "focus id=normative.left state=on", "activate id=normative.left"),
                "left is focused before it is activated");
        require(ordered(first, "step 4 tab", "focus id=normative.right state=on"),
                "Tab from inner moves focus to right");
        require(ordered(first, "step 5 type a", "text id=normative.right value=a"),
                "the typed text reaches the focused control");
        require(ordered(first, "step 6 wheel over right", "pointer id=normative.right phase=target action=wheel"),
                "the wheel reaches the control under the pointer");
        require(first.find("display lines=") != std::string::npos && first.find("\nend\n") != std::string::npos,
                "the trace ends with the window's display trace");
        for (int index = 1; index + 1 < argc; ++index) {
            if (std::string_view(argv[index]) == "--write") {
                write_trace(argv[index + 1], "normative-1.00.trace", first);
                write_trace(argv[index + 1], "normative-2.00.trace", doubled);
            }
        }
        std::cout << first;
        std::cout << "gui_forms_normative_trace_tests: reference replayed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_normative_trace_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
