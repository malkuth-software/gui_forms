#pragma once

// Shared command-line contract for the GUI.Forms demonstration programs
// (Gallery, Correspondence and the labs) on every host:
//
//   --automation        Windows: enable the WM_COPYDATA automation channel.
//   --smoke-test        The packaged-program proof, shaped like PlaySuite's:
//                       the program runs normally, a UI timer closes the
//                       window once a frame has been presented, the host
//                       prints its close receipt and main prints
//                       "GUI.Forms window smoke passed" before exiting 0.
//                       When no frame was presented within ten seconds the
//                       window still closes, but main prints a failure line
//                       to stderr and exits 1.
//   --capture <path>    After the first presented frame that covers the whole
//                       window, write the framebuffer as PNG to <path>, print
//                       {"automation":"capture","saved":true|false,"path":"..."}
//                       to stdout and close. Exit code 0 only when saved.
//   --size <W>x<H>      Create the window at this logical size.
//   --scale <factor>    Recorded as a hint only; hosts take the device scale
//                       from the display.
//   --page <id>         The Gallery opens on this page (its stable id, as in
//                       demo/gallery_page_specs.hpp), as its navigation row
//                       would show it; with --capture, that page is
//                       captured. Other programs ignore it.
//   --backdrop <list>   The compositor lab asks for this window backdrop:
//                       one to three WindowBackdrop names, comma-separated,
//                       preferred first (e.g. window_material,transparent).
//                       Other programs ignore it.
//   --theme <id>        The Gallery compiles its project theme onto this
//                       base: gui-forms (the default) or
//                       windows-professional, as its Theme settings page
//                       does. Other programs ignore it.
//
// Header-only so every demo executable can include it without a new library.
// It is demo-facing code: no host, renderer or platform type appears here.

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/window.hpp"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::demo {

struct ProgramOptions final {
    bool automation{};
    bool smoke_test{};
    std::string capture_path;
    std::optional<Size> capture_size;
    double scale_hint{};
    std::string page;
    std::string backdrop;
    std::string theme;
    // --scheme light|dark: the window's colour-scheme override; empty
    // follows the host's setting.
    std::string scheme;
};

inline void print_program_usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s [--automation] [--smoke-test] [--capture <path.png>] "
                 "[--size <W>x<H>] [--scale <factor>] [--page <id>] [--backdrop <list>] [--theme <id>]\n",
                 program != nullptr && *program != '\0' ? program : "gui-forms-demo");
}

namespace detail {

inline bool parse_positive_double(std::string_view text, double& value) {
    if (text.empty()) return false;
    const std::string buffer(text);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(buffer.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(parsed) || parsed <= 0.0) {
        return false;
    }
    value = parsed;
    return true;
}

// Accepts "<W>x<H>" with positive finite numbers, e.g. 1280x800 or 1280.5x800.
inline bool parse_size(std::string_view text, Size& size) {
    const std::size_t separator = text.find_first_of("xX");
    if (separator == std::string_view::npos) return false;
    double width{};
    double height{};
    if (!parse_positive_double(text.substr(0, separator), width) ||
        !parse_positive_double(text.substr(separator + 1U), height)) {
        return false;
    }
    size = {width, height};
    return true;
}

} // namespace detail

// Returns nullopt after printing usage to stderr when an argument is unknown,
// a value is missing, or a value is malformed.
inline std::optional<ProgramOptions> parse_program_options(int argc, char** argv) {
    ProgramOptions options;
    const char* program = argc > 0 && argv != nullptr ? argv[0] : "";
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            print_program_usage(program);
            return std::nullopt;
        }
        const std::string_view argument(argv[index]);
        if (argument == "--automation") {
            options.automation = true;
            continue;
        }
        if (argument == "--smoke-test") {
            options.smoke_test = true;
            continue;
        }
        const bool wants_value = argument == "--capture" || argument == "--size" ||
                                 argument == "--scale" || argument == "--page" ||
                                 argument == "--backdrop" || argument == "--theme" ||
                                 argument == "--scheme";
        if (!wants_value) {
            print_program_usage(program);
            return std::nullopt;
        }
        if (index + 1 >= argc || argv[index + 1] == nullptr) {
            print_program_usage(program);
            return std::nullopt;
        }
        const std::string_view value(argv[++index]);
        if (argument == "--capture") {
            if (value.empty()) {
                print_program_usage(program);
                return std::nullopt;
            }
            options.capture_path = std::string(value);
        } else if (argument == "--page") {
            if (value.empty()) {
                print_program_usage(program);
                return std::nullopt;
            }
            options.page = std::string(value);
        } else if (argument == "--backdrop") {
            if (value.empty()) {
                print_program_usage(program);
                return std::nullopt;
            }
            options.backdrop = std::string(value);
        } else if (argument == "--theme") {
            if (value.empty()) {
                print_program_usage(program);
                return std::nullopt;
            }
            options.theme = std::string(value);
        } else if (argument == "--scheme") {
            if (value != "light" && value != "dark") {
                print_program_usage(program);
                return std::nullopt;
            }
            options.scheme = std::string(value);
        } else if (argument == "--size") {
            Size size{};
            if (!detail::parse_size(value, size)) {
                print_program_usage(program);
                return std::nullopt;
            }
            options.capture_size = size;
        } else {
            double scale{};
            if (!detail::parse_positive_double(value, scale)) {
                print_program_usage(program);
                return std::nullopt;
            }
            options.scale_hint = scale;
        }
    }
    return options;
}

// Copies the portable capture fields into any host option struct that carries
// them (MacHostOptions, WindowsHostOptions, LinuxHostOptions). Host-specific
// fields such as automation_enabled are set by the main that owns them.
template <class HostOptions>
inline void apply_program_options(const ProgramOptions& program, HostOptions& host) {
    host.capture_path = program.capture_path;
    host.capture_size = program.capture_size;
}

// --smoke-test, the way PlaySuite proves its packaged programs: nothing
// special is rendered or captured. The host attaches and shows the window as
// it always does; once it reports ready, a UI timer on the window's root
// control polls the presented-frame count and requests the ordinary close as
// soon as one frame is on screen (or after the deadline, so a program that
// never presents still exits). main then prints the pass line when a frame
// had been presented and the host returned 0, and a failure line otherwise.
struct SmokeTest final {
    Window* window{};
    FrameTime started{};
    bool completed{};
    // Observed when the close was requested. The core declines to paint an
    // occluded window, so a window the host never reported visible closes at
    // the deadline with nothing presented.
    bool presented{};
    bool occluded{};
    std::function<void()> request_close;
    FrameRequestToken timer;
};

inline constexpr std::string_view smoke_test_pass_line = "GUI.Forms window smoke passed";

struct SmokeTestTick final {
    std::weak_ptr<SmokeTest> state;

    void operator()(FrameTime now) const {
        const std::shared_ptr<SmokeTest> live = state.lock();
        if (!live || (*live).completed || !(*live).request_close || (*live).window == nullptr) {
            return;
        }
        const double seconds = std::chrono::duration<double>(now - (*live).started).count();
        const bool presented = (*(*live).window).metrics_snapshot().frames_presented >= 1U;
        if ((presented && seconds >= 1.0) || seconds >= 10.0) {
            (*live).completed = true;
            (*live).presented = presented;
            (*live).occluded = (*(*live).window).occluded();
            (*live).request_close();
        }
    }
};

using HostReadyCallback = std::function<void(
    std::function<void()>, std::function<void()>,
    std::function<HostDialogResult(const HostDialogRequest&)>,
    std::function<HostClipboardTextResult()>, std::function<HostServiceStatus(std::string_view)>)>;

struct SmokeTestHostReady final {
    std::shared_ptr<SmokeTest> state;
    HostReadyCallback previous;

    void operator()(std::function<void()> wake, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)> show_dialog,
                    std::function<HostClipboardTextResult()> read_clipboard_text,
                    std::function<HostServiceStatus(std::string_view)> write_clipboard_text) const {
        (*state).request_close = request_close;
        (*state).started = FrameClock::now();
        const Control::Ptr root = (*(*state).window).root();
        const FrameInterval interval = std::chrono::milliseconds(50);
        (*state).timer = (*(*state).window).schedule_ui_timer(
            *root, interval, (*state).started + interval, SmokeTestTick{state});
        if (previous) {
            previous(std::move(wake), std::move(request_close), std::move(show_dialog),
                     std::move(read_clipboard_text), std::move(write_clipboard_text));
        }
    }
};

// Arms the smoke on a host option struct (MacHostOptions, WindowsHostOptions,
// LinuxHostOptions) for the window the host is about to run. Returns null when
// --smoke-test was not given; pass the result to smoke_test_result.
template <class HostOptions>
[[nodiscard]] inline std::shared_ptr<SmokeTest> arm_smoke_test(const ProgramOptions& program,
                                                               HostOptions& host, Window& window) {
    if (!program.smoke_test) return nullptr;
    std::shared_ptr<SmokeTest> state = std::make_shared<SmokeTest>();
    (*state).window = &window;
    host.print_metrics_on_close = true;
    host.host_ready = SmokeTestHostReady{state, std::move(host.host_ready)};
    return state;
}

// The process exit code after the host returned: unchanged without a smoke;
// 1 when the smoke never reached its scheduled close or closed without a
// presented frame; the host's code otherwise, with the pass line printed on 0.
inline int smoke_test_result(const std::shared_ptr<SmokeTest>& state, int exit_code) {
    if (!state) return exit_code;
    if (!(*state).completed) {
        std::fprintf(stderr, "GUI.Forms window smoke did not reach its scheduled close\n");
        return exit_code == 0 ? 1 : exit_code;
    }
    if (!(*state).presented) {
        std::fprintf(stderr, "GUI.Forms window smoke failed: no frame was presented%s\n",
                     (*state).occluded ? " (the window was occluded at the deadline)" : "");
        return exit_code == 0 ? 1 : exit_code;
    }
    if (exit_code == 0) {
        std::fprintf(stdout, "%.*s\n", static_cast<int>(smoke_test_pass_line.size()),
                     smoke_test_pass_line.data());
        std::fflush(stdout);
    }
    return exit_code;
}

} // namespace gui_forms::demo
