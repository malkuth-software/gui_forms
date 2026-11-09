// Focus restore (SurfaceRoot::restore_focus): a disabled or hidden window
// gives its last focused control focus back when it is enabled or shown
// again, and when it is reactivated; a control disabled inside the window
// loses focus for good, as in Windows Forms.
#include "gui_forms/control.hpp"
#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A window over a root with two focusable fields.
struct FocusWindow final {
    Control::Ptr root{make_control<Control>(StableId("focus.root"))};
    Control::Ptr first{make_control<Control>(StableId("focus.first"))};
    Control::Ptr second{make_control<Control>(StableId("focus.second"))};
    std::unique_ptr<Window> window{};

    FocusWindow() {
        (*first).focusable = true;
        (*second).focusable = true;
        (*root).add_child(first);
        (*root).add_child(second);
        window = std::make_unique<Window>(root, Size{200.0, 100.0});
    }
};

void test_disabled_window_gives_focus_back() {
    FocusWindow fixture;
    Window& window = *fixture.window;
    require(window.request_focus(fixture.second), "the second field took no focus");
    (*fixture.root).enabled = false;
    require(!window.focused_control(), "a disabled window kept a focused control");
    (*fixture.root).enabled = true;
    require(window.focused_control() == fixture.second, "the enabled window did not give focus back");
}

void test_hidden_window_gives_focus_back() {
    FocusWindow fixture;
    Window& window = *fixture.window;
    require(window.request_focus(fixture.first), "the first field took no focus");
    (*fixture.root).visible = false;
    require(!window.focused_control(), "a hidden window kept a focused control");
    (*fixture.root).visible = true;
    require(window.focused_control() == fixture.first, "the shown window did not give focus back");
}

void test_disabled_control_loses_focus() {
    FocusWindow fixture;
    Window& window = *fixture.window;
    require(window.request_focus(fixture.second), "the second field took no focus");
    (*fixture.second).enabled = false;
    require(!window.focused_control(), "a disabled control kept focus");
    (*fixture.second).enabled = true;
    require(!window.focused_control(), "a control enabled again took focus back");
}

void test_reactivation_gives_focus_back() {
    FocusWindow fixture;
    Window& window = *fixture.window;
    window.set_active(true);
    require(window.request_focus(fixture.first), "the first field took no focus");
    // Withdrawn while unfocusable: the re-enable cannot give it back.
    (*fixture.root).enabled = false;
    (*fixture.first).focusable = false;
    (*fixture.root).enabled = true;
    require(!window.focused_control(), "an unfocusable control took focus back");
    (*fixture.first).focusable = true;
    window.set_active(false);
    window.set_active(true);
    require(window.focused_control() == fixture.first, "reactivation did not give focus back");
    require(window.restore_focus() == fixture.first, "restore_focus does not report the focused control");
}

void test_focus_moved_elsewhere_is_kept() {
    FocusWindow fixture;
    Window& window = *fixture.window;
    require(window.request_focus(fixture.first), "the first field took no focus");
    (*fixture.root).enabled = false;
    (*fixture.root).enabled = true;
    require(window.request_focus(fixture.second), "the second field took no focus");
    (*fixture.root).visible = false;
    (*fixture.root).visible = true;
    require(window.focused_control() == fixture.second, "focus went back past a later move");
}

void test_removed_control_is_not_restored() {
    FocusWindow fixture;
    Window& window = *fixture.window;
    require(window.request_focus(fixture.second), "the second field took no focus");
    (*fixture.root).enabled = false;
    static_cast<void>((*fixture.root).remove_child((*fixture.second).runtime_id()));
    (*fixture.root).enabled = true;
    require(!window.focused_control(), "a removed control took focus back");
    require(!window.restore_focus(), "restore_focus found a removed control");
}
} // namespace

int main() {
    try {
        test_disabled_window_gives_focus_back();
        test_hidden_window_gives_focus_back();
        test_disabled_control_loses_focus();
        test_reactivation_gives_focus_back();
        test_focus_moved_elsewhere_is_kept();
        test_removed_control_is_not_restored();
        std::cout << "gui_forms_focus_restore_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_focus_restore_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
