// Menus on Popup windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part
// S), on the headless reference host: the menu-handling defect of
// 2026-11-09, where a menu opened as a host window much larger than the
// menu, painted with the theme's window material round the panel.
//
// - A menu panel's Popup window is exactly the panel's face plus the
//   overlay shadow's outsets while popups are translucent, and the face
//   alone (no shadow) while they are opaque: before any compositor report,
//   in high contrast and with reduced transparency.
// - The face drops from its invoker: a menu strip item's lower left corner,
//   on the desktop, through the owner's client origin; a submenu opens at
//   the parent's right edge level with its row.
// - The Popup window's backplane is clear round the panel: no theme window
//   material and no theme backdrop chain.
// - Rows take the menu strip's `menu_item` part, so the engine's built-in
//   rule gives them the designed hover (the dithered bulge rising while
//   hot) and a row whose submenu shows the open plate.
// - A panel is as wide as its rows need, shortcut column included.
// - The keyboard drives the Popups: Down, Right opens the submenu's Popup,
//   Left closes it, Escape closes the menu and hides every Popup.
// - The ribbon's two-pane application menu shows its panel in the picker
//   Popup, and its submenu rows open their items in a menu Popup beside
//   the row; a submenu command runs with the application menu's event and
//   closes both.
#include "gui_forms/application.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/menu_strip.hpp"
#include "gui_forms/controls/ribbon.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "application/headless_application.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) noexcept {
    const bool close = std::abs(left - right) < 0.001;
    return close;
}

struct KeepReady final {
    ApplicationWindowHandle* handle{};
    Window** model{};
    void operator()(Window& window, ApplicationWindowHandle value) const {
        *handle = value;
        *model = &window;
    }
};

// A compositing desktop that grants the transparent backdrop.
CompositorState compositing_desktop() {
    CompositorState state{};
    state.compositing = CompositorPresence::present;
    state.support[static_cast<std::size_t>(WindowBackdrop::transparent)] = WindowBackdropSupport::supported;
    state.transparency_enabled = true;
    return state;
}

// The primary window "main", 800 x 600 at 100,100 on the headless work area
// (0,0 1920 x 1040), holding a menu strip across its top.
struct Desk final {
    ApplicationWindowHandle main{};
    Window* model{};
    std::shared_ptr<MenuStrip> strip{};
    std::shared_ptr<Ribbon> ribbon{};
    std::unique_ptr<host::HeadlessApplication> application{};

    explicit Desk(const bool compositing) {
        std::vector<ApplicationWindow> windows;
        ApplicationWindow primary{};
        primary.stable_id = "main";
        std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
        strip = make_control<MenuStrip>(StableId("strip"));
        (*strip).set_requested_bounds(Rect{0.0, 0.0, 800.0, 26.0});
        (*root).add_child(strip);
        ribbon = make_control<Ribbon>(StableId("ribbon"));
        RibbonTabSpec home{.stable_id = "ribbon.home", .text = "&Home"};
        RibbonGroupSpec group{.stable_id = "ribbon.document", .caption = "Document"};
        group.commands.push_back({.stable_id = "ribbon.new", .command = std::make_shared<Command>("new", "New")});
        home.groups.push_back(std::move(group));
        (*ribbon).set_tabs({std::move(home)});
        (*ribbon).set_requested_bounds(Rect{0.0, 30.0, 800.0, 140.0});
        (*root).add_child(ribbon);
        primary.model = std::make_unique<Window>(root, Size{800.0, 600.0});
        primary.options.title = "main";
        primary.options.initial_size = {800.0, 600.0};
        primary.options.ready = KeepReady{&main, &model};
        windows.push_back(std::move(primary));
        application = std::make_unique<host::HeadlessApplication>(std::move(windows));
        require(model != nullptr && main.active(), "the main window is ready");
        if (compositing) (*application).simulate_compositor_state(compositing_desktop());
        (*model).perform_layout();
    }
    host::HeadlessWindowing& windowing() { return (*application).windowing(); }
    // The visible Popup windows, back to front.
    std::vector<HostWindowId> popups() {
        std::vector<HostWindowId> shown;
        const std::vector<HostWindowId> order = windowing().z_order();
        for (std::size_t index = order.size(); index > 0U; --index) {
            const HostWindowId id = order[index - 1U];
            if (id != main.id() && windowing().native_visible(id)) shown.push_back(id);
        }
        return shown;
    }
    Rect client() {
        const std::optional<Rect> content = windowing().native_content(main.id());
        require(content.has_value(), "the main window is placed");
        return *content;
    }
};

// The menu's commands, kept alive by the test.
struct Commands final {
    std::shared_ptr<Command> open = std::make_shared<Command>("file.open", "Open");
    std::shared_ptr<Command> save = std::make_shared<Command>("file.save", "Save");
    std::shared_ptr<Command> tile = std::make_shared<Command>("file.tile", "Tile");
    std::shared_ptr<Command> fill = std::make_shared<Command>("file.fill", "Fill");
    std::shared_ptr<Command> quit = std::make_shared<Command>("file.quit", "Exit");
    Commands() {
        (*save).set_shortcut("Ctrl+Shift+Alt+S");
    }
};

// File: Open, Save (with a long shortcut), a separator, a Background
// submenu (Tile, Fill), Exit.
std::vector<MenuStripItemSpec> strip_items(const Commands& commands) {
    std::vector<MenuItemSpec> background;
    background.emplace_back("file.background.tile", MenuItemKind::command, commands.tile);
    background.emplace_back("file.background.fill", MenuItemKind::command, commands.fill);
    std::vector<MenuItemSpec> file;
    file.emplace_back("file.open", MenuItemKind::command, commands.open);
    file.emplace_back("file.save", MenuItemKind::command, commands.save);
    file.emplace_back("file.rule", MenuItemKind::separator);
    file.emplace_back("file.background", MenuItemKind::submenu, std::shared_ptr<Command>{}, "Background",
                      std::move(background));
    file.emplace_back("file.quit", MenuItemKind::command, commands.quit);
    std::vector<MenuStripItemSpec> items;
    MenuStripItemSpec entry{};
    entry.stable_id = "file";
    entry.text = "&File";
    entry.items = std::move(file);
    items.push_back(std::move(entry));
    return items;
}

KeyEvent key_down(std::uint32_t key) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = key;
    return event;
}

// The panel of `depth` in its Popup window's model.
std::shared_ptr<Control> panel_of(Window& owner, const std::size_t depth) {
    Window* const popup = owner.popup_window_model(depth);
    require(popup != nullptr, "the depth's Popup window shows");
    const std::string id = "strip.menu.overlay.panel." + std::to_string(depth);
    std::shared_ptr<Control> panel = (*popup).find(id);
    require(panel != nullptr, "the panel is in its Popup window");
    return panel;
}

// 28-unit rows, a 9-unit separator and the panel's two 2-unit borders.
constexpr double file_panel_height = 4.0 * 28.0 + 9.0 + 4.0;

void a_translucent_popup_is_the_face_plus_its_shadow() {
    Desk desk(true);
    Commands commands;
    (*desk.strip).set_items(strip_items(commands));
    require((*desk.model).popup_windows_translucent(), "a compositing desktop has translucent popups");
    require((*desk.strip).open(0U, MenuOpenMode::keyboard), "the File menu opens");
    const std::vector<HostWindowId> shown = desk.popups();
    require(shown.size() == 1U, "the menu's panel has one Popup window");
    const std::optional<Rect> window = desk.windowing().native_content(shown[0]);
    require(window.has_value(), "the Popup window is placed");
    const std::shared_ptr<Control> panel = panel_of(*desk.model, 0U);
    const Insets outsets = overlay_shadow_outsets(*panel);
    require(outsets.left > 0.0 && outsets.bottom > 0.0, "the panel casts the overlay shadow");
    const double face_width = (*panel).requested_bounds().width;
    require(near((*window).width, face_width + outsets.left + outsets.right),
            "the window is as wide as the face plus the shadow's outsets");
    require(near((*window).height, file_panel_height + outsets.top + outsets.bottom),
            "the window is as tall as the face plus the shadow's outsets");
    require(near((*panel).requested_bounds().x, outsets.left), "the face sits inside the shadow's outsets");
    // Hung from the File item's lower left corner (the strip's left edge;
    // its bottom less one unit, as MenuStrip opens it), through the owner's
    // client origin.
    const Rect strip = (*desk.strip).absolute_bounds();
    const Rect client = desk.client();
    require(near((*window).x + outsets.left, client.x + strip.x), "the face's left edge is the item's");
    require(near((*window).y + outsets.top, client.y + strip.y + strip.height - 1.0),
            "the face's top edge is the item's bottom");
    Window* const popup = (*desk.model).popup_window_model(0U);
    require((*popup).client_size() == Size{(*window).width, (*window).height}, "the model is the window's size");
    require((*popup).content_surface_declared() && (*popup).backdrop_grant().granted == WindowBackdrop::transparent,
            "the Popup window is a clear content surface over the transparent backdrop");
    (*desk.strip).close();
}

void an_opaque_popup_is_the_face_alone() {
    // No compositor report: nothing but opaque can be granted.
    {
        Desk desk(false);
        Commands commands;
        (*desk.strip).set_items(strip_items(commands));
        require(!(*desk.model).popup_windows_translucent(), "without a compositor popups are opaque");
        require((*desk.strip).open(0U, MenuOpenMode::keyboard), "the File menu opens");
        const std::vector<HostWindowId> shown = desk.popups();
        require(shown.size() == 1U, "the menu's panel has one Popup window");
        const std::optional<Rect> window = desk.windowing().native_content(shown[0]);
        const std::shared_ptr<Control> panel = panel_of(*desk.model, 0U);
        require(near((*window).width, (*panel).requested_bounds().width) && near((*window).height, file_panel_height),
                "an opaque Popup window holds the face alone, with no room for a shadow");
        const Rect client = desk.client();
        const Rect strip = (*desk.strip).absolute_bounds();
        require(near((*window).x, client.x + strip.x), "the face's left edge is still the item's");
        (*desk.strip).close();
    }
    // High contrast withdraws the transparent backdrop.
    {
        Desk desk(true);
        CompositorState contrast = compositing_desktop();
        contrast.high_contrast = true;
        (*desk.application).simulate_compositor_state(contrast);
        require(!(*desk.model).popup_windows_translucent(), "high contrast makes popups opaque");
    }
    // So does reduced transparency.
    {
        Desk desk(true);
        CompositorState reduced = compositing_desktop();
        reduced.reduced_transparency = true;
        reduced.transparency_enabled = false;
        (*desk.application).simulate_compositor_state(reduced);
        require(!(*desk.model).popup_windows_translucent(), "reduced transparency makes popups opaque");
    }
}

void the_popup_backplane_is_clear_round_the_panel() {
    Desk desk(true);
    Commands commands;
    (*desk.strip).set_items(strip_items(commands));
    require((*desk.strip).open(0U, MenuOpenMode::keyboard), "the File menu opens");
    Window* const popup = (*desk.model).popup_window_model(0U);
    require(popup != nullptr, "the Popup window shows");
    // A content surface over the transparent grant: its backplane is clear
    // (no theme window material) and the theme's window backdrop chain does
    // not reach it, so only the panel and its shadow show.
    require((*popup).content_surface_declared(), "the Popup window is a content surface");
    require(!(*popup).backdrop_chain().has_value(), "the theme's window backdrop chain does not reach it");
    require((*popup).backdrop_grant().granted == WindowBackdrop::transparent, "the transparent backdrop is granted");
    require(!(*popup).declare_content_surface(), "a bound window cannot be declared a content surface again");
    (*desk.strip).close();
}

void rows_take_the_designed_hover() {
    const CompiledThemeRules rules =
        CompiledThemeRules::compile_with_built_in(ThemeRuleSheet{}, builtin_primitive_registry());
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "menu_strip";
    facts.stable_id = "subject";
    facts.part = "menu_item";
    facts.role = PartRole::face;
    const CompiledThemeRules::Resolution rest = rules.resolve(facts);
    require(rest.outcome == CompiledThemeRules::Outcome::primitive &&
                (*rest.primitive).descriptor().name == "dithered_bulge" && rest.transition_count == 1U,
            "a menu item is the dithered bulge with its rise (the designed hover)");
    AnatomyStateSet open{};
    open.insert(AnatomyState::open);
    facts.states = open;
    const CompiledThemeRules::Resolution plate = rules.resolve(facts);
    require(plate.outcome == CompiledThemeRules::Outcome::built_in_selected && plate.transition_count == 0U,
            "an item whose menu shows keeps the plate");
}

void panels_fit_their_rows() {
    Desk desk(true);
    Commands commands;
    std::vector<MenuStripItemSpec> items = strip_items(commands);
    items[0].items[0].text = "Open a picture from a folder far away on the network";
    (*desk.strip).set_items(std::move(items));
    require((*desk.strip).open(0U, MenuOpenMode::keyboard), "the File menu opens");
    const std::shared_ptr<Control> panel = panel_of(*desk.model, 0U);
    const double width = (*panel).requested_bounds().width;
    require(width > 268.0, "a long label widens the panel");
    const ResolvedTextLayout label = (*desk.model).resolve_text_layout_utf8(
        "Open a picture from a folder far away on the network", FontSpec{FontRole::control, 11.0, 400, false, 0.12});
    require(width >= 30.0 + label.logical_size.width + 25.0, "the label fits between the gutter and the chevron");
    (*desk.strip).close();
}

// Plan Paint's Paste menu (two short rows) and its three-row Fill/Tile/Center
// submenu were 268 units wide whatever their rows (the old default
// minimum, 532 px at 2x). A panel is as wide as its widest row, at least
// the 120-unit minimum.
void a_short_menu_is_as_wide_as_its_rows() {
    Desk desk(true);
    Commands commands;
    std::vector<MenuItemSpec> paste;
    paste.emplace_back("paste.here", MenuItemKind::command, commands.open, "Paste");
    // Commands without shortcuts: a shortcut column would widen the panel
    // (Save's is long).
    paste.emplace_back("paste.new", MenuItemKind::command, commands.tile, "Paste as new");
    std::vector<MenuItemSpec> placing;
    placing.emplace_back("place.fill", MenuItemKind::command, commands.fill, "Fill");
    placing.emplace_back("place.tile", MenuItemKind::command, commands.tile, "Tile");
    placing.emplace_back("place.center", MenuItemKind::command, commands.quit, "Center");
    std::vector<MenuItemSpec> short_items;
    short_items.emplace_back("short.paste", MenuItemKind::submenu, std::shared_ptr<Command>{}, "Paste",
                             std::move(paste));
    short_items.emplace_back("short.place", MenuItemKind::submenu, std::shared_ptr<Command>{}, "Place",
                             std::move(placing));
    std::vector<MenuStripItemSpec> items;
    MenuStripItemSpec entry{};
    entry.stable_id = "edit";
    entry.text = "&Edit";
    entry.items = std::move(short_items);
    items.push_back(std::move(entry));
    (*desk.strip).set_items(std::move(items));
    require((*desk.strip).open(0U, MenuOpenMode::keyboard), "the Edit menu opens");
    const double top_width = (*panel_of(*desk.model, 0U)).requested_bounds().width;
    require(top_width >= 120.0 && top_width < 200.0, "a two-row menu is as wide as its rows, at least the minimum");
    // A keyboard open focuses the first row, Paste.
    require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), "Down reaches Place");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::right)), "Right opens the Place submenu");
    require((*(*desk.model).popup_window_model(1U)).find("strip.menu.overlay.row.place.center") != nullptr,
            "the submenu is Place's");
    const double child_width = (*panel_of(*desk.model, 1U)).requested_bounds().width;
    require(child_width >= 120.0 && child_width < 200.0, "a three-row submenu is as wide as its rows");
    (*desk.strip).close();
}

void the_keyboard_drives_submenu_popups() {
    Desk desk(true);
    Commands commands;
    (*desk.strip).set_items(strip_items(commands));
    require((*desk.strip).open(0U, MenuOpenMode::keyboard), "the File menu opens");
    // Open, Save, (rule), Background: three Downs from Open.
    require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), "Down reaches the panel");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), "Down skips the rule");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::right)), "Right opens the submenu");
    require(desk.popups().size() == 2U, "the submenu shows in a second Popup window");
    const std::shared_ptr<Control> parent = panel_of(*desk.model, 0U);
    const std::shared_ptr<Control> child = panel_of(*desk.model, 1U);
    const std::optional<Rect> parent_window = desk.windowing().native_content(desk.popups()[0]);
    const std::optional<Rect> child_window = desk.windowing().native_content(desk.popups()[1]);
    const Insets parent_outsets = overlay_shadow_outsets(*parent);
    const Insets child_outsets = overlay_shadow_outsets(*child);
    const double parent_face_x = (*parent_window).x + parent_outsets.left;
    const double parent_face_y = (*parent_window).y + parent_outsets.top;
    const double child_face_x = (*child_window).x + child_outsets.left;
    const double child_face_y = (*child_window).y + child_outsets.top;
    require(near(child_face_x, parent_face_x + (*parent).requested_bounds().width - 3.0),
            "the submenu opens at the parent's right edge");
    // Background is the fourth row, after two rows and the rule.
    require(near(child_face_y, parent_face_y + 2.0 * 28.0 + 9.0),
            "the submenu's first row is level with the row that opened it");
    Window* const child_model = (*desk.model).popup_window_model(1U);
    require((*child_model).focused_control() == (*child_model).find("strip.menu.overlay.row.file.background.tile"),
            "the submenu's first command takes the menu's focus");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::left)), "Left closes the submenu");
    require(desk.popups().size() == 1U, "the submenu's Popup window hides");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), "Escape closes the menu");
    require(desk.popups().empty() && !(*desk.strip).is_open(), "every Popup window hides");
}

struct RecordInvoked final {
    std::vector<std::string>* items{};
    void operator()(const MenuItemInvocation& invocation) const { (*items).push_back(invocation.item_id); }
};

void the_two_pane_menu_lifts_and_opens_submenus() {
    Desk desk(true);
    Commands commands;
    std::vector<MenuItemSpec> background;
    background.emplace_back("file.background.tile", MenuItemKind::command, commands.tile);
    background.emplace_back("file.background.fill", MenuItemKind::command, commands.fill);
    std::vector<MenuItemSpec> file;
    file.emplace_back("file.open", MenuItemKind::command, commands.open);
    file.emplace_back("file.rule", MenuItemKind::separator);
    file.emplace_back("file.background", MenuItemKind::submenu, std::shared_ptr<Command>{}, "Background",
                      std::move(background));
    file.emplace_back("file.quit", MenuItemKind::command, commands.quit);
    (*desk.ribbon).set_application_menu(std::move(file));
    (*desk.ribbon).set_recent_items({{"recent.report", "Report.png", "C:\\Pictures"}});
    (*desk.model).perform_layout();
    std::vector<std::string> invoked;
    SubscriptionToken recorded = (*desk.ribbon).application_menu_item_invoked().subscribe(RecordInvoked{&invoked});
    require((*desk.ribbon).show_application_menu(MenuOpenMode::keyboard), "the two-pane menu opens");
    require(desk.popups().size() == 1U, "its panel shows in the picker Popup");
    const Control::Ptr catcher = (*desk.ribbon).application_menu_overlay();
    require(catcher != nullptr && !(*catcher).semantic_descriptor().exposed,
            "the overlay left in the owner is the panel's catcher");
    const std::optional<Rect> panel_window = desk.windowing().native_content(desk.popups()[0]);
    const Rect file_tab = (*desk.ribbon).parts().application_button.bounds;
    const Rect ribbon = (*desk.ribbon).absolute_bounds();
    const Rect client = desk.client();
    require(panel_window.has_value() && (*panel_window).y + 0.5 >= client.y + ribbon.y + file_tab.y + file_tab.height -
                                                                       overlay_shadow_outsets(*catcher).top - 20.0,
            "the panel hangs below the File tab");
    // Open is focused; Down skips the rule to Background; Right opens it.
    require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), "Down moves to the submenu row");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::right)), "Right opens the submenu");
    require(desk.popups().size() == 2U, "the submenu shows in a menu Popup beside the panel");
    const std::optional<Rect> submenu_window = desk.windowing().native_content(desk.popups()[1]);
    require(submenu_window.has_value() && (*submenu_window).x > (*panel_window).x + 100.0,
            "the submenu opens right of its row");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), "Escape closes the submenu");
    require(desk.popups().size() == 1U && (*desk.ribbon).menu_open(), "the panel stays open");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::right)), "Right opens the submenu again");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::enter)), "Enter runs its first command");
    require(invoked.size() == 1U && invoked[0] == "file.background.tile",
            "the submenu's command runs with the application menu's event");
    require(!(*desk.ribbon).menu_open() && desk.popups().empty(), "and both menus close");
    require((*desk.ribbon).show_application_menu(MenuOpenMode::keyboard), "the menu opens again");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), "Escape closes it");
    require(!(*desk.ribbon).menu_open() && desk.popups().empty(), "and hides its Popup");
    recorded.disconnect();
}

// One way of dismissing the application menu, by turn: Escape (for the
// two-pane menu twice: the submenu, then the menu), the File tab, a click away, the program, the
// owner window moving, and choosing the submenu's command.
enum class Dismissal : std::uint8_t { escape, file_tab, click_away, program, owner_moved, choose };
constexpr std::size_t dismissal_count = 6U;

// Opens the application menu `cycles` times, opens its Background submenu
// each time, and dismisses the menu a different way each time; after each
// close nothing is left behind (no menu, no submenu, no Popup shown, no
// focus scope) and the window count stays that of the first cycle (warm
// windows are reused, never stacked).
void cycle_application_menu(Desk& desk, const std::size_t cycles, const char* const kind) {
    const Rect ribbon = (*desk.ribbon).absolute_bounds();
    const Rect file_tab = (*desk.ribbon).parts().application_button.bounds;
    const Point file_centre{ribbon.x + file_tab.x + file_tab.width / 2.0, ribbon.y + file_tab.y + file_tab.height / 2.0};
    const Point away{700.0, 560.0};
    std::size_t windows_after_first = 0U;
    for (std::size_t cycle = 0U; cycle < cycles; ++cycle) {
        const std::string label = std::string(kind) + " cycle " + std::to_string(cycle) + ": ";
        const Dismissal dismissal = static_cast<Dismissal>(cycle % dismissal_count);
        if (cycle % 2U == 0U) {
            static_cast<void>((*desk.model).dispatch_pointer({PointerAction::down, PointerButton::primary, file_centre}));
            static_cast<void>((*desk.model).dispatch_pointer({PointerAction::up, PointerButton::primary, file_centre}));
        } else {
            require((*desk.ribbon).show_application_menu(MenuOpenMode::keyboard), label + "the menu opens");
        }
        (*desk.model).perform_layout();
        require((*desk.ribbon).menu_open() && desk.popups().size() == 1U, label + "the menu shows in one Popup");
        // Background is the first row: a menu opened by pointer focuses no
        // row, so Down reaches it first.
        if (cycle % 2U == 0U) static_cast<void>((*desk.model).dispatch_key(key_down(PhysicalKey::down)));
        static_cast<void>((*desk.model).dispatch_key(key_down(PhysicalKey::right)));
        (*desk.model).perform_layout();
        require(desk.popups().size() == 2U, label + "the submenu shows in a second Popup");
        if (dismissal == Dismissal::escape) {
            // The one-pane menu closes whole on Escape; the two-pane menu's
            // submenu closes first, leaving the panel.
            static_cast<void>((*desk.model).dispatch_key(key_down(PhysicalKey::escape)));
            if ((*desk.ribbon).menu_open()) {
                require(desk.popups().size() == 1U, label + "Escape closes the submenu first");
                static_cast<void>((*desk.model).dispatch_key(key_down(PhysicalKey::escape)));
            }
        } else if (dismissal == Dismissal::owner_moved) {
            (*desk.model).report_frame_moved();
        } else if (dismissal == Dismissal::choose) {
            static_cast<void>((*desk.model).dispatch_key(key_down(PhysicalKey::enter)));
        } else if (dismissal == Dismissal::file_tab) {
            static_cast<void>((*desk.model).dispatch_pointer({PointerAction::down, PointerButton::primary, file_centre}));
            static_cast<void>((*desk.model).dispatch_pointer({PointerAction::up, PointerButton::primary, file_centre}));
        } else if (dismissal == Dismissal::click_away) {
            static_cast<void>((*desk.model).dispatch_pointer({PointerAction::down, PointerButton::primary, away}));
            static_cast<void>((*desk.model).dispatch_pointer({PointerAction::up, PointerButton::primary, away}));
        } else {
            (*desk.ribbon).close_menus();
        }
        (*desk.model).perform_layout();
        require(!(*desk.ribbon).menu_open(), label + "the menu closes");
        require(desk.popups().empty(), label + "no Popup is left showing");
        require((*desk.model).active_focus_scope_root() == nullptr, label + "no focus scope is left open");
        if (cycle == 0U) windows_after_first = desk.windowing().window_count();
        require(desk.windowing().window_count() == windows_after_first, label + "no window is left behind");
    }
    require((*desk.ribbon).show_application_menu(MenuOpenMode::keyboard) && desk.popups().size() == 1U,
            std::string(kind) + ": the menu still opens after the cycles");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)) && !(*desk.ribbon).menu_open(),
            std::string(kind) + ": and still answers its keys");
}

// Background (Tile, Fill), Open, Exit: the submenu first.
std::vector<MenuItemSpec> submenu_first(const Commands& commands) {
    std::vector<MenuItemSpec> background;
    background.emplace_back("file.background.tile", MenuItemKind::command, commands.tile);
    background.emplace_back("file.background.fill", MenuItemKind::command, commands.fill);
    std::vector<MenuItemSpec> file;
    file.emplace_back("file.background", MenuItemKind::submenu, std::shared_ptr<Command>{}, "Background",
                      std::move(background));
    file.emplace_back("file.open", MenuItemKind::command, commands.open);
    file.emplace_back("file.quit", MenuItemKind::command, commands.quit);
    return file;
}

// Owner, 2026-11-10: the main menu opened three times and the program
// stopped responding. Both application menus, the one-pane menu and the
// two-pane menu beside recent items, open and close twelve times, by
// pointer and keyboard, dismissed by Escape, the File tab, a click away and
// the program.
void the_application_menu_opens_and_closes_repeatedly() {
    {
        Desk desk(true);
        Commands commands;
        (*desk.ribbon).set_application_menu(submenu_first(commands));
        (*desk.model).perform_layout();
        cycle_application_menu(desk, 12U, "one-pane");
    }
    {
        Desk desk(true);
        Commands commands;
        (*desk.ribbon).set_application_menu(submenu_first(commands));
        (*desk.ribbon).set_recent_items({{"recent.report", "Report.png", "C:\\Pictures"}});
        (*desk.model).perform_layout();
        cycle_application_menu(desk, 12U, "two-pane");
    }
}

// Moves the pointer to the middle of `row` in the menu's Popup window of
// `depth`.
void hover_row(Window& owner, const std::size_t depth, const char* const row) {
    Window* const popup = owner.popup_window_model(depth);
    require(popup != nullptr, "the depth's Popup window shows");
    const std::shared_ptr<Control> target = (*popup).find(std::string("strip.menu.overlay.row.") + row);
    require(target != nullptr, "the row is in its Popup window");
    const Rect bounds = (*target).absolute_bounds();
    const Point middle{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
    static_cast<void>((*popup).dispatch_pointer({PointerAction::move, PointerButton::none, middle}));
    (*popup).perform_layout();
}

// Owner, 2026-11-10 (Plan Paint, gdb2 to gdb4): hovering a submenu row,
// moving to a command row below it and back crashed in open_submenu on
// freed rows. Twelve times over: the submenu opens on its row, closes on
// the next, opens again on its row, each time in its own warm Popup window,
// and nothing of an earlier submenu is reached again (run under the
// sanitizer, a stale row is a reported error).
void hovering_a_submenu_row_away_and_back_reopens_it() {
    Desk desk(true);
    Commands commands;
    (*desk.strip).set_items(strip_items(commands));
    require((*desk.strip).open(0U, MenuOpenMode::pointer), "the File menu opens");
    std::size_t windows_after_first = 0U;
    for (std::size_t cycle = 0U; cycle < 12U; ++cycle) {
        const std::string label = "hover cycle " + std::to_string(cycle) + ": ";
        hover_row(*desk.model, 0U, "file.background");
        require(desk.popups().size() == 2U, label + "the submenu opens on its row");
        require((*desk.model).popup_window_model(1U) != nullptr &&
                    (*(*desk.model).popup_window_model(1U)).find("strip.menu.overlay.row.file.background.tile") != nullptr,
                label + "the submenu shows its rows");
        // Save, above: the panel may still be revealing, its last row clipped.
        hover_row(*desk.model, 0U, "file.save");
        require(desk.popups().size() == 1U && (*desk.strip).is_open(), label + "the next row closes the submenu");
        if (cycle == 0U) windows_after_first = desk.windowing().window_count();
        require(desk.windowing().window_count() == windows_after_first, label + "no window is left behind");
    }
    hover_row(*desk.model, 0U, "file.background");
    hover_row(*desk.model, 1U, "file.background.fill");
    require(desk.popups().size() == 2U, "the submenu's own rows take the hover");
    (*desk.model).report_frame_moved();
    require(!(*desk.strip).is_open() && desk.popups().empty(), "the owner moving closes the menu and its submenu");
}

// Contention fuzzer seed 1 (tools/contention_fuzz.py, replay: the File menu
// with its Background submenu open, then a button that shows a context menu
// activated through automation, past the menu's outside-press layer). The
// open chain's focus scope held the new menu outside it, begin_focus_scope
// threw out of the window procedure and the program died. A window shows
// one menu chain: the new menu closes the open one, both ways round.
void a_menu_opening_closes_the_open_chain() {
    Desk desk(true);
    Commands commands;
    (*desk.strip).set_items(strip_items(commands));
    std::shared_ptr<ContextMenu> context = std::make_shared<ContextMenu>("context");
    std::vector<MenuItemSpec> items;
    items.emplace_back("context.open", MenuItemKind::command, commands.open);
    items.emplace_back("context.save", MenuItemKind::command, commands.save);
    (*context).set_items(std::move(items));
    for (std::size_t cycle = 0U; cycle < 4U; ++cycle) {
        const std::string label = "cycle " + std::to_string(cycle) + ": ";
        require((*desk.strip).open(0U, MenuOpenMode::keyboard), label + "the File menu opens");
        require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), label + "Down reaches the panel");
        require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), label + "Down skips the rule");
        require((*desk.model).dispatch_key(key_down(PhysicalKey::right)), label + "Right opens the submenu");
        require(desk.popups().size() == 2U, label + "the submenu shows");
        (*context).show(desk.ribbon, Point{200.0, 200.0}, MenuOpenMode::keyboard);
        require((*context).is_open() && !(*desk.strip).is_open(), label + "the context menu replaces the File menu");
        require(desk.popups().size() == 1U, label + "only the context menu's Popup window shows");
        require((*desk.strip).open(0U, MenuOpenMode::keyboard), label + "the File menu opens over the context menu");
        require(!(*context).is_open() && (*desk.strip).is_open(), label + "the File menu replaces the context menu");
        require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), label + "Escape closes the File menu");
        require(desk.popups().empty() && !(*desk.strip).is_open(), label + "no Popup window is left");
    }
}

} // namespace

int main() {
    try {
        a_translucent_popup_is_the_face_plus_its_shadow();
        an_opaque_popup_is_the_face_alone();
        the_popup_backplane_is_clear_round_the_panel();
        rows_take_the_designed_hover();
        panels_fit_their_rows();
        a_short_menu_is_as_wide_as_its_rows();
        the_keyboard_drives_submenu_popups();
        the_two_pane_menu_lifts_and_opens_submenus();
        the_application_menu_opens_and_closes_repeatedly();
        hovering_a_submenu_row_away_and_back_reopens_it();
        a_menu_opening_closes_the_open_chain();
    } catch (const std::exception& error) {
        std::cerr << "menu popup tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "menu popup tests passed\n";
    return 0;
}
