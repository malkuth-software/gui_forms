// BindingNavigator (stage W): the standard items, command enabled states
// that follow a BindingSource's position, count and permissions, moving by
// command and by the position box, adding and deleting records, and losing
// the source.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/binding_schema/binding_schema.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/binding/binding_table/binding_table.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/binding_navigator.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct SupplyRecord final {
    int* requests{};
    bool* supply{};
    void operator()(BindingNavigatorAddNew& request) const {
        ++*requests;
        // The source's table has the one column, name, so the row is positional.
        if (*supply) request.row = BindingRow{"record.new", {std::string("New")}};
    }
};

// The returned pointer borrows from `items`, so the vector must outlive it;
// a pointer into the temporary realized_items() returns would dangle, and the
// deleted overload refuses one.
const ToolStrip::Item* find_item(const std::vector<ToolStrip::Item>& items, std::string_view id) {
    for (const ToolStrip::Item& item : items) {
        if (item.stable_id == id) return &item;
    }
    return nullptr;
}
const ToolStrip::Item* find_item(std::vector<ToolStrip::Item>&& items, std::string_view id) = delete;

std::string count_text(const BindingNavigator& navigator) {
    for (const ToolStripItemSpec& item : navigator.items()) {
        if (item.stable_id == "count") return item.text;
    }
    return {};
}

// Replaces source's rows with count rows of one column, name.
void fill_rows(BindingSource& source, std::size_t count) {
    BindingTable& table = *source.table();
    static_cast<void>(table.find_or_add_column("name"));
    std::vector<BindingRow> rows;
    rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        rows.push_back(BindingRow{"record." + std::to_string(index), {std::string("Row")}});
    }
    table.assign(std::move(rows));
}

bool enabled(const std::shared_ptr<Command>& command) {
    const bool result = (*command).state().enabled;
    return result;
}

void standard_items_and_states() {
    std::shared_ptr<BindingNavigator> navigator = make_control<BindingNavigator>(StableId("navigator"));
    Window window(navigator, {700.0, 25.0});
    window.perform_layout();
    require(validate_control_anatomy((*navigator).anatomy()).defect == AnatomyDefect::none &&
                (*navigator).anatomy().control_id == "binding_navigator",
            "BindingNavigator publishes a valid anatomy");
    const std::vector<ToolStrip::Item> items = (*navigator).realized_items();
    require(find_item(items, "move-first") != nullptr && find_item(items, "position") != nullptr &&
                find_item(items, "count") != nullptr && find_item(items, "delete") != nullptr &&
                (*navigator).overflow_item_ids().empty(),
            "the navigator lays out its standard items");
    require(!enabled((*navigator).move_first_command()) && !enabled((*navigator).add_new_command()) &&
                !enabled((*navigator).delete_command()) && count_text(*navigator) == "of 0" &&
                !(*(*navigator).text_box("position")).enabled.read(),
            "without a source every action is disabled");

    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(window);
    fill_rows(*source, 3U);
    (*navigator).binding_source = source;
    require(count_text(*navigator) == "of 3" && (*(*navigator).text_box("position")).text.read() == "1",
            "the navigator shows the count and the one-based position");
    require(!enabled((*navigator).move_first_command()) && !enabled((*navigator).move_previous_command()) &&
                enabled((*navigator).move_next_command()) && enabled((*navigator).move_last_command()) &&
                enabled((*navigator).add_new_command()) && enabled((*navigator).delete_command()),
            "at the first record only the forward moves, add and delete are enabled");
    require((*(*navigator).move_next_command()).execute() && (*source).position() == 1 &&
                enabled((*navigator).move_previous_command()) && (*(*navigator).text_box("position")).text.read() == "2",
            "the next command moves the source and the states follow");

    const std::vector<ToolStrip::Item> moved_items = (*navigator).realized_items();
    const ToolStrip::Item* last = find_item(moved_items, "move-last");
    const Point at{(*last).parts.item_face.bounds.x + 4.0, (*last).parts.item_face.bounds.y + 4.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, at}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, at}));
    require((*source).position() == 2 && !enabled((*navigator).move_next_command()) &&
                !enabled((*navigator).move_last_command()),
            "clicking the last button moves to the last record");

    (*source).set_position(0);
    require((*(*navigator).text_box("position")).text.read() == "1", "a move made elsewhere shows in the navigator");
}

void position_box() {
    std::shared_ptr<BindingNavigator> navigator = make_control<BindingNavigator>(StableId("navigator"));
    Window window(navigator, {700.0, 25.0});
    window.perform_layout();
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(window);
    fill_rows(*source, 5U);
    (*navigator).binding_source = source;
    const std::shared_ptr<TextBox> box = (*navigator).text_box("position");
    require(window.request_focus(box), "the position box takes focus");
    (*box).text = "4";
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    require(window.dispatch_key(enter) && (*source).position() == 3, "Enter in the position box moves there");
    (*box).text = "9";
    static_cast<void>(window.dispatch_key(enter));
    require((*source).position() == 3 && (*box).text.read() == "4", "a position past the count is refused and restored");
    (*box).text = "x";
    static_cast<void>(window.dispatch_key(enter));
    require((*box).text.read() == "4", "text that is not a number is restored");
    (*box).text = "2";
    KeyEvent escape{KeyAction::down, PhysicalKey::escape};
    static_cast<void>(window.dispatch_key(escape));
    require((*source).position() == 3 && (*box).text.read() == "4", "Escape restores the shown position");
}

void add_and_delete() {
    std::shared_ptr<BindingNavigator> navigator = make_control<BindingNavigator>(StableId("navigator"));
    Window window(navigator, {700.0, 25.0});
    window.perform_layout();
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(window);
    fill_rows(*source, 2U);
    (*navigator).binding_source = source;
    int requests = 0;
    bool supply = false;
    SubscriptionToken token = (*navigator).adding_new().subscribe(SupplyRecord{&requests, &supply});
    require((*(*navigator).add_new_command()).execute() && requests == 1 && (*source).count() == 2U,
            "add without a supplied record adds nothing");
    supply = true;
    require((*(*navigator).add_new_command()).execute() && (*source).count() == 3U && (*source).position() == 2 &&
                count_text(*navigator) == "of 3",
            "add appends the supplied record and moves to it");
    require((*(*navigator).delete_command()).execute() && (*source).count() == 2U && count_text(*navigator) == "of 2",
            "delete removes the current record");
    (*source).set_allow_remove(false);
    require(!enabled((*navigator).delete_command()), "delete follows the source's removal permission");
    (*source).set_allow_new(false);
    require(!enabled((*navigator).add_new_command()), "add follows the source's addition permission");
    (*source).set_allow_remove(true);
    (*source).clear();
    require(count_text(*navigator) == "of 0" && !enabled((*navigator).move_last_command()) &&
                (*(*navigator).text_box("position")).text.read() == "0",
            "an empty source disables the moves");
    (*navigator).binding_source = nullptr;
    require(!enabled((*navigator).add_new_command()) && (*navigator).binding_source.read() == nullptr,
            "removing the source disables every action");
    require((*navigator).semantic_descriptor().name == "Binding navigator" &&
                (*navigator).semantic_descriptor().role == SemanticRole::toolbar,
            "the navigator is a named toolbar");
}

} // namespace

int main() {
    standard_items_and_states();
    position_box();
    add_and_delete();
    std::cout << "BindingNavigator: items, command states, moves, position box, add, delete and source loss passed\n";
    return 0;
}
