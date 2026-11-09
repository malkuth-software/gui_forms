#include "gui_forms/components/notify_icon/notify_icon.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

bool valid_text(std::string_view text) {
    const bool result = validate_utf8(text).valid();
    return result;
}

// The source name a menu row's command sees when the icon's menu runs it.
constexpr std::string_view notify_icon_source = "notify_icon";

} // namespace

NotifyIcon::NotifyIcon(Window& window) : window_lifetime_(window.lifetime_) {
    window.verify_access("NotifyIcon construction");
}

NotifyIcon::~NotifyIcon() {
    forget_icon();
}

Window* NotifyIcon::bound_window() const noexcept {
    const std::shared_ptr<detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

HostServices* NotifyIcon::current_services() const noexcept {
    Window* const window = bound_window();
    return window != nullptr ? (*window).host_services() : nullptr;
}

void NotifyIcon::require_ui(std::string_view operation) const {
    if (!is_alive()) throw std::logic_error("GUI.Forms cannot use a disposed NotifyIcon");
    Window* const window = bound_window();
    if (window == nullptr) throw std::logic_error("GUI.Forms cannot use a NotifyIcon after Window shutdown");
    (*window).verify_access(operation);
}

bool NotifyIcon::supported() const noexcept {
    HostServices* const services = current_services();
    if (services == nullptr) return false;
    const bool result = (*services).snapshot().capabilities.supports(HostCapability::notification_area);
    return result;
}

bool NotifyIcon::shown() const noexcept {
    // The icon is shown while the window still has the services that showed
    // it and they still hold its identity; a session that ended took the
    // icon with it.
    if (services_ == nullptr || current_services() != services_ || !tray_events_.connected()) return false;
    const bool result = (*services_).tray_icon_live(icon_id_);
    return result;
}

HostTrayIconRequest NotifyIcon::request() const {
    HostTrayIconRequest built;
    built.images = images_;
    built.tooltip = text.read();
    return built;
}

// icon is a view onto images_: every write synchronizes the host icon, even
// an unchanged one.
std::vector<HostImage> NotifyIcon::read_view(const decltype(icon)&) const {
    std::vector<HostImage> current = images_;
    return current;
}

void NotifyIcon::write_view(const decltype(icon)&, std::vector<HostImage> incoming) {
    require_ui("NotifyIcon icon mutation");
    if (incoming.size() > HostServices::maximum_tray_icon_images) {
        throw std::invalid_argument("NotifyIcon takes at most 8 icon images");
    }
    for (const HostImage& image : incoming) {
        if (validate_host_image(image.view()) != HostImageError::none ||
            image.width > HostServices::maximum_tray_icon_side || image.height > HostServices::maximum_tray_icon_side) {
            throw std::invalid_argument("NotifyIcon images must be valid and at most 256 pixels a side");
        }
    }
    images_ = std::move(incoming);
    status_ = synchronize(true);
}

void NotifyIcon::prepare_write(const decltype(text)&, std::string& incoming) {
    require_ui("NotifyIcon text mutation");
    if (incoming.size() > HostServices::maximum_tray_tooltip_bytes || !valid_text(incoming)) {
        throw std::invalid_argument("NotifyIcon text must be valid UTF-8 of at most 127 bytes");
    }
}

// A changed tooltip updates a shown icon; nothing is invalidated (a
// component does not paint).
void NotifyIcon::written(const decltype(text)&) {
    status_ = synchronize(true);
}

// visible is a view onto visible_: every write synchronizes, so a repeated
// true adds again an icon that a host session took with it.
bool NotifyIcon::read_view(const decltype(visible)&) const {
    return visible_;
}

void NotifyIcon::write_view(const decltype(visible)&, bool incoming) {
    require_ui("NotifyIcon visibility");
    visible_ = incoming;
    status_ = synchronize(false);
}

void NotifyIcon::prepare_write(const decltype(context_menu)&, std::shared_ptr<ContextMenu>&) {
    require_ui("NotifyIcon menu mutation");
}

void NotifyIcon::written(const decltype(context_menu)&) {}

// Leaves the icon's host state, removing a shown icon. Never throws.
void NotifyIcon::forget_icon() noexcept {
    if (shown()) {
        const HostServiceStatus removed = (*services_).remove_tray_icon(icon_id_);
        static_cast<void>(removed);
    }
    tray_events_.disconnect();
    services_ = nullptr;
    icon_id_ = {};
}

HostServiceStatus NotifyIcon::synchronize(bool content_changed) {
    const bool live = shown();
    if (!live && services_ != nullptr) forget_icon();
    if (!visible_) {
        forget_icon();
        return {};
    }
    HostServices* const services = current_services();
    if (services == nullptr) return {HostServiceError::unsupported};
    if (images_.empty()) {
        forget_icon();
        return {HostServiceError::invalid_argument};
    }
    if (live) {
        if (!content_changed) return {};
        const HostServiceStatus updated = (*services).update_tray_icon(icon_id_, request());
        return updated;
    }
    const HostTrayIconResult added = (*services).add_tray_icon(request());
    if (!added.status.accepted()) return added.status;
    services_ = services;
    icon_id_ = added.icon_id;
    // The token, not this component's disposal, owns the connection: it
    // stays connected until forget_icon, so a disposing icon can still tell
    // that its services hold it. Disconnection also reports that the
    // services' event (and the services) are gone.
    tray_events_ = (*services).tray_icon_events().subscribe(
        Delegate<const HostTrayIconEvent&>::bind<NotifyIcon, &NotifyIcon::on_tray_event>(*this));
    return added.status;
}

HostServiceStatus NotifyIcon::show_balloon_tip(std::string title, std::string message,
                                               HostTrayNotificationIcon symbol, std::chrono::milliseconds timeout) {
    require_ui("NotifyIcon notification");
    HostServices* const services = current_services();
    if (services == nullptr) {
        status_ = {HostServiceError::unsupported};
        return status_;
    }
    if (!(*services).snapshot().capabilities.supports(HostCapability::notification_area_messages)) {
        status_ = {HostServiceError::unsupported};
        return status_;
    }
    if (!shown()) {
        status_ = {HostServiceError::invalid_argument};
        return status_;
    }
    HostTrayNotificationRequest notification;
    notification.title = std::move(title);
    notification.text = std::move(message);
    notification.icon = symbol;
    // At most ten minutes; zero or less is the host's default.
    const long long milliseconds = timeout.count();
    const long long bounded = std::clamp<long long>(milliseconds, 0, 600000);
    notification.timeout_milliseconds = static_cast<std::uint32_t>(bounded);
    status_ = (*services).show_tray_notification(icon_id_, notification);
    return status_;
}

void NotifyIcon::on_tray_event(const HostTrayIconEvent& event) {
    if (!is_alive() || event.icon_id != icon_id_ || current_services() != services_) return;
    switch (event.action) {
    case HostTrayIconAction::click: {
        const NotifyIconClick clicked{PointerButton::primary};
        click_.emit(clicked);
        return;
    }
    case HostTrayIconAction::double_click:
        double_click_.emit();
        return;
    case HostTrayIconAction::context: {
        const NotifyIconClick clicked{PointerButton::secondary};
        click_.emit(clicked);
        if (is_alive()) show_menu();
        return;
    }
    case HostTrayIconAction::notification_clicked:
        balloon_tip_clicked_.emit();
        return;
    case HostTrayIconAction::notification_closed:
        balloon_tip_closed_.emit();
        return;
    }
    // An action outside the enumeration raises nothing.
    return;
}

// Shows the context menu's rows natively, then runs the chosen row's command.
void NotifyIcon::show_menu() {
    if (!context_menu.read() || !shown()) return;
    HostServices& services = *services_;
    if (!services.snapshot().capabilities.supports(HostCapability::notification_area_menus)) return;
    const std::vector<MenuItemSpec>& specs = (*context_menu.read()).items();
    HostTrayMenuRequest menu;
    menu.items.reserve(specs.size());
    for (const MenuItemSpec& spec : specs) {
        HostTrayMenuItem item;
        if (spec.kind == MenuItemKind::separator) {
            item.separator = true;
        } else if (spec.kind == MenuItemKind::submenu || !spec.command) {
            continue;
        } else {
            const CommandState& state = (*spec.command).state();
            if (!state.visible) continue;
            item.id = spec.stable_id;
            item.text = spec.text.empty() ? state.text : spec.text;
            item.enabled = state.enabled;
            item.checked = spec.kind != MenuItemKind::command && state.checked;
        }
        menu.items.push_back(std::move(item));
        if (menu.items.size() == HostServices::maximum_tray_menu_items) break;
    }
    if (menu.items.empty()) return;
    const HostTrayMenuResult chosen = services.show_tray_menu(icon_id_, menu);
    status_ = chosen.status;
    if (!chosen.status.accepted() || chosen.chosen_id.empty() || !is_alive() || !context_menu.read()) return;
    // The command may replace or remove this icon's menu, or the menu's rows:
    // everything the invocation reports is copied before it runs.
    const std::shared_ptr<ContextMenu> menu_owner = context_menu.read();
    std::shared_ptr<Command> command{};
    for (const MenuItemSpec& spec : (*menu_owner).items()) {
        if (spec.stable_id != chosen.chosen_id || !spec.command) continue;
        command = spec.command;
        break;
    }
    if (!command) return;
    const MenuItemInvocation invocation{(*menu_owner).stable_id(), chosen.chosen_id, (*command).stable_id(),
                                        std::string(notify_icon_source)};
    // Through the bound window's command chain (owner decision DK-47).
    Window* const routing = bound_window();
    bool executed = false;
    if (routing != nullptr) {
        executed = (*command).execute_routed(*routing, notify_icon_source);
    } else {
        executed = (*command).execute(notify_icon_source);
    }
    if (!executed || !is_alive()) return;
    menu_item_invoked_.emit(invocation);
}

void NotifyIcon::verify_dispose_thread() {
    if (Window* const window = bound_window()) (*window).verify_access("NotifyIcon disposal");
}

void NotifyIcon::on_dispose() noexcept {
    forget_icon();
    visible_ = false;
    window_lifetime_.reset();
}

namespace {

// The events NotifyIcon adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 5> notify_icon_interactions{{
    {.name = "click", .payload = "NotifyIconClick", .mutable_payload = false},
    {.name = "double_click", .payload = "", .mutable_payload = false},
    {.name = "balloon_tip_clicked", .payload = "", .mutable_payload = false},
    {.name = "balloon_tip_closed", .payload = "", .mutable_payload = false},
    {.name = "menu_item_invoked", .payload = "MenuItemInvocation", .mutable_payload = false},
}};

} // namespace

void NotifyIcon::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), notify_icon_interactions.begin(), notify_icon_interactions.end());
}

// property_codemod begin NotifyIcon (tools/property_families/notify_icon.json; generated, do not edit)

// property_codemod end NotifyIcon

} // namespace gui_forms
