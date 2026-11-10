// Ribbon (stage RB, phase 1): tab row and command plane geometry, groups
// with captions and dialog launchers, one command path for pointer,
// keyboard, mnemonic and accessibility, command state repainting only its
// command, minimizing by a tab click, hosted controls, menus, semantics, the
// F6 region and refusals.

#include "gui_forms/gui_forms.hpp"
#include "support/theme_definition_heap.hpp"

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

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { texts.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override { ++images; }

    std::vector<std::string> texts;
    int lines{};
    int images{};
};

bool painted(const RecordingPainter& painter, std::string_view text) {
    for (const std::string& seen : painter.texts) {
        if (seen == text) return true;
    }
    return false;
}

struct CountInvocation final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

struct RecordClick final {
    std::vector<std::string>* seen{};
    void operator()(const RibbonCommandClick& click) const { (*seen).push_back(click.command_id); }
};

struct RecordLaunch final {
    std::vector<std::string>* seen{};
    void operator()(const RibbonDialogLaunch& launch) const { (*seen).push_back(launch.group_id); }
};

struct Commands final {
    std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::shared_ptr<Command> cut = std::make_shared<Command>("cut", "Cut");
    std::shared_ptr<Command> copy = std::make_shared<Command>("copy", "&Copy");
    std::shared_ptr<Command> bold = std::make_shared<Command>("bold", "Bold");
    std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
};

std::vector<RibbonTabSpec> sample_tabs(const Commands& commands) {
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard", .dialog_launcher = true};
    clipboard.commands.push_back({.stable_id = "paste", .kind = RibbonCommandKind::split,
                                  .command = commands.paste, .image = ImageId{5},
                                  .drop_down = {MenuItemSpec("paste.special", MenuItemKind::command, commands.choice,
                                                             "Paste special")}});
    clipboard.commands.push_back({.stable_id = "cut", .size = RibbonCommandSize::small, .command = commands.cut});
    clipboard.commands.push_back({.stable_id = "copy", .size = RibbonCommandSize::small, .command = commands.copy});
    clipboard.commands.push_back({.stable_id = "bold", .kind = RibbonCommandKind::toggle,
                                  .size = RibbonCommandSize::small, .command = commands.bold,
                                  .image = ImageId{6}, .show_label = false});
    RibbonGroupSpec font{.stable_id = "font", .caption = "Font"};
    font.commands.push_back({.stable_id = "font-name", .kind = RibbonCommandKind::combo_box, .text = "Font",
                             .choices = {"Portsmouth", "Carlito"}});
    font.commands.push_back({.stable_id = "font-size", .kind = RibbonCommandKind::numeric_up_down, .text = "Size"});
    font.commands.push_back({.stable_id = "strike", .kind = RibbonCommandKind::check_box, .text = "Strike"});
    home.groups.push_back(std::move(clipboard));
    home.groups.push_back(std::move(font));
    RibbonTabSpec insert{.stable_id = "insert", .text = "&Insert"};
    RibbonGroupSpec tables{.stable_id = "tables", .caption = "Tables"};
    tables.commands.push_back({.stable_id = "table", .kind = RibbonCommandKind::drop_down, .text = "Table",
                               .drop_down = {MenuItemSpec("table.insert", MenuItemKind::command, commands.choice,
                                                          "Insert table")}});
    tables.commands.push_back({.stable_id = "find", .kind = RibbonCommandKind::text_box, .text = "Find"});
    insert.groups.push_back(std::move(tables));
    tabs.push_back(std::move(home));
    tabs.push_back(std::move(insert));
    return tabs;
}

std::shared_ptr<Ribbon> sample_ribbon(const Commands& commands) {
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(sample_tabs(commands));
    (*ribbon).set_application_menu({MenuItemSpec("file.open", MenuItemKind::command, commands.choice, "Open")});
    return ribbon;
}

// A borrow into `items`, which must outlive it: temporaries are refused
// (house style §1).
const Ribbon::CommandItem* find_command(const std::vector<Ribbon::CommandItem>& items, std::string_view id) {
    for (const Ribbon::CommandItem& item : items) {
        if (item.stable_id == id) return &item;
    }
    return nullptr;
}
const Ribbon::CommandItem* find_command(std::vector<Ribbon::CommandItem>&& items, std::string_view id) = delete;

// The item `id` names, which must be there: a missing one ends the test
// rather than dereferencing null. A borrow into `items`, as find_command is.
const Ribbon::CommandItem& command_named(const std::vector<Ribbon::CommandItem>& items, std::string_view id) {
    const Ribbon::CommandItem* const found = find_command(items, id);
    require(found != nullptr, "the item the test names is realized");
    return *found;
}
const Ribbon::CommandItem& command_named(std::vector<Ribbon::CommandItem>&& items, std::string_view id) = delete;

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

bool key(Window& window, std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

void geometry() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*ribbon).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "ribbon" &&
                anatomy.item_systems.size() == 3U,
            "Ribbon publishes a valid anatomy with tab, group and command systems");
    require((*ribbon).measure({900.0, 400.0}).height == 27.0 + 88.0,
            "the ribbon is its tab row over its command plane");
    const Ribbon::Parts parts = (*ribbon).parts();
    require(parts.tab_rail.present && parts.application_button.present && parts.command_plane.present &&
                parts.command_plane.bounds.y == parts.tab_rail.bounds.height,
            "the tab row is above the command plane");
    require(find_part(anatomy, "help_button") && !parts.help_button.present,
            "the help button's part is reserved and not yet placed (owner, N4)");
    const std::vector<Ribbon::Tab> tabs = (*ribbon).realized_tabs();
    require(tabs.size() == 2U && tabs[0].states.selected && !tabs[1].states.selected &&
                tabs[0].parts.tab.bounds.x >= parts.application_button.bounds.x + parts.application_button.bounds.width,
            "the tabs follow the File tab and the first is selected");
    const std::vector<Ribbon::Group> groups = (*ribbon).realized_groups();
    require(groups.size() == 2U && groups[0].parts.group_launcher.present && !groups[1].parts.group_launcher.present &&
                groups[0].parts.group_caption.present && groups[0].parts.group_divider.present &&
                groups[1].parts.group_region.bounds.x >= groups[0].parts.group_region.bounds.x +
                                                             groups[0].parts.group_region.bounds.width - 0.001,
            "groups sit side by side with captions, dividers and authored launchers");
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    const Ribbon::CommandItem* paste = find_command(items, "paste");
    const Ribbon::CommandItem* cut = find_command(items, "cut");
    const Ribbon::CommandItem* copy = find_command(items, "copy");
    const Ribbon::CommandItem* bold = find_command(items, "bold");
    require(paste != nullptr && (*paste).parts.command_icon.present && (*paste).parts.command_label.present &&
                (*paste).parts.command_arrow.present &&
                (*paste).parts.command_face.bounds.y + (*paste).parts.command_face.bounds.height <=
                    (*paste).parts.command_arrow.bounds.y + 0.001,
            "a large split shows its icon on its face over its label and arrow");
    require(cut != nullptr && copy != nullptr &&
                (*cut).parts.command_face.bounds.x == (*copy).parts.command_face.bounds.x &&
                (*copy).parts.command_face.bounds.y > (*cut).parts.command_face.bounds.y &&
                (*cut).parts.command_face.bounds.x > (*paste).parts.command_face.bounds.x,
            "small commands stack in a column beside the large one");
    require(bold != nullptr && !(*bold).parts.command_label.present && (*bold).parts.command_icon.present,
            "an authored icon-only small command shows its icon without a label");
    require((*copy).parts.command_label.present, "labels are shown at every size");

    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 900.0, 115.0}));
    require(painted(painter, "File") && painted(painter, "Home") && painted(painter, "Clipboard") &&
                painted(painter, "Paste") && painted(painter, "Copy") && !painted(painter, "&Copy") &&
                !painted(painter, "Bold") && painter.images >= 2 && painter.lines >= 5,
            "the ribbon paints its tabs, captions, labels without mnemonic markers, icons and launcher mark");
}

void one_command_path() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    int pastes = 0;
    int cuts = 0;
    int copies = 0;
    SubscriptionToken pasted = (*commands.paste).invoked().subscribe(CountInvocation{&pastes});
    SubscriptionToken cutted = (*commands.cut).invoked().subscribe(CountInvocation{&cuts});
    SubscriptionToken copied = (*commands.copy).invoked().subscribe(CountInvocation{&copies});
    std::vector<std::string> clicks;
    SubscriptionToken clicked = (*ribbon).command_clicked().subscribe(RecordClick{&clicks});
    std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    click(window, centre(command_named(items, "paste").parts.command_face.bounds));
    require(pastes == 1 && clicks.size() == 1U && clicks[0] == "paste",
            "a pointer click runs the command and reports it");

    require(window.request_focus(ribbon), "the ribbon takes focus");
    require(key(window, PhysicalKey::down) && key(window, PhysicalKey::enter) && pastes == 2,
            "Down enters the command plane and Enter runs the first command");
    require(key(window, PhysicalKey::right) && key(window, PhysicalKey::space) && cuts == 1,
            "Right moves to the next command and Space runs it");
    const std::vector<Ribbon::CommandItem> focused = (*ribbon).realized_commands();
    require(command_named(focused, "cut").states.focus_visible, "the keyboard's command shows the focus cue");
    require((*ribbon).on_semantic_child_action("ribbon.command.cut", SemanticAction::press, {}) && cuts == 2,
            "the semantic press runs the command");
    require(key(window, PhysicalKey::c, Modifier::alt) && copies == 1, "a command's mnemonic runs it");
    require(clicks.size() == 5U, "every path reports the same click");

    require((*ribbon).on_semantic_child_action("ribbon.command.bold", SemanticAction::press, {}) &&
                (*commands.bold).state().checked,
            "a toggle's semantic press flips its command's checked state");
    const std::vector<Ribbon::CommandItem> toggled = (*ribbon).realized_commands();
    require(command_named(toggled, "bold").states.checked, "and the toggle shows checked");
    (*commands.cut).set_enabled(false);
    static_cast<void>((*ribbon).on_semantic_child_action("ribbon.command.cut", SemanticAction::press, {}));
    const std::vector<Ribbon::CommandItem> disabled = (*ribbon).realized_commands();
    require(cuts == 2 && command_named(disabled, "cut").states.disabled,
            "a disabled command does not run and shows disabled");
}

void tab_navigation() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    std::vector<std::string> launches;
    SubscriptionToken launched = (*ribbon).dialog_launcher_clicked().subscribe(RecordLaunch{&launches});
    require(window.request_focus(ribbon), "the ribbon takes focus on its selected tab");
    require(key(window, PhysicalKey::tab) && (*ribbon).realized_tabs()[1].states.focus_visible &&
                (*ribbon).selected_tab.read() == std::size_t{0U},
            "Tab moves along the tab row without selecting");
    require(key(window, PhysicalKey::tab) && key(window, PhysicalKey::tab, Modifier::shift) &&
                (*ribbon).realized_tabs()[1].states.focus_visible,
            "Tab enters the plane and Shift+Tab comes back");
    require(key(window, PhysicalKey::left) && (*ribbon).realized_tabs()[0].states.focus_visible,
            "Left moves along the tab row");
    for (int step = 0; step < 6; ++step) static_cast<void>(key(window, PhysicalKey::tab));
    require((*ribbon).realized_groups()[0].parts.group_launcher.states.contains(AnatomyState::focus_visible) &&
                key(window, PhysicalKey::enter) && launches.size() == 1U,
            "Tab reaches the group's launcher after its commands, and Enter launches it");
    static_cast<void>(key(window, PhysicalKey::tab));
    require(window.focused_control() == (*ribbon).combo_box("font-name"),
            "Tab after the last stop leaves the ribbon for the next Tab stop, its first hosted control");
}

void state_repaints_only_its_command() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    const Rect cut = command_named(items, "cut").parts.command_face.bounds;
    (*commands.cut).set_enabled(false);
    const DamageRegion damage = window.take_damage();
    const Rect bounds = damage.bounds();
    require(!damage.empty() && bounds.x >= cut.x - 2.0 && bounds.y >= cut.y - 2.0 &&
                bounds.x + bounds.width <= cut.x + cut.width + 2.0 &&
                bounds.y + bounds.height <= cut.y + cut.height + 2.0,
            "an enabled change repaints only its command");
}

// Each damaged rectangle lies within `part` (with a small outset) or below
// it, where the menu shows (a drop-down opens below its command); nothing
// else of the ribbon, its tab row and its other commands, repaints.
bool damage_only_part_or_below(const DamageRegion& damage, Rect part) {
    constexpr double slack = 3.0;
    constexpr double shadow = 12.0;
    for (const Rect& rectangle : damage.rectangles()) {
        const bool in_part = rectangle.x >= part.x - slack && rectangle.y >= part.y - slack &&
                             rectangle.x + rectangle.width <= part.x + part.width + slack &&
                             rectangle.y + rectangle.height <= part.y + part.height + slack;
        const bool below = rectangle.y >= part.y + part.height - shadow;
        if (!in_part && !below) return false;
    }
    return true;
}

// Owner, 2026-11-09: opening the menu under Paste repainted the whole ribbon.
// Opening and closing a drop-down, and the hover and press changes that come
// with them, repaint only the invoking command; the menu is its own surface.
void menus_repaint_only_their_command() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    // Tall enough that the menus open below their commands.
    Window window(ribbon, {900.0, 400.0});
    window.perform_layout();
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    const Ribbon::CommandItem paste = *find_command(items, "paste");
    const Rect arrow = paste.parts.command_arrow.bounds;
    const Rect part = Rect::united(paste.parts.command_face.bounds, arrow);
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, centre(arrow)}));
    window.perform_layout();
    static_cast<void>(window.take_damage());
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, centre(arrow)}));
    window.perform_layout();
    require((*ribbon).menu_open(), "the split's arrow opens its menu");
    const DamageRegion opened = window.take_damage();
    require(!opened.empty() && damage_only_part_or_below(opened, part),
            "opening a drop-down repaints its command and the menu, not the ribbon");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, centre(arrow)}));
    window.perform_layout();
    static_cast<void>(window.take_damage());
    (*ribbon).close_menus();
    window.perform_layout();
    const DamageRegion closed = window.take_damage();
    require(!closed.empty() && damage_only_part_or_below(closed, part),
            "closing it repaints its command and where the menu was, not the ribbon");
    const std::vector<Ribbon::CommandItem> closed_items = (*ribbon).realized_commands();
    const Rect table = command_named(closed_items, "table").parts.command_face.bounds;
    static_cast<void>(window.take_damage());
    require((*ribbon).show_drop_down("table"), "a drop-down opens from the program");
    window.perform_layout();
    const DamageRegion table_opened = window.take_damage();
    (*ribbon).close_menus();
    window.perform_layout();
    const DamageRegion table_closed = window.take_damage();
    require(damage_only_part_or_below(table_opened, table) && damage_only_part_or_below(table_closed, table),
            "the same for a plain drop-down opened by the program");
}

void minimize_by_tab_click() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    const std::shared_ptr<ComboBox> font = (*ribbon).combo_box("font-name");
    const std::vector<Ribbon::Tab> tabs = (*ribbon).realized_tabs();
    click(window, centre(tabs[0].parts.tab.bounds));
    window.perform_layout();
    require((*ribbon).minimized.read() && !(*ribbon).parts().command_plane.present &&
                (*ribbon).measure({900.0, 400.0}).height == 27.0 && (*ribbon).realized_tabs().size() == 2U &&
                !(*font).effectively_visible(),
            "clicking the selected tab minimizes the plane, keeping the tabs");
    click(window, centre(tabs[0].parts.tab.bounds));
    window.perform_layout();
    require(!(*ribbon).minimized.read() && (*ribbon).parts().command_plane.present && (*font).effectively_visible(),
            "clicking it again shows the plane");
    click(window, centre(tabs[0].parts.tab.bounds));
    window.perform_layout();
    click(window, centre(tabs[1].parts.tab.bounds));
    window.perform_layout();
    require(!(*ribbon).minimized.read() && (*ribbon).selected_tab.read() == std::size_t{1U} && !(*font).effectively_visible() &&
                (*(*ribbon).text_box("find")).effectively_visible(),
            "clicking another tab while minimized selects it and shows the plane");
}

void hosted_controls() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    const std::shared_ptr<ComboBox> font = (*ribbon).combo_box("font-name");
    const std::shared_ptr<NumericUpDown> size = (*ribbon).numeric_up_down("font-size");
    const std::shared_ptr<CheckBox> strike = (*ribbon).check_box("strike");
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    require((*font).items.read().size() == 2U && (*strike).text.read() == "Strike" &&
                command_named(items, "font-name").parts.command_combo_box.bounds ==
                    (*font).committed_arranged_bounds() &&
                command_named(items, "font-size").parts.command_numeric_up_down.present &&
                command_named(items, "strike").parts.command_check_box.present,
            "hosted controls sit where the ribbon's layout puts their commands");
    require(window.request_focus(font) && window.focused_control() == font, "a hosted control is focusable");
    require((*size).effectively_visible() && !(*(*ribbon).text_box("find")).effectively_visible(),
            "only the selected tab's hosted controls show");
    bool threw = false;
    try {
        static_cast<void>((*ribbon).text_box("font-name"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "asking for the wrong kind of hosted control is refused");
}

void menus_and_launchers() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    std::vector<std::string> launches;
    SubscriptionToken launched = (*ribbon).dialog_launcher_clicked().subscribe(RecordLaunch{&launches});
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    const Ribbon::CommandItem paste = *find_command(items, "paste");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                               centre(paste.parts.command_arrow.bounds)}));
    const std::vector<Ribbon::CommandItem> opened = (*ribbon).realized_commands();
    require((*ribbon).menu_open() && command_named(opened, "paste").states.open,
            "a split's arrow opens its menu over the ribbon");
    (*ribbon).close_menus();
    click(window, centre((*ribbon).parts().application_button.bounds));
    require((*ribbon).menu_open() && (*ribbon).parts().application_button.states.contains(AnatomyState::open),
            "the File tab opens the application menu");
    (*ribbon).close_menus();
    click(window, centre((*ribbon).realized_groups()[0].parts.group_launcher.bounds));
    require(launches.size() == 1U && launches[0] == "clipboard", "a launcher click raises dialog_launcher_clicked");
    require((*ribbon).on_semantic_child_action("ribbon.launcher.clipboard", SemanticAction::press, {}) &&
                launches.size() == 2U,
            "the launcher's semantic press raises the same event");
}

void semantics_and_region() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    const std::vector<Control::Ptr> regions = window.focus_regions();
    require(regions.size() == 1U && regions[0] == ribbon, "an attached ribbon registers itself as an F6 region");
    const std::vector<SemanticNode> nodes = (*ribbon).semantic_virtual_children();
    require(nodes.size() == 3U && nodes[0].role == SemanticRole::menu_button && nodes[0].has_popup &&
                nodes[0].name == "File",
            "the File tab is a menu button with an overlay");
    require(nodes[1].role == SemanticRole::tab_group && nodes[1].children.size() == 2U &&
                nodes[1].children[0].role == SemanticRole::tab && nodes[1].children[0].name == "Home" &&
                nodes[1].children[1].position_in_set == std::size_t{2U} &&
                nodes[1].children[1].set_size == std::size_t{2U},
            "the tab row is a tab list with positioned tabs");
    const SemanticNode& plane = nodes[2];
    require(plane.role == SemanticRole::tab_panel && plane.name == "Home" && plane.children.size() == 2U &&
                plane.children[0].name == "Clipboard",
            "the command plane holds the selected tab's groups, named by caption");
    const SemanticNode& clipboard = plane.children[0];
    require(clipboard.children.size() == 5U && clipboard.children[0].role == SemanticRole::split_button &&
                clipboard.children[0].has_popup && clipboard.children[0].position_in_set == std::size_t{1U} &&
                clipboard.children[0].set_size == std::size_t{4U} &&
                clipboard.children[4].name == "Clipboard dialog launcher",
            "commands carry split roles, overlays and positions; the launcher is a button");
    require(nodes[1].children[0].access_key == "H" && nodes[1].children[1].access_key == "I" &&
                clipboard.children[2].name == "Copy" && clipboard.children[2].access_key == "C" &&
                clipboard.children[1].access_key.empty(),
            "a tab's or a command's mnemonic is its access key; a command without one has none");
    require((*ribbon).on_semantic_child_action("ribbon.tab.insert", SemanticAction::select, {}) &&
                (*ribbon).selected_tab.read() == std::size_t{1U},
            "selecting a tab node selects the tab");
    const std::string json = window.semantic_snapshot().to_json();
    require(json.find("\"role\":\"menu_button\"") != std::string::npos, "the snapshot carries the ribbon's nodes");
}

// A ribbon whose maximum size is narrower than the bounds its parent offers
// is arranged at its maximum width: its hosted controls sit where its own
// published parts (and its painting) put their commands.
void arranged_within_maximum_size() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    (*ribbon).maximum_size = {150.0, 0.0};
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    require((*ribbon).committed_arranged_bounds().width == 150.0, "the ribbon is arranged at its maximum width");
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    const std::shared_ptr<ComboBox> font = (*ribbon).combo_box("font-name");
    const Ribbon::CommandItem* item = find_command(items, "font-name");
    const bool shown = item != nullptr && (*item).parts.command_combo_box.present;
    require(shown == (*font).effectively_visible(), "a hosted control shows exactly where its command is published");
    require(!shown || (*item).parts.command_combo_box.bounds == (*font).committed_arranged_bounds(),
            "a hosted control sits on its published command part");
}

// Nothing the ribbon opens falls back to the window's theme: a drop-down
// menu (a ContextMenu, its layer and panels overlay roots) and the
// application menu take the ribbon's theme override, an ancestor's included.
void menus_take_the_ribbon_theme() {
    const Commands commands;
    std::shared_ptr<Panel> host = make_control<Panel>(StableId("host"));
    std::shared_ptr<Ribbon> ribbon = sample_ribbon(commands);
    (*ribbon).set_requested_bounds({0.0, 0.0, 900.0, 112.0});
    (*host).add_child(ribbon);
    Window window(host, {900.0, 300.0});
    window.perform_layout();
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    (*held).id = "ribbon-override";
    const std::shared_ptr<const Theme> override_theme = Theme::create(*held);
    (*host).set_theme_override(override_theme);
    window.perform_layout();
    require((*ribbon).show_drop_down("paste"), "the split's menu opens");
    window.perform_layout();
    const Control::Ptr panel = window.find("ribbon.menu.overlay.panel.0");
    const Control::Ptr row = window.find("ribbon.menu.overlay.row.paste.special");
    require(panel && row && (*panel).effective_theme().id() == "ribbon-override" &&
                (*row).effective_theme().id() == "ribbon-override",
            "a drop-down menu takes the theme override of the ribbon's ancestor");
    (*ribbon).close_menus();
    // Without recent items the application menu is the ribbon's one-pane
    // menu; with them it is the two-pane overlay.
    require((*ribbon).show_application_menu(), "the application menu opens");
    window.perform_layout();
    const Control::Ptr one_pane = window.find("ribbon.menu.overlay.panel.0");
    require(one_pane && (*one_pane).effective_theme().id() == "ribbon-override",
            "the one-pane application menu takes the ribbon's theme override");
    (*ribbon).close_menus();
    (*ribbon).set_recent_items({{"recent.report", "Report.txt", "C:\\Documents"}});
    require((*ribbon).show_application_menu(), "the two-pane application menu opens");
    window.perform_layout();
    const Control::Ptr application = window.find("ribbon.application-menu");
    require(application && (*application).effective_theme().id() == "ribbon-override",
            "the application menu takes the ribbon's theme override");
    (*ribbon).close_menus();
    (*host).clear_theme_override();
    require((*ribbon).show_drop_down("paste"), "the menu opens again");
    window.perform_layout();
    const Control::Ptr plain = window.find("ribbon.menu.overlay.panel.0");
    require(plain && (*plain).effective_theme().id() == window.theme().id(),
            "without an override the menu takes the window's theme");
    (*ribbon).close_menus();
}

// Plan Paint's port, items 10 to 12: a label is plain text in a row (no
// hit, focus or action; static text); commands beside_previous continue a
// row (Save over Undo and Redo); and a two-row stack stands from the top,
// level with the large command beside it.
void labels_and_rows() {
    const Commands commands;
    std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    std::shared_ptr<Command> undo = std::make_shared<Command>("undo", "Undo");
    std::shared_ptr<Command> redo = std::make_shared<Command>("redo", "Redo");
    int pastes = 0;
    SubscriptionToken counted = (*commands.paste).invoked().subscribe(CountInvocation{&pastes});
    RibbonTabSpec home{.stable_id = "home", .text = "Home"};
    RibbonGroupSpec history{.stable_id = "history", .caption = "History"};
    history.commands.push_back({.stable_id = "paste", .command = commands.paste});
    history.commands.push_back({.stable_id = "save", .size = RibbonCommandSize::small, .command = save});
    history.commands.push_back({.stable_id = "undo", .size = RibbonCommandSize::small, .command = undo});
    history.commands.push_back(
        {.stable_id = "redo", .size = RibbonCommandSize::small, .command = redo, .beside_previous = true});
    history.commands.push_back({.stable_id = "hint", .kind = RibbonCommandKind::label, .text = "Click to place"});
    RibbonGroupSpec pair{.stable_id = "pair", .caption = "Pair"};
    pair.commands.push_back({.stable_id = "cut", .command = commands.cut});
    pair.commands.push_back({.stable_id = "copy", .size = RibbonCommandSize::small, .command = commands.copy});
    pair.commands.push_back({.stable_id = "bold", .size = RibbonCommandSize::small, .command = commands.bold});
    pair.layouts.push_back(RibbonGroupLayout{RibbonGroupSize::large, {}, 2U});
    home.groups.push_back(std::move(history));
    home.groups.push_back(std::move(pair));
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(std::move(home));
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(tabs));
    Window window(ribbon, {900.0, 115.0});
    window.perform_layout();
    const std::vector<Ribbon::CommandItem> items = (*ribbon).realized_commands();
    const Rect paste_box = command_named(items, "paste").parts.command_focus.bounds;
    const Rect save_box = command_named(items, "save").parts.command_focus.bounds;
    const Rect undo_box = command_named(items, "undo").parts.command_focus.bounds;
    const Rect redo_box = command_named(items, "redo").parts.command_focus.bounds;
    require(save_box.y == paste_box.y && undo_box.x == save_box.x && undo_box.y == save_box.y + save_box.height,
            "Save stands in the column's first row, Undo in the second");
    require(redo_box.y == undo_box.y && redo_box.x >= undo_box.x + undo_box.width,
            "Redo continues Undo's row to its right");
    const Ribbon::CommandItem& hint = command_named(items, "hint");
    require(hint.parts.command_label.present && !hint.parts.command_focus.present &&
                !hint.parts.command_face.present && hint.parts.command_label.bounds.y == undo_box.y + undo_box.height,
            "the label is text alone, in the column's third row");
    const Rect cut_box = command_named(items, "cut").parts.command_focus.bounds;
    const Rect copy_box = command_named(items, "copy").parts.command_focus.bounds;
    const Rect bold_box = command_named(items, "bold").parts.command_focus.bounds;
    require(copy_box.y == cut_box.y && bold_box.y == copy_box.y + copy_box.height,
            "a two-row stack stands from the top, level with the large command");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 900.0, 115.0}));
    require(painted(painter, "Click to place"), "the label paints its text");
    click(window, centre(hint.parts.command_label.bounds));
    require(pastes == 0 && (*ribbon).realized_commands().size() == items.size(), "a click on a label runs nothing");
    require(window.request_focus(ribbon), "the ribbon takes focus");
    bool label_focused = false;
    for (int step = 0; step < 12; ++step) {
        static_cast<void>(key(window, PhysicalKey::tab));
        const std::vector<Ribbon::CommandItem> now = (*ribbon).realized_commands();
        if (command_named(now, "hint").states.focus_visible) label_focused = true;
    }
    require(!label_focused, "Tab never stops on a label");
    const std::vector<SemanticNode> nodes = (*ribbon).semantic_virtual_children();
    bool static_text = false;
    for (const SemanticNode& node : nodes) {
        for (const SemanticNode& group : node.children) {
            for (const SemanticNode& child : group.children) {
                if (child.name != "Click to place") continue;
                static_text = child.role == SemanticRole::static_text && child.actions.empty() &&
                              !has_semantic_state(child.states, SemanticState::focusable);
            }
        }
    }
    require(static_text, "the label is published as static text, neither focusable nor actionable");
    std::vector<RibbonTabSpec> refused;
    refused.push_back(RibbonTabSpec{.stable_id = "home", .text = "Home"});
    RibbonGroupSpec bad{.stable_id = "bad", .caption = "Bad"};
    bad.commands.push_back({.stable_id = "hint", .kind = RibbonCommandKind::label, .command = save, .text = "Hint"});
    refused[0].groups.push_back(std::move(bad));
    bool threw = false;
    try {
        (*ribbon).set_tabs(std::move(refused));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a label with a Command is refused");
    std::vector<RibbonTabSpec> first;
    first.push_back(RibbonTabSpec{.stable_id = "home", .text = "Home"});
    RibbonGroupSpec leading{.stable_id = "leading", .caption = "Leading"};
    leading.commands.push_back(
        {.stable_id = "save", .size = RibbonCommandSize::small, .command = save, .beside_previous = true});
    first[0].groups.push_back(std::move(leading));
    threw = false;
    try {
        (*ribbon).set_tabs(std::move(first));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a group's first command may not stand beside another");
}

void refusals() {
    const Commands commands;
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    std::vector<RibbonTabSpec> missing = sample_tabs(commands);
    missing[0].groups[0].commands[1].command = nullptr;
    bool threw = false;
    try {
        (*ribbon).set_tabs(std::move(missing));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a button without a Command is refused");
    std::vector<RibbonTabSpec> repeated = sample_tabs(commands);
    repeated[1].groups[0].stable_id = "clipboard";
    threw = false;
    try {
        (*ribbon).set_tabs(std::move(repeated));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "IDs are one namespace across tabs, groups and commands");
    std::vector<RibbonTabSpec> icon_only = sample_tabs(commands);
    icon_only[0].groups[0].commands[0].show_label = false;
    threw = false;
    try {
        (*ribbon).set_tabs(std::move(icon_only));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*ribbon).tabs().empty(), "a large command may not drop its label, and nothing changes");
}

} // namespace

int main() {
    geometry();
    one_command_path();
    tab_navigation();
    state_repaints_only_its_command();
    menus_repaint_only_their_command();
    minimize_by_tab_click();
    hosted_controls();
    menus_and_launchers();
    semantics_and_region();
    arranged_within_maximum_size();
    menus_take_the_ribbon_theme();
    refusals();
    labels_and_rows();
    std::cout << "Ribbon: geometry, one command path, item repaint, minimizing, hosted controls, menus, launchers, "
                 "semantics, F6 region and refusals passed\n";
    return 0;
}
