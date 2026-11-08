// NotifyIcon (stage W) and the portable notification-area contract of
// HostServices: identities, validation, bounds, threads and shutdown in the
// base class; the headless reference host; and the NotifyIcon component's
// icon, text, visibility, clicks, context menu, notification text and
// refusals.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

HostImage solid_image(std::uint32_t side) {
    HostImage image;
    image.width = side;
    image.height = side;
    image.row_bytes = static_cast<std::uint64_t>(side) * 4U;
    image.pixels.assign(static_cast<std::size_t>(image.row_bytes) * side, std::byte{0x7F});
    return image;
}

HostTrayIconRequest icon_request(std::string tooltip) {
    HostTrayIconRequest request;
    request.images.push_back(solid_image(16U));
    request.images.push_back(solid_image(32U));
    request.tooltip = std::move(tooltip);
    return request;
}

// A host without a notification area: every pure service refuses.
class TraylessServices final : public HostServices {
public:
    TraylessServices() : HostServices({HostCapabilities::current_protocol_version, "trayless", HostCapability::lifecycle}) {}
    ~TraylessServices() override { shutdown(); }

protected:
    HostMonitorResult query_monitors_impl() override { return {{HostServiceError::unsupported}, {}}; }
    HostServiceStatus set_cursor_impl(CursorKind) override { return {HostServiceError::unsupported}; }
    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override { return {HostServiceError::unsupported}; }
    HostClipboardTextResult read_clipboard_text_impl() override { return {}; }
    HostServiceStatus write_clipboard_text_impl(std::string_view) override { return {HostServiceError::unsupported}; }
    HostDialogResult show_dialog_impl(const HostDialogRequest&) override { return {}; }
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest&) override { return {HostServiceError::unsupported}; }
};

struct EventLog final {
    std::vector<HostTrayIconEvent> events;
    void on_event(const HostTrayIconEvent& event) { events.push_back(event); }
};

struct OtherThreadAdd final {
    HostServices* services{};
    HostServiceError* error{};
    void operator()() const { *error = (*services).add_tray_icon(icon_request("x")).status.error; }
};

void image_selection() {
    std::vector<HostImage> images;
    images.push_back(solid_image(32U));
    images.push_back(solid_image(16U));
    images.push_back(solid_image(24U));
    require(select_tray_icon_image(images, 20U) == 2U, "the smallest image reaching the wanted size is chosen");
    require(select_tray_icon_image(images, 16U) == 1U, "an exact size is chosen");
    require(select_tray_icon_image(images, 64U) == 0U, "the largest image is chosen when none reaches the size");
}

void contract() {
    host::HeadlessHostServices services;
    require(services.snapshot().capabilities.supports(HostCapability::notification_area) &&
                services.snapshot().capabilities.supports(HostCapability::notification_area_menus),
            "the headless reference host reports the notification area");
    require(services.snapshot().capabilities.to_json().find("notification_area_messages") != std::string::npos,
            "capabilities name the notification-area flags");
    HostTrayIconRequest empty;
    require(services.add_tray_icon(empty).status.error == HostServiceError::invalid_argument,
            "an icon without images is refused");
    HostTrayIconRequest huge = icon_request("big");
    huge.images.push_back(solid_image(300U));
    require(services.add_tray_icon(huge).status.error == HostServiceError::too_large,
            "an image larger than 256 pixels is refused");
    require(services.add_tray_icon(icon_request(std::string(128U, 'a'))).status.error == HostServiceError::too_large,
            "a tooltip over 127 bytes is refused");
    require(services.add_tray_icon(icon_request(std::string("\xFF"))).status.error == HostServiceError::invalid_utf8,
            "an invalid tooltip is refused");

    EventLog log;
    SubscriptionToken token = services.tray_icon_events().subscribe(
        Delegate<const HostTrayIconEvent&>::bind<EventLog, &EventLog::on_event>(log));
    const HostTrayIconResult added = services.add_tray_icon(icon_request("Radio"));
    require(added.status.accepted() && added.icon_id == HostTrayIconId{1U} && services.tray_icon_live(HostTrayIconId{1U}) &&
                services.snapshot().tray_icons == 1U,
            "an icon is added with the first identity");
    require(services.tray_icon(HostTrayIconId{1U}) != nullptr && (*services.tray_icon(HostTrayIconId{1U})).images.size() == 2U,
            "the host receives every image size");
    require(services.update_tray_icon(HostTrayIconId{9U}, icon_request("x")).error == HostServiceError::invalid_argument,
            "an unknown identity is refused");
    require(services.update_tray_icon(HostTrayIconId{1U}, icon_request("Radio, muted")).accepted() &&
                services.snapshot().tray_icon_updates == 1U,
            "a live icon updates");
    require(services.show_tray_notification(HostTrayIconId{1U}, {"Scan", "", HostTrayNotificationIcon::none, 0U}).error ==
                HostServiceError::invalid_argument,
            "a notification without text is refused");
    require(services.show_tray_notification(HostTrayIconId{1U}, {"Scan", "42 stations", HostTrayNotificationIcon::information, 0U})
                .accepted(),
            "a notification is shown");
    services.queue_tray_menu_choice("quit");
    HostTrayMenuRequest menu;
    menu.items = {{"open", "Open"}, {"", "", true, false, true}, {"quit", "Quit"}};
    const HostTrayMenuResult chosen = services.show_tray_menu(HostTrayIconId{1U}, menu);
    require(chosen.status.accepted() && chosen.chosen_id == "quit", "a native menu returns the chosen row");
    HostTrayMenuRequest unnamed;
    unnamed.items = {{"", "Nameless"}};
    require(services.show_tray_menu(HostTrayIconId{1U}, unnamed).status.error == HostServiceError::invalid_argument,
            "a menu row without an identity is refused");

    services.simulate_tray_icon_event({1U, HostTrayIconAction::double_click});
    services.simulate_tray_icon_event({HostTrayIconId{7U}, HostTrayIconAction::click});
    require(log.events.size() == 1U && log.events[0].action == HostTrayIconAction::double_click &&
                services.snapshot().tray_events == 1U,
            "events reach observers only for live icons");

    HostServiceError other_thread = HostServiceError::none;
    std::thread worker(OtherThreadAdd{&services, &other_thread});
    worker.join();
    require(other_thread == HostServiceError::wrong_thread, "another thread is refused");

    for (std::size_t index = 1U; index < HostServices::maximum_tray_icons; ++index) {
        require(services.add_tray_icon(icon_request("more")).status.accepted(), "icons up to the bound are added");
    }
    require(services.add_tray_icon(icon_request("one too many")).status.error == HostServiceError::too_large,
            "icons past the bound are refused");
    require(services.remove_tray_icon(HostTrayIconId{1U}).accepted() && !services.tray_icon_live(HostTrayIconId{1U}) &&
                services.remove_tray_icon(HostTrayIconId{1U}).error == HostServiceError::invalid_argument,
            "a removed identity is gone and not reused");
    services.shutdown();
    require(services.snapshot().tray_icons == 0U &&
                services.tray_trace().find("tray remove id=16") != std::string::npos,
            "shutdown removes every live icon");
    require(services.add_tray_icon(icon_request("late")).status.error == HostServiceError::after_shutdown,
            "a shut-down host refuses icons");

    TraylessServices trayless;
    require(trayless.add_tray_icon(icon_request("none")).status.error == HostServiceError::unsupported,
            "a host without the capability refuses icons as unsupported");
}

struct ClickLog final {
    std::vector<PointerButton> clicks;
    int double_clicks{};
    int balloon_clicks{};
    int balloon_closes{};
    std::vector<MenuItemInvocation> invocations;
    void on_click(const NotifyIconClick& click) { clicks.push_back(click.button); }
    void on_double_click() { ++double_clicks; }
    void on_balloon_clicked() { ++balloon_clicks; }
    void on_balloon_closed() { ++balloon_closes; }
    void on_menu(const MenuItemInvocation& invocation) { invocations.push_back(invocation); }
};

struct CountInvocations final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

void component() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("notify.root"));
    Window window(root, {320.0, 200.0});

    NotifyIcon unhosted(window);
    require(!unhosted.supported(), "a window without a host does not support notification icons");
    unhosted.icon = std::vector<HostImage>{solid_image(16U)};
    unhosted.visible = true;
    require(unhosted.status().error == HostServiceError::unsupported && !unhosted.shown(),
            "showing without a host is refused as unsupported");
    unhosted.dispose();

    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    NotifyIcon icon(window);
    require(icon.supported(), "the headless host supports notification icons");
    icon.visible = true;
    require(icon.status().error == HostServiceError::invalid_argument && !icon.shown(),
            "a visible icon without an image is not shown");
    icon.icon = std::vector<HostImage>{solid_image(16U), solid_image(32U)};
    require(icon.shown() && icon.status().accepted() && services.snapshot().tray_icons == 1U,
            "setting an image shows a visible icon");
    icon.text = "Radio: FM 98.1";
    require(services.tray_trace().find("tooltip=Radio: FM 98.1") != std::string::npos,
            "the tooltip reaches the host");
    bool refused = false;
    try {
        icon.text = std::string(200U, 'x');
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a tooltip over 127 bytes is refused");

    ClickLog log;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back(icon.click().subscribe(Delegate<const NotifyIconClick&>::bind<ClickLog, &ClickLog::on_click>(log)));
    tokens.push_back(icon.double_click().subscribe(Delegate<>::bind<ClickLog, &ClickLog::on_double_click>(log)));
    tokens.push_back(icon.balloon_tip_clicked().subscribe(Delegate<>::bind<ClickLog, &ClickLog::on_balloon_clicked>(log)));
    tokens.push_back(icon.balloon_tip_closed().subscribe(Delegate<>::bind<ClickLog, &ClickLog::on_balloon_closed>(log)));
    tokens.push_back(icon.menu_item_invoked().subscribe(
        Delegate<const MenuItemInvocation&>::bind<ClickLog, &ClickLog::on_menu>(log)));

    // This host's first icon: identity 1.
    const HostTrayIconId id{1U};
    services.simulate_tray_icon_event({id, HostTrayIconAction::click});
    services.simulate_tray_icon_event({id, HostTrayIconAction::double_click});
    require(log.clicks.size() == 1U && log.clicks[0] == PointerButton::primary && log.double_clicks == 1,
            "clicks and double clicks are raised");

    const std::shared_ptr<Command> open = std::make_shared<Command>("tray.open", "Open");
    const std::shared_ptr<Command> mute = std::make_shared<Command>("tray.mute", "Mute");
    int mutes = 0;
    SubscriptionToken mute_token = (*mute).invoked().subscribe(CountInvocations{&mutes});
    std::shared_ptr<ContextMenu> menu = std::make_shared<ContextMenu>("tray.menu");
    (*menu).set_items({MenuItemSpec("tray.menu.open", MenuItemKind::command, open),
                       MenuItemSpec("tray.menu.rule", MenuItemKind::separator),
                       MenuItemSpec("tray.menu.mute", MenuItemKind::check, mute)});
    icon.context_menu = menu;
    services.queue_tray_menu_choice("tray.menu.mute");
    services.simulate_tray_icon_event({id, HostTrayIconAction::context});
    require(log.clicks.size() == 2U && log.clicks[1] == PointerButton::secondary,
            "the context gesture is a secondary click");
    require(mutes == 1 && log.invocations.size() == 1U && log.invocations[0].item_id == "tray.menu.mute" &&
                log.invocations[0].command_id == "tray.mute",
            "the chosen menu row runs its command and is reported");
    require(services.tray_trace().find("tray menu id=1 items=3") != std::string::npos,
            "the menu's rows reach the host");

    require(icon.show_balloon_tip("Scan finished", "42 stations found").accepted() &&
                services.tray_trace().find("text=42 stations found") != std::string::npos,
            "notification text reaches the host");
    services.simulate_tray_icon_event({id, HostTrayIconAction::notification_clicked});
    services.simulate_tray_icon_event({id, HostTrayIconAction::notification_closed});
    require(log.balloon_clicks == 1 && log.balloon_closes == 1, "notification clicks and closes are raised");

    icon.visible = false;
    require(icon.status().accepted() && !icon.shown() && services.snapshot().tray_icons == 0U,
            "hiding removes the icon");
    require(icon.show_balloon_tip("Hidden", "Nobody sees this").error == HostServiceError::invalid_argument,
            "a hidden icon refuses notification text");
    icon.visible = true;
    require(icon.status().accepted() && icon.shown(), "the icon shows again");
    icon.dispose();
    require(services.snapshot().tray_icons == 0U, "disposal removes the icon");
}

// A menu command that takes the icon's context menu away while it runs.
struct DropIconMenu final {
    NotifyIcon* icon{};
    void operator()(const CommandInvocation&) const { (*icon).context_menu = nullptr; }
};

// The chosen row's command may replace or remove the icon's context menu;
// the invocation is still reported, with the menu and row it ran from.
void menu_removed_by_its_command() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("notify.menu-removed"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    NotifyIcon icon(window);
    icon.icon = std::vector<HostImage>{solid_image(16U)};
    icon.visible = true;
    require(icon.status().accepted() && icon.shown(), "the icon shows");
    ClickLog log;
    SubscriptionToken invoked = icon.menu_item_invoked().subscribe(
        Delegate<const MenuItemInvocation&>::bind<ClickLog, &ClickLog::on_menu>(log));
    const std::shared_ptr<Command> quit = std::make_shared<Command>("tray.quit", "Quit");
    SubscriptionToken dropping = (*quit).invoked().subscribe(DropIconMenu{&icon});
    std::shared_ptr<ContextMenu> menu = std::make_shared<ContextMenu>("tray.menu");
    (*menu).set_items({MenuItemSpec("tray.menu.quit", MenuItemKind::command, quit)});
    icon.context_menu = menu;
    services.queue_tray_menu_choice("tray.menu.quit");
    services.simulate_tray_icon_event({1U, HostTrayIconAction::context});
    require(!icon.context_menu.read(), "the command removed the icon's menu");
    require(log.invocations.size() == 1U && log.invocations[0].menu_id == "tray.menu" &&
                log.invocations[0].item_id == "tray.menu.quit" && log.invocations[0].command_id == "tray.quit",
            "the invocation is reported from the menu the row ran in");
}

void session_end() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("notify.session"));
    Window window(root, {320.0, 200.0});
    NotifyIcon icon(window);
    icon.icon = std::vector<HostImage>{solid_image(16U)};
    {
        host::HeadlessHost headless(window);
        icon.visible = true;
        require(icon.status().accepted() && icon.shown(), "the icon shows in the session");
    }
    require(!icon.shown() && icon.visible.read(), "an ended session took the icon with it");
    icon.visible = true;
    require(icon.status().error == HostServiceError::unsupported, "without a host the icon is refused");
}

} // namespace

int main() {
    image_selection();
    contract();
    component();
    menu_removed_by_its_command();
    session_end();
    std::cout << "NotifyIcon: image choice, tray contract, headless host, clicks, menu, notifications, "
                 "visibility, disposal and session end passed\n";
    return 0;
}
