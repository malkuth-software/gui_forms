// Ribbon phase 6 (stage RB; owner decisions 3, 13, 23, 26): the font and
// colour value commands, RibbonFontControl and RibbonColorPicker standalone
// and hosted in a Ribbon, the application menu's recent pane, and
// BackstageView with its navigation, Escape and the File tab returning to
// the document.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
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

// Records the solid fills a paint pass draws, in each control's local
// coordinates (translations are not applied).
class FillRecorder final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color color) override { fills.push_back({rect, color}); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    struct Fill final {
        Rect bounds{};
        Color color{};
    };
    std::vector<Fill> fills;
};

bool filled(const FillRecorder& painter, Rect bounds, Color color) {
    for (const FillRecorder::Fill& fill : painter.fills) {
        if (fill.bounds == bounds && fill.color == color) return true;
    }
    return false;
}

struct CountInvocation final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

struct RecordFont final {
    std::vector<FontChoice>* seen{};
    void operator()(const FontChoice& value) const { (*seen).push_back(value); }
};

struct RecordColor final {
    std::vector<std::optional<Color>>* seen{};
    void operator()(const std::optional<Color>& value) const { (*seen).push_back(value); }
};

struct RecordRecent final {
    std::vector<std::string>* seen{};
    void operator()(const RibbonRecentItemInvocation& invoked) const { (*seen).push_back(invoked.item_id); }
};

struct RecordMenu final {
    std::vector<std::string>* seen{};
    void operator()(const MenuItemInvocation& invoked) const { (*seen).push_back(invoked.item_id); }
};

struct RecordTopic final {
    std::vector<std::string>* seen{};
    void operator()(const BackstageTopicChange& change) const { (*seen).push_back(change.topic_id); }
};

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

Rect to_window(const Control& control, Rect local) {
    const Rect absolute = control.absolute_bounds();
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    window.perform_layout();
}

void press_key(Window& window, std::uint32_t key) {
    KeyEvent event{KeyAction::down, key};
    static_cast<void>(window.dispatch_key(event));
    window.perform_layout();
}

void value_commands() {
    std::shared_ptr<Command> font = std::make_shared<Command>("font", "Font");
    FontCommand font_command(font);
    int font_runs = 0;
    SubscriptionToken counted = (*font).invoked().subscribe(CountInvocation{&font_runs});
    std::vector<FontChoice> fonts;
    SubscriptionToken recorded = font_command.value_changed().subscribe(RecordFont{&fonts});
    font_command.set_value({"Portsmouth", 11.0});
    require(fonts.size() == 1U && font_runs == 0, "set_value changes the value without running the Command");
    require(font_command.commit({"Carlito", 11.0}, "test") && fonts.size() == 2U && font_runs == 1 &&
                font_command.value().family == "Carlito",
            "commit sets the value, then runs the Command");
    require(!font_command.commit({"", 0.0}, "test") && !font_command.commit({"Bad\nName", 10.0}, "test") &&
                !font_command.commit({"Carlito", 2000.0}, "test") && font_runs == 1,
            "commits of empty or invalid choices are refused");
    (*font).set_enabled(false);
    require(!font_command.commit({"Portsmouth", 12.0}, "test") && font_command.value().family == "Carlito",
            "a disabled Command refuses commits and keeps the value");
    bool threw = false;
    try {
        font_command.set_value({"Portsmouth", -1.0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "set_value refuses a negative size");

    std::shared_ptr<Command> color = std::make_shared<Command>("color", "Colour");
    ColorCommand color_command(color);
    int color_runs = 0;
    SubscriptionToken color_counted = (*color).invoked().subscribe(CountInvocation{&color_runs});
    for (std::uint8_t index = 0; index < 12U; ++index) {
        static_cast<void>(color_command.commit(Color::rgba(index, 0, 0), "test"));
    }
    require(color_runs == 12 && color_command.recent_colors().size() == ColorCommand::maximum_recent_colors &&
                color_command.recent_colors()[0] == Color::rgba(11, 0, 0) &&
                color_command.recent_colors()[9] == Color::rgba(2, 0, 0),
            "committed colours are recent, most recent first, the oldest falling off past ten");
    static_cast<void>(color_command.commit(Color::rgba(5, 0, 0), "test"));
    require(color_command.recent_colors()[0] == Color::rgba(5, 0, 0) && color_command.recent_colors().size() == 10U &&
                color_command.recent_colors()[1] == Color::rgba(11, 0, 0),
            "a recommitted colour moves to the front without repeating");
    static_cast<void>(color_command.commit(std::nullopt, "test"));
    require(!color_command.value() && color_command.recent_colors()[0] == Color::rgba(5, 0, 0),
            "Automatic is a value but not a recent colour");
    color_command.set_value(Color::rgba(1, 2, 3));
    require(color_runs == 14 && color_command.value() == Color::rgba(1, 2, 3), "set_value runs nothing");
}

void font_control() {
    std::shared_ptr<FontCommand> command = std::make_shared<FontCommand>(std::make_shared<Command>("font", "Font"));
    (*command).set_value({"Carlito", 12.0});
    std::shared_ptr<RibbonFontControl> control = make_control<RibbonFontControl>(StableId("font"));
    (*control).set_families({"Carlito", "Portsmouth", "Tahoma"});
    (*control).set_command(command);
    Window window(control, {200.0, 22.0});
    window.perform_layout();
    const std::shared_ptr<ComboBox> family = (*control).family_box();
    const std::shared_ptr<ComboBox> size = (*control).size_box();
    require((*family).selected_index() == std::size_t{0U} && (*size).selected_text() == "12",
            "the fields show the command's value");
    require((*control).parts().family_field.bounds == (*family).committed_arranged_bounds() &&
                (*control).parts().size_field.present,
            "the fields sit where the control's parts say");
    int runs = 0;
    SubscriptionToken counted = (*(*command).command()).invoked().subscribe(CountInvocation{&runs});
    (*family).set_selected_index(std::size_t{2U});
    require(runs == 1 && (*command).value().family == "Tahoma" && (*command).value().size == 12.0,
            "choosing a family commits it with the shown size");
    (*size).set_selected_index(std::size_t{0U});
    require(runs == 2 && (*command).value().size == 8.0, "choosing a size commits it with the shown family");
    (*command).set_value({"Unknown face", 10.5});
    require(!(*family).selected_index() && (*size).selected_text() == "10.5" && runs == 2,
            "a value outside the lists shows that field empty; the program's change runs nothing");
    (*(*command).command()).set_enabled(false);
    require(!(*family).enabled.read() && !(*size).enabled.read(), "a disabled command disables both fields");
    bool threw = false;
    try {
        (*control).set_sizes({10.0, 9.0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "sizes that do not ascend are refused");
    threw = false;
    try {
        (*control).set_families({"Twice", "Twice"});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*control).families().size() == 3U, "repeated families are refused");
}

void color_picker() {
    std::shared_ptr<ColorCommand> command = std::make_shared<ColorCommand>(std::make_shared<Command>("color", "Colour"));
    std::shared_ptr<RibbonColorPicker> picker = make_control<RibbonColorPicker>(StableId("picker"));
    (*picker).set_command(command);
    (*picker).set_text("Font &colour");
    (*picker).set_image(ImageId{4U});
    Window window(picker, {300.0, 300.0});
    window.perform_layout();
    require((*picker).natural_size().width == 34.0 && (*picker).parts().arrow.present &&
                (*picker).parts().color_bar.present && (*picker).parts().icon.present,
            "the picker is a face with an icon over a colour bar, and an arrow");
    int runs = 0;
    SubscriptionToken counted = (*(*command).command()).invoked().subscribe(CountInvocation{&runs});
    click(window, centre((*picker).parts().arrow.bounds));
    require((*picker).palette_open() && (*picker).parts().arrow.states.contains(AnatomyState::open),
            "the arrow opens the palette");
    std::vector<RibbonColorPicker::PaletteEntry> entries = (*picker).realized_entries();
    require(entries.size() == 11U && entries[0].kind == RibbonPaletteEntryKind::automatic &&
                entries[0].parts.entry_label.present && entries[0].states.selected &&
                entries[3].color == Color::rgba(255, 192, 0),
            "the palette holds Automatic (selected while no colour is set) and the ten standard colours");
    require(entries[0].parts.entry_well.present && !entries[0].parts.entry_sample.present &&
                entries[3].parts.entry_well.present &&
                entries[3].parts.entry_sample.content_color == Color::rgba(255, 192, 0),
            "Automatic shows an empty well; each colour a fill with its content colour inside its well");
    FillRecorder palette;
    static_cast<void>(window.paint(palette, {0.0, 0.0, 300.0, 300.0}));
    require(filled(palette, entries[3].parts.entry_sample.bounds, Color::rgba(255, 192, 0)),
            "the recording painter sees each swatch's content colour");
    const Control::Ptr popup = (*picker).palette_popup();
    click(window, centre(to_window(*popup, entries[2].parts.entry_face.bounds)));
    require(!(*picker).palette_open() && runs == 1 && (*command).value() == Color::rgba(255, 0, 0) &&
                (*command).recent_colors().size() == 1U,
            "a click on a colour commits it and closes the palette");
    // Owner decision W-7: the samples are fills carrying content colours,
    // painted by role.
    const RibbonColorPicker::Parts own = (*picker).parts();
    require(own.color_sample.present && own.color_sample.content_color == Color::rgba(255, 0, 0) &&
                own.color_bar.present && !own.color_bar.content_color,
            "the colour bar is a well holding a fill whose content colour is the value");
    FillRecorder bar;
    static_cast<void>(window.paint(bar, {0.0, 0.0, 300.0, 300.0}));
    require(filled(bar, own.color_sample.bounds, Color::rgba(255, 0, 0)),
            "the recording painter sees the colour bar's content colour");
    click(window, centre((*picker).parts().face.bounds));
    require(runs == 2 && (*command).value() == Color::rgba(255, 0, 0), "the face applies the current colour again");
    require(window.request_focus(picker), "the picker takes focus");
    press_key(window, PhysicalKey::down);
    require((*picker).palette_open(), "Down opens the palette");
    entries = (*picker).realized_entries();
    require(entries.size() == 12U && entries[11].kind == RibbonPaletteEntryKind::recent && entries[2].states.selected,
            "the recent colours follow the palette, and the current colour is selected");
    press_key(window, PhysicalKey::home);
    press_key(window, PhysicalKey::down);
    entries = (*picker).realized_entries();
    bool palette_focus = false;
    for (const RibbonColorPicker::PaletteEntry& entry : entries) {
        if (entry.kind == RibbonPaletteEntryKind::palette && entry.states.focus_visible) palette_focus = true;
    }
    require(palette_focus, "Down from Automatic reaches the palette row below it");
    press_key(window, PhysicalKey::home);
    for (int step = 0; step < 3; ++step) press_key(window, PhysicalKey::right);
    press_key(window, PhysicalKey::enter);
    require(!(*picker).palette_open() && (*command).value() == Color::rgba(255, 192, 0) && runs == 3,
            "the arrows move through the palette and Enter commits");
    press_key(window, PhysicalKey::down);
    press_key(window, PhysicalKey::escape);
    require(!(*picker).palette_open() && runs == 3 && window.focused_control() == picker,
            "Escape closes the palette without a choice and returns focus");
    const SemanticDescriptor semantics = (*picker).semantic_descriptor();
    require(semantics.role == SemanticRole::split_button && semantics.name == "Font colour" &&
                semantics.value == "#FFC000" && semantics.has_popup,
            "to accessibility the picker is a split button whose value is the colour");
    bool threw = false;
    try {
        (*picker).set_palette({Color::rgba(1, 1, 1), Color::rgba(1, 1, 1)});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*picker).palette().size() == 10U, "a palette that repeats a colour is refused");
}

std::shared_ptr<Ribbon> make_ribbon(const std::shared_ptr<RibbonFontControl>& font,
                                     const std::shared_ptr<RibbonColorPicker>& colour) {
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec group{.stable_id = "font-group", .caption = "Font"};
    group.commands.push_back({.stable_id = "font-control", .kind = RibbonCommandKind::font_control,
                              .text = "Font", .font_control = font});
    group.commands.push_back({.stable_id = "colour", .kind = RibbonCommandKind::color_picker, .text = "Colour",
                              .color_picker = colour});
    group.commands.push_back({.stable_id = "bold", .kind = RibbonCommandKind::toggle,
                              .size = RibbonCommandSize::small, .command = std::make_shared<Command>("bold", "Bold"),
                              .image = ImageId{2U}, .show_label = false});
    home.groups.push_back(std::move(group));
    RibbonTabSpec view{.stable_id = "view", .text = "&View"};
    RibbonGroupSpec zoom{.stable_id = "zoom", .caption = "Zoom"};
    zoom.commands.push_back({.stable_id = "zoom-in", .command = std::make_shared<Command>("zoom-in", "Zoom in")});
    view.groups.push_back(std::move(zoom));
    (*ribbon).set_tabs({std::move(home), std::move(view)});
    return ribbon;
}

void hosted_in_ribbon() {
    std::shared_ptr<RibbonFontControl> font = make_control<RibbonFontControl>(StableId("ribbon.font"));
    std::shared_ptr<RibbonColorPicker> colour = make_control<RibbonColorPicker>(StableId("ribbon.colour"));
    std::shared_ptr<Ribbon> ribbon = make_ribbon(font, colour);
    Window window(ribbon, {900.0, 300.0});
    window.perform_layout();
    const std::vector<Ribbon::CommandItem> commands = (*ribbon).realized_commands();
    require(commands.size() == 3U && commands[0].parts.command_font_control.present &&
                commands[0].parts.command_font_control.bounds == (*font).committed_arranged_bounds() &&
                commands[1].parts.command_color_picker.bounds == (*colour).committed_arranged_bounds() &&
                commands[0].parts.command_font_control.bounds.width == (*font).natural_size().width,
            "the font control and the colour picker sit on the ribbon's rows at their own widths");
    require((*font).parent() == ribbon && (*colour).parent() == ribbon, "the ribbon adds them as its children");
    const std::vector<RibbonLadderEntry> ladder = (*ribbon).reduction_ladder(0U);
    window.resize({ladder.back().width, 300.0});
    window.perform_layout();
    require((*ribbon).show_group_popup("font-group"), "the group collapses and opens in its popup");
    window.perform_layout();
    require((*font).parent() == (*ribbon).group_popup() && (*colour).parent() == (*ribbon).group_popup() &&
                (*font).effectively_visible() && (*colour).effectively_visible(),
            "in the popup they move with the group");
    (*ribbon).close_group_popup();
    require((*font).parent() == ribbon, "and return when it closes");
    std::vector<RibbonTabSpec> twice = std::vector<RibbonTabSpec>((*ribbon).tabs().begin(), (*ribbon).tabs().end());
    twice[1].groups[0].commands.push_back(
        {.stable_id = "again", .kind = RibbonCommandKind::font_control, .font_control = font});
    bool threw = false;
    try {
        (*ribbon).set_tabs(std::move(twice));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a control used twice is refused");
    std::vector<RibbonTabSpec> missing = std::vector<RibbonTabSpec>((*ribbon).tabs().begin(), (*ribbon).tabs().end());
    missing[1].groups[0].commands.push_back({.stable_id = "none", .kind = RibbonCommandKind::color_picker});
    threw = false;
    try {
        (*ribbon).set_tabs(std::move(missing));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a colour picker command without its picker is refused");
}

void recent_pane() {
    std::shared_ptr<Ribbon> ribbon = make_ribbon(make_control<RibbonFontControl>(StableId("ribbon.font")),
                                                 make_control<RibbonColorPicker>(StableId("ribbon.colour")));
    std::shared_ptr<Command> open = std::make_shared<Command>("open", "&Open");
    std::shared_ptr<Command> save = std::make_shared<Command>("save", "&Save");
    (*ribbon).set_application_menu({MenuItemSpec("file.open", MenuItemKind::command, open),
                                    MenuItemSpec("file.rule", MenuItemKind::separator),
                                    MenuItemSpec("file.save", MenuItemKind::command, save)});
    (*ribbon).set_recent_items({{"recent.report", "Report.txt", "C:\\Documents"},
                                {"recent.notes", "Notes.txt", "C:\\Documents"}});
    Window window(ribbon, {900.0, 400.0});
    window.perform_layout();
    std::vector<std::string> recent;
    SubscriptionToken recorded = (*ribbon).recent_item_invoked().subscribe(RecordRecent{&recent});
    std::vector<std::string> menu;
    SubscriptionToken menu_recorded = (*ribbon).application_menu_item_invoked().subscribe(RecordMenu{&menu});
    int saves = 0;
    SubscriptionToken saved = (*save).invoked().subscribe(CountInvocation{&saves});
    click(window, centre((*ribbon).parts().application_button.bounds));
    require((*ribbon).menu_open() && (*ribbon).parts().application_button.states.contains(AnatomyState::open),
            "with recent items the File tab opens the two-pane menu");
    const Control::Ptr popup = (*ribbon).application_menu_popup();
    std::vector<SemanticNode> rows = (*popup).semantic_virtual_children();
    require(rows.size() == 4U && rows[0].name == "Open" && rows[1].name == "Save" && rows[2].name == "1 Report.txt" &&
                rows[2].description == "C:\\Documents" && rows[3].position_in_set == std::size_t{2U} &&
                rows[3].set_size == std::size_t{2U},
            "the commands sit beside the numbered recent items, each positioned in its pane");
    require(rows[2].bounds.x > rows[0].bounds.x + rows[0].bounds.width - 1.0 &&
                rows[0].bounds.y >= (*ribbon).parts().application_button.bounds.height,
            "the recent pane is right of the commands, and the panel opens below the File tab");
    click(window, centre(rows[3].bounds));
    require(!(*ribbon).menu_open() && recent.size() == 1U && recent[0] == "recent.notes",
            "a click on a recent item reports it and closes the menu");
    require((*ribbon).show_application_menu(MenuOpenMode::keyboard), "the menu opens by keyboard");
    window.perform_layout();
    press_key(window, PhysicalKey::down);
    press_key(window, PhysicalKey::enter);
    require(saves == 1 && menu.size() == 1U && menu[0] == "file.save" && !(*ribbon).menu_open(),
            "Down skips the separator and Enter runs the command, with the application menu's event");
    require((*ribbon).show_application_menu(MenuOpenMode::keyboard), "the menu opens again");
    window.perform_layout();
    press_key(window, PhysicalKey::right);
    press_key(window, PhysicalKey::enter);
    require(recent.size() == 2U && recent[1] == "recent.report", "Right moves to the recent pane");
    require((*ribbon).show_application_menu(MenuOpenMode::pointer), "and by pointer");
    window.perform_layout();
    press_key(window, PhysicalKey::escape);
    require(!(*ribbon).menu_open(), "Escape closes it");
    require((*ribbon).show_application_menu(MenuOpenMode::pointer), "and again");
    window.perform_layout();
    click(window, {880.0, 390.0});
    require(!(*ribbon).menu_open(), "a press away closes it");
    bool threw = false;
    try {
        (*ribbon).set_application_menu({MenuItemSpec("file.check", MenuItemKind::check, open)});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*ribbon).application_menu().size() == 3U,
            "beside recent items the application menu takes commands and separators only");
    (*ribbon).set_recent_items({});
    click(window, centre((*ribbon).parts().application_button.bounds));
    require((*ribbon).menu_open(),
            "without recent items the File tab opens the plain menu");
    (*ribbon).close_menus();
}

void backstage() {
    std::shared_ptr<Ribbon> ribbon = make_ribbon(make_control<RibbonFontControl>(StableId("ribbon.font")),
                                                 make_control<RibbonColorPicker>(StableId("ribbon.colour")));
    std::shared_ptr<BackstageView> view = make_control<BackstageView>(StableId("backstage"));
    std::shared_ptr<Command> save = std::make_shared<Command>("save", "&Save");
    std::shared_ptr<Command> close = std::make_shared<Command>("close", "&Close");
    std::shared_ptr<Label> info = make_control<Label>(StableId("backstage.info"), "Information about the document");
    std::shared_ptr<Button> print_button = make_control<Button>(StableId("backstage.print.go"), "Print");
    (*view).set_items({{.stable_id = "save", .command = save, .image = ImageId{1U}},
                       {.stable_id = "info", .kind = BackstageItemKind::topic, .text = "&Info", .page = info},
                       {.stable_id = "print", .kind = BackstageItemKind::topic, .text = "&Print", .page = print_button},
                       {.stable_id = "close", .command = close, .definitive = true}});
    (*ribbon).set_backstage(view);
    Window window(ribbon, {900.0, 500.0});
    window.perform_layout();
    std::vector<std::string> topics;
    SubscriptionToken recorded = (*view).topic_changed().subscribe(RecordTopic{&topics});
    int saves = 0;
    int closes = 0;
    SubscriptionToken saved = (*save).invoked().subscribe(CountInvocation{&saves});
    SubscriptionToken closed = (*close).invoked().subscribe(CountInvocation{&closes});
    require((*view).selected_topic() == std::size_t{1U}, "the first topic is selected");
    click(window, centre((*ribbon).parts().application_button.bounds));
    require((*ribbon).backstage_open() && (*ribbon).parts().application_button.states.contains(AnatomyState::open),
            "the File tab opens Backstage");
    const Rect bounds = (*view).committed_arranged_bounds();
    require(bounds.y == 24.0 && bounds.height == 476.0 && bounds.width == 900.0,
            "Backstage covers everything below the tab row");
    require((*info).effectively_visible() && !(*print_button).effectively_visible() &&
                (*info).committed_arranged_bounds().x == (*view).parts().page.bounds.x,
            "the selected topic's page shows beside the column");
    std::vector<BackstageView::Item> items = (*view).realized_items();
    require(items.size() == 4U && items[1].states.selected && items[0].parts.item_icon.present,
            "the column lists its commands and topics");
    click(window, centre(to_window(*view, items[2].parts.item_face.bounds)));
    require(topics.size() == 1U && topics[0] == "print" && (*print_button).effectively_visible() &&
                !(*info).effectively_visible(),
            "a click on a topic shows its page");
    click(window, centre(to_window(*view, items[0].parts.item_face.bounds)));
    require(saves == 1 && (*ribbon).backstage_open(), "a fast command runs and Backstage stays");
    click(window, centre(to_window(*view, items[3].parts.item_face.bounds)));
    require(closes == 1 && !(*ribbon).backstage_open(), "a definitive command runs and closes Backstage");

    require(window.request_focus(ribbon), "the ribbon takes focus");
    require((*ribbon).show_application_menu(), "the keyboard opens Backstage");
    window.perform_layout();
    require(window.focused_control() == view, "focus moves into Backstage");
    press_key(window, PhysicalKey::home);
    press_key(window, PhysicalKey::enter);
    require(saves == 2, "Home and Enter run the column's first command");
    press_key(window, PhysicalKey::escape);
    require(!(*ribbon).backstage_open() && window.focused_control() == ribbon,
            "Escape returns to the document, focus back on the ribbon");
    require((*ribbon).show_backstage(), "Backstage opens again");
    window.perform_layout();
    click(window, centre((*ribbon).parts().application_button.bounds));
    require(!(*ribbon).backstage_open(), "the File tab closes it");
    require((*ribbon).show_backstage(), "and again");
    window.perform_layout();
    click(window, centre((*ribbon).realized_tabs()[1].parts.tab.bounds));
    require(!(*ribbon).backstage_open() && (*ribbon).selected_tab() == std::size_t{1U} && !(*ribbon).minimized(),
            "a tab closes it and selects that tab");
    require((*ribbon).show_backstage(), "Backstage reopens");
    window.perform_layout();
    window.resize({700.0, 400.0});
    window.perform_layout();
    require((*view).committed_arranged_bounds().width == 700.0 &&
                (*view).committed_arranged_bounds().height == 376.0,
            "Backstage follows the window's size");
    const std::vector<SemanticNode> nodes = (*view).semantic_virtual_children();
    require(nodes.size() == 4U && nodes[2].role == SemanticRole::tab && nodes[0].role == SemanticRole::button &&
                nodes[2].name == "Print" && nodes[0].name == "Save",
            "topics are tabs and commands buttons to accessibility");
    (*ribbon).close_backstage();
    bool threw = false;
    try {
        (*view).set_items({{.stable_id = "topic", .kind = BackstageItemKind::topic, .text = "Topic"}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*view).items().size() == 4U, "a topic without a page is refused");
}

// The palette's keyboard entry outlives the entries when the recent colours
// are cleared while the palette is open; the arrows then move from the last
// entry that remains.
void color_palette_shrinks_while_open() {
    std::shared_ptr<ColorCommand> command = std::make_shared<ColorCommand>(std::make_shared<Command>("ink", "Ink"));
    static_cast<void>((*command).commit(Color::rgba(1, 2, 3), "test"));
    static_cast<void>((*command).commit(Color::rgba(4, 5, 6), "test"));
    std::shared_ptr<RibbonColorPicker> picker = make_control<RibbonColorPicker>(StableId("shrinking"));
    (*picker).set_command(command);
    Window window(picker, {300.0, 300.0});
    window.perform_layout();
    require(window.request_focus(picker), "the picker takes focus");
    press_key(window, PhysicalKey::down);
    press_key(window, PhysicalKey::end);
    require((*picker).palette_open() && (*picker).realized_entries().size() == 13U &&
                (*picker).realized_entries()[12].states.focus_visible,
            "End puts the keyboard on the last recent colour");
    (*command).clear_recent_colors();
    press_key(window, PhysicalKey::up);
    const std::vector<RibbonColorPicker::PaletteEntry> entries = (*picker).realized_entries();
    require(entries.size() == 11U && entries[0].states.focus_visible,
            "Up from past the remaining entries moves from the last one, to Automatic above the palette row");
    (*picker).close_palette();
}

// A control whose maximum size is narrower than the bounds its parent offers
// is arranged at its maximum width: its child controls sit on its published
// parts, which paint and hit testing use.
void arranged_within_maximum_size() {
    std::shared_ptr<BackstageView> view = make_control<BackstageView>(StableId("limited.backstage"));
    std::shared_ptr<Label> info = make_control<Label>(StableId("limited.backstage.info"), "Information");
    (*view).set_items({{.stable_id = "info", .kind = BackstageItemKind::topic, .text = "Info", .page = info}});
    (*view).maximum_size = {300.0, 0.0};
    Window backstage_window(view, {900.0, 400.0});
    backstage_window.perform_layout();
    require((*view).committed_arranged_bounds().width == 300.0 &&
                (*info).committed_arranged_bounds() == (*view).parts().page.bounds,
            "a Backstage page sits on the published page part within the view's maximum width");

    std::shared_ptr<RibbonFontControl> font = make_control<RibbonFontControl>(StableId("limited.font"));
    (*font).maximum_size = {100.0, 0.0};
    Window font_window(font, {200.0, 22.0});
    font_window.perform_layout();
    require((*font).committed_arranged_bounds().width == 100.0 &&
                (*(*font).family_box()).committed_arranged_bounds() == (*font).parts().family_field.bounds,
            "a font control's family field sits on its published part within the control's maximum width");
}

} // namespace

int main() {
    value_commands();
    font_control();
    color_picker();
    hosted_in_ribbon();
    recent_pane();
    backstage();
    arranged_within_maximum_size();
    color_palette_shrinks_while_open();
    std::cout << "Ribbon file area: value commands, font control, colour picker, hosting, recent pane and Backstage "
                 "passed\n";
    return 0;
}
