// The Gallery published through the ATK adapter, for the AT-SPI client
// tests/linux_accessibility_gallery_client.py (stage A, A-L2).
//
// Two X windows: the Gallery and a supplement window
// (tests/linux_accessibility_gallery_session.hpp), each with its own
// LinuxAccessibility frame under one application. Host services come from
// the headless host, so host dialogs answer at once and nothing advances
// without a command.
//
// Execution: this program's one thread is the UI thread. It iterates the
// default GLib main context (where the AT-SPI bridge calls the ATK
// adapter), drains X events, and reads line commands from standard input.
// After each command it publishes both windows, then writes the answer, so
// the client reads an answer and the published tree of one semantic
// generation. Exit 0 after "quit"; 1 when the display cannot be opened;
// 2 when standard input closes first.

#include "../src/host/linux/accessibility/linux_accessibility.hpp"
#include "linux_accessibility_gallery_session.hpp"

#include <X11/Xlib.h>
#include <poll.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

// No `using namespace gui_forms`: Xlib declares a global `Window` typedef,
// which would make gui_forms::Window ambiguous here.

namespace {

// Reads what standard input has ready (without blocking) into `pending`.
// Returns false at end of input.
bool read_ready_input(std::string& pending) {
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    if (poll(&descriptor, 1, 0) <= 0) return true;
    char buffer[4096] = {};
    const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count <= 0) return false;
    pending.append(buffer, static_cast<std::size_t>(count));
    return true;
}

void drain_x_events(Display* display) {
    while (XPending(display) > 0) {
        XEvent event{};
        XNextEvent(display, &event);
    }
}

} // namespace

int main() {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        std::cerr << "linux accessibility gallery fixture: no X display\n";
        return 1;
    }
    const ::Window gallery_native = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
        static_cast<unsigned int>(gui_forms::gallery::gallery_default_size.width),
        static_cast<unsigned int>(gui_forms::gallery::gallery_default_size.height), 0, 0, 0xffffff);
    const ::Window supplement_native = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
        static_cast<unsigned int>(gui_forms::accessibility_session::supplement_size.width),
        static_cast<unsigned int>(gui_forms::accessibility_session::supplement_size.height), 0, 0, 0xffffff);
    XMapWindow(display, gallery_native);
    XMapWindow(display, supplement_native);
    XFlush(display);
    int status = 2;
    {
        // Declared in dependency order: each borrower after what it borrows,
        // so it is destroyed first.
        std::unique_ptr<gui_forms::Window> gallery_window = gui_forms::gallery::make_gallery();
        std::unique_ptr<gui_forms::Window> supplement_window = gui_forms::accessibility_session::make_supplement();
        gui_forms::host::HeadlessHost host(*gallery_window);
        gui_forms::accessibility_session::GallerySession session(*gallery_window, host, *supplement_window);
        gui_forms::host::linux_detail::LinuxAccessibility gallery_accessibility(
            display, gallery_native, *gallery_window, "GUI.Forms Gallery accessibility fixture");
        gui_forms::host::linux_detail::LinuxAccessibility supplement_accessibility(
            display, supplement_native, *supplement_window, "GUI.Forms accessibility supplement");
        std::cout << session.ready_line() << '\n' << std::flush;
        std::string pending;
        bool finished = false;
        while (!finished) {
            gui_forms::host::linux_detail::accessibility_dispatch();
            gui_forms::host::linux_detail::accessibility_wait(ConnectionNumber(display), STDIN_FILENO, 50);
            drain_x_events(display);
            if (!read_ready_input(pending)) break;
            std::size_t end = pending.find('\n');
            while (end != std::string::npos && !finished) {
                const std::string line = pending.substr(0, end);
                pending.erase(0, end + 1U);
                const std::string answer = session.handle(line, finished);
                gallery_accessibility.update();
                supplement_accessibility.update();
                std::cout << answer << '\n' << std::flush;
                end = pending.find('\n');
            }
        }
        if (finished) status = 0;
        supplement_accessibility.detach();
        gallery_accessibility.detach();
    }
    XDestroyWindow(display, supplement_native);
    XDestroyWindow(display, gallery_native);
    XCloseDisplay(display);
    return status;
}
