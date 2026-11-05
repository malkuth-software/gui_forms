#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    std::shared_ptr<CommandOverflowPanel> shelf =
        make_control<CommandOverflowPanel>(StableId("overflow.shelf"));
    std::shared_ptr<Panel> selection =
        make_control<Panel>(StableId("overflow.shelf.selection"));
    std::shared_ptr<Panel> arrange =
        make_control<Panel>(StableId("overflow.shelf.arrange"));
    std::shared_ptr<Button> arrange_button =
        make_control<Button>(StableId("overflow.shelf.arrange.view"), "View");
    std::shared_ptr<DropDownButton> more =
        make_control<DropDownButton>(StableId("overflow.shelf.more"), "More",
                                     DropDownButtonMode::menu);
    Window window;

    Fixture() : window(shelf, {600.0, 66.0}) {
        (*selection).set_requested_bounds({0.0, 0.0, 176.0, 60.0});
        (*arrange).set_requested_bounds({0.0, 0.0, 306.0, 60.0});
        (*more).set_requested_bounds({0.0, 0.0, 72.0, 43.0});
        (*arrange).add_child(arrange_button);
        (*shelf).add_child(selection);
        (*shelf).add_child(arrange);
        (*shelf).add_child(more);
        (*shelf).set_group_gap(4.0);
        (*shelf).set_group_spec(*selection, {176.0, 176.0, 176.0, 2U});
        (*shelf).set_group_spec(*arrange, {306.0, 306.0, 306.0, 1U});
        (*shelf).set_overflow_actuator(*more);
        window.perform_layout();
    }
};

void test_full_and_collapsed_projection() {
    Fixture fixture;
    const CommandOverflowSnapshot full = (*fixture.shelf).layout_snapshot();
    require(!full.overflow_actuator_visible && !full.overflow &&
                full.groups.size() == 2U && !full.groups[0].collapsed &&
                !full.groups[1].collapsed &&
                (*fixture.more).layout_collapsed() && (*fixture.more).visible.type(),
            "full shelf must retain both groups and layout-collapse only the actuator");

    fixture.window.resize({440.0, 66.0});
    fixture.window.perform_layout();
    const CommandOverflowSnapshot narrow = (*fixture.shelf).layout_snapshot();
    require(narrow.overflow_actuator_visible && !narrow.overflow &&
                !narrow.groups[0].collapsed && narrow.groups[1].collapsed &&
                !(*fixture.selection).layout_collapsed() &&
                (*fixture.arrange).layout_collapsed() &&
                !(*fixture.more).layout_collapsed() &&
                narrow.groups[1].stable_id == "overflow.shelf.arrange",
            "narrow shelf must collapse the lower-priority whole group and publish its stable id");

    fixture.window.resize({240.0, 66.0});
    fixture.window.perform_layout();
    const CommandOverflowSnapshot compact = (*fixture.shelf).layout_snapshot();
    require(compact.overflow_actuator_visible && !compact.overflow &&
                compact.groups[0].collapsed && compact.groups[1].collapsed &&
                !(*fixture.more).layout_collapsed(),
            "compact shelf must preserve the overflow actuator after all groups collapse");
}

void test_focus_protection_and_atomic_metadata() {
    Fixture fixture;
    require(fixture.window.request_focus(fixture.arrange_button),
            "arrange command must accept focus before collapse");
    fixture.window.resize({440.0, 66.0});
    fixture.window.perform_layout();
    const CommandOverflowSnapshot protected_snapshot =
        (*fixture.shelf).layout_snapshot();
    require(protected_snapshot.groups[0].collapsed &&
                !protected_snapshot.groups[1].collapsed &&
                fixture.window.focused_control() == fixture.arrange_button,
            "focused group must be protected while another authored group can collapse");

    const CommandOverflowGroupSpec before =
        *(*fixture.shelf).group_spec(*fixture.selection);
    bool rejected = false;
    try {
        (*fixture.shelf).set_group_spec(
            *fixture.selection, {176.0, 176.0, 176.0, 1U});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*fixture.shelf).group_spec(*fixture.selection) == before,
            "duplicate priority rejection must preserve the prior retained spec atomically");
}

} // namespace

int main() {
    try {
        test_full_and_collapsed_projection();
        test_focus_protection_and_atomic_metadata();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "command_overflow_panel_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
