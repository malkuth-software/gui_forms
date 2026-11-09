// QuickAccessToolbar (ribbon phase 4): a ToolStrip of flat command buttons
// with a customize button at its end; commands only; the customize menu's
// check rows and placement row, by pointer and keyboard; overflow with the
// customize button kept visible; placement as a property and an event;
// inert unknown IDs; semantics.

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

struct RecordCustomization final {
    std::vector<QuickAccessToolbarCustomization>* seen{};
    void operator()(const QuickAccessToolbarCustomization& change) const { (*seen).push_back(change); }
};

struct RecordPlacement final {
    std::vector<QuickAccessToolbarPlacement>* seen{};
    void operator()(const QuickAccessToolbarPlacementChange& change) const { (*seen).push_back(change.placement); }
};

struct CountInvocation final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

struct Commands final {
    std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    std::shared_ptr<Command> undo = std::make_shared<Command>("undo", "Undo");
    std::shared_ptr<Command> redo = std::make_shared<Command>("redo", "Redo");
    std::shared_ptr<Command> print = std::make_shared<Command>("print", "Quick Print");
};

std::vector<QuickAccessToolbarCommand> declared(const Commands& commands) {
    std::vector<QuickAccessToolbarCommand> result;
    result.push_back({.command = commands.save});
    result.push_back({.command = commands.undo});
    result.push_back({.command = commands.redo});
    result.push_back({.command = commands.print, .shown = false});
    return result;
}

bool refuses_items(QuickAccessToolbar& toolbar, std::vector<ToolStripItemSpec> items) {
    try {
        toolbar.set_items(std::move(items));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void parts_and_commands_only() {
    const Commands commands;
    std::shared_ptr<QuickAccessToolbar> toolbar = make_control<QuickAccessToolbar>(StableId("qat"));
    (*toolbar).set_commands(declared(commands));
    Window window(toolbar, {320.0, 25.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*toolbar).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "quick_access_toolbar" && anatomy.item_systems.size() == 1U,
            "the toolbar publishes a valid anatomy with ToolStrip's item system");
    const QuickAccessToolbar::Parts parts = (*toolbar).parts();
    require(parts.customize_button.present && parts.customize_glyph.present && !parts.grip.present &&
                !parts.overflow_button.present,
            "a wide toolbar shows its customize button, no grip and no overflow button");
    require(parts.customize_button.bounds.x + parts.customize_button.bounds.width > 310.0,
            "the customize button sits at the toolbar's far end");
    const std::vector<ToolStrip::Item> items = (*toolbar).realized_items();
    require(items.size() == 3U && items[0].stable_id == "save" && items[2].stable_id == "redo",
            "the shown commands are the items, in declared order");
    require(items[2].parts.item_face.bounds.x + items[2].parts.item_face.bounds.width <=
                parts.customize_button.bounds.x + 0.001,
            "items sit before the customize button");
    require(!items[0].states.hot && items[0].parts.item_label.present,
            "a command without an image shows its text");

    require(refuses_items(*toolbar, {{.stable_id = "label", .kind = ToolStripItemKind::label, .text = "Label"}}),
            "a label is not a command button");
    require(refuses_items(*toolbar, {{.stable_id = "plain", .text = "No command"}}),
            "a button without a command is refused");
    require((*toolbar).realized_items().size() == 3U, "a refused set_items changes nothing");

    std::vector<QuickAccessToolbarCommand> repeated = declared(commands);
    repeated.push_back({.command = commands.save});
    bool threw = false;
    try {
        (*toolbar).set_commands(repeated);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a repeated command ID is refused");
    std::vector<QuickAccessToolbarCommand> many;
    for (int index = 0; index < 21; ++index) {
        many.push_back({.command = std::make_shared<Command>("c" + std::to_string(index), "C")});
    }
    threw = false;
    try {
        (*toolbar).set_commands(many);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*toolbar).commands().size() == 4U, "more than 20 commands are refused");

    int saves = 0;
    SubscriptionToken counted = (*commands.save).invoked().subscribe(CountInvocation{&saves});
    const Point save_point = centre((*toolbar).realized_items()[0].parts.item_face.bounds);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, save_point}),
            "a toolbar button takes the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, save_point}));
    require(saves == 1, "a toolbar button executes its command");
}

void customize_menu() {
    const Commands commands;
    std::shared_ptr<QuickAccessToolbar> toolbar = make_control<QuickAccessToolbar>(StableId("qat"));
    (*toolbar).set_commands(declared(commands));
    Window window(toolbar, {320.0, 25.0});
    window.perform_layout();
    std::vector<QuickAccessToolbarCustomization> changes;
    std::vector<QuickAccessToolbarPlacement> placements;
    SubscriptionToken changed = (*toolbar).customized().subscribe(RecordCustomization{&changes});
    SubscriptionToken placed = (*toolbar).placement_changed().subscribe(RecordPlacement{&placements});

    const std::vector<MenuItemSpec>& rows = (*toolbar).customize_menu_items();
    require(rows.size() == 6U && rows[0].kind == MenuItemKind::check && rows[3].kind == MenuItemKind::check &&
                rows[4].kind == MenuItemKind::separator && rows[5].kind == MenuItemKind::command,
            "the menu lists each declared command with a check, a separator and the placement row");
    require((*rows[0].command).state().checked && !(*rows[3].command).state().checked &&
                (*rows[0].command).state().text == "Save" && (*rows[5].command).state().text == "Show Below the Ribbon",
            "the checks show which commands the toolbar shows");

    // Pointer: the customize button opens the menu.
    const Point customize = centre((*toolbar).parts().customize_button.bounds);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, customize}),
            "the customize button takes the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, customize}));
    require((*toolbar).customize_menu_open() && (*toolbar).parts().customize_button.states.contains(AnatomyState::open),
            "pressing the customize button opens the menu and publishes open");
    (*toolbar).close_customize_menu();
    require(!(*toolbar).customize_menu_open(), "the menu closes");

    // Keyboard: Down on the focused toolbar opens the menu with its first
    // row selected; Enter unchecks Save.
    require(window.request_focus(toolbar), "the toolbar takes focus");
    KeyEvent down{KeyAction::down, PhysicalKey::down};
    require(window.dispatch_key(down) && (*toolbar).customize_menu_open(), "Down opens the customize menu");
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    static_cast<void>(window.dispatch_key(enter));
    require(!(*toolbar).customize_menu_open() && changes.size() == 1U && changes[0].command_id == "save" &&
                !changes[0].shown && !(*toolbar).command_shown("save"),
            "choosing a checked row hides its command and reports it");
    require((*toolbar).realized_items().size() == 2U && (*toolbar).realized_items()[0].stable_id == "undo",
            "a hidden command leaves the toolbar");
    require(!(*(*toolbar).customize_menu_items()[0].command).state().checked, "its row is unchecked");

    // End selects the placement row; choosing it moves the toolbar below.
    require((*toolbar).show_customize_menu(), "the menu reopens");
    KeyEvent end{KeyAction::down, PhysicalKey::end};
    static_cast<void>(window.dispatch_key(end));
    static_cast<void>(window.dispatch_key(enter));
    require((*toolbar).placement.read() == QuickAccessToolbarPlacement::below_ribbon && placements.size() == 1U &&
                placements[0] == QuickAccessToolbarPlacement::below_ribbon,
            "the placement row moves the toolbar below the ribbon and reports it");
    require((*(*toolbar).customize_menu_items()[5].command).state().text == "Show Above the Ribbon",
            "the placement row then offers the other placement");
    (*toolbar).placement = QuickAccessToolbarPlacement::below_ribbon;
    require(placements.size() == 1U, "setting the same placement reports nothing");

    // The application shows a command: it joins the toolbar's end.
    (*toolbar).set_command_shown("print", true);
    const std::vector<std::string>& shown = (*toolbar).shown_command_ids();
    require(shown.size() == 3U && shown.back() == "print" && changes.size() == 2U && changes[1].shown,
            "showing a command appends it and reports it");
    bool threw = false;
    try {
        (*toolbar).set_command_shown("missing", true);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "an undeclared command cannot be shown");
}

void overflow_keeps_customize() {
    const Commands commands;
    std::shared_ptr<QuickAccessToolbar> toolbar = make_control<QuickAccessToolbar>(StableId("qat"));
    std::vector<QuickAccessToolbarCommand> all = declared(commands);
    all[3].shown = true;
    (*toolbar).set_commands(std::move(all));
    Window window(toolbar, {110.0, 25.0});
    window.perform_layout();
    const std::vector<std::string> overflowed = (*toolbar).overflow_item_ids();
    const QuickAccessToolbar::Parts parts = (*toolbar).parts();
    require(!overflowed.empty() && overflowed.back() == "print", "a narrow toolbar overflows from its last command");
    require(parts.overflow_button.present && parts.customize_button.present &&
                parts.overflow_button.bounds.x + parts.overflow_button.bounds.width <=
                    parts.customize_button.bounds.x + 0.001,
            "the overflow button sits left of the customize button, which stays visible");
    require((*toolbar).show_overflow() && (*toolbar).drop_down_open(), "the overflow menu opens");
    (*toolbar).close_drop_down();
}

void unknown_ids_and_semantics() {
    const Commands commands;
    std::shared_ptr<QuickAccessToolbar> toolbar = make_control<QuickAccessToolbar>(StableId("qat"));
    (*toolbar).set_commands(declared(commands));
    Window window(toolbar, {320.0, 25.0});
    window.perform_layout();
    std::vector<QuickAccessToolbarCustomization> changes;
    SubscriptionToken changed = (*toolbar).customized().subscribe(RecordCustomization{&changes});
    (*toolbar).set_shown_command_ids({"print", "later.version.command", "save"});
    const std::vector<ToolStrip::Item> items = (*toolbar).realized_items();
    require(items.size() == 2U && items[0].stable_id == "print" && items[1].stable_id == "save",
            "restored IDs set the order; an unknown ID shows nothing");
    require((*toolbar).shown_command_ids().size() == 3U && (*toolbar).shown_command_ids()[1] == "later.version.command",
            "an unknown ID is kept in place");
    require(changes.empty(), "restoring state reports no customization");

    const SemanticDescriptor descriptor = (*toolbar).semantic_descriptor();
    const std::vector<SemanticNode> children = (*toolbar).semantic_virtual_children();
    require(descriptor.role == SemanticRole::toolbar && descriptor.name == "Quick Access Toolbar",
            "the toolbar is a named toolbar");
    require(children.size() == 3U && children[0].role == SemanticRole::button &&
                children[2].role == SemanticRole::menu_button && children[2].has_popup &&
                children[2].stable_id == "qat.customize",
            "its buttons are buttons and the customize button a menu button with an overlay");
    require((*toolbar).on_semantic_child_action("qat.customize", SemanticAction::expand, {}) &&
                (*toolbar).customize_menu_open(),
            "expanding the customize node opens the menu");
    const std::vector<SemanticNode> open_children = (*toolbar).semantic_virtual_children();
    require(has_semantic_state(open_children[2].states, SemanticState::expanded),
            "the open menu shows as expanded");
    require((*toolbar).on_semantic_child_action("qat.customize", SemanticAction::collapse, {}) &&
                !(*toolbar).customize_menu_open(),
            "collapsing it closes the menu");
}

} // namespace

int main() {
    parts_and_commands_only();
    customize_menu();
    overflow_keeps_customize();
    unknown_ids_and_semantics();
    std::cout << "QuickAccessToolbar: parts, commands only, customize menu, placement, overflow, unknown IDs and "
                 "semantics passed\n";
    return 0;
}
