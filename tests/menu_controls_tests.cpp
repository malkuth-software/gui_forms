#include "gui_forms/menu_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"
#include "support/named_callbacks.hpp"
#include "support/typography_painter.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const SemanticNode* find_semantic(const std::vector<SemanticNode>& nodes,
                                  std::string_view id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        if (const SemanticNode* found = find_semantic(node.children, id)) return found;
    }
    return nullptr;
}
const SemanticNode* find_semantic(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

bool has_action(const SemanticNode& node, const SemanticAction action) {
    const std::vector<SemanticAction>::const_iterator found =
        std::find(node.actions.begin(), node.actions.end(), action);
    const bool present = found != node.actions.end();
    return present;
}

// The subscription ends before the borrowed Window. Root owns the target;
// an expired destination causes no focus request.
class FocusDestination final {
public:
    FocusDestination(Window& window, const Control::Ptr& destination)
        : window_(window), destination_(destination) {}
    void operator()(const CommandInvocation&) const {
        const Control::Ptr target = destination_.lock();
        if (!target) return;
        const bool focused = window_.request_focus(target);
        static_cast<void>(focused);
    }
private:
    Window& window_;
    std::weak_ptr<Control> destination_{};
};

class RecordCommandIdAndSource final {
public:
    explicit RecordCommandIdAndSource(std::string& trace) : trace_(trace) {}

    void operator()(const CommandInvocation& invocation) const {
        trace_ = invocation.command_id + "@" + invocation.source_id;
    }

private:
    std::string& trace_;
};

class RecordCommandId final {
public:
    explicit RecordCommandId(std::string& trace) : trace_(trace) {}

    void operator()(const CommandInvocation& invocation) const {
        trace_ = invocation.command_id;
    }

private:
    std::string& trace_;
};

class RecordMenuStripInvocation final {
public:
    explicit RecordMenuStripInvocation(std::string& trace) : trace_(trace) {}

    void operator()(const MenuStripInvocation& invocation) const {
        trace_ = invocation.top_level_id + "/" + invocation.item.command_id;
    }

private:
    std::string& trace_;
};

void test_context_menu_command_snapshot_keyboard_nesting_and_restore() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> root = make_control<TableLayoutPanelCompat>(StableId("root"));
    (*root).column_count = 1U;
    (*root).row_count = 1U;
    (*root).set_column_style(0U, {TableSizeMode::percent, 1.0});
    (*root).set_row_style(0U, {TableSizeMode::percent, 1.0});
    std::shared_ptr<gui_forms::Button> owner = make_control<Button>(StableId("owner"), "Open context menu");
    (*owner).set_requested_bounds({20.0, 20.0, 160.0, 32.0});
    (*root).add_child(owner);
    (*root).cell[*owner] = GridCell{.row = 0U, .column = 0U};
    Window window(root, {400.0, 300.0});
    const bool operation_check_1 = window.request_focus(owner);
    require(operation_check_1, "menu invoker must accept initial focus");

    std::shared_ptr<gui_forms::Command> open = std::make_shared<Command>("file.open", "Open");
    (*open).set_default_action(true);
    std::shared_ptr<gui_forms::Command> copy = std::make_shared<Command>("edit.copy", "Copy");
    (*copy).set_shortcut("Ctrl+C");
    (*copy).set_description("Copy fixture reference");
    std::shared_ptr<gui_forms::Command> remove = std::make_shared<Command>("file.delete", "Delete");
    (*remove).set_enabled(false);
    (*remove).set_destructive(true);
    (*remove).set_availability_reason("Fixture is read-only");
    std::shared_ptr<gui_forms::Command> details = std::make_shared<Command>("view.details", "Details");
    (*details).set_checked(true);
    std::shared_ptr<gui_forms::Command> icons = std::make_shared<Command>("view.icons", "Icons");

    ContextMenu menu("object.context");
    menu.preferred_width = 180.0;
    require(menu.preferred_width.read() == 180.0,
            "ContextMenu must retain an explicit preferred overlay width");
    menu.set_items({
        {"open", MenuItemKind::command, open},
        {"separator.primary", MenuItemKind::separator},
        {"copy", MenuItemKind::command, copy},
        {"delete", MenuItemKind::command, remove},
        {"details", MenuItemKind::check, details},
        {"view", MenuItemKind::submenu, {}, "View", {
            {"view.icons", MenuItemKind::radio, icons},
            {"view.details", MenuItemKind::radio, details},
        }},
    });

    std::string command_trace{};
    SubscriptionToken copy_invoked = (*copy).invoked().subscribe(
        RecordCommandIdAndSource(command_trace));
    std::size_t open_changes{};
    SubscriptionToken opened = menu.open_changed().subscribe(
        test_support::IncrementCounter<std::size_t, bool>(open_changes));

    menu.show(owner, {100.0, 100.0}, MenuOpenMode::pointer);
    const Control::Ptr original_panel = window.focused_control();
    const std::size_t original_open_changes = open_changes;
    bool invalid_mode_refused = false;
    try {
        menu.show(owner, {140.0, 140.0}, static_cast<MenuOpenMode>(255));
    } catch (const std::invalid_argument&) {
        invalid_mode_refused = true;
    }
    require(invalid_mode_refused && menu.is_open() &&
            window.focused_control() == original_panel && open_changes == original_open_changes,
            "invalid context opening mode must preserve the existing overlay and focus");
    require(window.focused_control() == window.find("object.context.overlay.panel.0"),
            "pointer opening must not focus a command");
    const SemanticSnapshot pointer_snapshot = window.semantic_snapshot();
    const SemanticNode* pointer_panel = find_semantic(pointer_snapshot.roots, "object.context.overlay.panel.0");
    require(pointer_panel && has_semantic_state((*pointer_panel).states, SemanticState::focused),
            "pointer-opened menu focus must remain accessible");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::down}) &&
            window.focused_control() == window.find("object.context.overlay.row.open"),
            "Down after pointer opening must select the first enabled command");
    menu.close();
    require(window.focused_control() == owner, "pointer menu close must restore invoker");
    menu.show(owner, {100.0, 100.0}, MenuOpenMode::pointer);
    require(window.dispatch_key({KeyAction::down, PhysicalKey::up}) &&
            window.focused_control() == window.find("object.context.overlay.row.view"),
            "Up after pointer opening must select the last enabled command");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right}) &&
            window.focused_control() == window.find("object.context.overlay.row.view.icons"),
            "keyboard entry from pointer opening must retain submenu navigation");
    menu.close();
    ContextMenu disabled_menu("disabled.context");
    disabled_menu.set_items({{"disabled", MenuItemKind::command, remove}});
    disabled_menu.show(owner, {100.0, 100.0}, MenuOpenMode::pointer);
    require(window.dispatch_key({KeyAction::down, PhysicalKey::down}) &&
            window.focused_control() == window.find("disabled.context.overlay.panel.0"),
            "all-disabled menu must retain container focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
            !disabled_menu.is_open() && window.focused_control() == owner,
            "all-disabled menu must close and restore focus on Escape");
    open_changes = 0U;
    menu.show(owner, {395.0, 295.0});
    require(menu.is_open() && window.focus_scope_depth() == 1U,
            "opening a context menu must create one contained focus scope");
    const Control::Ptr panel = window.find("object.context.overlay.panel.0");
    require(panel && (*panel).absolute_bounds().width == 180.0 &&
                (*panel).absolute_bounds().x >= 0.0 &&
                (*panel).absolute_bounds().y >= 0.0 &&
                (*panel).absolute_bounds().x + (*panel).absolute_bounds().width <= 400.0 &&
                (*panel).absolute_bounds().y + (*panel).absolute_bounds().height <= 300.0,
            "root menu placement must avoid every client edge");
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* menu_node = find_semantic(
        snapshot.roots, "object.context.overlay.panel.0");
    const SemanticNode* details_node = find_semantic(
        snapshot.roots, "object.context.overlay.row.details");
    const SemanticNode* delete_node = find_semantic(
        snapshot.roots, "object.context.overlay.row.delete");
    const Control::Ptr delete_row = window.find(
        "object.context.overlay.row.delete");
    require(menu_node && (*menu_node).role == SemanticRole::menu && details_node &&
                (*details_node).role == SemanticRole::menu_item &&
                has_semantic_state((*details_node).states, SemanticState::checked) &&
                delete_node && (*delete_node).description.find("read-only") !=
                    std::string::npos,
            "menu semantics must publish menu/item/check and availability facts");
    const bool operation_check_2 = static_cast<bool>(delete_row);
    require(operation_check_2, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_3 = !(*delete_row).focusable();
    require(operation_check_3, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_4 = !(*delete_row).cursor.read();
    require(operation_check_4, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_5 = !has_semantic_state((*delete_node).states,
                                    SemanticState::enabled);
    require(operation_check_5, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_6 = !has_action(*delete_node, SemanticAction::focus);
    require(operation_check_6, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_7 = !has_action(*delete_node, SemanticAction::press);
    require(operation_check_7, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_8 = !window.request_focus(delete_row);
    require(operation_check_8, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");
    const bool operation_check_9 = !window.perform_semantic_action(
                    "object.context.overlay.row.delete", SemanticAction::press);
    require(operation_check_9, "disabled menu rows must explain unavailability without a hand cursor or actionable semantics");

    const bool operation_check_10 = window.dispatch_key({KeyAction::down, PhysicalKey::down});
    require(operation_check_10, "Down must skip a separator and reach the next enabled command");
    const bool operation_check_11 = (*window.focused_control()).stable_id().value() ==
                    "object.context.overlay.row.copy";
    require(operation_check_11, "Down must skip a separator and reach the next enabled command");
    const bool operation_check_12 = window.dispatch_key({KeyAction::down, PhysicalKey::down});
    require(operation_check_12, "keyboard traversal must skip disabled command rows");
    const bool operation_check_13 = (*window.focused_control()).stable_id().value() ==
                    "object.context.overlay.row.details";
    require(operation_check_13, "keyboard traversal must skip disabled command rows");
    const bool operation_check_14 = window.dispatch_key({KeyAction::down, PhysicalKey::up});
    require(operation_check_14, "Enter must execute shared command once, close, and restore invoker focus");
    const bool operation_check_15 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_15, "Enter must execute shared command once, close, and restore invoker focus");
    const bool operation_check_16 = !menu.is_open();
    require(operation_check_16, "Enter must execute shared command once, close, and restore invoker focus");
    const bool operation_check_17 = window.focus_scope_depth() == 0U;
    require(operation_check_17, "Enter must execute shared command once, close, and restore invoker focus");
    const bool operation_check_18 = window.focused_control() == owner;
    require(operation_check_18, "Enter must execute shared command once, close, and restore invoker focus");
    const bool operation_check_19 = command_trace.starts_with("edit.copy@object.context.overlay.row.copy");
    require(operation_check_19, "Enter must execute shared command once, close, and restore invoker focus");

    std::string nested_trace{};
    SubscriptionToken icons_invoked = (*icons).invoked().subscribe(
        RecordCommandId(nested_trace));
    menu.show(owner, {120.0, 80.0});
    const bool operation_check_20 = window.dispatch_key({KeyAction::down, PhysicalKey::end});
    require(operation_check_20, "Right must open a bounded nested submenu and focus its first command");
    const bool operation_check_21 = (*window.focused_control()).stable_id().value() ==
                    "object.context.overlay.row.view";
    require(operation_check_21, "Right must open a bounded nested submenu and focus its first command");
    const bool operation_check_22 = window.dispatch_key({KeyAction::down, PhysicalKey::right});
    require(operation_check_22, "Right must open a bounded nested submenu and focus its first command");
    const bool operation_check_23 = window.find("object.context.overlay.panel.1") != nullptr;
    require(operation_check_23, "Right must open a bounded nested submenu and focus its first command");
    const bool operation_check_24 = (*window.focused_control()).stable_id().value() ==
                    "object.context.overlay.row.view.icons";
    require(operation_check_24, "Right must open a bounded nested submenu and focus its first command");
    const bool operation_check_25 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_25, "nested keyboard activation must converge on shared command authority");
    const bool operation_check_26 = nested_trace == "view.icons";
    require(operation_check_26, "nested keyboard activation must converge on shared command authority");
    const bool operation_check_27 = !menu.is_open();
    require(operation_check_27, "nested keyboard activation must converge on shared command authority");

    (*icons).set_enabled(false);
    (*details).set_enabled(false);
    menu.show(owner, {120.0, 80.0});
    const SemanticSnapshot disabled_submenu_snapshot =
        window.semantic_snapshot();
    const SemanticNode* disabled_submenu = find_semantic(
        disabled_submenu_snapshot.roots,
        "object.context.overlay.row.view");
    const Control::Ptr disabled_submenu_row = window.find(
        "object.context.overlay.row.view");
    const bool operation_check_28 = disabled_submenu;
    require(operation_check_28, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_29 = static_cast<bool>(disabled_submenu_row);
    require(operation_check_29, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_30 = !(*disabled_submenu_row).focusable();
    require(operation_check_30, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_31 = !(*disabled_submenu_row).cursor.read();
    require(operation_check_31, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_32 = (*disabled_submenu).description.find(
                    "No commands are currently available") !=
                    std::string::npos;
    require(operation_check_32, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_33 = (*disabled_submenu).actions.empty();
    require(operation_check_33, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_34 = !window.perform_semantic_action(
                    "object.context.overlay.row.view",
                    SemanticAction::expand);
    require(operation_check_34, "submenu with no actionable descendant must explain and enforce its disabled state");
    const bool operation_check_35 = window.find("object.context.overlay.panel.1") == nullptr;
    require(operation_check_35, "submenu with no actionable descendant must explain and enforce its disabled state");
    menu.close();
    (*icons).set_enabled(true);
    (*details).set_enabled(true);

    menu.show(owner, {120.0, 80.0});
    const bool operation_check_36 = window.dispatch_pointer({PointerAction::down,
                                     PointerButton::primary,
                                     {2.0, 2.0}});
    require(operation_check_36, "click-away must revoke the overlay and restore focus");
    const bool operation_check_37 = !menu.is_open();
    require(operation_check_37, "click-away must revoke the overlay and restore focus");
    const bool operation_check_38 = window.focused_control() == owner;
    require(operation_check_38, "click-away must revoke the overlay and restore focus");
    require(open_changes == 8U,
            "four complete open/close cycles must publish eight ordered state changes");

    std::shared_ptr<gui_forms::Button> destination = make_control<Button>(
        StableId("command.focus-destination"), "Destination");
    (*destination).set_requested_bounds({210.0, 20.0, 120.0, 32.0});
    (*root).add_child(destination);
    std::shared_ptr<gui_forms::Command> focus_command =
        std::make_shared<Command>("command.focus", "Focus destination");
    SubscriptionToken focus_invoked = (*focus_command).invoked().subscribe(
        FocusDestination(window, destination));
    menu.set_items({{"focus", MenuItemKind::command, focus_command}});
    menu.show(owner, {120.0, 80.0});
    const bool operation_check_39 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_39, "menu teardown must not overwrite the focus destination deliberately chosen by an invoked command");
    const bool operation_check_40 = !menu.is_open();
    require(operation_check_40, "menu teardown must not overwrite the focus destination deliberately chosen by an invoked command");
    const bool operation_check_41 = window.focused_control() == destination;
    require(operation_check_41, "menu teardown must not overwrite the focus destination deliberately chosen by an invoked command");
}

void test_context_menu_scroll_and_validation_bounds() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("scroll.root"));
    std::shared_ptr<gui_forms::Button> owner = make_control<Button>(StableId("scroll.owner"), "Menu");
    (*owner).set_requested_bounds({4.0, 4.0, 80.0, 30.0});
    (*root).add_child(owner);
    Window window(root, {320.0, 190.0});
    const bool operation_check_42 = window.request_focus(owner);
    require(operation_check_42, "scroll menu owner must focus");

    std::vector<MenuItemSpec> specs{};
    std::vector<std::shared_ptr<Command>> commands{};
    for (std::size_t index = 0; index < 30U; ++index) {
        std::shared_ptr<gui_forms::Command> command = std::make_shared<Command>(
            "long." + std::to_string(index), "Command " + std::to_string(index));
        commands.push_back(command);
        specs.push_back({"item." + std::to_string(index),
                         MenuItemKind::command, command});
    }
    ContextMenu menu("long.menu");
    menu.set_items(std::move(specs));
    menu.show(owner, {20.0, 20.0});
    const Control::Ptr panel = window.find("long.menu.overlay.panel.0");
    require(panel && (*panel).absolute_bounds().height <= 182.0,
            "long menu must use a bounded on-screen scroll viewport");
    const bool operation_check_43 = window.dispatch_key({KeyAction::down, PhysicalKey::end});
    require(operation_check_43, "End must scroll the last logical command into the visible menu viewport");
    const bool operation_check_44 = (*window.focused_control()).stable_id().value() ==
                    "long.menu.overlay.row.item.29";
    require(operation_check_44, "End must scroll the last logical command into the visible menu viewport");
    const Rect focused_bounds = (*window.focused_control()).absolute_bounds();
    require((*panel).absolute_bounds().contains(Point{
                focused_bounds.x + focused_bounds.width * .5,
                focused_bounds.y + focused_bounds.height * .5}),
            "End must reveal the last logical command inside the menu viewport");
    const bool operation_check_45 = window.dispatch_key({KeyAction::down, PhysicalKey::escape});
    require(operation_check_45, "Escape must close a scrolled menu without selecting a command");
    const bool operation_check_46 = !menu.is_open();
    require(operation_check_46, "Escape must close a scrolled menu without selecting a command");

    bool duplicate_rejected{};
    try {
        menu.set_items({{"same", MenuItemKind::command, commands.front()},
                        {"same", MenuItemKind::command, commands.back()}});
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "menu model validation must reject duplicate stable identities");
}

// Item IDs that become part of a StableId are checked when they are set,
// not later, when an overlay row or hosted control is made from them.
void test_item_ids_follow_the_stable_id_segment_rule() {
    bool identity_refused{};
    try {
        const ContextMenu refused_menu("menu_id");
    } catch (const std::invalid_argument&) {
        identity_refused = true;
    }
    require(identity_refused, "a ContextMenu identity follows the segment rule");

    ContextMenu menu("rule.menu");
    const std::shared_ptr<Command> copy = std::make_shared<Command>("rule.copy", "Copy");
    bool menu_item_refused{};
    try {
        menu.set_items({{"copy_path", MenuItemKind::command, copy}});
    } catch (const std::invalid_argument&) {
        menu_item_refused = true;
    }
    require(menu_item_refused, "a menu item ID follows the segment rule");
    menu.set_items({{"copy-path", MenuItemKind::command, copy}});

    const std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("rule.strip"));
    bool strip_item_refused{};
    try {
        (*strip).set_items({{.stable_id = "move_first", .kind = ToolStripItemKind::label, .text = "First"}});
    } catch (const std::invalid_argument&) {
        strip_item_refused = true;
    }
    require(strip_item_refused, "a ToolStrip item ID follows the segment rule, since overflow rows carry it");
    (*strip).set_items({{.stable_id = "move-first", .kind = ToolStripItemKind::label, .text = "First"}});
}

void test_menu_strip_retained_switching_commands_and_semantics() {
    std::shared_ptr<gui_forms::TableLayoutPanelCompat> root = make_control<TableLayoutPanelCompat>(StableId("menustrip.root"));
    (*root).column_count = 1U;
    (*root).row_count = 2U;
    (*root).set_column_style(0U, {TableSizeMode::percent, 1.0});
    (*root).set_row_style(0U, {TableSizeMode::absolute, 24.0});
    (*root).set_row_style(1U, {TableSizeMode::percent, 1.0});
    std::shared_ptr<gui_forms::MenuStrip> strip = make_control<MenuStrip>(StableId("menustrip"));
    (*strip).item_padding = 18.0;
    require((*strip).item_padding.read() == 18.0,
            "MenuStrip must retain explicit top-level item padding");
    std::shared_ptr<gui_forms::Panel> content = make_control<Panel>(StableId("menustrip.content"));
    (*root).add_child(strip);
    (*root).add_child(content);
    (*root).cell[*strip] = GridCell{.row = 0U, .column = 0U};
    (*root).cell[*content] = GridCell{.row = 1U, .column = 0U};

    std::shared_ptr<gui_forms::Command> open = std::make_shared<Command>("file.open", "Open");
    std::shared_ptr<gui_forms::Command> details = std::make_shared<Command>("view.details", "Details");
    std::shared_ptr<gui_forms::Command> icons = std::make_shared<Command>("view.icons", "Icons");
    (*strip).set_items({
        {"menustrip.file", "File", {
            {"file.open", MenuItemKind::command, open},
        }},
        {"menustrip.view", "View", {
            {"view.icons", MenuItemKind::radio, icons},
            {"view.details", MenuItemKind::radio, details},
        }},
    });
    (*strip).set_selected_item_id("menustrip.view");
    require((*strip).selected_item_id() == "menustrip.view",
            "MenuStrip must retain one selected command category independently of overlay state");
    Window window(root, {420.0, 240.0});

    require((*strip).open(0U, MenuOpenMode::pointer) &&
            window.focused_control() == window.find("menustrip.menu.overlay.panel.0"),
            "pointer menu strip opening must not select a command");
    const Control::Ptr original_strip_panel = window.focused_control();
    for (const std::size_t invalid_index : {std::size_t{0}, std::size_t{1}}) {
        bool invalid_mode_refused = false;
        try {
            static_cast<void>((*strip).open(invalid_index, static_cast<MenuOpenMode>(255)));
        } catch (const std::invalid_argument&) {
            invalid_mode_refused = true;
        }
        require(invalid_mode_refused && window.focused_control() == original_strip_panel,
                "invalid mode must not reuse or replace an existing menu-strip overlay");
    }
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right}) &&
            window.focused_control() == window.find("menustrip.menu.overlay.row.view.icons"),
            "keyboard switching after pointer opening must select the new menu first row");
    (*strip).close();
    require((*strip).open(0U), "fixture must restore initial menu position");
    (*strip).close();

    const std::vector<SemanticNode> selected_closed =
        (*strip).semantic_virtual_children();
    require(!has_semantic_state(selected_closed[0].states,
                                SemanticState::selected) &&
                !has_semantic_state(selected_closed[0].states,
                                    SemanticState::expanded) &&
                has_semantic_state(selected_closed[1].states,
                                   SemanticState::selected) &&
                !has_semantic_state(selected_closed[1].states,
                                    SemanticState::expanded),
            "closed MenuStrip must publish selected category without falsely expanding it");

    const bool operation_check_47 = window.request_focus(strip);
    require(operation_check_47, "menu strip must accept keyboard focus");
    const bool operation_check_48 = window.dispatch_key({KeyAction::down, PhysicalKey::down});
    require(operation_check_48, "Down on the menu bar must open its focused retained menu");
    const bool operation_check_49 = (*strip).is_open();
    require(operation_check_49, "Down on the menu bar must open its focused retained menu");
    const bool operation_check_50 = (*strip).active_index() == 0U;
    require(operation_check_50, "Down on the menu bar must open its focused retained menu");
    const bool operation_check_51 = window.focus_scope_depth() == 1U;
    require(operation_check_51, "Down on the menu bar must open its focused retained menu");
    const std::vector<SemanticNode> file_open =
        (*strip).semantic_virtual_children();
    require(has_semantic_state(file_open[0].states,
                               SemanticState::expanded) &&
                !has_semantic_state(file_open[0].states,
                                    SemanticState::selected) &&
                has_semantic_state(file_open[1].states,
                                   SemanticState::selected) &&
                !has_semantic_state(file_open[1].states,
                                    SemanticState::expanded),
            "transient File expansion must not steal or falsely expand the selected View category");
    const bool operation_check_52 = window.dispatch_key({KeyAction::down, PhysicalKey::right});
    require(operation_check_52, "Right from a root command must switch top-level menus in one overlay scope");
    const bool operation_check_53 = (*strip).is_open();
    require(operation_check_53, "Right from a root command must switch top-level menus in one overlay scope");
    const bool operation_check_54 = (*strip).active_index() == 1U;
    require(operation_check_54, "Right from a root command must switch top-level menus in one overlay scope");
    const bool operation_check_55 = (*window.focused_control()).stable_id().value() ==
                    "menustrip.menu.overlay.row.view.icons";
    require(operation_check_55, "Right from a root command must switch top-level menus in one overlay scope");

    std::string trace{};
    SubscriptionToken invoked = (*strip).item_invoked().subscribe(
        RecordMenuStripInvocation(trace));
    const bool operation_check_56 = window.dispatch_key({KeyAction::down, PhysicalKey::down});
    require(operation_check_56, "menu-bar keyboard invocation must execute and restore bar focus");
    const bool operation_check_57 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_57, "menu-bar keyboard invocation must execute and restore bar focus");
    const bool operation_check_58 = trace == "menustrip.view/view.details";
    require(operation_check_58, "menu-bar keyboard invocation must execute and restore bar focus");
    const bool operation_check_59 = !(*strip).is_open();
    require(operation_check_59, "menu-bar keyboard invocation must execute and restore bar focus");
    const bool operation_check_60 = window.focused_control() == strip;
    require(operation_check_60, "menu-bar keyboard invocation must execute and restore bar focus");

    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* bar = find_semantic(snapshot.roots, "menustrip");
    const SemanticNode* file = find_semantic(snapshot.roots, "menustrip.file");
    require(bar && (*bar).role == SemanticRole::menu_bar && file &&
                (*file).role == SemanticRole::menu_bar_item,
            "menu strip must publish distinct bar and top-level item roles");
    const bool operation_check_61 = window.perform_semantic_action("menustrip.file",
                                           SemanticAction::expand);
    require(operation_check_61, "semantic expansion must open the same retained overlay path");
    const bool operation_check_62 = (*strip).is_open();
    require(operation_check_62, "semantic expansion must open the same retained overlay path");
    const bool operation_check_63 = (*strip).active_index() == 0U;
    require(operation_check_63, "semantic expansion must open the same retained overlay path");
    (*strip).close();

    const bool operation_check_64 = window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     {70.0, 12.0}});
    require(operation_check_64, "pointer activation must open the top-level item under its geometry");
    const bool operation_check_65 = (*strip).active_index() == 1U;
    require(operation_check_65, "pointer activation must open the top-level item under its geometry");
    require(window.focused_control() == window.find("menustrip.menu.overlay.panel.0"),
            "pointer dispatch must leave menu rows unselected");
    const bool operation_check_66 = window.dispatch_pointer({PointerAction::move, PointerButton::none,
                                     {12.0, 12.0}});
    require(operation_check_66, "moving across the open menu bar must switch menus without click-through");
    const bool operation_check_67 = (*strip).active_index() == 0U;
    require(operation_check_67, "moving across the open menu bar must switch menus without click-through");
    require(window.focused_control() == window.find("menustrip.menu.overlay.panel.0"),
            "pointer switching must leave the new menu rows unselected");
    (*strip).close();

    (*strip).set_items({
        {"menustrip.file", "File", {
            {"file.open", MenuItemKind::command, open},
        }},
        {"menustrip.view", "View", {
            {"view.icons", MenuItemKind::radio, icons},
        }},
    });
    require((*strip).selected_item_id() == "menustrip.view",
            "MenuStrip model replacement must retain a still-visible selected stable identity");
    (*strip).set_items({
        {"menustrip.file", "File", {
            {"file.open", MenuItemKind::command, open},
        }},
    });
    require((*strip).selected_item_id().empty(),
            "MenuStrip model replacement must clear a selected identity that no longer exists");
    bool hidden_selection_rejected{};
    try {
        (*strip).set_items({
            {"menustrip.file", "File", {
                {"file.open", MenuItemKind::command, open},
            }, true, false},
        });
        (*strip).set_selected_item_id("menustrip.file");
    } catch (const std::out_of_range&) {
        hidden_selection_rejected = true;
    }
    require(hidden_selection_rejected,
            "MenuStrip must reject a hidden selected top-level identity");
}

void test_menu_mnemonics_strip_markers_and_overlay_activation() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("mnemonic.root"));
    std::shared_ptr<gui_forms::MenuStrip> strip = make_control<MenuStrip>(StableId("mnemonic.strip"));
    (*strip).set_requested_bounds({0.0, 0.0, 360.0, 24.0});
    std::shared_ptr<gui_forms::Button> owner = make_control<Button>(StableId("mnemonic.owner"), "Owner");
    (*owner).set_requested_bounds({20.0, 50.0, 90.0, 28.0});
    (*root).add_child(strip);
    (*root).add_child(owner);

    std::shared_ptr<gui_forms::Command> first = std::make_shared<Command>("first", "First");
    std::shared_ptr<gui_forms::Command> second = std::make_shared<Command>("second", "Second");
    (*strip).set_items({
        {"mnemonic.file", "&File", {
            {"first", MenuItemKind::command, first},
        }},
        {"mnemonic.format", "&Format", {
            {"second", MenuItemKind::command, second},
        }},
    });
    Window window(root, {360.0, 220.0});
    window.perform_layout();

    const std::vector<SemanticNode> top_level = (*strip).semantic_virtual_children();
    require(top_level.size() == 2U && top_level[0].name == "File" &&
                top_level[1].name == "Format",
            "MenuStrip mnemonic markers must stay out of retained semantics");

    KeyEvent alt_f{KeyAction::down, PhysicalKey::f};
    alt_f.modifiers = Modifier::alt;
    const bool operation_check_68 = window.dispatch_key(alt_f);
    require(operation_check_68, "a top-level menu mnemonic must focus and open its retained overlay");
    const bool operation_check_69 = (*strip).is_open();
    require(operation_check_69, "a top-level menu mnemonic must focus and open its retained overlay");
    const bool operation_check_70 = (*strip).active_index() == 0U;
    require(operation_check_70, "a top-level menu mnemonic must focus and open its retained overlay");
    (*strip).close();
    const bool operation_check_71 = window.dispatch_key(alt_f);
    require(operation_check_71, "duplicate top-level menu mnemonics must cycle deterministically");
    const bool operation_check_72 = (*strip).is_open();
    require(operation_check_72, "duplicate top-level menu mnemonics must cycle deterministically");
    const bool operation_check_73 = (*strip).active_index() == 1U;
    require(operation_check_73, "duplicate top-level menu mnemonics must cycle deterministically");
    (*strip).close();

    std::shared_ptr<gui_forms::Command> copy = std::make_shared<Command>("copy", "Copy");
    std::uint64_t copy_count{};
    SubscriptionToken copy_invoked = (*copy).invoked().subscribe(
        test_support::IncrementCounter<std::uint64_t,
                                       const CommandInvocation&>(copy_count));
    ContextMenu overlay("mnemonic.overlay");
    overlay.set_items({
        {"copy", MenuItemKind::command, copy, "&Copy"},
    });
    overlay.show(owner, {20.0, 84.0});
    const Control::Ptr row = window.find("mnemonic.overlay.overlay.row.copy");
    require(row && (*row).semantic_descriptor().name == "Copy",
            "overlay menu rows must expose marker-free authored text");
    KeyEvent alt_c{KeyAction::down, PhysicalKey::c};
    alt_c.modifiers = Modifier::alt;
    const bool operation_check_74 = window.dispatch_key(alt_c);
    require(operation_check_74, "an active overlay mnemonic must execute through shared command authority");
    const bool operation_check_75 = copy_count == 1U;
    require(operation_check_75, "an active overlay mnemonic must execute through shared command authority");
    const bool operation_check_76 = !overlay.is_open();
    require(operation_check_76, "an active overlay mnemonic must execute through shared command authority");

    (*strip).use_mnemonic = false;
    const bool operation_check_77 = !window.dispatch_key(alt_f);
    require(operation_check_77, "UseMnemonic false must leave the ampersand literal and revoke activation");
}

// A plain letter in an open menu is that item's mnemonic, without Alt, as
// in a native Windows menu: one match activates, several take the focus in
// turn, and no match leaves the menu open.
void test_menu_plain_letter_mnemonics() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("letter.root"));
    std::shared_ptr<gui_forms::Button> owner = make_control<Button>(StableId("letter.owner"), "Owner");
    (*owner).set_requested_bounds({20.0, 20.0, 90.0, 28.0});
    (*root).add_child(owner);
    Window window(root, {360.0, 320.0});
    window.perform_layout();

    std::shared_ptr<gui_forms::Command> cut = std::make_shared<Command>("cut", "Cut");
    std::shared_ptr<gui_forms::Command> copy = std::make_shared<Command>("copy", "Copy");
    std::shared_ptr<gui_forms::Command> paste = std::make_shared<Command>("paste", "Paste");
    std::shared_ptr<gui_forms::Command> erase = std::make_shared<Command>("delete", "Delete");
    std::shared_ptr<gui_forms::Command> zoom = std::make_shared<Command>("zoom", "Zoom");
    (*erase).set_enabled(false);
    std::string trace;
    SubscriptionToken cut_invoked = (*cut).invoked().subscribe(RecordCommandId(trace));
    SubscriptionToken copy_invoked = (*copy).invoked().subscribe(RecordCommandId(trace));
    SubscriptionToken paste_invoked = (*paste).invoked().subscribe(RecordCommandId(trace));
    SubscriptionToken zoom_invoked = (*zoom).invoked().subscribe(RecordCommandId(trace));
    ContextMenu menu("letter.menu");
    menu.set_items({
        {"cut", MenuItemKind::command, cut, "Cu&t"},
        {"copy", MenuItemKind::command, copy, "&Copy"},
        {"close", MenuItemKind::command, cut, "&Close"},
        {"paste", MenuItemKind::command, paste, "&Paste"},
        {"delete", MenuItemKind::command, erase, "&Delete"},
        {"more", MenuItemKind::submenu, {}, "&More", {
            {"zoom", MenuItemKind::command, zoom, "&Zoom"},
        }},
    });

    TextInputEvent typed_p{"p"};
    menu.show(owner, {20.0, 50.0});
    require(window.dispatch_text(typed_p) && trace == "paste" && !menu.is_open(),
            "one matching plain letter must activate its item and close the menu");

    trace.clear();
    menu.show(owner, {20.0, 50.0});
    TextInputEvent typed_c{"c"};
    require(window.dispatch_text(typed_c), "a shared plain letter must be handled");
    Control::Ptr focused = window.focused_control();
    require(focused && (*focused).semantic_descriptor().name == "Copy" &&
                menu.is_open() && trace.empty(),
            "a shared plain letter must move the focus to the next match without activating");
    TextInputEvent typed_capital_c{"C"};
    require(window.dispatch_text(typed_capital_c), "a capital letter must match as well");
    focused = window.focused_control();
    require(focused && (*focused).semantic_descriptor().name == "Close" && trace.empty(),
            "a shared plain letter must move to the following match, ignoring case");
    require(window.dispatch_text(typed_c), "a shared plain letter must wrap round");
    focused = window.focused_control();
    require(focused && (*focused).semantic_descriptor().name == "Copy" && trace.empty(),
            "a shared plain letter must wrap round to the first match");

    TextInputEvent typed_x{"x"};
    TextInputEvent typed_d{"d"};
    TextInputEvent typed_two{"pc"};
    TextInputEvent composing{"p"};
    composing.composing = true;
    require(!window.dispatch_text(typed_x) && !window.dispatch_text(typed_d) &&
                !window.dispatch_text(typed_two) && !window.dispatch_text(composing) &&
                menu.is_open() && trace.empty(),
            "an unmatched, disabled, multi-character or composing input must leave the menu open");

    TextInputEvent typed_m{"m"};
    require(window.dispatch_text(typed_m), "a submenu's plain letter must be handled");
    focused = window.focused_control();
    require(focused && (*focused).semantic_descriptor().name == "Zoom" && menu.is_open(),
            "a submenu's plain letter must open it and focus its first item");
    TextInputEvent typed_z{"z"};
    require(window.dispatch_text(typed_z) && trace == "zoom" && !menu.is_open(),
            "a plain letter in a submenu must activate the submenu's item");

    trace.clear();
    menu.show(owner, {20.0, 50.0}, MenuOpenMode::pointer);
    require(window.dispatch_text(typed_p) && trace == "paste" && !menu.is_open(),
            "a plain letter must act while the menu panel itself has the focus");
}

class MenuFontMetrics final : public TextMetricsProvider {
public:
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view text, FontSpec font) override {
        ResolvedTextLayout result = estimate_text_layout_utf8(text, font);
        result.logical_size = {
            static_cast<double>(text.size()) * (font.size + font.letter_spacing),
            font.size * 1.5};
        return result;
    }
};

void test_menu_font_controls_geometry_paint_and_hit_testing() {
    MenuFontMetrics metrics{};
    const std::shared_ptr<MenuStrip> strip = make_control<MenuStrip>(StableId("font.menu"));
    const std::shared_ptr<Command> command = std::make_shared<Command>("font.open", "Open");
    (*strip).set_items({
        {"font.first", "First", {{"first.open", MenuItemKind::command, command}}},
        {"font.second", "Second", {{"second.open", MenuItemKind::command, command}}},
    });
    require((*strip).font.read() == FontSpec{FontRole::control, 10.5, 400, false, 0.12},
            "menu font must retain its existing default");
    Window window(strip, {600.0, 80.0});
    window.set_text_metrics_provider(&metrics);
    window.perform_layout();
    const double old_width = (*strip).semantic_virtual_children().front().bounds.width;
    const FontSpec chosen{FontRole::content, 20.0, 800, true, 1.0};
    (*strip).font = chosen;
    (*strip).set_selected_item_id("font.first");
    window.perform_layout();
    const std::vector<SemanticNode> nodes = (*strip).semantic_virtual_children();
    require((*strip).font.read() == chosen && nodes.front().bounds.width == 127.0 &&
                nodes.front().bounds.width > old_width &&
                (*strip).measure({600.0, 100.0}).height == 40.0,
            "menu measurement and semantic bounds must use the chosen font metrics");
    test_support::TypographyPainter painter{};
    (*strip).on_paint(painter, {0.0, 0.0, 600.0, 80.0});
    const test_support::PaintedText* first = painter.find("First");
    const test_support::PaintedText* second = painter.find("Second");
    require(first && second && (*first).font == chosen && (*second).font == chosen,
            "selected and normal menu labels must preserve the chosen heavier weight and typography");
    const bool operation_check_78 = window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     {old_width + 2.0, 12.0}});
    require(operation_check_78, "font-expanded menu area must hit the same first item painted there");
    const bool operation_check_79 = (*strip).active_index() == 0U;
    require(operation_check_79, "font-expanded menu area must hit the same first item painted there");
    (*strip).close();
    window.set_text_scale(1.5);
    window.perform_layout();
    require((*strip).semantic_virtual_children().front().bounds.width > nodes.front().bounds.width,
            "menu geometry must also follow the effective text scale");
    const double invalid[] = {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};
    for (const double size : invalid) {
        FontSpec malformed = chosen;
        malformed.size = size;
        bool rejected{};
        try { (*strip).font = malformed; }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && (*strip).font.read() == chosen,
                "invalid menu font sizes must preserve the previous typography");
    }
    FontSpec malformed = chosen;
    malformed.letter_spacing = std::numeric_limits<double>::quiet_NaN();
    bool rejected{};
    try { (*strip).font = malformed; }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && (*strip).font.read() == chosen,
            "invalid menu tracking must be rejected without mutation");
}

} // namespace

int main() {
    try {
        test_context_menu_command_snapshot_keyboard_nesting_and_restore();
        test_context_menu_scroll_and_validation_bounds();
        test_item_ids_follow_the_stable_id_segment_rule();
        test_menu_strip_retained_switching_commands_and_semantics();
        test_menu_mnemonics_strip_markers_and_overlay_activation();
        test_menu_plain_letter_mnemonics();
        test_menu_font_controls_geometry_paint_and_hit_testing();
        std::cout << "gui_forms_menu_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_menu_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
