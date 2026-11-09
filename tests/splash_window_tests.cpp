// Splash windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S,
// SPLASH_STUDY.md) on the headless reference host. A Splash is a popup-kind
// window that takes no input: the tooltip presenter shows its bubble in one
// kept per window (warm standby), opened hidden on first use and shown,
// moved and hidden after that; a run that cannot open one shows the overlay
// as before. A warm show allocates nothing: counted by this executable's
// own operator new (a test-only replacement, never in a library), with the
// headless host's trace (formatted on the heap) turned off.
//
// Also here: menus on warm Popup windows (each panel in the owner's Popup
// of its depth, keys through the owner, closed by an outside click),
// placed against the monitor's work area past the owner's edges, a
// ComboBox's list and a TextBox's suggestions lifted into the picker Popup
// (owner, 2026-11-08: Part S scope for 0.5), ErrorProvider's message in the
// Splash window, and the
// splashscreen shown before Application::create on a headless surface,
// handing over to the run's first window.

#include "gui_forms/application.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/components/error_provider/error_provider.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel/combo_box/combo_box.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"
#include "application/headless_application.hpp"
#include "application/headless_splashscreen.hpp"
#include "../src/core/image/png_writer.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool counting_allocations = false;
std::uint64_t counted_allocations = 0U;
// The sizes of the first counted allocations, printed when a warm show
// allocates (fixed storage: recording allocates nothing).
std::size_t counted_sizes[16]{};
} // namespace

void* operator new(std::size_t size) {
    if (counting_allocations) {
        if (counted_allocations < 16U) counted_sizes[counted_allocations] = size;
        ++counted_allocations;
    }
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}
void* operator new[](std::size_t size) {
    void* allocation = operator new(size);
    return allocation;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::string& text, const char* fragment) {
    const bool found = text.find(fragment) != std::string::npos;
    return found;
}

// A plain PNG artwork of `width` x `height` pixels.
std::vector<std::byte> artwork(const std::uint32_t width, const std::uint32_t height) {
    const std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U, std::byte{0xff});
    image::PngEncodeInput input{};
    input.width = width;
    input.height = height;
    input.row_bytes = static_cast<std::uint64_t>(width) * 4U;
    input.pixels = std::span<const std::byte>(pixels.data(), pixels.size());
    input.order = image::PngChannelOrder::bgra;
    input.alpha = image::PngSourceAlpha::premultiplied;
    std::vector<std::byte> encoded{};
    require(image::encode_png(input, encoded), "the artwork encodes");
    return encoded;
}

struct KeepReady final {
    ApplicationWindowHandle* handle{};
    Window** model{};
    void operator()(Window& window, ApplicationWindowHandle value) const {
        *handle = value;
        *model = &window;
    }
};

// The primary window "main", 800 x 600 at 100,100 on the headless work
// area (0,0 1920 x 1040), with one control that has a tooltip.
struct Desk final {
    ApplicationWindowHandle main{};
    Window* model{};
    std::shared_ptr<Control> anchor{};
    std::unique_ptr<host::HeadlessApplication> application{};

    Desk() {
        std::vector<ApplicationWindow> windows;
        ApplicationWindow primary{};
        primary.stable_id = "main";
        std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
        anchor = make_control<Control>(StableId("anchor"));
        (*anchor).set_requested_bounds(Rect{10.0, 10.0, 80.0, 24.0});
        (*anchor).tool_tip_text = "Saves the document";
        (*root).add_child(anchor);
        primary.model = std::make_unique<Window>(root, Size{800.0, 600.0});
        primary.options.title = "main";
        primary.options.initial_size = {800.0, 600.0};
        primary.options.ready = KeepReady{&main, &model};
        windows.push_back(std::move(primary));
        application = std::make_unique<host::HeadlessApplication>(std::move(windows));
        require(model != nullptr && main.active(), "the main window is ready");
        (*model).perform_layout();
    }
    host::HeadlessWindowing& windowing() { return (*application).windowing(); }
    // The one window of the run other than main, or none.
    std::optional<HostWindowId> splash() {
        const std::vector<HostWindowId> order = windowing().z_order();
        for (const HostWindowId id : order) {
            if (id != main.id()) return id;
        }
        return std::nullopt;
    }
};

void tooltip_shows_in_a_splash_window() {
    Desk desk;
    desk.windowing().clear_trace();
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    require((*desk.model).tool_tip_visible(), "the tooltip shows");
    const std::optional<HostWindowId> splash = desk.splash();
    require(splash.has_value() && desk.windowing().window_count() == 2U, "the tooltip has its own window");
    require(desk.windowing().kind(*splash) == HostWindowKind::popup, "a Splash is a popup-kind window");
    require(desk.windowing().owner(*splash) == desk.main.id(), "the window that shows it owns it");
    const std::string& trace = desk.windowing().trace();
    require(contains(trace, "transient=tool_tip") && contains(trace, "focus=passive"),
            "the host is told it is a tooltip Splash, never focused");
    require(contains(trace, "visible=0"), "it opens hidden (warm standby) and is shown after");
    require(desk.windowing().native_visible(*splash), "it shows");
    const std::optional<Rect> content = desk.windowing().native_content(*splash);
    require(content.has_value(), "it is placed");
    // Below the control: the face at the owner's client origin (100,100)
    // plus the anchor's (10,10) plus (8, 24 + 7), less the shadow's outsets.
    require((*content).x < 118.0 && (*content).y < 141.0 && (*content).x > 80.0 && (*content).y > 100.0,
            "the face sits below the control on the desktop");
    (*desk.model).hide_tool_tip();
    require(!(*desk.model).tool_tip_visible(), "the tooltip hides");
    require(desk.windowing().window_count() == 2U && !desk.windowing().native_visible(*splash),
            "hiding keeps the window, hidden, for the next show");
}

void a_warm_show_reuses_the_window() {
    Desk desk;
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> first = desk.splash();
    (*desk.model).hide_tool_tip();
    // One more round settles the text engine's and the lines' storage.
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    (*desk.model).hide_tool_tip();
    desk.windowing().set_tracing(false);
    // The rounds after measure the warm path.
    std::uint64_t warm[2]{};
    for (int round = 0; round < 2; ++round) {
        counted_allocations = 0U;
        counting_allocations = true;
        (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
        counting_allocations = false;
        warm[round] = counted_allocations;
        const std::uint64_t recorded = counted_allocations < 16U ? counted_allocations : 16U;
        for (std::uint64_t index = 0U; index < recorded; ++index) {
            std::printf("  warm allocation %llu: %zu bytes\n", static_cast<unsigned long long>(index),
                        counted_sizes[index]);
        }
        require((*desk.model).tool_tip_visible(), "the warm tooltip shows");
        require(desk.splash() == first && desk.windowing().window_count() == 2U, "the same window shows it");
        require(desk.windowing().native_visible(*first), "and it is visible");
        (*desk.model).hide_tool_tip();
    }
    std::printf("splash warm tooltip show: %llu and %llu allocations\n", static_cast<unsigned long long>(warm[0]),
                static_cast<unsigned long long>(warm[1]));
    require(warm[0] == 0U && warm[1] == 0U, "a warm show allocates nothing");
}

void falls_back_to_the_overlay() {
    Desk desk;
    desk.windowing().set_splash_windows(false);
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    require((*desk.model).tool_tip_visible(), "the tooltip shows without Splash windows");
    require(desk.windowing().window_count() == 1U, "as an overlay in its window");
    (*desk.model).hide_tool_tip();
}

void closes_with_its_control() {
    Desk desk;
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> splash = desk.splash();
    (*desk.anchor).visible = false;
    (*desk.model).perform_layout();
    require(!(*desk.model).tool_tip_visible(), "the tooltip closes when its control hides");
    require(splash.has_value() && !desk.windowing().native_visible(*splash), "its window hides");
}

void survives_the_host_closing_it() {
    Desk desk;
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> splash = desk.splash();
    require(splash.has_value(), "the Splash window is open");
    require(desk.windowing().destroy_window(*splash).accepted(), "the host may destroy it");
    require(!(*desk.model).tool_tip_visible(), "the tooltip closes with it");
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> again = desk.splash();
    require(again.has_value() && *again != *splash, "the next show opens a new Splash window");
    (*desk.model).hide_tool_tip();
}

ApplicationWindow splash_entry(const char* id, HostPopupFocus focus) {
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = "main";
    entry.model = std::make_unique<Window>(make_control<Control>(StableId("splash.root")), Size{40.0, 20.0});
    entry.options.initial_size = {40.0, 20.0};
    entry.options.minimum_size = {1.0, 1.0};
    HostPopupPlacement placement{};
    placement.kind = HostTransientKind::tool_tip;
    placement.focus = focus;
    entry.popup = placement;
    return entry;
}

void admission_rules() {
    Desk desk;
    const ApplicationWindows windows = desk.main.windows();
    const ApplicationWindowOpenResult focused = windows.open_window(splash_entry("main/focused", HostPopupFocus::takes_focus));
    require(focused.status.error == HostServiceError::invalid_argument, "a Splash never takes focus");
    desk.windowing().set_splash_windows(false);
    const ApplicationWindowOpenResult refused = windows.open_window(splash_entry("main/refused", HostPopupFocus::passive));
    require(refused.status.error == HostServiceError::unsupported, "a host without Splash windows refuses one");
    desk.windowing().set_splash_windows(true);
    const ApplicationWindowOpenResult opened = windows.open_window(splash_entry("main/opened", HostPopupFocus::passive));
    require(opened.status.accepted(), "a host with Splash windows opens one");
    require(std::string(host_transient_kind_name(HostTransientKind::splashscreen)) == "splashscreen" &&
                host_transient_is_splash(HostTransientKind::tool_tip) &&
                !host_transient_is_splash(HostTransientKind::popup),
            "the kinds are named and classified");
}
// The context menu's commands, kept alive by the test.
struct MenuCommands final {
    std::shared_ptr<Command> open = std::make_shared<Command>("file.open", "Open");
    std::shared_ptr<Command> icons = std::make_shared<Command>("view.icons", "Icons");
    std::shared_ptr<Command> list = std::make_shared<Command>("view.list", "List");
};

std::vector<MenuItemSpec> menu_items(const MenuCommands& commands) {
    std::vector<MenuItemSpec> items;
    items.emplace_back("open", MenuItemKind::command, commands.open);
    std::vector<MenuItemSpec> view;
    view.emplace_back("view.icons", MenuItemKind::radio, commands.icons);
    view.emplace_back("view.list", MenuItemKind::radio, commands.list);
    items.emplace_back("view", MenuItemKind::submenu, std::shared_ptr<Command>{}, "View", std::move(view));
    return items;
}

KeyEvent key_down(std::uint32_t key) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = key;
    return event;
}

std::size_t visible_popups(Desk& desk) {
    std::size_t count = 0U;
    const std::vector<HostWindowId> order = desk.windowing().z_order();
    for (const HostWindowId id : order) {
        if (id != desk.main.id() && desk.windowing().native_visible(id)) ++count;
    }
    return count;
}

void menu_panels_show_in_popup_windows() {
    Desk desk;
    MenuCommands commands;
    ContextMenu menu("desk.menu");
    menu.set_items(menu_items(commands));
    desk.windowing().clear_trace();
    menu.show(desk.anchor, Point{40.0, 40.0}, MenuOpenMode::keyboard);
    require(menu.is_open(), "the menu opens");
    require(visible_popups(desk) == 1U, "its panel shows in one Popup window");
    const std::string& trace = desk.windowing().trace();
    require(contains(trace, "transient=popup") && contains(trace, "focus=passive"),
            "a menu panel is a passive Popup");
    require((*desk.model).focused_control() == (*desk.model).find("desk.menu.overlay.layer"),
            "the owner keeps the focus, on the menu's layer");
    // Keys go through the owner to the panel's Popup: Down then Right opens
    // the submenu in the Popup of depth 1.
    require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), "Down reaches the panel");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::right)), "Right reaches the panel");
    require(visible_popups(desk) == 2U, "the submenu shows in a second Popup window");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), "Escape reaches the panel");
    require(!menu.is_open() && visible_popups(desk) == 0U, "Escape closes the menu and hides its Popups");
    const std::size_t kept = desk.windowing().window_count();
    menu.show(desk.anchor, Point{40.0, 40.0}, MenuOpenMode::pointer);
    require(desk.windowing().window_count() == kept && visible_popups(desk) == 1U,
            "the next opening reuses the kept Popup windows");
    // A click in the owner, outside the panels, closes the menu.
    PointerEvent press{};
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = Point{600.0, 500.0};
    static_cast<void>((*desk.model).dispatch_pointer(press));
    require(!menu.is_open() && visible_popups(desk) == 0U, "an outside click closes the menu");
}

// The one visible Popup window's content rectangle on the desktop.
Rect visible_popup_content(Desk& desk) {
    std::optional<Rect> content{};
    const std::vector<HostWindowId> order = desk.windowing().z_order();
    for (const HostWindowId id : order) {
        if (id != desk.main.id() && desk.windowing().native_visible(id)) content = desk.windowing().native_content(id);
    }
    require(content.has_value(), "a Popup window is placed");
    return *content;
}

// The owner's client is 800 x 600 at 100,100 on the 1920 x 1040 work area:
// a menu opened near its lower right corner reaches past it, inside the
// work area, instead of being pushed back into the client.
void menus_reach_past_the_owner() {
    Desk desk;
    MenuCommands commands;
    ContextMenu menu("desk.menu");
    menu.set_items(menu_items(commands));
    menu.show(desk.anchor, Point{780.0, 580.0}, MenuOpenMode::keyboard);
    require(menu.is_open() && visible_popups(desk) == 1U, "the menu shows in its Popup window");
    const Rect content = visible_popup_content(desk);
    require(content.x + content.width > 900.0 && content.y + content.height > 700.0,
            "the panel reaches past the owner's right and bottom edges");
    require(content.x + content.width <= 1920.0 + 32.0 && content.y + content.height <= 1040.0 + 32.0,
            "the panel stays inside the work area (but for its shadow)");
    menu.close();
}

// A ComboBox near the owner's bottom edge drops its list below the edge in
// the picker Popup; the owner's keys reach the list there, and Escape,
// unhandled by the list, closes it through the owner's layer.
void combo_box_list_shows_in_a_popup() {
    Desk desk;
    const std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("desk.combo"));
    (*combo).items = {"Low latency", "Balanced", "High fidelity", "Archive"};
    (*combo).set_requested_bounds(Rect{10.0, 560.0, 160.0, 24.0});
    (*(*desk.model).root()).add_child(combo);
    (*desk.model).perform_layout();
    (*combo).dropped_down = true;
    require((*combo).dropped_down.read() && visible_popups(desk) == 1U, "the list shows in a Popup window");
    const Rect content = visible_popup_content(desk);
    require(content.y + content.height > 700.0 && content.y > 600.0,
            "the list hangs below the field, past the owner's bottom edge");
    require((*desk.model).find("desk.combo.overlay.list") == nullptr,
            "the list is not in the owner's tree");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::down)), "Down reaches the list");
    require((*combo).selected_index.read().has_value(), "the list's selection follows the keys");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), "Escape reaches the owner's layer");
    require(!(*combo).dropped_down.read() && visible_popups(desk) == 0U, "Escape closes the list and hides its Popup");
    desk.windowing().set_popup_windows(false);
    (*combo).dropped_down = true;
    require((*combo).dropped_down.read() && (*desk.model).find("desk.combo.overlay.list") != nullptr,
            "without Popup windows the list shows in the owner's layer");
    (*combo).dropped_down = false;
}

// A TextBox's suggestion list near the owner's bottom edge shows in the
// picker Popup below the edge while the box keeps the keys: typing goes on
// in the box, and Escape ends the completion.
void auto_complete_list_shows_in_a_popup() {
    Desk desk;
    const std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("desk.box"));
    (*box).set_requested_bounds(Rect{10.0, 560.0, 200.0, 28.0});
    std::shared_ptr<AutoCompleteStringCollection> cities = (*box).auto_complete_custom_source.read();
    (*cities).add_range({"Calgary", "California", "Cambridge", "Caen"});
    (*box).auto_complete_source = AutoCompleteSource::custom_source;
    (*box).auto_complete_mode = AutoCompleteMode::suggest;
    (*(*desk.model).root()).add_child(box);
    (*desk.model).perform_layout();
    require((*desk.model).request_focus(box), "the box takes focus");
    TextInputEvent typed{};
    typed.text_utf8 = "Ca";
    require((*desk.model).dispatch_text(typed), "the box takes the typing");
    require((*box).auto_complete_open() && visible_popups(desk) == 1U, "the suggestions show in a Popup window");
    const Rect content = visible_popup_content(desk);
    require(content.y + content.height > 700.0, "the list hangs past the owner's bottom edge");
    require((*desk.model).focused_control() == box, "the box keeps the focus");
    TextInputEvent more{};
    more.text_utf8 = "l";
    require((*desk.model).dispatch_text(more) && (*box).text.read() == "Cal", "typing goes on in the box");
    require((*desk.model).dispatch_key(key_down(PhysicalKey::escape)), "Escape reaches the box");
    require(!(*box).auto_complete_open() && visible_popups(desk) == 0U, "Escape ends the completion and hides its Popup");
}

// ErrorProvider's message is the glyph's tooltip text, which the tooltip
// presenter shows in the window's Splash window (no input).
void error_message_shows_in_the_splash_window() {
    Desk desk;
    ErrorProvider errors(*desk.model);
    errors.set_error(desk.anchor, "Server address is not reachable");
    (*desk.model).perform_layout();
    const Rect glyph = errors.snapshot().icons.front().bounds;
    const Control::Ptr hit = (*desk.model).hit_test(Point{glyph.x + glyph.width * 0.5, glyph.y + glyph.height * 0.5});
    require(hit != nullptr && (*hit).tool_tip_text.read() == "Server address is not reachable",
            "the glyph carries the message as its tooltip text");
    (*desk.model).show_tool_tip(hit, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> splash = desk.splash();
    require(splash.has_value() && desk.windowing().native_visible(*splash) &&
                desk.windowing().kind(*splash) == HostWindowKind::popup,
            "the message shows in the Splash window");
}

void menu_falls_back_to_the_overlay() {
    Desk desk;
    desk.windowing().set_popup_windows(false);
    MenuCommands commands;
    ContextMenu menu("desk.menu");
    menu.set_items(menu_items(commands));
    menu.show(desk.anchor, Point{40.0, 40.0}, MenuOpenMode::keyboard);
    require(menu.is_open() && desk.windowing().window_count() == 1U, "without Popup windows it opens as an overlay");
    require((*desk.model).focused_control() == (*desk.model).find("desk.menu.overlay.row.open"),
            "the overlay's first command takes the focus");
    menu.close();
}

// The opaque path (owner, 2026-11-08: Q-S2 (b)): a run whose transient
// windows have no per-pixel alpha (X11 without a compositing manager)
// still shows tooltips and menus in their own windows, sized to the face
// alone, with no shadow.
// The tooltip's window on a run whose transient windows are `translucent`
// or not.
Rect tool_tip_window(const bool translucent) {
    Desk desk;
    desk.windowing().set_transient_windows_translucent(translucent);
    require((*desk.model).popup_windows_translucent() == translucent, "the window reports its popup windows");
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> splash = desk.splash();
    require(splash.has_value() && desk.windowing().native_visible(*splash), "the tooltip has its own window");
    const std::optional<Rect> content = desk.windowing().native_content(*splash);
    require(content.has_value(), "the tooltip's window is placed");
    return *content;
}

// The first menu panel's Popup window on a run whose transient windows are
// `translucent` or not.
Rect menu_window(const bool translucent) {
    Desk desk;
    desk.windowing().set_transient_windows_translucent(translucent);
    MenuCommands commands;
    ContextMenu menu("desk.menu");
    menu.set_items(menu_items(commands));
    menu.show(desk.anchor, Point{40.0, 40.0}, MenuOpenMode::keyboard);
    require(menu.is_open() && visible_popups(desk) == 1U, "the menu panel has its own Popup window");
    std::optional<Rect> content{};
    const std::vector<HostWindowId> order = desk.windowing().z_order();
    for (const HostWindowId id : order) {
        if (id != desk.main.id() && desk.windowing().native_visible(id)) content = desk.windowing().native_content(id);
    }
    require(content.has_value(), "the menu's Popup window is placed");
    menu.close();
    return *content;
}

void opaque_transient_windows_hold_the_face_alone() {
    const Rect translucent_tip = tool_tip_window(true);
    const Rect opaque_tip = tool_tip_window(false);
    require(opaque_tip.width < translucent_tip.width && opaque_tip.height < translucent_tip.height &&
                opaque_tip.x > translucent_tip.x && opaque_tip.y > translucent_tip.y,
            "an opaque tooltip window holds the face alone, with no room for a shadow");
    const Rect translucent_menu = menu_window(true);
    const Rect opaque_menu = menu_window(false);
    require(opaque_menu.width < translucent_menu.width && opaque_menu.height < translucent_menu.height,
            "an opaque menu window holds the panel alone, with no room for a shadow");
}

void splashscreen_shows_and_hands_over() {
    const std::shared_ptr<host::HeadlessSplashscreenRecord> record = std::make_shared<host::HeadlessSplashscreenRecord>();
    SplashscreenOptions options{};
    options.artwork = artwork(440U, 240U);
    options.status = "Starting";
    SplashscreenResult shown =
        Splashscreen::show_on(options, std::make_unique<host::HeadlessSplashscreenSurface>(record));
    require(static_cast<bool>(shown) && (*record).presented == 1U, "the splashscreen shows its first frame");
    Splashscreen& splashscreen = *shown.splashscreen;
    require(splashscreen.size().width == 440.0 && splashscreen.size().height == 240.0,
            "the window is the artwork's size");
    require(splashscreen.set_status("Loading the theme") == SplashscreenError::none &&
                (*record).status == "Loading the theme" && (*record).presented == 2U,
            "a status change presents a frame at once");
    require(splashscreen.set_progress(0.5) == SplashscreenError::none && (*record).progress == 0.5 &&
                (*record).presented == 3U,
            "a progress change presents a frame at once");
    require(splashscreen.set_progress(1.5) == SplashscreenError::invalid_argument && (*record).progress == 0.5,
            "a progress outside [0, 1] is refused");
    const SplashscreenResult second =
        Splashscreen::show_on(options, std::make_unique<host::HeadlessSplashscreenSurface>(nullptr));
    require(second.error == SplashscreenError::already_shown, "one splashscreen at a time");
    {
        // The run's first window takes over.
        Desk desk;
        require(!splashscreen.shown() && (*record).closed, "the first window of the run closes it");
    }
    require(splashscreen.set_status("Done") == SplashscreenError::none && (*record).presented == 3U,
            "after the handover a status change presents nothing");
    SplashscreenOptions bad{};
    const SplashscreenResult missing =
        Splashscreen::show_on(bad, std::make_unique<host::HeadlessSplashscreenSurface>(nullptr));
    require(missing.error == SplashscreenError::invalid_argument, "a splashscreen without artwork is refused");
    bad.artwork = artwork(40U, 40U);
    const SplashscreenResult small =
        Splashscreen::show_on(bad, std::make_unique<host::HeadlessSplashscreenSurface>(nullptr));
    require(small.error == SplashscreenError::invalid_argument, "artwork out of range is refused");
}
} // namespace

int main() {
    try {
        tooltip_shows_in_a_splash_window();
        a_warm_show_reuses_the_window();
        falls_back_to_the_overlay();
        closes_with_its_control();
        survives_the_host_closing_it();
        admission_rules();
        menu_panels_show_in_popup_windows();
        menu_falls_back_to_the_overlay();
        menus_reach_past_the_owner();
        combo_box_list_shows_in_a_popup();
        auto_complete_list_shows_in_a_popup();
        error_message_shows_in_the_splash_window();
        opaque_transient_windows_hold_the_face_alone();
        splashscreen_shows_and_hands_over();
    } catch (const std::exception& error) {
        std::cerr << "splash window test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "splash window tests passed\n";
    return 0;
}
