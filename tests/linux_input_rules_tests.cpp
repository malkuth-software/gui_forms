// The X11 host's computed input (owner's mouse-event rule, 2026-11-08):
// click counts from the desktop's double-click time and distance, and the
// keys that ask for a context menu. No Xlib, so tested on every platform.

#include "../src/host/linux/application/linux_input_rules.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace gui_forms;
using namespace gui_forms::host::linux_detail;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_click_counts() {
    LinuxClickHistory history{};
    const std::uint32_t interval = 400U;
    const double distance = default_double_click_distance;
    require(next_click_count(history, 1U, 1000U, {10.0, 10.0}, interval, distance) == 1U, "a first press is one");
    require(next_click_count(history, 1U, 1200U, {12.0, 8.0}, interval, distance) == 2U,
            "a press within the time and distance is the second");
    require(next_click_count(history, 1U, 1300U, {12.0, 8.0}, interval, distance) == 3U, "and the third");
    require(next_click_count(history, 1U, 1400U, {12.0, 8.0}, interval, distance) == 1U, "then one again");
    require(next_click_count(history, 1U, 1900U, {12.0, 8.0}, interval, distance) == 1U,
            "a press after the interval is one");
    require(next_click_count(history, 1U, 2000U, {12.0 + distance + 1.0, 8.0}, interval, distance) == 1U,
            "a press beyond the distance is one");
    require(next_click_count(history, 3U, 2100U, {12.0 + distance + 1.0, 8.0}, interval, distance) == 1U,
            "another button starts again");
    LinuxClickHistory wrapped{};
    static_cast<void>(next_click_count(wrapped, 1U, 0xFFFFFF00U, {0.0, 0.0}, interval, distance));
    require(next_click_count(wrapped, 1U, 0x00000010U, {0.0, 0.0}, interval, distance) == 2U,
            "a wrapped server time compares by its difference");
    LinuxClickHistory desktop{};
    static_cast<void>(next_click_count(desktop, 1U, 0U, {0.0, 0.0}, 250U, 5.0));
    require(next_click_count(desktop, 1U, 260U, {4.0, 4.0}, 250U, 5.0) == 1U,
            "the desktop's own interval is used");
}

void test_context_keys() {
    require(requests_keyboard_context(PhysicalKey::context_menu, Modifier::none), "the Menu key asks");
    require(requests_keyboard_context(PhysicalKey::f10, Modifier::shift), "Shift+F10 asks");
    require(!requests_keyboard_context(PhysicalKey::f10, Modifier::none), "F10 alone does not");
    require(!requests_keyboard_context(PhysicalKey::f10, Modifier::shift | Modifier::control),
            "Ctrl+Shift+F10 does not");
}

} // namespace

int main() {
    try {
        test_click_counts();
        test_context_keys();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "linux input rules tests passed\n";
    return EXIT_SUCCESS;
}
