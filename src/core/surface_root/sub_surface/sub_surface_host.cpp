#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include "gui_forms/window.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace gui_forms {

namespace {

// The device extent of a logical one at `scale`, rounded outward (resize
// rule 2); zero when it is empty or does not fit.
[[nodiscard]] std::uint32_t device_extent(double logical, double scale) noexcept {
    const double extent = std::ceil(logical * scale);
    if (!std::isfinite(extent) || extent < 1.0) return 0U;
    if (extent > static_cast<double>(std::numeric_limits<std::int32_t>::max())) return 0U;
    return static_cast<std::uint32_t>(extent);
}

// A request's tooltip item ID among the host's items.
constexpr std::string_view request_item_prefix = "sub-surface-request.";

[[nodiscard]] std::string request_item_id(std::uint64_t id) {
    std::string item(request_item_prefix);
    item += std::to_string(id);
    return item;
}

// The request ID an item ID names; zero for an ID that is not a request's.
[[nodiscard]] std::uint64_t request_id_of(std::string_view item_id) noexcept {
    if (item_id.substr(0, request_item_prefix.size()) != request_item_prefix) return 0U;
    const std::string_view digits = item_id.substr(request_item_prefix.size());
    std::uint64_t id = 0U;
    const std::from_chars_result parsed = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) return 0U;
    return id;
}

} // namespace

class SubSurfaceHost::RequestTarget final : public SubSurfaceRequestTarget {
public:
    explicit RequestTarget(SubSurfaceHost& host) noexcept : host_(&host) {}
    void receive_request(const SubSurfaceRequest& request) override {
        if (host_ == nullptr) return;
        (*host_).receive_request(request);
    }
    void receive_revoke(std::uint64_t id) override {
        if (host_ == nullptr) return;
        (*host_).receive_revoke(id);
    }
    // The host stopped this target's thread or is going away.
    void revoke() noexcept { host_ = nullptr; }

private:
    // Observes the host that owns this object; null once revoked.
    SubSurfaceHost* host_{};
};

class SubSurfaceHost::AlertRelay final : public SurfaceRequestAnswer {
public:
    explicit AlertRelay(SubSurfaceHost& host) noexcept : host_(&host) {}
    void request_answered(const SurfaceRequestOutcome& outcome) override {
        if (host_ == nullptr) return;
        (*host_).relay_alert(outcome);
    }
    // The host stopped the thread these alerts were raised for.
    void revoke() noexcept { host_ = nullptr; }

private:
    // Observes the host that owns this object; null once revoked.
    SubSurfaceHost* host_{};
};

class SubSurfaceHost::KeyReturn final : public SubSurfaceKeyReturn {
public:
    explicit KeyReturn(SubSurfaceHost& host) noexcept : host_(&host) {}
    void return_unhandled_key(const KeyEvent& event, std::uint64_t) override {
        if (host_ == nullptr) return;
        (*host_).continue_returned_key(event);
    }
    // The host stopped this target's thread or is going away.
    void revoke() noexcept { host_ = nullptr; }

private:
    // Observes the host that owns this object; null once revoked.
    SubSurfaceHost* host_{};
};

SubSurfaceHost::SubSurfaceHost(StableId stable_id)
    : Control(std::move(stable_id)) {
    set_focusable(true);
    // The sub-surface's tooltip requests are this host's tooltip parts.
    set_publishes_item_tool_tips(true);
}

SubSurfaceHost::~SubSurfaceHost() {
    stop_thread();
}

void SubSurfaceHost::set_content(std::unique_ptr<SubSurfaceContent> content, bool opaque) {
    require_mutable();
    const bool had_requests = !requests_.empty();
    stop_thread();
    // A tooltip the old content asked for closes.
    SurfaceRoot* const root = attached_surface_root();
    if (had_requests && root != nullptr) (*root).tool_tip_items_changed(*this);
    content_ = std::move(content);
    opaque_ = opaque;
    configure_region();
    invalidate(Dirty::paint);
}

SubSurfaceState SubSurfaceHost::state() const {
    if (!thread_) return SubSurfaceState::stopped;
    const SubSurfaceState state = (*thread_).state();
    return state;
}

std::string SubSurfaceHost::failure() const {
    if (!thread_) return std::string{};
    std::string failure = (*thread_).failure();
    return failure;
}

SubSurfaceStatistics SubSurfaceHost::statistics() const {
    if (!thread_) return SubSurfaceStatistics{};
    const SubSurfaceStatistics statistics = (*thread_).statistics();
    return statistics;
}

WindowDispatcher SubSurfaceHost::dispatcher() const {
    if (!thread_) return WindowDispatcher{};
    const WindowDispatcher dispatcher = (*thread_).dispatcher();
    return dispatcher;
}

void SubSurfaceHost::arrange(Rect final_bounds) {
    Control::arrange(final_bounds);
    configure_region();
}

void SubSurfaceHost::on_paint(Painter& painter, Rect) {
    if (!surface_) return;
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    painter.draw_live_surface(surface_, bounds, 1.0);
}

namespace {

constexpr std::array<PartField<SubSurfaceHost::Parts>, 3> sub_surface_host_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &SubSurfaceHost::Parts::region},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &SubSurfaceHost::Parts::content},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &SubSurfaceHost::Parts::focus},
}};

constexpr std::array<StateField<SubSurfaceHost::States>, 5> sub_surface_host_state_fields{{
    {.state = AnatomyState::normal, .member = &SubSurfaceHost::States::normal},
    {.state = AnatomyState::enabled, .member = &SubSurfaceHost::States::enabled},
    {.state = AnatomyState::disabled, .member = &SubSurfaceHost::States::disabled},
    {.state = AnatomyState::focused, .member = &SubSurfaceHost::States::focused},
    {.state = AnatomyState::focus_visible, .member = &SubSurfaceHost::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 3> sub_surface_host_parts = part_declarations(sub_surface_host_part_fields);
constexpr std::array<AnatomyState, 5> sub_surface_host_states = state_declarations(sub_surface_host_state_fields);

constexpr ControlAnatomy sub_surface_host_anatomy{
    .control_id = "sub_surface_host",
    .parts = sub_surface_host_parts,
    .states = sub_surface_host_states,
    .custom_states = {},
};

} // namespace

SemanticDescriptor SubSurfaceHost::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::pane;
    descriptor.name = accessible_name.read().empty() ? "Sub-surface" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

const ControlAnatomy& SubSurfaceHost::anatomy() const noexcept {
    return sub_surface_host_anatomy;
}

void SubSurfaceHost::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    // The composed frame covers the region once there is a surface to show.
    if (surface_) record.content = PartGeometry::from_bounds(bounds);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), sub_surface_host_part_fields, record, geometry);
}

SubSurfaceHost::Parts SubSurfaceHost::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), sub_surface_host_part_fields, published.parts(), record);
    return record;
}

SubSurfaceHost::States SubSurfaceHost::states() const {
    States record;
    read_states(sub_surface_host_state_fields, anatomy_states(), record);
    return record;
}

void SubSurfaceHost::on_attached_to_window() {
    Control::on_attached_to_window();
    Window* const window = attached_window();
    if (window != nullptr) {
        theme_subscription_ = (*window).theme_changed().subscribe(
            *this, Delegate<const Theme&>::bind<SubSurfaceHost, &SubSurfaceHost::on_window_theme_changed>(*this));
        presentation_subscription_ = (*window).presentation_changed().subscribe(
            *this, Delegate<const PresentationSettings&>::bind<
                       SubSurfaceHost, &SubSurfaceHost::on_window_presentation_changed>(*this));
        active_subscription_ = (*window).active_changed().subscribe(
            *this, Delegate<bool>::bind<SubSurfaceHost, &SubSurfaceHost::on_window_active_changed>(*this));
        tool_tip_subscription_ = (*window).tool_tip_visibility_changed().subscribe(
            *this, Delegate<const ToolTipEvent&>::bind<SubSurfaceHost, &SubSurfaceHost::on_window_tool_tip>(*this));
    }
    configure_region();
}

void SubSurfaceHost::on_window_theme_changed(const Theme&) {
    environment_changed_ = true;
    configure_region();
}

void SubSurfaceHost::on_window_presentation_changed(const PresentationSettings&) {
    environment_changed_ = true;
    configure_region();
}

void SubSurfaceHost::on_window_active_changed(bool) {
    update_keyboard_focus(SubSurfaceFocusEntry::restore);
}

void SubSurfaceHost::on_pointer(PointerEvent& event) {
    if (!thread_) return;
    hold_capture_for(event);
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::pointer;
    input.pointer = event;
    input.pointer.position = point_from_window(event.position);
    input.pointer.phase = EventPhase::target;
    input.pointer.handled = false;
    // Entering the host is a move inside the sub-surface: its own hover
    // starts there.
    if (input.pointer.action == PointerAction::enter) input.pointer.action = PointerAction::move;
    const std::uint64_t sequence = (*thread_).post_input(input);
    event.handled = true;
    if (input.pointer.action != PointerAction::leave) return;
    // Requests the sub-surface made before it sees this leave are stale.
    pointer_left_sequence_ = sequence;
    revoke_requests(SubSurfaceToolTipOrigin::pointer);
}

void SubSurfaceHost::hold_capture_for(const PointerEvent& event) {
    // The primary button is the window's: it captures on the press and
    // releases before routing the release.
    SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr || event.button == PointerButton::none || event.button == PointerButton::primary) return;
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(event.button));
    const Control::Ptr captured = (*root).captured_control();
    const bool holding = captured.get() == this;
    if (event.action == PointerAction::down) {
        if (holding) {
            pressed_buttons_ = static_cast<std::uint8_t>(pressed_buttons_ | bit);
            return;
        }
        // Not holding: any buttons recorded before lost their capture
        // (revoked, or released by the window) and send no release here.
        pressed_buttons_ = bit;
        (*root).capture_pointer(shared_from_this(), event.pointer_id == 0U ? 1U : event.pointer_id);
        took_capture_ = true;
        return;
    }
    if (event.action != PointerAction::up) return;
    pressed_buttons_ = static_cast<std::uint8_t>(pressed_buttons_ & static_cast<std::uint8_t>(~bit));
    if (pressed_buttons_ != 0U) return;
    // Only a capture this host took is released here; one the window took
    // for a primary press is the window's to release.
    if (holding && took_capture_) (*root).release_pointer();
    took_capture_ = false;
}

std::optional<Rect> SubSurfaceHost::text_input_caret() const {
    if (!thread_) return std::nullopt;
    const std::optional<Rect> caret = (*thread_).text_input_caret();
    return caret;
}

void SubSurfaceHost::on_key(KeyEvent& event) {
    if (!thread_) return;
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::key;
    input.key = event;
    input.key.phase = EventPhase::target;
    input.key.handled = false;
    static_cast<void>((*thread_).post_input(input));
    event.handled = true;
}

void SubSurfaceHost::on_text_input(TextInputEvent& event) {
    if (!thread_) return;
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::text;
    input.text = event;
    input.text.handled = false;
    static_cast<void>((*thread_).post_input(input));
    event.handled = true;
}

void SubSurfaceHost::on_focus_changed(bool focused) {
    Control::on_focus_changed(focused);
    SubSurfaceFocusEntry entry = SubSurfaceFocusEntry::restore;
    SurfaceRoot* const root = attached_surface_root();
    if (focused && root != nullptr) {
        if ((*root).last_focus_traversal_ == SurfaceRoot::FocusTraversal::forward) {
            entry = SubSurfaceFocusEntry::first;
        } else if ((*root).last_focus_traversal_ == SurfaceRoot::FocusTraversal::backward) {
            entry = SubSurfaceFocusEntry::last;
        }
    }
    update_keyboard_focus(entry);
}

void SubSurfaceHost::update_keyboard_focus(SubSurfaceFocusEntry entry) {
    SurfaceRoot* const root = attached_surface_root();
    bool inside = false;
    if (thread_ && root != nullptr && (*root).active()) {
        const Control::Ptr focused = (*root).focused_control();
        inside = focused.get() == this;
    }
    if (inside == keyboard_inside_) return;
    keyboard_inside_ = inside;
    SubSurfaceInput input;
    input.kind = inside ? SubSurfaceInputKind::focus_enter : SubSurfaceInputKind::focus_leave;
    input.focus_entry = entry;
    const std::uint64_t sequence = (*thread_).post_input(input);
    if (inside) return;
    focus_left_sequence_ = sequence;
    revoke_requests(SubSurfaceToolTipOrigin::keyboard);
}

std::vector<SemanticNode> SubSurfaceHost::tool_tip_items() const {
    std::vector<SemanticNode> items;
    if (requests_.empty()) return items;
    items.reserve(requests_.size());
    const Rect origin = absolute_bounds();
    for (const SubSurfaceRequest& request : requests_) {
        SemanticNode item;
        item.runtime_id = runtime_id().value;
        item.stable_id = request_item_id(request.id);
        item.role = SemanticRole::generic;
        item.description = request.description;
        item.tool_tip = request.text;
        item.states = SemanticState::enabled;
        if (request.origin != SubSurfaceToolTipOrigin::program) {
            item.bounds = Rect{origin.x + request.anchor.x, origin.y + request.anchor.y, request.anchor.width,
                               request.anchor.height};
        }
        if (request.origin == SubSurfaceToolTipOrigin::keyboard) item.states |= SemanticState::focused;
        items.push_back(std::move(item));
    }
    return items;
}

void SubSurfaceHost::receive_request(const SubSurfaceRequest& request) {
    ++request_statistics_.received;
    if (request.kind == SubSurfaceRequestKind::alert) {
        receive_alert(request);
        return;
    }
    Window* const window = attached_window();
    const bool stale = request.content_epoch != content_epoch_ ||
                       (request.origin == SubSurfaceToolTipOrigin::pointer &&
                        request.input_sequence < pointer_left_sequence_) ||
                       (request.origin == SubSurfaceToolTipOrigin::keyboard &&
                        request.input_sequence < focus_left_sequence_);
    if (stale) {
        ++request_statistics_.stale;
        answer(request.id, SurfaceRequestResult::stale);
        return;
    }
    bool allowed = window != nullptr;
    if (allowed && request.origin == SubSurfaceToolTipOrigin::pointer) {
        allowed = (*window).tool_tips_shown() && (*window).tool_tip_settings().show_on_hover;
    } else if (allowed && request.origin == SubSurfaceToolTipOrigin::keyboard) {
        allowed = (*window).tool_tips_shown() && (*window).tool_tip_settings().show_on_focus;
    }
    if (!allowed) {
        ++request_statistics_.declined;
        answer(request.id, SurfaceRequestResult::declined);
        return;
    }
    // The sub-surface revoked any earlier request of this origin first;
    // one left over is replaced.
    std::vector<SubSurfaceRequest>::iterator kept = requests_.begin();
    for (std::vector<SubSurfaceRequest>::iterator current = requests_.begin(); current != requests_.end();
         ++current) {
        if ((*current).origin == request.origin) continue;
        if (kept != current) *kept = std::move(*current);
        ++kept;
    }
    requests_.erase(kept, requests_.end());
    requests_.push_back(request);
    if (request.origin == SubSurfaceToolTipOrigin::program) {
        const Rect origin = absolute_bounds();
        const Rect anchor{origin.x + request.anchor.x, origin.y + request.anchor.y, request.anchor.width,
                          request.anchor.height};
        (*window).show_requested_tool_tip(shared_from_this(), request_item_id(request.id), request.text, anchor,
                                          request.duration);
        return;
    }
    (*window).tool_tip_items_changed(*this);
}

void SubSurfaceHost::receive_alert(const SubSurfaceRequest& request) {
    // The host's root decides: a window shows it, a sub-surface (a nested
    // host) forwards it again.
    SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr || !alert_relay_) {
        ++request_statistics_.declined;
        answer(request.id, SurfaceRequestResult::declined);
        return;
    }
    AlertRequest content;
    content.text = request.text;
    content.caption = request.caption;
    content.buttons = request.buttons;
    content.icon = request.icon;
    content.default_choice = request.default_choice;
    // The sub-surface validated the content as raise_alert does here.
    const std::uint64_t raised = (*root).raise_alert(shared_from_this(), std::move(content), alert_relay_);
    RaisedAlert record;
    record.raised = raised;
    record.request = request.id;
    alerts_.push_back(record);
    ++request_statistics_.alerts;
}

void SubSurfaceHost::relay_alert(const SurfaceRequestOutcome& outcome) {
    std::vector<RaisedAlert>::iterator found = alerts_.begin();
    while (found != alerts_.end() && (*found).raised != outcome.id) ++found;
    if (found == alerts_.end()) return;
    const std::uint64_t request = (*found).request;
    alerts_.erase(found);
    answer(request, outcome.result, outcome.choice);
}

void SubSurfaceHost::receive_revoke(std::uint64_t id) {
    for (std::vector<RaisedAlert>::iterator alert = alerts_.begin(); alert != alerts_.end(); ++alert) {
        if ((*alert).request != id) continue;
        const std::uint64_t raised = (*alert).raised;
        alerts_.erase(alert);
        ++request_statistics_.withdrawn;
        SurfaceRoot* const root = attached_surface_root();
        if (root != nullptr) (*root).withdraw_alert(raised);
        return;
    }
    std::vector<SubSurfaceRequest>::iterator found = requests_.begin();
    while (found != requests_.end() && (*found).id != id) ++found;
    if (found == requests_.end()) return;
    requests_.erase(found);
    ++request_statistics_.withdrawn;
    SurfaceRoot* const root = attached_surface_root();
    if (root != nullptr) (*root).tool_tip_items_changed(*this);
}

void SubSurfaceHost::revoke_requests(SubSurfaceToolTipOrigin origin) {
    bool revoked = false;
    std::vector<SubSurfaceRequest>::iterator kept = requests_.begin();
    for (std::vector<SubSurfaceRequest>::iterator current = requests_.begin(); current != requests_.end();
         ++current) {
        if ((*current).origin == origin) {
            ++request_statistics_.revoked;
            answer((*current).id, SurfaceRequestResult::revoked);
            revoked = true;
            continue;
        }
        if (kept != current) *kept = std::move(*current);
        ++kept;
    }
    requests_.erase(kept, requests_.end());
    SurfaceRoot* const root = attached_surface_root();
    if (revoked && root != nullptr) (*root).tool_tip_items_changed(*this);
}

void SubSurfaceHost::answer(std::uint64_t id, SurfaceRequestResult result, HostDialogChoice choice) {
    if (!thread_) return;
    SurfaceRequestOutcome outcome;
    outcome.id = id;
    outcome.result = result;
    outcome.choice = choice;
    (*thread_).post_request_outcome(outcome);
}

void SubSurfaceHost::on_window_tool_tip(const ToolTipEvent& change) {
    if (change.target_stable_id != stable_id().value()) return;
    const std::uint64_t id = request_id_of(change.item_id);
    if (id == 0U) return;
    std::vector<SubSurfaceRequest>::const_iterator found = requests_.cbegin();
    while (found != requests_.cend() && (*found).id != id) ++found;
    if (found == requests_.cend()) return;
    if (change.shown) ++request_statistics_.shown;
    answer(id, change.shown ? SurfaceRequestResult::shown : SurfaceRequestResult::closed);
}

void SubSurfaceHost::continue_returned_key(const KeyEvent& event) {
    SurfaceRoot* const root = attached_surface_root();
    const Control::Ptr focused = root != nullptr ? (*root).focused_control() : Control::Ptr{};
    if (focused.get() != this) {
        ++returned_keys_discarded_;
        return;
    }
    static_cast<void>((*root).resume_unhandled_key(focused, event));
}

void SubSurfaceHost::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    theme_subscription_.disconnect();
    presentation_subscription_.disconnect();
    active_subscription_.disconnect();
    tool_tip_subscription_.disconnect();
    // The window drops the LiveSurface registration with the control; the
    // thread is revoked and joined here, while the window still exists.
    stop_thread();
    Control::on_detaching_from_window(former_window);
}

void SubSurfaceHost::configure_region() {
    // A host inside a sub-surface has no window to compose into; it shows
    // nothing (nesting is not part of stage S2).
    Window* const window = attached_window();
    if (window == nullptr) return;
    if (!thread_ && !content_) return;
    const Rect bounds = committed_arranged_bounds();
    const double scale = (*window).scale();
    const std::uint32_t pixel_width = device_extent(bounds.width, scale);
    const std::uint32_t pixel_height = device_extent(bounds.height, scale);
    if (pixel_width == 0U || pixel_height == 0U) return;
    // Per axis (resize rules): only a changed device size or scale
    // reconfigures; a new arrangement at the same size signals nothing.
    const bool unchanged = thread_ && !environment_changed_ && pixel_width == pixel_width_ &&
                           pixel_height == pixel_height_ && scale == configured_scale_;
    if (unchanged) return;
    const Size logical{bounds.width, bounds.height};
    if (!thread_) {
        LiveSurfaceDescription description;
        description.width = pixel_width;
        description.height = pixel_height;
        description.opaque = opaque_;
        std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
        if (!surface) return;
        std::unique_ptr<PaintFramebuffer> text_measure = (*window).create_framebuffer(Size{1.0, 1.0}, scale);
        const std::shared_ptr<ApplicationResources> start_resources = (*window).resources();
        // The parent's user theme: layered, it is layered there too; replacing,
        // it is theme_ptr() already.
        std::shared_ptr<const Theme> layered_user_theme{};
        if ((*start_resources).user_theme_policy() == UserThemePolicy::layered) {
            layered_user_theme = (*start_resources).user_theme_ptr();
        }
        thread_ = std::make_unique<SubSurfaceThread>(std::move(content_), surface, (*start_resources).theme_ptr(),
                                                     std::move(text_measure), std::move(layered_user_theme));
        surface_ = std::move(surface);
        key_return_ = std::make_shared<KeyReturn>(*this);
        (*thread_).set_key_return((*window).dispatcher(), key_return_);
        request_target_ = std::make_shared<RequestTarget>(*this);
        alert_relay_ = std::make_shared<AlertRelay>(*this);
        (*thread_).set_request_target((*window).dispatcher(), request_target_);
        pointer_left_sequence_ = 0U;
        focus_left_sequence_ = 0U;
        keyboard_inside_ = false;
        update_keyboard_focus(SubSurfaceFocusEntry::restore);
    }
    ++content_epoch_;
    SubSurfaceConfiguration configuration;
    configuration.content_epoch = content_epoch_;
    configuration.logical_size = logical;
    configuration.scale = scale;
    configuration.pixel_width = pixel_width;
    configuration.pixel_height = pixel_height;
    configuration.opaque = opaque_;
    // A framebuffer only for a new device size: an environment change
    // repaints into the one the sub-surface has.
    const bool resized = pixel_width != pixel_width_ || pixel_height != pixel_height_ || scale != configured_scale_;
    if (resized) configuration.framebuffer = (*window).create_framebuffer(logical, scale);
    const std::shared_ptr<ApplicationResources> resources = (*window).resources();
    configuration.theme = (*resources).theme_ptr();
    configuration.presentation = (*window).presentation_settings();
    environment_changed_ = false;
    (*thread_).configure(std::move(configuration));
    pixel_width_ = pixel_width;
    pixel_height_ = pixel_height;
    configured_scale_ = scale;
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*window).queue_live_surface_presentation(self, surface_));
    static_cast<void>((*window).require_live_surface_content(self, content_epoch_));
    invalidate(Dirty::paint);
}

void SubSurfaceHost::stop_thread() noexcept {
    if (!thread_) return;
    // Keys already posted back find the target revoked.
    (*key_return_).revoke();
    key_return_.reset();
    // Requests already posted find the target revoked; the open ones go
    // (a tooltip showing one closes with the host's overlay or at the next
    // items change).
    (*request_target_).revoke();
    request_target_.reset();
    requests_.clear();
    // Alerts still waiting are withdrawn; one showing closes into the
    // revoked relay, unanswered.
    (*alert_relay_).revoke();
    alert_relay_.reset();
    SurfaceRoot* const root = attached_surface_root();
    for (const RaisedAlert& alert : alerts_) {
        if (root != nullptr) (*root).withdraw_alert(alert.raised);
    }
    alerts_.clear();
    keyboard_inside_ = false;
    pressed_buttons_ = 0U;
    took_capture_ = false;
    (*thread_).stop();
    thread_.reset();
    pixel_width_ = 0U;
    pixel_height_ = 0U;
    configured_scale_ = 0.0;
}

} // namespace gui_forms
