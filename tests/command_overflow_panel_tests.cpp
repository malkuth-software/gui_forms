#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>
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
        (*shelf).group_gap = 4.0;
        (*shelf).group_spec[*selection] = CommandOverflowGroupSpec{176.0, 176.0, 176.0, 2U};
        (*shelf).group_spec[*arrange] = CommandOverflowGroupSpec{306.0, 306.0, 306.0, 1U};
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
                (*fixture.more).layout_collapsed() && (*fixture.more).visible.read(),
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

    const CommandOverflowGroupSpec before = *(*fixture.shelf).group_spec[*fixture.selection].read();
    bool rejected = false;
    try {
        (*fixture.shelf).group_spec[*fixture.selection] = CommandOverflowGroupSpec{176.0, 176.0, 176.0, 1U};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*fixture.shelf).group_spec[*fixture.selection].read() == before,
            "duplicate priority rejection must preserve the prior retained spec atomically");
}

} // namespace

void test_group_spec_child_property() {
    Fixture fixture;
    CommandOverflowPanel& shelf = *fixture.shelf;
    const std::vector<PropertyDescriptor> provided = shelf.child_property_descriptors();
    require(provided.size() == 1U && provided[0].name == "group_spec" && provided[0].kind == BindingValueKind::text,
            "overflow child table: one text group_spec");
    const std::optional<BindingValue> named = shelf.child_property_value(*fixture.selection, "group_spec");
    require(named && std::get<std::string>(*named) == "176 176 176 priority 2",
            "group spec: the text form reads the extents and the priority");
    shelf.set_child_property_value(*fixture.selection, "group-spec", BindingValue{std::string{"150 170.5 0"}});
    const std::optional<CommandOverflowGroupSpec> written = shelf.group_spec[*fixture.selection].read();
    require(written && (*written).minimum == 150.0 && (*written).preferred == 170.5 && (*written).maximum == 0.0 &&
                !(*written).collapse_priority,
            "group spec: a name-keyed text write without a priority reaches the typed read");
    require(!shelf.group_spec[*fixture.more].read() &&
                !shelf.should_serialize_child_property(*fixture.more, "group_spec"),
            "group spec: a child without one reads none and is not serialized");

    bool malformed_refused = false;
    try {
        shelf.set_child_property_value(*fixture.selection, "group_spec", BindingValue{std::string{"150 170 rank 2"}});
    } catch (const std::invalid_argument&) {
        malformed_refused = true;
    }
    bool actuator_refused = false;
    try {
        shelf.group_spec[*fixture.more] = CommandOverflowGroupSpec{72.0, 72.0, 72.0, 3U};
    } catch (const std::invalid_argument&) {
        actuator_refused = true;
    }
    require(malformed_refused && actuator_refused && shelf.group_spec[*fixture.selection].read() == written,
            "group spec: malformed text and the actuator are refused before mutation");

    shelf.reset_child_property(*fixture.selection, "group_spec");
    require(!shelf.group_spec[*fixture.selection].read(), "group spec: the reset (empty text) clears it");
    shelf.group_spec[*fixture.arrange] = std::nullopt;
    static_cast<void>(shelf.remove_child((*fixture.selection).runtime_id()));
    shelf.add_child(fixture.selection);
    require(!shelf.group_spec[*fixture.selection].read(), "group spec: the value is dropped when the child leaves");
}

int main() {
    try {
        test_full_and_collapsed_projection();
        test_focus_protection_and_atomic_metadata();
        test_group_spec_child_property();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "command_overflow_panel_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
