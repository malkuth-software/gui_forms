// F6 focus regions (owner decision RB-17): a window-level ordered list of
// regions that F6 and Shift+F6 cycle, entering each at the control that last
// had focus in it or at its first Tab stop; refusals; and a control that
// handles F6 itself keeping it.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// A focusable control that keeps F6 for itself, as ObjectView's details
// header does.
class KeepsF6 final : public Control {
public:
    explicit KeepsF6(StableId stable_id) : Control(std::move(stable_id)) { focusable = true; }
    void on_key(KeyEvent& event) override {
        if (event.action == KeyAction::down && event.physical_key == PhysicalKey::f6) {
            ++kept;
            event.handled = true;
        }
    }
    int kept{};
};

struct Region final {
    std::shared_ptr<Panel> panel;
    std::shared_ptr<Button> first;
    std::shared_ptr<Button> second;
};

Region region(const std::shared_ptr<Panel>& root, const std::string& name, double y) {
    Region result;
    result.panel = make_control<Panel>(StableId(name));
    result.first = make_control<Button>(StableId(name + ".first"), "First");
    result.second = make_control<Button>(StableId(name + ".second"), "Second");
    (*result.panel).set_bounds({0.0, y, 300.0, 40.0});
    (*result.first).set_bounds({4.0, 4.0, 80.0, 28.0});
    (*result.second).set_bounds({100.0, 4.0, 80.0, 28.0});
    (*result.panel).add_child(result.first);
    (*result.panel).add_child(result.second);
    (*root).add_child(result.panel);
    return result;
}

bool press(Window& window, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, PhysicalKey::f6};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

void cycles_three_regions() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const Region ribbon = region(root, "ribbon", 0.0);
    const Region work = region(root, "work", 50.0);
    const Region status = region(root, "status", 100.0);
    std::shared_ptr<Button> outside = make_control<Button>(StableId("outside"), "Outside");
    (*outside).set_bounds({0.0, 150.0, 80.0, 28.0});
    (*root).add_child(outside);
    Window window(root, {300.0, 200.0});
    window.perform_layout();
    window.set_focus_regions({ribbon.panel, work.panel, status.panel});
    require(window.focus_regions().size() == 3U && window.focus_regions()[1] == work.panel,
            "the window keeps its regions in order");

    require(window.request_focus(outside) && press(window) && window.focused_control() == ribbon.first,
            "F6 from outside every region enters the first region at its first Tab stop");
    require(window.request_focus(ribbon.second), "focus moves within the first region");
    require(press(window) && window.focused_control() == work.first, "F6 enters the next region");
    require(window.request_focus(work.second) && press(window) && window.focused_control() == status.first,
            "F6 moves on to the third region");
    require(press(window) && window.focused_control() == ribbon.second,
            "F6 wraps to the first region at the control that last had focus there");
    require(press(window, Modifier::shift) && window.focused_control() == status.first,
            "Shift+F6 moves to the previous region, wrapping backwards");
    require(press(window, Modifier::shift) && window.focused_control() == work.second,
            "Shift+F6 returns to the work region's remembered control");
    require(window.request_focus(outside) && press(window, Modifier::shift) &&
                window.focused_control() == status.first,
            "Shift+F6 from outside every region enters the last region");

    (*work.panel).enabled = false;
    require(window.request_focus(ribbon.first) && press(window) && window.focused_control() == status.first,
            "a disabled region is skipped");
    (*work.panel).enabled = true;
    KeyEvent control_f6{KeyAction::down, PhysicalKey::f6};
    control_f6.modifiers = Modifier::control;
    static_cast<void>(window.dispatch_key(control_f6));
    require(window.focused_control() == status.first, "Control+F6 is left to the application");

    window.set_focus_regions({});
    require(!press(window) && window.focused_control() == status.first, "an empty list turns F6 cycling off");
}

void refusals_and_owners() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const Region left = region(root, "left", 0.0);
    const Region right = region(root, "right", 50.0);
    std::shared_ptr<KeepsF6> keeper = make_control<KeepsF6>(StableId("keeper"));
    (*keeper).set_bounds({200.0, 4.0, 40.0, 28.0});
    (*right.panel).add_child(keeper);
    Window window(root, {300.0, 120.0});
    window.perform_layout();
    window.set_focus_regions({left.panel, right.panel});
    bool threw = false;
    try {
        window.set_focus_regions({left.panel, left.first});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && window.focus_regions().size() == 2U,
            "nested regions are refused and the previous list is kept");
    threw = false;
    try {
        window.set_focus_regions({left.panel, left.panel});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a repeated region is refused");
    std::shared_ptr<Panel> foreign = make_control<Panel>(StableId("foreign"));
    threw = false;
    try {
        window.set_focus_regions({foreign});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a region outside the window is refused");

    require(window.request_focus(keeper) && press(window) && (*keeper).kept == 1 &&
                window.focused_control() == keeper,
            "a control that handles F6 itself keeps the key");
    require(window.move_focus_region(true) && window.focused_control() == left.first,
            "move_focus_region moves as F6 does");
    require(window.move_focus_region(false) && window.focused_control() == keeper,
            "moving back enters the region at its remembered control");
}

} // namespace

int main() {
    cycles_three_regions();
    refusals_and_owners();
    std::cout << "F6 focus regions: order, wrapping, remembered focus, skipping, refusals and owners passed\n";
    return 0;
}
