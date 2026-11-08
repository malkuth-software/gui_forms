#include "../src/host/linux/accessibility/linux_accessibility.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/input_controls.hpp"
#include <X11/Xlib.h>
#include <chrono>
#include <iostream>
using namespace gui_forms;
using namespace gui_forms::host::linux_detail;
namespace {
struct Fixture {
    int presses = 0;
    void pressed(ButtonBase&) { ++presses; }
};
}
int main() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) { return 1; }
    ::Window native = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 400, 300, 0, 0, 0xffffff);
    XMapWindow(display, native); XFlush(display);
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("press"), "Draw mark");
    (*button).set_requested_bounds({10, 10, 160, 30}); (*root).add_child(button);
    std::shared_ptr<NumericUpDown> number = make_control<NumericUpDown>(StableId("position"));
    (*number).accessible_name = "Canvas X"; (*number).set_range(0, 200); (*number).set_requested_bounds({10, 50, 160, 30}); (*root).add_child(number);
    std::shared_ptr<TextBox> text = make_control<TextBox>(StableId("text"));
    (*text).accessible_name = "Artwork text"; (*text).set_requested_bounds({10, 90, 160, 30}); (*root).add_child(text);
    Fixture fixture; SubscriptionToken token = (*button).clicked().subscribe(Delegate<ButtonBase&>::bind<Fixture, &Fixture::pressed>(fixture));
    gui_forms::Window model(root, {400, 300}); model.perform_layout();
    LinuxAccessibility accessibility(display, native, model, "Plan accessibility fixture");
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (std::chrono::steady_clock::now() < deadline) {
        accessibility_dispatch(); accessibility.update();
        // The client's last request puts the caret after the combining
        // sequence of "e", U+0301 and "x" (byte 3), after the Text checks.
        if (fixture.presses == 1 && (*number).value() == 37 && (*text).text.read() == "e\xCC\x81x" &&
            (*text).selection().caret == Utf8Offset(3U)) {
            std::cout << "AT-SPI action, numeric value, UTF-8 text edit and Text interface passed\n";
            accessibility.detach(); XDestroyWindow(display, native); XCloseDisplay(display); return 0;
        }
        accessibility_wait(ConnectionNumber(display), -1, 100);
        while (XPending(display)) { XEvent event{}; XNextEvent(display, &event); }
    }
    std::cerr << "AT-SPI fixture timed out: presses=" << fixture.presses << " value=" << (*number).value() << " text=" << (*text).text.read() << '\n';
    accessibility.detach(); XDestroyWindow(display, native); XCloseDisplay(display); return 2;
}
