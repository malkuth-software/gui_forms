#include "gui_forms/platform/windows_host.hpp"
#include "support/normative_trace.hpp"
#include "support/windows_own_window.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// The normative trace on the Win32 host (release-1.0 condition 2;
// tests/support/normative_trace.hpp): the scene in a real Win32 window run by
// host::run_windows, each step posted as window messages to that window's
// own queue (PostMessageW, as the Windows host tests post theirs, so it runs
// on a private desktop where nothing is ever foreground), and the trace
// written to --output (default normative/windows.trace in the working
// directory). tools/normative_trace.py compare checks it against the
// headless reference at the window's scale. --scale <s> fixes the device
// scale (WindowsHostOptions::scale_override); without it the window follows
// its monitor, and only scales 1 and 2 have references.
//
// Messages per step, in device pixels (the step's logical point times the
// scale, rounded):
// - a move is WM_MOUSEMOVE at the client point;
// - a click WM_LBUTTONDOWN and WM_LBUTTONUP at the client point (posted
//   messages never become WM_LBUTTONDBLCLK, which the system makes only from
//   hardware input);
// - the wheel one WM_MOUSEWHEEL of -WHEEL_DELTA (one notch towards the user,
//   one line down) at the screen point, since wheel messages carry screen
//   coordinates;
// - a key WM_KEYDOWN of its virtual key and set-1 scan code, then
//   WM_KEYUP. The text of a typed key comes through the host's own path:
//   the host's loop calls TranslateMessage, which posts WM_CHAR for the
//   key down. WM_KEYUP is not posted with the key down: it is posted from
//   the key-release window's procedure (below) once the key down has been
//   taken, so it queues after the WM_CHAR wherever TranslateMessage puts
//   that character.

namespace {

constexpr std::string_view window_title_base = "GUI.Forms normative trace";

// The private message the key-release window receives: wParam the virtual
// key, lParam the WM_KEYUP lParam to post to the scene's window.
constexpr UINT release_key_message = WM_APP + 1U;
constexpr const wchar_t* key_release_class = L"GUIForms.NormativeTrace.KeyRelease";

// The injector's context: the scene's window, found on first use, its title
// (unique to this process), and the message-only window that posts each
// key's release. Owned by main for the whole run; the key-release window is
// destroyed after the run.
struct WindowsInjection final {
    std::wstring title{};
    HWND target{};
    HWND key_release{};
    double scale{1.0};
};

// The key-release window's procedure: posts the WM_KEYUP a key step left
// pending to the scene's window, whose handle the window carries in its
// user data.
LRESULT CALLBACK key_release_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message != release_key_message) return DefWindowProcW(window, message, wparam, lparam);
    const HWND target = reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (target != nullptr) PostMessageW(target, WM_KEYUP, wparam, lparam);
    return 0;
}

// A message-only window on this thread, so the host's loop (which takes
// every message of the thread) dispatches its messages in queue order with
// the scene window's. Null when it could not be made.
HWND make_key_release_window() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof window_class;
    window_class.lpfnWndProc = key_release_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = key_release_class;
    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    const HWND window = CreateWindowExW(0, key_release_class, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
    return window;
}

HWND target_window(WindowsInjection& injection) {
    if (injection.target == nullptr) {
        injection.target = gui_forms::test_support::own_gui_forms_window(injection.title.c_str());
        if (injection.target != nullptr && injection.key_release != nullptr) {
            SetWindowLongPtrW(injection.key_release, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(injection.target));
        }
    }
    return injection.target;
}

// The step's logical point in device pixels of the client area.
POINT client_point(const WindowsInjection& injection, const gui_forms::Point position) {
    POINT point{};
    point.x = static_cast<LONG>(std::lround(position.x * injection.scale));
    point.y = static_cast<LONG>(std::lround(position.y * injection.scale));
    return point;
}

LPARAM point_parameter(const POINT point) {
    const LPARAM parameter = MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y));
    return parameter;
}

void post_motion(WindowsInjection& injection, const gui_forms::Point position) {
    const POINT point = client_point(injection, position);
    PostMessageW(injection.target, WM_MOUSEMOVE, 0, point_parameter(point));
}

void post_click(WindowsInjection& injection, const gui_forms::Point position) {
    const POINT point = client_point(injection, position);
    PostMessageW(injection.target, WM_LBUTTONDOWN, MK_LBUTTON, point_parameter(point));
    PostMessageW(injection.target, WM_LBUTTONUP, 0, point_parameter(point));
}

void post_wheel_down(WindowsInjection& injection, const gui_forms::Point position) {
    POINT point = client_point(injection, position);
    if (ClientToScreen(injection.target, &point) == 0) return;
    const WPARAM delta = MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA));
    PostMessageW(injection.target, WM_MOUSEWHEEL, delta, point_parameter(point));
}

// The virtual key and set-1 scan code for the HID usages the script uses;
// a virtual key of 0 for any other.
struct WindowsKey final {
    WPARAM virtual_key{};
    LPARAM scan_code{};
};

WindowsKey windows_key(const std::uint32_t physical_key) {
    WindowsKey key{};
    if (physical_key == gui_forms::PhysicalKey::tab) {
        key.virtual_key = VK_TAB;
        key.scan_code = 0x0F;
    } else if (physical_key == gui_forms::PhysicalKey::a) {
        key.virtual_key = 'A';
        key.scan_code = 0x1E;
    }
    return key;
}

// The key down now; its release through the key-release window (see the
// file comment). Key message lParam: repeat count in bits 0-15, scan code in
// 16-23; a release also sets bit 30 (the key was down) and bit 31 (it is
// being released).
void post_key(WindowsInjection& injection, const std::uint32_t physical_key) {
    const WindowsKey key = windows_key(physical_key);
    if (key.virtual_key == 0U) return;
    const LPARAM down = static_cast<LPARAM>(1) | (key.scan_code << 16);
    const LPARAM up = down | (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
    PostMessageW(injection.target, WM_KEYDOWN, key.virtual_key, down);
    if (injection.key_release != nullptr) {
        PostMessageW(injection.key_release, release_key_message, key.virtual_key, up);
    } else {
        PostMessageW(injection.target, WM_KEYUP, key.virtual_key, up);
    }
}

// NormativeInjector's target: the step's window messages.
void inject_step(void* const context, const gui_forms::tests::NormativeStep& step) {
    WindowsInjection& injection = *static_cast<WindowsInjection*>(context);
    if (target_window(injection) == nullptr) return;
    switch (step.kind) {
    case gui_forms::tests::NormativeStepKind::pointer_move:
        post_motion(injection, step.position);
        break;
    case gui_forms::tests::NormativeStepKind::click:
        post_click(injection, step.position);
        break;
    case gui_forms::tests::NormativeStepKind::wheel_down:
        post_wheel_down(injection, step.position);
        break;
    case gui_forms::tests::NormativeStepKind::key:
    case gui_forms::tests::NormativeStepKind::typed:
        post_key(injection, step.physical_key);
        break;
    default:
        break;
    }
}

// The run's state, owned by main for the whole run; the driver is made in
// host_ready, once the window and its close request exist.
struct RunState final {
    gui_forms::tests::NormativeScene* scene{};
    gui_forms::Window* window{};
    WindowsInjection* injection{};
    std::string output{};
    std::unique_ptr<gui_forms::tests::NormativeDriver> driver{};
};

using VoidCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>;
using ReadClipboardCallback = std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback = std::function<gui_forms::HostServiceStatus(std::string_view)>;

class StartDriver final {
public:
    explicit StartDriver(RunState& state) noexcept : state_(state) {}

    void operator()(VoidCallback, VoidCallback request_close, ShowDialogCallback, ReadClipboardCallback,
                    WriteClipboardCallback) const {
        // The host attached the window at its device scale before
        // host_ready.
        const double scale = (*state_.window).scale();
        (*state_.injection).scale = scale;
        (*state_.scene).begin(scale);
        gui_forms::tests::NormativeInjector injector{};
        injector.inject = &inject_step;
        injector.context = state_.injection;
        state_.driver = std::make_unique<gui_forms::tests::NormativeDriver>(
            *state_.scene, *state_.window, injector, state_.output, std::move(request_close));
        (*state_.driver).start();
    }

private:
    RunState& state_;
};

} // namespace

int main(int argc, char** argv) {
    RunState state{};
    state.output = "normative/windows.trace";
    double scale_override = 0.0;
    for (int index = 1; index + 1 < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--output") state.output = argv[index + 1];
        if (argument == "--scale") scale_override = std::strtod(argv[index + 1], nullptr);
    }
    if (!(scale_override == 0.0 || (scale_override >= 1.0 && scale_override <= 4.0))) {
        std::printf("windows normative trace: --scale must be from 1 to 4\nfailures=1\n");
        return 1;
    }
    const std::string title = gui_forms::test_support::process_title(window_title_base);
    WindowsInjection injection{};
    injection.title = gui_forms::test_support::process_title_wide(window_title_base);
    injection.key_release = make_key_release_window();
    if (injection.key_release == nullptr) {
        std::printf("windows normative trace: the key-release window could not be made\nfailures=1\n");
        return 1;
    }
    state.injection = &injection;
    // Declared before the window it records, which the host destroys
    // before run_windows returns.
    gui_forms::tests::NormativeScene scene;
    std::unique_ptr<gui_forms::Window> window = scene.make_window();
    state.scene = &scene;
    state.window = window.get();
    gui_forms::host::WindowsHostOptions options;
    options.title = title;
    options.initial_size = gui_forms::tests::normative_window_size;
    options.scale_override = scale_override;
    options.print_metrics_on_close = false;
    options.host_ready = StartDriver(state);
    const int result = gui_forms::host::run_windows(std::move(window), std::move(options));
    const bool written = state.driver != nullptr && (*state.driver).written();
    state.driver.reset();
    DestroyWindow(injection.key_release);
    if (!written) {
        std::printf("windows normative trace: the trace was not written (host result %d)\nfailures=1\n", result);
        return 1;
    }
    std::printf("windows normative trace written to %s\nfailures=%d\n", state.output.c_str(), result == 0 ? 0 : 1);
    return result == 0 ? 0 : 1;
}
