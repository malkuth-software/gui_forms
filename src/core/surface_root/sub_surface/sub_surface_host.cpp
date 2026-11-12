#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include "gui_forms/window.hpp"
#include "../access/surface_request_access.hpp"
#include "../access/surface_forwarding_access.hpp"

#include <algorithm>
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

// Translates the sub-surface's nodes into the window, in place (the class
// comment of SubSurfaceHost): `prefix` is the host's stable identity and a
// '.', `offset` the host's position in the window, `keep_focus` whether the
// sub-surface has keyboard focus.
// Recurses once per semantic level: at most the control nesting limit
// (64) plus maximum_virtual_semantic_depth (16) levels, both enforced
// before a snapshot is published (semantic_types.hpp).
void translate_sub_surface_nodes(std::vector<SemanticNode>& nodes, const std::string& prefix, Point offset,
                                 bool keep_focus) {
    for (SemanticNode& node : nodes) {
        std::string identity = prefix;
        identity.back() = '#';
        identity += std::to_string(node.runtime_id);
        node.runtime_id = virtual_semantic_runtime_id(identity);
        node.stable_id.insert(0U, prefix);
        if (!node.controlled_panel.empty()) node.controlled_panel.insert(0U, prefix);
        node.bounds.x += offset.x;
        node.bounds.y += offset.y;
        if (!keep_focus) {
            const std::uint32_t focused_bit = static_cast<std::uint32_t>(SemanticState::focused);
            node.states = static_cast<SemanticState>(static_cast<std::uint32_t>(node.states) & ~focused_bit);
        }
        translate_sub_surface_nodes(node.children, prefix, offset, keep_focus);
    }
}

// Where an action for one translated node goes in the sub-surface.
struct SubSurfaceNodeTarget final {
    const SemanticNode* node{};
    // The node's control, or the nearest control above a virtual child.
    RuntimeId control{};
    // The virtual child's stable identity; empty for a control's own node.
    std::string_view virtual_child{};
};

// Finds the node of the sub-surface's own snapshot whose stable identity is
// `inner`, depth first in publication order, under the control `owner`.
// False when there is none.
// Recurses once per semantic level: at most the control nesting limit
// (64) plus maximum_virtual_semantic_depth (16) levels, both enforced
// before a snapshot is published (semantic_types.hpp).
bool find_sub_surface_node(const std::vector<SemanticNode>& nodes, std::string_view inner, RuntimeId owner,
                           SubSurfaceNodeTarget& found) {
    for (const SemanticNode& node : nodes) {
        const bool virtual_node = is_virtual_semantic_runtime_id(node.runtime_id);
        const RuntimeId control = virtual_node ? owner : RuntimeId{node.runtime_id};
        if (node.stable_id == inner) {
            found.node = &node;
            found.control = control;
            found.virtual_child = virtual_node ? std::string_view(node.stable_id) : std::string_view{};
            return true;
        }
        if (find_sub_surface_node(node.children, inner, control, found)) return true;
    }
    return false;
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

class SubSurfaceHost::SemanticsNotice final : public SubSurfaceSemanticsListener {
public:
    explicit SemanticsNotice(SubSurfaceHost& host) noexcept : host_(&host) {}
    void sub_surface_semantics_changed() override {
        if (host_ == nullptr) return;
        (*host_).take_sub_surface_semantics();
    }
    // The host stopped this target's thread or is going away.
    void revoke() noexcept { host_ = nullptr; }

private:
    // Observes the host that owns this object; null once revoked.
    SubSurfaceHost* host_{};
};

class SubSurfaceHost::Notices final : public SubSurfaceEndListener, public LiveSurfacePairingListener {
public:
    explicit Notices(SubSurfaceHost& host) noexcept : host_(&host) {}
    void sub_surface_ended() override {
        if (host_ == nullptr) return;
        (*host_).notice_thread_ended();
    }
    void live_surface_pairing_missed(std::uint64_t content_tag) override {
        if (host_ == nullptr) return;
        (*host_).notice_pairing_missed(content_tag);
    }
    void stopped(const SubSurfaceStopped& stopped) {
        if (host_ == nullptr) return;
        (*host_).notice_stopped(stopped);
    }
    // The host is going away.
    void revoke() noexcept { host_ = nullptr; }

private:
    // Observes the host that owns this object; null once revoked.
    SubSurfaceHost* host_{};
};

// Posted by the host to the window's dispatcher when it stopped its
// thread: raises `stopped` after the stop, unless the host went first.
class SubSurfaceHost::PostedStop final {
public:
    PostedStop(std::weak_ptr<Notices> notices, SubSurfaceStopped stopped) noexcept
        : notices_(std::move(notices)), stopped_(std::move(stopped)) {}
    void operator()() const {
        const std::shared_ptr<Notices> notices = notices_.lock();
        if (!notices) return;
        (*notices).stopped(stopped_);
    }

private:
    std::weak_ptr<Notices> notices_;
    SubSurfaceStopped stopped_{};
};

class SubSurfaceHost::PostedEnd final {
public:
    explicit PostedEnd(std::weak_ptr<Notices> notices) noexcept : notices_(std::move(notices)) {}
    void operator()() const {
        const std::shared_ptr<Notices> notices = notices_.lock();
        if (!notices) return;
        (*notices).sub_surface_ended();
    }

private:
    std::weak_ptr<Notices> notices_;
};

namespace {

// The events SubSurfaceHost adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> sub_surface_host_interactions{{
    {.name = "stopped", .payload = "SubSurfaceStopped", .mutable_payload = false},
    {.name = "pairing_missed", .payload = "std::uint64_t", .mutable_payload = false},
}};

} // namespace

SubSurfaceHost::SubSurfaceHost(StableId stable_id)
    : Control(std::move(stable_id)), notices_(std::make_shared<Notices>(*this)) {
    focusable = true;
    // The sub-surface's tooltip requests are this host's tooltip parts.
    set_publishes_item_tool_tips(true);
}

SubSurfaceHost::~SubSurfaceHost() {
    // Notices already posted find the host gone; the stop below posts one
    // that finds the same.
    (*notices_).revoke();
    stop_thread();
}

void SubSurfaceHost::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), sub_surface_host_interactions.begin(),
                        sub_surface_host_interactions.end());
}

void SubSurfaceHost::notice_thread_ended() {
    // A notice of a thread the host has stopped since, or already noticed,
    // or of an earlier thread while a newer one runs, raises nothing.
    if (!thread_ || end_noticed_) return;
    if ((*thread_).state() != SubSurfaceState::failed) return;
    end_noticed_ = true;
    SubSurfaceStopped ended;
    ended.state = SubSurfaceState::failed;
    ended.failure = (*thread_).failure();
    stopped_.emit(ended);
}

void SubSurfaceHost::notice_stopped(const SubSurfaceStopped& stopped) {
    stopped_.emit(stopped);
}

void SubSurfaceHost::notice_pairing_missed(std::uint64_t epoch) {
    pairing_missed_.emit(epoch);
}

void SubSurfaceHost::set_content(std::unique_ptr<SubSurfaceContent> content, bool opaque) {
    adopt_content(content, opaque);
}

void SubSurfaceHost::adopt_content(std::unique_ptr<SubSurfaceContent>& content, bool opaque) {
    require_mutable();
    const bool had_requests = !requests_.empty();
    const bool had_semantics = static_cast<bool>(semantics_);
    stop_thread();
    // A tooltip the old content asked for closes.
    SurfaceRoot* const root = attached_surface_root();
    if (had_requests && root != nullptr) detail::SurfaceRequestAccess::tool_tip_items_changed(*root, *this);
    content_ = std::move(content);
    opaque_ = opaque;
    redraw_requested_ = false;
    pending_update_.reset();
    configure_region();
    invalidate(had_semantics ? Dirty::paint | Dirty::semantics : Dirty::paint);
}

std::uint64_t SubSurfaceHost::redraw_all(std::unique_ptr<SubSurfaceContentUpdate> update) {
    require_mutable();
    redraw_requested_ = true;
    pending_update_ = std::move(update);
    const std::uint64_t before = content_epoch_;
    configure_region();
    // Not sent: the update waits for the first configuration.
    if (redraw_requested_ || content_epoch_ == before) return 0U;
    return content_epoch_;
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

void SubSurfaceHost::set_publishes_semantics(bool publishes) {
    require_mutable();
    if (publishes == publishes_semantics_) return;
    publishes_semantics_ = publishes;
    Window* const window = attached_window();
    if (!publishes && semantics_notice_) {
        // Notices already posted find the listener revoked; the thread posts
        // no more.
        (*semantics_notice_).revoke();
        semantics_notice_.reset();
        if (thread_) (*thread_).set_semantics_listener(WindowDispatcher{}, std::weak_ptr<SubSurfaceSemanticsListener>{});
    } else if (publishes && thread_ && window != nullptr) {
        semantics_notice_ = std::make_shared<SemanticsNotice>(*this);
        (*thread_).set_semantics_listener((*window).dispatcher(), semantics_notice_);
    }
    semantics_.reset();
    // The newest snapshot the running sub-surface published, if any.
    if (publishes) take_sub_surface_semantics();
    invalidate(Dirty::semantics);
}

SemanticDescriptor SubSurfaceHost::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    if (!publishes_semantics_) {
        // Neither a node nor descendants: the virtual children are empty too.
        descriptor.exposed = false;
        return descriptor;
    }
    descriptor.role = SemanticRole::pane;
    descriptor.name = accessible_name.read().empty() ? "Sub-surface" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> SubSurfaceHost::semantic_virtual_children() const {
    if (!publishes_semantics_ || !semantics_) return {};
    std::vector<SemanticNode> nodes = (*semantics_).roots;
    std::string prefix(stable_id().value());
    prefix += '.';
    const Rect bounds = absolute_bounds();
    translate_sub_surface_nodes(nodes, prefix, Point{bounds.x, bounds.y}, keyboard_inside_);
    return nodes;
}

bool SubSurfaceHost::on_semantic_child_action(std::string_view stable_id_text, SemanticAction action,
                                              std::string_view value) {
    if (!publishes_semantics_ || !semantics_ || !thread_) return false;
    const std::string_view own = stable_id().value();
    const bool prefixed = stable_id_text.size() > own.size() + 1U && stable_id_text.substr(0U, own.size()) == own &&
                          stable_id_text[own.size()] == '.';
    if (!prefixed) return false;
    const std::string_view inner = stable_id_text.substr(own.size() + 1U);
    SubSurfaceNodeTarget target;
    if (!find_sub_surface_node((*semantics_).roots, inner, RuntimeId{}, target) || target.control.value == 0U) {
        return false;
    }
    const SemanticNode& node = *target.node;
    const bool offered = std::find(node.actions.begin(), node.actions.end(), action) != node.actions.end();
    const bool focus_offered = action == SemanticAction::focus && has_semantic_state(node.states, SemanticState::focusable);
    if (!offered && !focus_offered) return false;
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::semantic_action;
    input.semantic.control = target.control;
    input.semantic.virtual_child = std::string(target.virtual_child);
    input.semantic.action = action;
    input.semantic.value = std::string(value);
    const std::uint64_t sequence = (*thread_).post_input(input);
    if (sequence == 0U) return false;
    // Keyboard focus enters the sub-surface through the host. The entry is
    // posted after the action and restores the control focused then, which
    // is the one the action focused.
    if (action == SemanticAction::focus) {
        SurfaceRoot* const root = attached_surface_root();
        if (root != nullptr) static_cast<void>((*root).request_focus(shared_from_this()));
    }
    return true;
}

void SubSurfaceHost::take_sub_surface_semantics() {
    if (!thread_) return;
    std::shared_ptr<const SemanticSnapshot> taken = (*thread_).take_semantics();
    if (taken == semantics_) return;
    semantics_ = std::move(taken);
    invalidate(Dirty::semantics);
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
    // The window sends an enter just before routing the move that caused
    // it, at the same position: that move starts the sub-surface's own
    // hover, so the enter is not forwarded (forwarded as a move it
    // delivered every entering move twice).
    if (event.action == PointerAction::enter) {
        event.handled = true;
        return;
    }
    hold_capture_for(event);
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::pointer;
    input.pointer = event;
    input.pointer.position = point_from_window(event.position);
    input.pointer.phase = EventPhase::target;
    input.pointer.handled = false;
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
        const detail::SurfaceForwardingAccess::FocusTraversal traversal =
            detail::SurfaceForwardingAccess::last_focus_traversal(*root);
        if (traversal == detail::SurfaceForwardingAccess::FocusTraversal::forward) {
            entry = SubSurfaceFocusEntry::first;
        } else if (traversal == detail::SurfaceForwardingAccess::FocusTraversal::backward) {
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
    // The sub-surface's nodes are focused only while it has keyboard focus.
    if (semantics_) invalidate(Dirty::semantics);
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
        detail::SurfaceRequestAccess::show_requested_tool_tip(*window, shared_from_this(), request_item_id(request.id),
                                                              request.text, anchor, request.duration);
        return;
    }
    detail::SurfaceRequestAccess::tool_tip_items_changed(*window, *this);
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
    if (root != nullptr) detail::SurfaceRequestAccess::tool_tip_items_changed(*root, *this);
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
    if (revoked && root != nullptr) detail::SurfaceRequestAccess::tool_tip_items_changed(*root, *this);
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
    static_cast<void>(detail::SurfaceForwardingAccess::resume_unhandled_key(*root, focused, event));
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
    const bool unchanged = thread_ && !environment_changed_ && !redraw_requested_ && pixel_width == pixel_width_ &&
                           pixel_height == pixel_height_ && scale == configured_scale_;
    if (unchanged) return;
    const Size logical{bounds.width, bounds.height};
    const bool started_now = !thread_;
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
        if (publishes_semantics_) {
            semantics_notice_ = std::make_shared<SemanticsNotice>(*this);
            (*thread_).set_semantics_listener((*window).dispatcher(), semantics_notice_);
        }
        notice_dispatcher_ = (*window).dispatcher();
        end_noticed_ = false;
        (*thread_).set_end_listener(notice_dispatcher_, notices_);
        // A content that failed before the listener was set posted nothing;
        // a failure after it posts its own notice, and the second of two
        // finds end_noticed_ set.
        if ((*thread_).state() == SubSurfaceState::failed) {
            static_cast<void>(notice_dispatcher_.begin_invoke(PostedEnd(notices_)));
        }
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
    configuration.update = std::move(pending_update_);
    environment_changed_ = false;
    redraw_requested_ = false;
    (*thread_).configure(std::move(configuration));
    pixel_width_ = pixel_width;
    pixel_height_ = pixel_height;
    configured_scale_ = scale;
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*window).queue_live_surface_presentation(self, surface_));
    static_cast<void>((*window).require_live_surface_content(self, content_epoch_));
    static_cast<void>((*window).set_live_surface_pairing_listener(self, notices_));
    // Only a new surface or device size repaints the host: the window
    // composes each new frame through the live surface path, and a
    // redraw-all sent while its owner paints (a DataGridView's cell region)
    // must not damage that owner again.
    if (resized || started_now) invalidate(Dirty::paint);
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
    // Snapshot notices already posted find the target revoked; the nodes go
    // with the thread.
    if (semantics_notice_) (*semantics_notice_).revoke();
    semantics_notice_.reset();
    semantics_.reset();
    keyboard_inside_ = false;
    pressed_buttons_ = 0U;
    took_capture_ = false;
    (*thread_).stop();
    // `stopped` for this thread, unless its failure was noticed already:
    // posted, so that it is raised after this stop (which may run inside a
    // detach or a set_content), never within it. A post that is refused (the
    // window closed) or cannot allocate raises nothing.
    if (!end_noticed_) {
        end_noticed_ = true;
        try {
            SubSurfaceStopped ended;
            ended.state = (*thread_).state();
            ended.failure = (*thread_).failure();
            static_cast<void>(notice_dispatcher_.begin_invoke(PostedStop(notices_, std::move(ended))));
        } catch (const std::exception&) {
            // Dropped, as the comment above says.
        }
    }
    notice_dispatcher_ = WindowDispatcher{};
    thread_.reset();
    pixel_width_ = 0U;
    pixel_height_ = 0U;
    configured_scale_ = 0.0;
}

} // namespace gui_forms
