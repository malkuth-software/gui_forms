#include "gui_forms/components/tool_tip/tool_tip.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

ToolTip::ToolTip(Window& window) : window_lifetime_(window.lifetime_) {
    window.verify_access("ToolTip construction");
    window_subscription_ = window.tool_tip_visibility_changed().subscribe(
        *this, Delegate<const ToolTipEvent&>::bind<ToolTip, &ToolTip::window_tool_tip_changed>(*this));
}

ToolTip::~ToolTip() {
    if (is_alive()) {
        try {
            dispose();
        } catch (...) {
            window_subscription_.disconnect();
            targets_.clear();
        }
    }
}

Window* ToolTip::bound_window() const noexcept {
    const std::shared_ptr<detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

Window& ToolTip::require_access(std::string_view operation) const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot use a disposed ToolTip");
    }
    Window* owner = bound_window();
    if (owner == nullptr) {
        throw std::logic_error("GUI.Forms cannot use a ToolTip after Window shutdown");
    }
    (*owner).verify_access(operation);
    return *owner;
}

bool ToolTip::owns(const Control& target) const {
    const TargetMap::const_iterator found = targets_.find(target.stable_id().value());
    const bool owned = found != targets_.end() && (*found).second.lock().get() == &target;
    return owned;
}

void ToolTip::set_tool_tip(const std::shared_ptr<Control>& target, std::string text) {
    Window& owner = require_access("ToolTip mapping mutation");
    if (!target || !(*target).is_alive() || (*target).attached_window() != &owner) {
        throw std::invalid_argument("GUI.Forms ToolTip target must be a live attached control in its Window");
    }
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("GUI.Forms ToolTip text must be valid UTF-8");
    }
    const std::string key((*target).stable_id().value());
    const bool removing = text.empty();
    (*target).tool_tip_text = std::move(text);
    if (removing) {
        targets_.erase(key);
        return;
    }
    targets_.insert_or_assign(key, std::weak_ptr<Control>(target));
}

std::string ToolTip::tool_tip(const Control& target) const {
    return target.tool_tip_text.read();
}

bool ToolTip::remove_tool_tip(Control& target) {
    static_cast<void>(require_access("ToolTip mapping removal"));
    if (!owns(target)) return false;
    targets_.erase(std::string(target.stable_id().value()));
    if (target.is_alive()) target.tool_tip_text = std::string{};
    return true;
}

void ToolTip::clear() {
    static_cast<void>(require_access("ToolTip mapping clear"));
    // Detached first: clearing a target's text may raise events that reach
    // this ToolTip again.
    TargetMap cleared = std::move(targets_);
    targets_.clear();
    for (const std::pair<const std::string, std::weak_ptr<Control>>& entry : cleared) {
        const std::shared_ptr<Control> target = entry.second.lock();
        if (target && (*target).is_alive()) (*target).tool_tip_text = std::string{};
    }
}

// The settings are the window's (ToolTipSettings), read and written field by
// field; Window::set_tool_tip_settings refuses a negative delay and a width
// outside 80 to 2048, changing nothing.
std::chrono::milliseconds ToolTip::read_view(const decltype(initial_delay)&) const {
    const std::chrono::milliseconds current = require_access("ToolTip settings read").tool_tip_settings().initial_delay;
    return current;
}

void ToolTip::write_view(const decltype(initial_delay)&, std::chrono::milliseconds incoming) {
    Window& owner = require_access("ToolTip initial-delay mutation");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.initial_delay = incoming;
    owner.set_tool_tip_settings(settings);
}

std::chrono::milliseconds ToolTip::read_view(const decltype(reshow_delay)&) const {
    const std::chrono::milliseconds current = require_access("ToolTip settings read").tool_tip_settings().reshow_delay;
    return current;
}

void ToolTip::write_view(const decltype(reshow_delay)&, std::chrono::milliseconds incoming) {
    Window& owner = require_access("ToolTip reshow-delay mutation");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.reshow_delay = incoming;
    owner.set_tool_tip_settings(settings);
}

std::chrono::milliseconds ToolTip::read_view(const decltype(auto_pop_delay)&) const {
    const std::chrono::milliseconds current = require_access("ToolTip settings read").tool_tip_settings().auto_pop_delay;
    return current;
}

void ToolTip::write_view(const decltype(auto_pop_delay)&, std::chrono::milliseconds incoming) {
    Window& owner = require_access("ToolTip auto-pop mutation");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.auto_pop_delay = incoming;
    owner.set_tool_tip_settings(settings);
}

bool ToolTip::read_view(const decltype(show_on_hover)&) const {
    const bool current = require_access("ToolTip settings read").tool_tip_settings().show_on_hover;
    return current;
}

void ToolTip::write_view(const decltype(show_on_hover)&, bool incoming) {
    Window& owner = require_access("ToolTip hover policy mutation");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.show_on_hover = incoming;
    owner.set_tool_tip_settings(settings);
}

bool ToolTip::read_view(const decltype(show_on_focus)&) const {
    const bool current = require_access("ToolTip settings read").tool_tip_settings().show_on_focus;
    return current;
}

void ToolTip::write_view(const decltype(show_on_focus)&, bool incoming) {
    Window& owner = require_access("ToolTip focus policy mutation");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.show_on_focus = incoming;
    owner.set_tool_tip_settings(settings);
}

bool ToolTip::read_view(const decltype(show_always)&) const {
    const bool current = require_access("ToolTip settings read").tool_tip_settings().show_when_disabled;
    return current;
}

void ToolTip::write_view(const decltype(show_always)&, bool incoming) {
    Window& owner = require_access("ToolTip enabled-target policy mutation");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.show_when_disabled = incoming;
    owner.set_tool_tip_settings(settings);
}

double ToolTip::read_view(const decltype(maximum_width)&) const {
    const double current = require_access("ToolTip settings read").tool_tip_settings().maximum_width;
    return current;
}

void ToolTip::write_view(const decltype(maximum_width)&, double incoming) {
    Window& owner = require_access("ToolTip maximum width change");
    ToolTipSettings settings = owner.tool_tip_settings();
    settings.maximum_width = incoming;
    owner.set_tool_tip_settings(settings);
}

void ToolTip::show(const std::shared_ptr<Control>& target) {
    Window& owner = require_access("ToolTip explicit show");
    if (!target || !owns(*target)) {
        throw std::invalid_argument("ToolTip explicit show requires a mapped target");
    }
    owner.show_tool_tip(target);
}

void ToolTip::show(const std::shared_ptr<Control>& target, std::chrono::milliseconds duration) {
    Window& owner = require_access("ToolTip explicit show");
    if (duration.count() < 0) {
        throw std::invalid_argument("ToolTip duration may not be negative");
    }
    if (!target || !owns(*target)) {
        throw std::invalid_argument("ToolTip explicit show requires a mapped target");
    }
    owner.show_tool_tip(target, duration);
}

void ToolTip::hide() {
    Window& owner = require_access("ToolTip hide");
    const std::shared_ptr<Control> shown = owner.tool_tip_target();
    if (shown && owner.tool_tip_item_id().empty() && owns(*shown)) owner.hide_tool_tip();
}

bool ToolTip::visible() const {
    const std::shared_ptr<Control> shown = active_control();
    return static_cast<bool>(shown);
}

std::shared_ptr<Control> ToolTip::active_control() const {
    if (!is_alive()) return {};
    Window* owner = bound_window();
    if (owner == nullptr) return {};
    const std::shared_ptr<Control> shown = (*owner).tool_tip_target();
    if (!shown || !(*owner).tool_tip_item_id().empty() || !owns(*shown)) return {};
    return shown;
}

// Forwards the window's event for this ToolTip's targets only; a part's
// tooltip (a strip item's) is never one of them.
void ToolTip::window_tool_tip_changed(const ToolTipEvent& change) {
    if (!change.item_id.empty()) return;
    const TargetMap::const_iterator found = targets_.find(change.target_stable_id);
    if (found == targets_.end()) return;
    visibility_changed_.emit(change);
}

void ToolTip::verify_dispose_thread() {
    if (Window* owner = bound_window()) (*owner).verify_access("ToolTip disposal");
}

void ToolTip::on_dispose() noexcept {
    window_subscription_.disconnect();
    TargetMap cleared = std::move(targets_);
    targets_.clear();
    for (const std::pair<const std::string, std::weak_ptr<Control>>& entry : cleared) {
        const std::shared_ptr<Control> target = entry.second.lock();
        if (!target || !(*target).is_alive() || bound_window() == nullptr) continue;
        try {
            (*target).tool_tip_text = std::string{};
        } catch (...) {
            // A target the window can no longer change keeps its text; its
            // tooltip closes with it.
        }
    }
    window_lifetime_.reset();
}

namespace {

// The events ToolTip adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 1> tool_tip_interactions{{
    {.name = "visibility_changed", .payload = "ToolTipEvent", .mutable_payload = false},
}};

} // namespace

void ToolTip::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), tool_tip_interactions.begin(), tool_tip_interactions.end());
}

// property_codemod begin ToolTip (tools/property_families/tool_tip.json; generated, do not edit)

// property_codemod end ToolTip

} // namespace gui_forms
