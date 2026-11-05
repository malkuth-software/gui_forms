#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp"

#include "ribbon_tool_tip_layer/ribbon_tool_tip_layer.hpp"
#include "ribbon_tool_tip_bubble.hpp"

#include "gui_forms/controls/tool_strip/tool_strip.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/timer.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

std::atomic<std::uint64_t> next_provider_id{1U};

// The overlay's maximum width and its distance from its anchor, in logical
// units.
constexpr double maximum_bubble_width = 318.0;
constexpr double anchor_gap = 4.0;
constexpr double client_inset = 4.0;

bool valid_content(const RibbonToolTipContent& content) {
    const bool result = validate_utf8(content.title).valid() && validate_utf8(content.description).valid() &&
                        validate_utf8(content.shortcut).valid() && validate_utf8(content.disabled_reason).valid() &&
                        validate_utf8(content.help_topic).valid();
    return result;
}

} // namespace

// One mapped control: its own content, its items' content when it is a
// ToolStrip, and the subscriptions that observe it.
struct RibbonToolTip::Entry final {
    std::weak_ptr<Control> target;
    // The content for the control as a whole; none when only its items
    // have content.
    std::optional<RibbonToolTipContent> control_content{};
    std::vector<ItemMapping> items;
    SubscriptionToken pointer;
    SubscriptionToken focus;
    SubscriptionToken strip_focus;
};

struct RibbonToolTip::TargetPointerObserver final {
    RibbonToolTip* provider{};
    std::weak_ptr<Control> target;

    void operator()(const PointerEvent& event) const {
        const std::shared_ptr<Control> retained = target.lock();
        if (retained) (*provider).target_pointer(retained, event);
    }
};

struct RibbonToolTip::TargetFocusObserver final {
    RibbonToolTip* provider{};
    std::weak_ptr<Control> target;

    void operator()(bool focused) const {
        const std::shared_ptr<Control> retained = target.lock();
        if (retained) (*provider).target_focus(retained, focused);
    }
};

struct RibbonToolTip::StripFocusObserver final {
    RibbonToolTip* provider{};
    std::weak_ptr<Control> target;

    void operator()(const ToolStripItemFocus& focus) const {
        const std::shared_ptr<Control> retained = target.lock();
        if (retained) (*provider).strip_focus(retained, focus.item_id);
    }
};

struct RibbonToolTip::PopupRevocationObserver final {
    RibbonToolTip* provider{};
    std::weak_ptr<detail::WindowLifetime> window_lifetime;

    void operator()() const {
        const std::shared_ptr<detail::WindowLifetime> lifetime = window_lifetime.lock();
        if (lifetime && (*lifetime).window != nullptr) (*provider).popup_revoked();
    }
};

class RibbonToolTip::PopupHolder final {
public:
    explicit PopupHolder(PopupToken value) : token(std::move(value)) {}
    PopupToken token;
};

class RibbonToolTip::AcceleratorHolder final {
public:
    explicit AcceleratorHolder(AcceleratorToken value) : token(std::move(value)) {}
    AcceleratorToken token;
};

bool RibbonToolTip::HelpAccelerator::operator()() const {
    const bool handled = (*provider).request_help();
    return handled;
}

RibbonToolTip::RibbonToolTip(Window& window)
    : window_lifetime_(window.lifetime_), timer_(std::make_unique<Timer>(window)),
      provider_id_(next_provider_id.fetch_add(1U)) {
    window.verify_access("RibbonToolTip construction");
    timer_subscription_ = (*timer_).tick().subscribe(
        *this, Delegate<>::bind<RibbonToolTip, &RibbonToolTip::timer_tick>(*this));
    // F1 belongs to a showing tooltip before the focused control and any
    // HelpProvider see it; with no tooltip showing the key passes on.
    AcceleratorOptions options{};
    options.before_focused_route = true;
    accelerator_ = std::make_unique<AcceleratorHolder>(window.register_accelerator(
        *this, KeyGesture{PhysicalKey::f1, Modifier::none}, HelpAccelerator{this}, options));
}

RibbonToolTip::~RibbonToolTip() {
    if (is_alive()) {
        try {
            dispose();
        } catch (...) {
            popup_.reset();
            accelerator_.reset();
            timer_.reset();
        }
    }
}

Window* RibbonToolTip::bound_window() const noexcept {
    const std::shared_ptr<detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

void RibbonToolTip::require_access(std::string_view operation) const {
    if (!is_alive()) throw std::logic_error("GUI.Forms cannot use a disposed RibbonToolTip");
    Window* owner = bound_window();
    if (!owner) throw std::logic_error("GUI.Forms cannot use a RibbonToolTip after Window shutdown");
    (*owner).verify_access(operation);
}

// -- mappings ------------------------------------------------------------------

RibbonToolTip::Entry* RibbonToolTip::find_entry(const Control& target) {
    const EntryMap::iterator found = entries_.find(target.runtime_id().value);
    return found == entries_.end() ? nullptr : (*found).second.get();
}

const RibbonToolTip::Entry* RibbonToolTip::find_entry(const Control& target) const {
    const EntryMap::const_iterator found = entries_.find(target.runtime_id().value);
    return found == entries_.end() ? nullptr : (*found).second.get();
}

RibbonToolTip::Entry& RibbonToolTip::require_entry(const std::shared_ptr<Control>& target) {
    if (Entry* existing = find_entry(*target)) return *existing;
    std::unique_ptr<Entry> entry = std::make_unique<Entry>();
    (*entry).target = target;
    const std::weak_ptr<Control> weak_target = target;
    (*entry).pointer = (*target).pointer_observed().subscribe(*this, TargetPointerObserver{this, weak_target});
    (*entry).focus = (*target).focus_observed().subscribe(*this, TargetFocusObserver{this, weak_target});
    if (const std::shared_ptr<ToolStrip> strip = std::dynamic_pointer_cast<ToolStrip>(target)) {
        (*entry).strip_focus =
            (*strip).item_focus_changed().subscribe(*this, StripFocusObserver{this, weak_target});
    }
    Entry& result = *entry;
    entries_.emplace((*target).runtime_id().value, std::move(entry));
    return result;
}

void RibbonToolTip::set_tool_tip(const std::shared_ptr<Control>& target, RibbonToolTipContent content) {
    require_access("RibbonToolTip mapping mutation");
    Window* owner = bound_window();
    if (!target || !(*target).is_alive() || !(*target).attached() ||
        (*owner).find((*target).stable_id().value()).get() != target.get()) {
        throw std::invalid_argument("GUI.Forms RibbonToolTip target must be a live attached control in its Window");
    }
    // The target's plain tooltip can supply the title.
    if (!valid_content(content) ||
        (content.title.empty() && !content.command && (*target).tool_tip_text.read().empty())) {
        throw std::invalid_argument(
            "GUI.Forms RibbonToolTip content needs valid UTF-8 and a title, a command or a plain tooltip");
    }
    Entry& entry = require_entry(target);
    if (!entry.control_content.has_value()) set_cover(*target, {}, true);
    entry.control_content = std::move(content);
    if (visible_target_.lock() == target && visible_item_.empty()) show_now(target, {}, keyboard_initiated_);
}

void RibbonToolTip::set_item_tool_tip(const std::shared_ptr<ToolStrip>& strip, std::string_view item_id,
                                      RibbonToolTipContent content) {
    require_access("RibbonToolTip mapping mutation");
    Window* owner = bound_window();
    if (!strip || !(*strip).is_alive() || !(*strip).attached() ||
        (*owner).find((*strip).stable_id().value()).get() != strip.get()) {
        throw std::invalid_argument("GUI.Forms RibbonToolTip target must be a live attached control in its Window");
    }
    if (!valid_content(content)) throw std::invalid_argument("GUI.Forms RibbonToolTip content must be valid UTF-8");
    // An item's own command, text or plain tooltip can supply the title.
    bool known = false;
    bool titled = !content.title.empty() || content.command;
    for (const ToolStripItemSpec& item : (*strip).items()) {
        if (item.stable_id != item_id) continue;
        known = true;
        if (item.command || !item.text.empty() || !item.tool_tip_text.empty()) titled = true;
    }
    if (item_id.empty() || !known) throw std::out_of_range("GUI.Forms RibbonToolTip item is not on the strip");
    if (!titled) throw std::invalid_argument("GUI.Forms RibbonToolTip content needs a title or command");
    Entry& entry = require_entry(strip);
    for (ItemMapping& mapping : entry.items) {
        if (mapping.item_id != item_id) continue;
        mapping.content = std::move(content);
        if (visible_target_.lock() == strip && visible_item_ == item_id) {
            show_now(strip, mapping.item_id, keyboard_initiated_);
        }
        return;
    }
    entry.items.push_back({std::string(item_id), std::move(content)});
    set_cover(*strip, item_id, true);
}

void RibbonToolTip::set_cover(const Control& target, std::string_view item_id, bool covered) {
    if (Window* owner = bound_window()) (*owner).cover_tool_tip(target.runtime_id(), item_id, covered);
}

void RibbonToolTip::uncover_all() noexcept {
    Window* owner = bound_window();
    if (owner == nullptr) return;
    for (const std::pair<const std::uint64_t, std::unique_ptr<Entry>>& mapped : entries_) {
        const Entry& entry = *mapped.second;
        const RuntimeId target{mapped.first};
        try {
            if (entry.control_content.has_value()) (*owner).cover_tool_tip(target, {}, false);
            for (const ItemMapping& mapping : entry.items) (*owner).cover_tool_tip(target, mapping.item_id, false);
        } catch (...) {
            // Uncovering only erases map entries; a failed allocation of the
            // key leaves a cover of a mapping that no longer shows anything.
        }
    }
}

const RibbonToolTipContent* RibbonToolTip::find_content(const Entry& entry, std::string_view item_id) const {
    if (item_id.empty()) return entry.control_content.has_value() ? &*entry.control_content : nullptr;
    for (const ItemMapping& mapping : entry.items) {
        if (mapping.item_id == item_id) return &mapping.content;
    }
    return nullptr;
}

bool RibbonToolTip::has_tool_tip(const Control& target, std::string_view item_id) const {
    const Entry* entry = find_entry(target);
    const bool result = entry != nullptr && find_content(*entry, item_id) != nullptr;
    return result;
}

bool RibbonToolTip::remove_tool_tip(const Control& target, std::string_view item_id) {
    require_access("RibbonToolTip mapping removal");
    Entry* entry = find_entry(target);
    if (!entry || !find_content(*entry, item_id)) return false;
    const bool affects_pending = pending_target_.lock().get() == &target && pending_item_ == item_id;
    const bool affects_visible = visible_target_.lock().get() == &target && visible_item_ == item_id;
    if (affects_pending || affects_visible) hide();
    set_cover(target, item_id, false);
    if (item_id.empty()) {
        (*entry).control_content.reset();
    } else {
        std::vector<ItemMapping>::iterator found = (*entry).items.begin();
        while (found != (*entry).items.end() && (*found).item_id != item_id) ++found;
        (*entry).items.erase(found);
    }
    if (!(*entry).control_content.has_value() && (*entry).items.empty()) entries_.erase(target.runtime_id().value);
    return true;
}

void RibbonToolTip::clear() {
    require_access("RibbonToolTip mapping clear");
    hide();
    uncover_all();
    entries_.clear();
}

void RibbonToolTip::set_initial_delay(std::chrono::milliseconds delay) {
    require_access("RibbonToolTip initial-delay mutation");
    if (delay.count() < 0) throw std::invalid_argument("RibbonToolTip delay may not be negative");
    initial_delay_ = delay;
}

void RibbonToolTip::set_auto_pop_delay(std::chrono::milliseconds delay) {
    require_access("RibbonToolTip auto-pop mutation");
    if (delay.count() < 0) throw std::invalid_argument("RibbonToolTip delay may not be negative");
    auto_pop_delay_ = delay;
}

void RibbonToolTip::set_help_line_text(std::string text) {
    require_access("RibbonToolTip help line mutation");
    if (!validate_utf8(text).valid()) throw std::invalid_argument("RibbonToolTip help line must be valid UTF-8");
    help_line_text_ = std::move(text);
}

// -- content -------------------------------------------------------------------

RibbonToolTipContent RibbonToolTip::resolve(const Control& target, std::string_view item_id,
                                            const RibbonToolTipContent& authored) const {
    RibbonToolTipContent result = authored;
    // Without a title of its own, the thing's plain tooltip names it.
    if (result.title.empty()) result.title = plain_tool_tip(target, item_id);
    bool enabled = target.effectively_enabled();
    const ToolStrip* strip = dynamic_cast<const ToolStrip*>(&target);
    if (strip && !item_id.empty()) {
        for (const ToolStripItemSpec& item : (*strip).items()) {
            if (item.stable_id != item_id) continue;
            if (!result.command) result.command = item.command;
            if (!item.command && !item.enabled) enabled = false;
            if (result.title.empty()) result.title = item.text;
        }
    }
    if (result.command) {
        const CommandState& state = (*result.command).state();
        if (result.title.empty()) result.title = state.text;
        if (result.description.empty()) result.description = state.description;
        if (result.shortcut.empty()) result.shortcut = state.shortcut;
        if (result.disabled_reason.empty()) result.disabled_reason = state.availability_reason;
        if (!state.enabled) enabled = false;
    }
    if (enabled) result.disabled_reason.clear();
    return result;
}

std::string RibbonToolTip::plain_tool_tip(const Control& target, std::string_view item_id) const {
    if (item_id.empty()) return target.tool_tip_text.read();
    const std::vector<SemanticNode> nodes = target.semantic_virtual_children();
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == item_id) return node.tool_tip;
    }
    return {};
}

Rect RibbonToolTip::anchor_bounds(const Control& target, std::string_view item_id) const {
    const Rect whole = target.absolute_bounds();
    if (item_id.empty()) return whole;
    // An item's bounds come from the strip's published semantic nodes, which
    // are in window coordinates.
    const std::vector<SemanticNode> nodes = target.semantic_virtual_children();
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == item_id) return node.bounds;
    }
    return whole;
}

// -- triggers ------------------------------------------------------------------

void RibbonToolTip::target_pointer(const std::shared_ptr<Control>& target, const PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        if (pending_target_.lock() == target || visible_target_.lock() == target) hide();
        return;
    }
    if (event.action != PointerAction::enter && event.action != PointerAction::move) {
        // A press dismisses the tooltip, as ToolTip's owner popups are.
        if (event.action == PointerAction::down &&
            (pending_target_.lock() == target || visible_target_.lock() == target)) {
            hide();
        }
        return;
    }
    std::string item_id;
    if (const std::shared_ptr<ToolStrip> strip = std::dynamic_pointer_cast<ToolStrip>(target)) {
        item_id = (*strip).item_id_at(event.position);
    }
    const Entry* entry = find_entry(*target);
    if (!entry) return;
    // An item without its own mapping shows the strip's, if any.
    if (!item_id.empty() && !find_content(*entry, item_id)) item_id.clear();
    const bool same_pending = pending_target_.lock() == target && pending_item_ == item_id;
    const bool same_visible = visible_target_.lock() == target && visible_item_ == item_id;
    if (same_pending || same_visible) return;
    if (pending_target_.lock() == target || visible_target_.lock() == target) hide();
    if (find_content(*entry, item_id)) schedule_show(target, item_id, false);
}

void RibbonToolTip::target_focus(const std::shared_ptr<Control>& target, bool focused) {
    if (!focused) {
        if (pending_target_.lock() == target || visible_target_.lock() == target) hide();
        return;
    }
    // A strip reports its focused item through item_focus_changed.
    if (std::dynamic_pointer_cast<ToolStrip>(target)) return;
    const Entry* entry = find_entry(*target);
    if (entry && find_content(*entry, {})) schedule_show(target, {}, true);
}

void RibbonToolTip::strip_focus(const std::shared_ptr<Control>& target, const std::string& item_id) {
    const Entry* entry = find_entry(*target);
    if (!entry) return;
    std::string shown_id = item_id;
    if (!find_content(*entry, shown_id)) shown_id.clear();
    const bool same_visible = visible_target_.lock() == target && visible_item_ == shown_id;
    if (same_visible) return;
    if (pending_target_.lock() == target || visible_target_.lock() == target) hide();
    if (find_content(*entry, shown_id)) schedule_show(target, shown_id, true);
}

void RibbonToolTip::schedule_show(const std::shared_ptr<Control>& target, std::string_view item_id,
                                  bool keyboard_initiated) {
    if (!(*target).effectively_visible()) return;
    pending_target_ = target;
    pending_item_ = std::string(item_id);
    keyboard_initiated_ = keyboard_initiated;
    arm(initial_delay_, PendingAction::show);
}

void RibbonToolTip::arm(std::chrono::milliseconds delay, PendingAction action) {
    if ((*timer_).enabled()) (*timer_).stop();
    pending_action_ = action;
    if (delay.count() == 0) {
        timer_tick();
        return;
    }
    (*timer_).set_interval(delay);
    (*timer_).start();
}

void RibbonToolTip::timer_tick() {
    if ((*timer_).enabled()) (*timer_).stop();
    const PendingAction action = std::exchange(pending_action_, PendingAction::none);
    switch (action) {
    case PendingAction::show: {
        const std::shared_ptr<Control> target = pending_target_.lock();
        const std::string item_id = std::exchange(pending_item_, std::string{});
        pending_target_.reset();
        if (target) show_now(target, item_id, keyboard_initiated_);
        return;
    }
    case PendingAction::auto_hide:
        close_overlay(true);
        return;
    case PendingAction::none:
        // The timer fired with nothing pending.
        return;
    }
    // An action outside the enumeration does nothing.
    return;
}

// -- overlay -------------------------------------------------------------------

void RibbonToolTip::show(const std::shared_ptr<Control>& target, std::string_view item_id) {
    require_access("RibbonToolTip explicit show");
    const Entry* entry = target ? find_entry(*target) : nullptr;
    if (!entry || !find_content(*entry, item_id)) {
        throw std::invalid_argument("RibbonToolTip explicit show requires a mapped target");
    }
    show_now(target, std::string(item_id), true);
}

void RibbonToolTip::show_now(const std::shared_ptr<Control>& target, const std::string& item_id,
                             bool keyboard_initiated) {
    Window* owner = bound_window();
    const Entry* entry = target ? find_entry(*target) : nullptr;
    const RibbonToolTipContent* authored = entry ? find_content(*entry, item_id) : nullptr;
    if (!owner || !authored || !(*target).effectively_visible()) return;
    if ((*timer_).enabled()) (*timer_).stop();
    pending_action_ = PendingAction::none;
    pending_target_.reset();
    pending_item_.clear();
    if (visible()) close_overlay(true);

    RibbonToolTipContent resolved = resolve(*target, item_id, *authored);
    RibbonToolTipBubbleContent shown;
    shown.title = resolved.title;
    if (!resolved.shortcut.empty()) shown.shortcut = "(" + resolved.shortcut + ")";
    shown.description = resolved.description;
    shown.disabled_reason = resolved.disabled_reason;
    if (!resolved.help_topic.empty()) shown.help_line = help_line_text_;
    shown.image = resolved.image;
    const RibbonToolTipLayout layout =
        layout_ribbon_tool_tip(*target, shown, maximum_bubble_width * (*target).effective_text_scale());
    const Size size = layout.size;

    const std::string prefix = "ribbon-tooltip." + std::to_string(provider_id_) + "." +
                         std::to_string((*target).runtime_id().value);
    std::shared_ptr<RibbonToolTipLayer> layer = make_control<RibbonToolTipLayer>(StableId(prefix + ".layer"));
    (*layer).set_requested_bounds({0.0, 0.0, (*owner).client_size().width, (*owner).client_size().height});
    std::shared_ptr<RibbonToolTipBubble> bubble =
        make_control<RibbonToolTipBubble>(StableId(prefix + ".bubble"), std::move(shown), layout);
    // Below the anchor, left-aligned with it; above it when the client has
    // no room below; always inside the client.
    const Rect anchor = anchor_bounds(*target, item_id);
    const Size client = (*owner).client_size();
    double x = anchor.x;
    double y = anchor.y + anchor.height + anchor_gap;
    if (y + size.height > client.height - client_inset) y = anchor.y - size.height - anchor_gap;
    x = std::clamp(x, client_inset, std::max(client_inset, client.width - size.width - client_inset));
    y = std::clamp(y, client_inset, std::max(client_inset, client.height - size.height - client_inset));
    (*bubble).set_requested_bounds({x, y, size.width, size.height});
    (*layer).add_child(bubble);

    // The tooltip describes disabled targets too, so the owner need not be
    // enabled.
    PopupToken token = (*owner).open_popup(target, layer, PopupOptions{.require_enabled_owner = false});
    overlay_layer_ = layer;
    overlay_bubble_ = bubble;
    visible_target_ = target;
    visible_item_ = item_id;
    keyboard_initiated_ = keyboard_initiated;
    shown_content_ = resolved;
    popup_ = std::make_unique<PopupHolder>(std::move(token));
    const std::weak_ptr<detail::WindowLifetime> weak_window = window_lifetime_;
    if (Event<>* closed = (*popup_).token.closed_event()) {
        popup_subscription_ = (*closed).subscribe(*this, PopupRevocationObserver{this, weak_window});
    }
    const RibbonToolTipEvent change{std::string((*target).stable_id().value()), item_id, resolved, true,
                                    keyboard_initiated};
    visibility_changed_.emit(change);
    if (auto_pop_delay_.count() > 0) arm(auto_pop_delay_, PendingAction::auto_hide);
}

void RibbonToolTip::hide() {
    require_access("RibbonToolTip hide");
    pending_target_.reset();
    pending_item_.clear();
    pending_action_ = PendingAction::none;
    if ((*timer_).enabled()) (*timer_).stop();
    close_overlay(true);
}

bool RibbonToolTip::visible() const noexcept {
    const bool result = popup_ && (*popup_).token.connected();
    return result;
}

std::shared_ptr<Control> RibbonToolTip::active_control() const noexcept {
    return visible_target_.lock();
}

Rect RibbonToolTip::overlay_bounds() const {
    if (!visible() || !overlay_bubble_) return {};
    return (*overlay_bubble_).requested_bounds();
}

void RibbonToolTip::close_overlay(bool emit_change) {
    const std::shared_ptr<Control> target = visible_target_.lock();
    const bool was_visible = visible();
    popup_subscription_.disconnect();
    std::unique_ptr<PopupHolder> popup = std::move(popup_);
    // As in ToolTip: keep the overlay tree alive until disconnect lets the
    // Window detach it and release its stable IDs.
    if (popup) (*popup).token.disconnect();
    overlay_layer_.reset();
    overlay_bubble_.reset();
    visible_target_.reset();
    const std::string item_id = std::exchange(visible_item_, std::string{});
    RibbonToolTipContent content = std::exchange(shown_content_, RibbonToolTipContent{});
    if (emit_change && was_visible && target) {
        const RibbonToolTipEvent change{std::string((*target).stable_id().value()), item_id, std::move(content),
                                        false, keyboard_initiated_};
        visibility_changed_.emit(change);
    }
}

void RibbonToolTip::popup_revoked() {
    const std::shared_ptr<Control> target = visible_target_.lock();
    popup_subscription_.disconnect();
    popup_.reset();
    overlay_layer_.reset();
    overlay_bubble_.reset();
    visible_target_.reset();
    const std::string item_id = std::exchange(visible_item_, std::string{});
    RibbonToolTipContent content = std::exchange(shown_content_, RibbonToolTipContent{});
    if (target) {
        const RibbonToolTipEvent change{std::string((*target).stable_id().value()), item_id, std::move(content),
                                        false, keyboard_initiated_};
        visibility_changed_.emit(change);
    }
}

// -- help ----------------------------------------------------------------------

bool RibbonToolTip::request_help() {
    if (!is_alive() || !visible() || shown_content_.help_topic.empty()) return false;
    const std::shared_ptr<Control> target = visible_target_.lock();
    if (!target) return false;
    const RibbonToolTipHelpRequest request{std::string((*target).stable_id().value()), visible_item_,
                                           shown_content_.help_topic};
    // The help surface the application opens replaces the tooltip.
    hide();
    help_requested_.emit(request);
    return true;
}

// -- lifetime ------------------------------------------------------------------

void RibbonToolTip::verify_dispose_thread() {
    if (Window* owner = bound_window()) (*owner).verify_access("RibbonToolTip disposal");
}

void RibbonToolTip::on_dispose() noexcept {
    try {
        pending_target_.reset();
        pending_action_ = PendingAction::none;
        if (timer_ && (*timer_).is_alive()) (*timer_).dispose();
        close_overlay(false);
        uncover_all();
        entries_.clear();
    } catch (...) {
    }
    accelerator_.reset();
    timer_subscription_.disconnect();
    timer_.reset();
    window_lifetime_.reset();
}

namespace {

// The events RibbonToolTip adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 2> ribbon_tool_tip_interactions{{
    {.name = "visibility_changed", .payload = "RibbonToolTipEvent", .mutable_payload = false},
    {.name = "help_requested", .payload = "RibbonToolTipHelpRequest", .mutable_payload = false},
}};

} // namespace

void RibbonToolTip::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), ribbon_tool_tip_interactions.begin(), ribbon_tool_tip_interactions.end());
}

} // namespace gui_forms
