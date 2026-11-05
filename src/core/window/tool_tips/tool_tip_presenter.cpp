#include "tool_tip_presenter.hpp"

#include "tool_tip_bubble.hpp"
#include "gui_forms/application.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace gui_forms::detail {

namespace {

// The font every tooltip draws with, before the window's text scale.
constexpr FontSpec tool_tip_font{FontRole::content, 12.0, 400, false};

[[nodiscard]] bool contains_point(Rect bounds, Point point) noexcept {
    const bool inside = !bounds.empty() && point.x >= bounds.x && point.y >= bounds.y &&
                        point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
    return inside;
}

// The deepest node under `point` among `nodes` and their children, or null.
// The pointer is borrowed from `nodes`.
[[nodiscard]] const SemanticNode* node_at(const std::vector<SemanticNode>& nodes, Point point) {
    for (const SemanticNode& node : nodes) {
        if (!contains_point(node.bounds, point)) continue;
        const SemanticNode* deeper = node_at(node.children, point);
        if (deeper != nullptr) return deeper;
        return &node;
    }
    return nullptr;
}
const SemanticNode* node_at(std::vector<SemanticNode>&& nodes, Point point) = delete;

// The first node among `nodes` and their children whose states hold `state`,
// or null. The pointer is borrowed from `nodes`.
[[nodiscard]] const SemanticNode* node_with_state(const std::vector<SemanticNode>& nodes,
                                                  SemanticState state) {
    for (const SemanticNode& node : nodes) {
        if (has_semantic_state(node.states, state)) return &node;
        const SemanticNode* deeper = node_with_state(node.children, state);
        if (deeper != nullptr) return deeper;
    }
    return nullptr;
}
const SemanticNode* node_with_state(std::vector<SemanticNode>&& nodes, SemanticState state) = delete;

[[nodiscard]] ToolTipSubject nothing_over(Rect region) {
    ToolTipSubject subject{};
    subject.anchor = region;
    return subject;
}

[[nodiscard]] ToolTipSubject subject_of(const Control::Ptr& control, std::string item_id, std::string text,
                                        Rect anchor) {
    ToolTipSubject subject{};
    subject.control = control;
    subject.item_id = std::move(item_id);
    subject.text = std::move(text);
    subject.anchor = anchor;
    return subject;
}

} // namespace

ToolTipPresenter::Overlay::Overlay(std::shared_ptr<ToolTipLayer> layer_control,
                                   std::shared_ptr<ToolTipBubble> bubble_control, PopupToken popup_token) noexcept
    : layer(std::move(layer_control)), bubble(std::move(bubble_control)), token(std::move(popup_token)) {}

ToolTipPresenter::Overlay::~Overlay() {
    closed.disconnect();
    token.disconnect();
    bubble.reset();
    layer.reset();
}

void ToolTipPresenter::Wake::operator()(FrameTime now) const {
    (*presenter).woke(now);
}

void ToolTipPresenter::PopupClosed::operator()() const {
    (*presenter).popup_closed();
}

ToolTipPresenter::ToolTipPresenter(Window& window) noexcept : window_(window) {}

ToolTipPresenter::~ToolTipPresenter() {
    shutdown();
}

void ToolTipPresenter::shutdown() noexcept {
    wake_.disconnect();
    // The overlay's destructor disconnects its popup before the state goes.
    state_ = Idle{};
    keyboard_subject_ = ToolTipSubject{};
}

// --- Switches and the things that may show -------------------------------

bool ToolTipPresenter::hover_allowed() const noexcept {
    const bool allowed = window_.tool_tips_shown() && window_.tool_tip_settings().show_on_hover;
    return allowed;
}

bool ToolTipPresenter::focus_allowed() const noexcept {
    const bool allowed = window_.tool_tips_shown() && window_.tool_tip_settings().show_on_focus;
    return allowed;
}

// A cover of the control itself covers every part of it too.
bool ToolTipPresenter::covered(const Control& control, std::string_view item_id) const {
    const std::pair<std::uint64_t, std::string> whole{control.runtime_id().value, std::string{}};
    if (covers_.find(whole) != covers_.end()) return true;
    if (item_id.empty()) return false;
    const std::pair<std::uint64_t, std::string> part{control.runtime_id().value, std::string(item_id)};
    const bool found = covers_.find(part) != covers_.end();
    return found;
}

bool ToolTipPresenter::available(const Control& control) const noexcept {
    if (!control.effectively_visible()) return false;
    const bool shown = window_.tool_tip_settings().show_when_disabled || control.effectively_enabled();
    return shown;
}

// The thing under the pointer: from the control under it outwards, a part
// of a control that publishes part tooltips, else the control's own text
// (a control whose tooltip shows only over its owner's item is passed
// over). A part's tooltip only overrides where it exists: over a part
// without one, the control's own text shows, as anywhere else on it
// (coordinator decision, 2026-11-02). A disabled control, unless disabled
// tooltips show, and a disabled or covered part end the search with
// nothing.
ToolTipSubject ToolTipPresenter::resolve_pointer(const Control::Ptr& hovered, Point position) const {
    for (Control::Ptr current = hovered; current; current = (*current).parent()) {
        const Rect bounds = (*current).absolute_bounds();
        if (!available(*current)) return nothing_over(bounds);
        if ((*current).publishes_item_tool_tips()) {
            const std::vector<SemanticNode> nodes = (*current).semantic_virtual_children();
            const SemanticNode* node = node_at(nodes, position);
            if (node != nullptr) {
                const bool enabled = window_.tool_tip_settings().show_when_disabled ||
                                     has_semantic_state((*node).states, SemanticState::enabled);
                if (!(*node).tool_tip.empty()) {
                    if (!enabled || covered(*current, (*node).stable_id)) return nothing_over((*node).bounds);
                    return subject_of(current, (*node).stable_id, (*node).tool_tip, (*node).bounds);
                }
            }
        }
        if ((*current).tool_tip_presentation() != ToolTipPresentation::owner_item &&
            !(*current).tool_tip_text.type().empty()) {
            if (covered(*current, {})) return nothing_over(bounds);
            return subject_of(current, {}, (*current).tool_tip_text.type(), bounds);
        }
    }
    const Rect region = hovered ? (*hovered).absolute_bounds() : Rect{};
    return nothing_over(region);
}

// The thing under keyboard focus: from the focused control outwards, the
// part the keyboard is on (the focused part, or the selected one of the
// focused control) of a control that publishes part tooltips, else the
// control's own text. A typed-text field shows its tooltip as a hint inside
// itself, so it ends the search with nothing.
ToolTipSubject ToolTipPresenter::resolve_focus(const Control::Ptr& focused) const {
    for (Control::Ptr current = focused; current; current = (*current).parent()) {
        const Rect bounds = (*current).absolute_bounds();
        if ((*current).tool_tip_presentation() == ToolTipPresentation::field_hint ||
            !available(*current)) return nothing_over(bounds);
        if ((*current).publishes_item_tool_tips()) {
            const std::vector<SemanticNode> nodes = (*current).semantic_virtual_children();
            const SemanticNode* node = node_with_state(nodes, SemanticState::focused);
            if (node == nullptr && current == focused) node = node_with_state(nodes, SemanticState::selected);
            if (node != nullptr && !(*node).tool_tip.empty() && !(*node).bounds.empty()) {
                if (covered(*current, (*node).stable_id)) return nothing_over((*node).bounds);
                return subject_of(current, (*node).stable_id, (*node).tool_tip, (*node).bounds);
            }
        }
        if ((*current).tool_tip_presentation() != ToolTipPresentation::owner_item &&
            !(*current).tool_tip_text.type().empty()) {
            if (covered(*current, {})) return nothing_over(bounds);
            return subject_of(current, {}, (*current).tool_tip_text.type(), bounds);
        }
    }
    return nothing_over({});
}

bool ToolTipPresenter::same_thing(const ToolTipSubject& left, const ToolTipSubject& right) const noexcept {
    const bool same = left.control.lock() == right.control.lock() && left.item_id == right.item_id &&
                      left.text.empty() == right.text.empty();
    return same;
}

// --- Waiting and waking ---------------------------------------------------

std::chrono::milliseconds ToolTipPresenter::show_delay(FrameTime now) const noexcept {
    const ToolTipSettings& settings = window_.tool_tip_settings();
    const bool recently_closed = last_closed_ != FrameTime{} && now - last_closed_ <= settings.initial_delay;
    return recently_closed ? settings.reshow_delay : settings.initial_delay;
}

void ToolTipPresenter::wait(ToolTipOrigin origin, ToolTipSubject subject, FrameTime now) {
    const std::chrono::milliseconds delay = show_delay(now);
    Waiting waiting{};
    waiting.origin = origin;
    waiting.delay = delay;
    waiting.subject = std::move(subject);
    state_ = std::move(waiting);
    arm(now + delay);
}

void ToolTipPresenter::arm(FrameTime deadline) {
    wake_ = window_.request_tool_tip_wake(deadline, Wake{this});
}

void ToolTipPresenter::woke(FrameTime now) {
    wake_.disconnect();
    if (Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin == ToolTipOrigin::pointer) {
            // The pointer must rest for the whole delay: a later motion
            // moves the deadline.
            const FrameTime due = last_motion_ + (*waiting).delay;
            if (now < due) {
                arm(due);
                return;
            }
            if (!hover_allowed()) {
                state_ = Idle{};
                return;
            }
            Control::Ptr hovered = window_.hit_test(pointer_);
            ToolTipSubject subject = resolve_pointer(hovered, pointer_);
            if (subject.text.empty()) {
                Settled settled{};
                settled.region = subject.anchor;
                settled.hovered = hovered;
                state_ = std::move(settled);
                return;
            }
            raise(open(ToolTipOrigin::pointer, std::move(subject), std::nullopt));
            return;
        }
        if (!focus_allowed()) {
            state_ = Idle{};
            return;
        }
        ToolTipSubject subject = resolve_focus(window_.focused_control());
        if (subject.text.empty()) {
            state_ = Idle{};
            return;
        }
        raise(open(ToolTipOrigin::keyboard, std::move(subject), std::nullopt));
        return;
    }
    if (Showing* showing = std::get_if<Showing>(&state_)) {
        // The auto-pop: a pointer tooltip does not show again until the
        // pointer leaves its thing.
        State next{Idle{}};
        if ((*showing).origin == ToolTipOrigin::pointer) {
            Settled settled{};
            settled.region = (*showing).subject.anchor;
            settled.hovered = window_.hit_test(pointer_);
            next = std::move(settled);
        }
        raise(close(std::move(next)));
        return;
    }
    // A wake in any other state is stale and does nothing.
    return;
}

// --- Showing and closing --------------------------------------------------

std::optional<ToolTipEvent> ToolTipPresenter::open(ToolTipOrigin origin, ToolTipSubject subject,
                                                   std::optional<std::chrono::milliseconds> duration) {
    const Control::Ptr control = subject.control.lock();
    if (!control || !(*control).is_alive() || (*control).attached_window() != &window_ ||
        !available(*control) || subject.text.empty()) {
        state_ = Idle{};
        return std::nullopt;
    }
    const ToolTipSettings& settings = window_.tool_tip_settings();
    ToolTipLines lines{};
    lines.font = (*control).effective_font(tool_tip_font);
    break_tool_tip_lines(*control, subject.text, settings.maximum_width, lines);

    const std::string prefix = "tooltip." + std::to_string(next_overlay_);
    ++next_overlay_;
    std::shared_ptr<ToolTipLayer> layer = make_control<ToolTipLayer>(StableId(prefix + ".layer"));
    const Size client = window_.client_size();
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    std::shared_ptr<ToolTipBubble> bubble =
        make_control<ToolTipBubble>(StableId(prefix + ".bubble"), subject.text, std::move(lines));
    const Size face = (*bubble).face_size();
    (*bubble).set_requested_bounds({0.0, 0.0, face.width, face.height});
    (*layer).add_child(bubble);

    PopupOptions options{};
    options.require_enabled_owner = !settings.show_when_disabled;
    PopupToken token = window_.open_popup(control, layer, options);
    std::unique_ptr<Overlay> overlay =
        std::make_unique<Overlay>(std::move(layer), std::move(bubble), std::move(token));
    if (Event<>* closed = (*overlay).token.closed_event()) {
        (*overlay).closed = (*closed).subscribe(PopupClosed{this});
    }

    ToolTipEvent change{};
    change.target_stable_id = std::string((*control).stable_id().value());
    change.item_id = subject.item_id;
    change.text = subject.text;
    change.shown = true;
    change.keyboard_initiated = origin == ToolTipOrigin::keyboard;

    Showing showing{};
    showing.origin = origin;
    showing.subject = std::move(subject);
    showing.overlay = std::move(overlay);
    position(showing);
    state_ = std::move(showing);

    const std::chrono::milliseconds stay = duration.value_or(settings.auto_pop_delay);
    if (stay.count() > 0) arm(FrameClock::now() + stay);
    return change;
}

std::optional<ToolTipEvent> ToolTipPresenter::close(State next) {
    Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr) {
        state_ = std::move(next);
        if (!std::holds_alternative<Waiting>(state_)) wake_.disconnect();
        return std::nullopt;
    }
    ToolTipEvent change{};
    if (const Control::Ptr control = (*showing).subject.control.lock()) {
        change.target_stable_id = std::string((*control).stable_id().value());
    }
    change.item_id = (*showing).subject.item_id;
    change.text = (*showing).subject.text;
    change.shown = false;
    change.keyboard_initiated = (*showing).origin == ToolTipOrigin::keyboard;
    // Moving the state out first leaves the presenter consistent while the
    // overlay's destructor detaches the popup.
    State previous = std::move(state_);
    state_ = std::move(next);
    if (!std::holds_alternative<Waiting>(state_)) wake_.disconnect();
    last_closed_ = FrameClock::now();
    return change;
}

void ToolTipPresenter::replace(ToolTipOrigin origin, ToolTipSubject subject,
                               std::optional<std::chrono::milliseconds> duration) {
    raise(close(Idle{}));
    if (std::holds_alternative<Showing>(state_)) return;
    raise(open(origin, std::move(subject), duration));
}

// Below the thing for keyboard focus and the program, below and right of
// the pointer otherwise; above when it would leave the window's bottom, and
// kept 4 units inside the client area.
void ToolTipPresenter::position(Showing& showing) {
    const std::shared_ptr<ToolTipBubble>& bubble = (*showing.overlay).bubble;
    const Rect current = (*bubble).requested_bounds();
    const Size client = window_.client_size();
    const bool anchored = showing.origin != ToolTipOrigin::pointer;
    const Rect anchor = showing.subject.anchor;
    double x = anchored ? anchor.x + 8.0 : pointer_.x + 14.0;
    double y = anchored ? anchor.y + anchor.height + 7.0 : pointer_.y + 19.0;
    if (x + current.width > client.width - 4.0) {
        x = std::max(4.0, client.width - current.width - 4.0);
    }
    if (y + current.height > client.height - 4.0) {
        y = anchored ? anchor.y - current.height - 7.0 : pointer_.y - current.height - 9.0;
    }
    y = std::clamp(y, 4.0, std::max(4.0, client.height - current.height - 4.0));
    x = std::clamp(x, 4.0, std::max(4.0, client.width - current.width - 4.0));
    (*bubble).set_requested_bounds({x, y, current.width, current.height});
}

void ToolTipPresenter::raise(const std::optional<ToolTipEvent>& change) {
    if (change) window_.tool_tip_visibility_changed_.emit(*change);
}

void ToolTipPresenter::popup_closed() {
    raise(close(Idle{}));
}

// --- Window input ---------------------------------------------------------

// The thing under the pointer is found when the pointer reaches it (it
// leaves the region of the last thing found), not at every motion: a thing
// with a tooltip waits for the pointer to rest, and a region with nothing
// to show settles until the pointer leaves it.
void ToolTipPresenter::pointer_wait(const Control::Ptr& hovered, FrameTime now) {
    if (!hover_allowed()) {
        if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
            if ((*waiting).origin == ToolTipOrigin::pointer) {
                state_ = Idle{};
                wake_.disconnect();
            }
        }
        return;
    }
    if (const Settled* settled = std::get_if<Settled>(&state_)) {
        if (contains_point((*settled).region, pointer_) && (*settled).hovered.lock() == hovered) return;
    }
    if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin != ToolTipOrigin::pointer) return;
        // Still over the thing that waits: the wake checks the rest.
        if (contains_point((*waiting).subject.anchor, pointer_) && (*waiting).hovered.lock() == hovered) return;
    }
    ToolTipSubject subject = resolve_pointer(hovered, pointer_);
    if (subject.text.empty()) {
        Settled settled{};
        settled.region = subject.anchor;
        settled.hovered = hovered;
        state_ = std::move(settled);
        wake_.disconnect();
        return;
    }
    wait(ToolTipOrigin::pointer, std::move(subject), now);
    if (Waiting* waiting = std::get_if<Waiting>(&state_)) (*waiting).hovered = hovered;
}

void ToolTipPresenter::pointer_moved(Point position, const Control::Ptr& hovered, FrameTime now) {
    pointer_ = position;
    last_motion_ = now;
    if (Showing* showing = std::get_if<Showing>(&state_)) {
        // Keyboard and program tooltips stay while the pointer moves.
        if ((*showing).origin != ToolTipOrigin::pointer) return;
        if (contains_point((*showing).subject.anchor, position) && hover_allowed()) return;
        const std::optional<ToolTipEvent> change = close(Idle{});
        pointer_wait(hovered, now);
        raise(change);
        return;
    }
    pointer_wait(hovered, now);
}

void ToolTipPresenter::pointer_pressed() {
    State next{Idle{}};
    if (const Showing* showing = std::get_if<Showing>(&state_)) {
        if ((*showing).origin == ToolTipOrigin::pointer) {
            Settled settled{};
            settled.region = (*showing).subject.anchor;
            settled.hovered = window_.hit_test(pointer_);
            next = std::move(settled);
        }
        raise(close(std::move(next)));
        return;
    }
    if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin != ToolTipOrigin::pointer) return;
        // A press over the thing the pointer waits on ends the wait until
        // the pointer leaves it.
        Control::Ptr hovered = window_.hit_test(pointer_);
        const ToolTipSubject subject = resolve_pointer(hovered, pointer_);
        Settled settled{};
        settled.region = subject.anchor;
        settled.hovered = hovered;
        state_ = std::move(settled);
        wake_.disconnect();
    }
}

void ToolTipPresenter::pointer_left() {
    if (const Showing* showing = std::get_if<Showing>(&state_)) {
        if ((*showing).origin == ToolTipOrigin::pointer) raise(close(Idle{}));
        return;
    }
    if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin != ToolTipOrigin::pointer) return;
    }
    state_ = Idle{};
    wake_.disconnect();
}

void ToolTipPresenter::focus_changed(const Control::Ptr& focused) {
    std::optional<ToolTipEvent> change{};
    if (const Showing* showing = std::get_if<Showing>(&state_)) {
        if ((*showing).origin == ToolTipOrigin::keyboard) change = close(Idle{});
    } else if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin == ToolTipOrigin::keyboard) {
            state_ = Idle{};
            wake_.disconnect();
        }
    }
    keyboard_subject_ = ToolTipSubject{};
    if (focused && focus_allowed() && window_.focus_cue_visible()) {
        ToolTipSubject subject = resolve_focus(focused);
        keyboard_subject_ = subject;
        if (!subject.text.empty()) {
            if (!change) change = close(Idle{});
            wait(ToolTipOrigin::keyboard, std::move(subject), FrameClock::now());
        }
    }
    raise(change);
}

// A handled key may have moved the keyboard within the focused control
// (a strip's arrow keys) or shown the focus cue (Tab): the thing under
// focus is found again, and a different one waits to show.
void ToolTipPresenter::key_handled() {
    if (!focus_allowed() || !window_.focus_cue_visible()) return;
    const Control::Ptr focused = window_.focused_control();
    if (!focused) return;
    ToolTipSubject subject = resolve_focus(focused);
    if (same_thing(subject, keyboard_subject_) && keyboard_subject_.control.lock()) return;
    keyboard_subject_ = subject;
    std::optional<ToolTipEvent> change{};
    if (std::holds_alternative<Showing>(state_)) {
        change = close(Idle{});
    } else if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin == ToolTipOrigin::keyboard) {
            state_ = Idle{};
            wake_.disconnect();
        }
    }
    if (!subject.text.empty()) wait(ToolTipOrigin::keyboard, std::move(subject), FrameClock::now());
    raise(change);
}

void ToolTipPresenter::root_arranged() {
    Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr || (*showing).origin == ToolTipOrigin::pointer || !(*showing).subject.item_id.empty()) {
        return;
    }
    const Control::Ptr control = (*showing).subject.control.lock();
    if (!control) return;
    const Rect anchor = (*control).absolute_bounds();
    if (anchor == (*showing).subject.anchor) return;
    (*showing).subject.anchor = anchor;
    position(*showing);
}

void ToolTipPresenter::text_changed(const Control& control) {
    if (Waiting* waiting = std::get_if<Waiting>(&state_)) {
        // A thing waiting to show whose text went waits no longer; the wake
        // finds the thing again, so new text needs nothing here.
        if ((*waiting).subject.item_id.empty() && (*waiting).subject.control.lock().get() == &control &&
            control.tool_tip_text.type().empty()) {
            state_ = Idle{};
            wake_.disconnect();
        }
        return;
    }
    const Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr || !(*showing).subject.item_id.empty() ||
        (*showing).subject.control.lock().get() != &control) {
        return;
    }
    if (control.tool_tip_text.type().empty()) {
        raise(close(Idle{}));
        return;
    }
    const ToolTipOrigin origin = (*showing).origin;
    ToolTipSubject subject = (*showing).subject;
    subject.text = control.tool_tip_text.type();
    replace(origin, std::move(subject), std::nullopt);
}

void ToolTipPresenter::settings_changed() {
    if (const Showing* showing = std::get_if<Showing>(&state_)) {
        const ToolTipOrigin origin = (*showing).origin;
        const bool allowed = origin == ToolTipOrigin::program ||
                             (origin == ToolTipOrigin::pointer ? hover_allowed() : focus_allowed());
        if (!allowed) {
            raise(close(Idle{}));
            return;
        }
        // Shown again, for a new width.
        ToolTipSubject subject = (*showing).subject;
        replace(origin, std::move(subject), std::nullopt);
        return;
    }
    if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        const bool allowed = (*waiting).origin == ToolTipOrigin::pointer ? hover_allowed() : focus_allowed();
        if (!allowed) {
            state_ = Idle{};
            wake_.disconnect();
        }
    }
}

void ToolTipPresenter::cover(RuntimeId control, std::string_view item_id, bool covered_now) {
    const std::pair<std::uint64_t, std::string> key{control.value, std::string(item_id)};
    if (covered_now) {
        ++covers_[key];
        return;
    }
    const std::map<std::pair<std::uint64_t, std::string>, std::size_t>::iterator found = covers_.find(key);
    if (found == covers_.end()) return;
    if ((*found).second <= 1U) {
        covers_.erase(found);
        return;
    }
    --(*found).second;
}

void ToolTipPresenter::show_program(const Control::Ptr& target,
                                    std::optional<std::chrono::milliseconds> duration) {
    ToolTipSubject subject = subject_of(target, {}, (*target).tool_tip_text.type(), (*target).absolute_bounds());
    replace(ToolTipOrigin::program, std::move(subject), duration);
}

void ToolTipPresenter::hide() {
    raise(close(Idle{}));
}

// --- Queries --------------------------------------------------------------

bool ToolTipPresenter::visible() const noexcept {
    const bool showing = std::holds_alternative<Showing>(state_);
    return showing;
}

Control::Ptr ToolTipPresenter::target() const noexcept {
    const Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr) return {};
    const Control::Ptr control = (*showing).subject.control.lock();
    return control;
}

std::string ToolTipPresenter::item_id() const {
    const Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr) return {};
    return (*showing).subject.item_id;
}

std::string ToolTipPresenter::text() const {
    const Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr) return {};
    return (*showing).subject.text;
}

} // namespace gui_forms::detail
