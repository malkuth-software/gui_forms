#include "gui_forms/platform/linux_host.hpp"
#include "support/normative_trace.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// The normative trace on the X11 host (release-1.0 condition 2;
// tests/support/normative_trace.hpp): the scene in a real X11 window, each
// step sent as X events from a second display connection to the window
// (XSendEvent, as the Linux host tests send theirs), and the trace written
// to --output (default normative/x11.trace in the working directory).
// tools/normative_trace.py compare checks it against the headless
// reference at the window's scale. Run under an X server (the Linux VM's
// session, or Xvfb).
//
// X events per step: a move is MotionNotify; a click ButtonPress and
// ButtonRelease of button 1; the wheel button 5 pressed and released (X11's
// wheel down); a key KeyPress and KeyRelease of the key's keysym's keycode,
// so a typed key's text comes through the host's own lookup.

namespace {

using X11Window = ::Window;

constexpr const char* window_title = "GUI.Forms normative trace";

X11Window find_window(Display* const display, const X11Window root, const std::string& name) {
    char* title = nullptr;
    if (XFetchName(display, root, &title) != 0 && title != nullptr) {
        const bool equal = name == title;
        XFree(title);
        if (equal) return root;
    }
    X11Window returned{};
    X11Window parent{};
    X11Window* children = nullptr;
    unsigned int count{};
    if (XQueryTree(display, root, &returned, &parent, &children, &count) == 0) return 0;
    X11Window found = 0;
    for (unsigned int index = 0U; index < count && found == 0; ++index) {
        found = find_window(display, children[index], name);
    }
    if (children != nullptr) XFree(children);
    return found;
}

// The injector's context: the second connection and the scene's window,
// found on first use. Owned by main; the connection is closed after the run.
struct X11Injection final {
    Display* display{};
    X11Window target{};
};

X11Window target_window(X11Injection& injection) {
    if (injection.target == 0) {
        injection.target = find_window(injection.display, DefaultRootWindow(injection.display), window_title);
    }
    return injection.target;
}

void send_button(X11Injection& injection, const int type, const unsigned int button, const gui_forms::Point at) {
    XEvent event{};
    event.xbutton.type = type;
    event.xbutton.display = injection.display;
    event.xbutton.window = injection.target;
    event.xbutton.root = DefaultRootWindow(injection.display);
    event.xbutton.same_screen = True;
    event.xbutton.button = button;
    event.xbutton.x = static_cast<int>(at.x);
    event.xbutton.y = static_cast<int>(at.y);
    event.xbutton.state = type == ButtonRelease && button == Button1 ? Button1Mask : 0U;
    const long mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    XSendEvent(injection.display, injection.target, False, mask, &event);
}

void send_motion(X11Injection& injection, const gui_forms::Point at) {
    XEvent event{};
    event.xmotion.type = MotionNotify;
    event.xmotion.display = injection.display;
    event.xmotion.window = injection.target;
    event.xmotion.root = DefaultRootWindow(injection.display);
    event.xmotion.same_screen = True;
    event.xmotion.x = static_cast<int>(at.x);
    event.xmotion.y = static_cast<int>(at.y);
    XSendEvent(injection.display, injection.target, False, PointerMotionMask, &event);
}

void send_key(X11Injection& injection, const KeySym symbol) {
    XEvent event{};
    event.xkey.type = KeyPress;
    event.xkey.display = injection.display;
    event.xkey.window = injection.target;
    event.xkey.root = DefaultRootWindow(injection.display);
    event.xkey.same_screen = True;
    event.xkey.keycode = XKeysymToKeycode(injection.display, symbol);
    XSendEvent(injection.display, injection.target, False, KeyPressMask, &event);
    event.xkey.type = KeyRelease;
    XSendEvent(injection.display, injection.target, False, KeyReleaseMask, &event);
}

// The keysym for the HID usages the script uses; NoSymbol for any other.
KeySym key_symbol(const std::uint32_t physical_key) {
    if (physical_key == gui_forms::PhysicalKey::tab) return XK_Tab;
    if (physical_key == gui_forms::PhysicalKey::a) return XK_a;
    return NoSymbol;
}

// NormativeInjector's target: the step's X events.
void inject_step(void* const context, const gui_forms::tests::NormativeStep& step) {
    X11Injection& injection = *static_cast<X11Injection*>(context);
    if (target_window(injection) == 0) return;
    switch (step.kind) {
    case gui_forms::tests::NormativeStepKind::pointer_move:
        send_motion(injection, step.position);
        break;
    case gui_forms::tests::NormativeStepKind::click:
        send_button(injection, ButtonPress, Button1, step.position);
        send_button(injection, ButtonRelease, Button1, step.position);
        break;
    case gui_forms::tests::NormativeStepKind::wheel_down:
        send_button(injection, ButtonPress, Button5, step.position);
        send_button(injection, ButtonRelease, Button5, step.position);
        break;
    case gui_forms::tests::NormativeStepKind::key:
    case gui_forms::tests::NormativeStepKind::typed:
        send_key(injection, key_symbol(step.physical_key));
        break;
    default:
        break;
    }
    XFlush(injection.display);
}

// The run's state, owned by main for the whole run; the driver is made in
// host_ready, once the window and its close request exist.
struct RunState final {
    gui_forms::tests::NormativeScene* scene{};
    gui_forms::Window* window{};
    X11Injection* injection{};
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
        (*state_.scene).begin((*state_.window).scale());
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
    state.output = "normative/x11.trace";
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == "--output") state.output = argv[index + 1];
    }
    X11Injection injection{};
    injection.display = XOpenDisplay(nullptr);
    if (injection.display == nullptr) {
        std::printf("x11 normative trace: no X display\nfailures=1\n");
        return 1;
    }
    state.injection = &injection;
    // Declared before the window it records, which the host destroys
    // before run_linux returns.
    gui_forms::tests::NormativeScene scene;
    std::unique_ptr<gui_forms::Window> window = scene.make_window();
    state.scene = &scene;
    state.window = window.get();
    gui_forms::host::LinuxHostOptions options;
    options.title = window_title;
    options.initial_size = gui_forms::tests::normative_window_size;
    options.host_ready = StartDriver(state);
    const int result = gui_forms::host::run_linux(std::move(window), std::move(options));
    const bool written = state.driver != nullptr && (*state.driver).written();
    state.driver.reset();
    XCloseDisplay(injection.display);
    if (!written) {
        std::printf("x11 normative trace: the trace was not written (host result %d)\nfailures=1\n", result);
        return 1;
    }
    std::printf("x11 normative trace written to %s\nfailures=%d\n", state.output.c_str(), result == 0 ? 0 : 1);
    return result == 0 ? 0 : 1;
}
