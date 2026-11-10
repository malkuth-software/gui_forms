#include "tool_tip_presenter.hpp"

#include "tool_tip_bubble.hpp"
#include "../../application/transient_window.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "../../display/recording_painter/recording_painter.hpp"
#include "../../display/snapshot/display_snapshot.hpp"
#include "../../application/native_popup_record.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace gui_forms::detail {

namespace {

// The font every tooltip draws with, before the window's text scale.
constexpr FontSpec tool_tip_font{FontRole::content, 12.0, 400, false};

// The node among `nodes` and their children whose stable ID is `stable_id`,
// or null. The pointer is borrowed from `nodes`.
[[nodiscard]] const SemanticNode* item_with_id(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* deeper = item_with_id(node.children, stable_id);
        if (deeper != nullptr) return deeper;
    }
    return nullptr;
}
const SemanticNode* item_with_id(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

} // namespace

ToolTipPresenter::Overlay::Overlay(std::shared_ptr<ToolTipLayer> layer_control,
                                   std::shared_ptr<ToolTipBubble> bubble_control, OverlayToken overlay_token) noexcept
    : layer(std::move(layer_control)), bubble(std::move(bubble_control)), token(std::move(overlay_token)) {}

ToolTipPresenter::Overlay::~Overlay() {
    closed.disconnect();
    token.disconnect();
    bubble.reset();
    layer.reset();
}

void ToolTipPresenter::Wake::operator()(FrameTime now) const {
    (*presenter).woke(now);
}

void ToolTipPresenter::OverlayClosed::operator()() const {
    (*presenter).overlay_closed();
}

void ToolTipPresenter::TransientClosed::operator()() const {
    (*presenter).transient_closed();
}

ToolTipPresenter::ToolTipPresenter(Window& window) noexcept : window_(window) {}

ToolTipPresenter::~ToolTipPresenter() {
    shutdown();
}

void ToolTipPresenter::shutdown() noexcept {
    wake_.disconnect();
    // The Splash window's close is not heard while the presenter ends.
    transient_closed_.disconnect();
    // The overlay's destructor disconnects its overlay before the state goes.
    state_ = Idle{};
    keyboard_subject_ = ToolTipSubject{};
    // Destroys the Splash window (its content detached first), then the
    // bubble it showed.
    transient_.reset();
    transient_bubble_.reset();
    pending_bake_ = PendingBake{};
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
    prebake(subject);
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
            ToolTipSubject subject =
                resolve_pointer_subject(hovered, pointer_, window_.tool_tip_settings(), covers_);
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
        ToolTipSubject subject = resolve_focus_subject(window_.focused_control(), window_.tool_tip_settings(), covers_);
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
        !available(window_.tool_tip_settings(), *control) || subject.text.empty()) {
        state_ = Idle{};
        return std::nullopt;
    }
    const ToolTipSettings& settings = window_.tool_tip_settings();
    // The kept lines, so that a warm show breaks into storage it already
    // has (the bubble swaps them with its own).
    ToolTipLines& lines = transient_lines_;
    lines.font = (*control).effective_font(tool_tip_font);
    break_tool_tip_lines(*control, subject.text, settings.maximum_width, lines);

    // Written into the recycled event's storage (raise returns it).
    ToolTipEvent change = std::move(spare_event_);
    change.target_stable_id.assign((*control).stable_id().value());
    change.item_id.assign(subject.item_id);
    change.text.assign(subject.text);
    change.shown = true;
    change.keyboard_initiated = origin == ToolTipOrigin::keyboard;
    const std::chrono::milliseconds stay = duration.value_or(settings.auto_pop_delay);

    // The tooltip Splash window where the run can open one (owner,
    // 2026-11-08: APP_WINDOW_RULES_DESIGN Part S); else the overlay below.
    Showing splash{};
    splash.origin = origin;
    splash.subject = std::move(subject);
    if (show_in_splash(splash, lines)) {
        state_ = std::move(splash);
        if (stay.count() > 0) arm(FrameClock::now() + stay);
        return change;
    }
    subject = std::move(splash.subject);

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

    OverlayOptions options{};
    options.require_enabled_owner = !settings.show_when_disabled;
    OverlayToken token = window_.open_overlay(control, layer, options);
    std::unique_ptr<Overlay> overlay =
        std::make_unique<Overlay>(std::move(layer), std::move(bubble), std::move(token));
    if (Event<>* closed = (*overlay).token.closed_event()) {
        (*overlay).closed = (*closed).subscribe(OverlayClosed{this});
    }

    Showing showing{};
    showing.origin = origin;
    showing.subject = std::move(subject);
    showing.overlay = std::move(overlay);
    position(showing);
    state_ = std::move(showing);
    if (stay.count() > 0) arm(FrameClock::now() + stay);
    return change;
}

TransientWindow* ToolTipPresenter::splash_window() {
    if (transient_ && (*transient_).connected()) return transient_.get();
    if (transient_refused_) return nullptr;
    // A run that cannot open one now (no run yet, a host without Splash
    // windows) is asked again on a later show; that check opens nothing.
    if (native_popup_refusal(window_.application_window_, HostTransientKind::tool_tip) !=
        NativePopupRefusal::none) {
        return nullptr;
    }
    transient_closed_.disconnect();
    transient_.reset();
    TransientWindowOpening opening{};
    opening.owner = window_.application_window_;
    opening.kind = HostTransientKind::tool_tip;
    opening.serial = next_overlay_;
    ++next_overlay_;
    opening.key_bindings = window_.key_bindings();
    opening.resources = window_.resources();
    transient_ = TransientWindow::open(opening);
    if (!transient_) {
        transient_refused_ = true;
        return nullptr;
    }
    transient_closed_ = (*transient_).closed().subscribe(TransientClosed{this});
    return transient_.get();
}

bool ToolTipPresenter::show_in_splash(Showing& showing, ToolTipLines& lines) {
    TransientWindow* splash = splash_window();
    if (splash == nullptr) return false;
    if (!transient_bubble_) {
        transient_bubble_ = make_control<ToolTipBubble>(StableId("tooltip.splash.bubble"), std::string(),
                                                       ToolTipLines{});
    }
    (*transient_bubble_).set_content(showing.subject.text, lines);
    // An opaque Splash window (X11 without a compositing manager) holds the
    // face alone: no shadow (owner, 2026-11-08: Q-S2 (b)).
    (*transient_bubble_).set_shadow(native_popup_translucent(window_.application_window_));
    const bool placed = position(showing);
    if (placed) adopt_bake();
    return placed;
}

// --- Splash baking ----------------------------------------------------------

void ToolTipPresenter::prebake(const ToolTipSubject& subject) {
    pending_bake_ = PendingBake{};
    const SplashBakerSlot slot = current_splash_baker();
    if (slot.baker == nullptr || subject.text.empty()) return;
    if (!transient_ || !(*transient_).connected() || !transient_bubble_) return;
    const SurfaceRoot* const root = (*transient_bubble_).attached_surface_root();
    const Control::Ptr control = subject.control.lock();
    if (root == nullptr || !control || !(*control).is_alive()) return;

    // Broken and measured as open() will break them for this subject.
    bake_lines_.font = (*control).effective_font(tool_tip_font);
    break_tool_tip_lines(*control, subject.text, window_.tool_tip_settings().maximum_width, bake_lines_);
    const Size face = tool_tip_face_size(bake_lines_);
    const bool shadow = native_popup_translucent(window_.application_window_);
    const Insets outsets = shadow ? overlay_shadow_outsets(*transient_bubble_) : Insets{};
    const double scale = (*root).scale();
    const Size whole{face.width + outsets.left + outsets.right, face.height + outsets.top + outsets.bottom};

    // Recorded here, on the UI thread, where the theme and the text engine
    // live; the snapshot owns everything the worker reads.
    RecordingPainter recorder((*root).text_metrics_provider(), DeviceScale{scale});
    recorder.save();
    recorder.translate(Point{outsets.left, outsets.top});
    paint_tool_tip_bubble(recorder, *transient_bubble_, face, bake_lines_, shadow);
    recorder.restore();
    const std::shared_ptr<const DisplayChunk> chunk =
        recorder.finish(0U, PaintPlane::overlay, Rect{0.0, 0.0, whole.width, whole.height});
    if (!chunk) return;
    DisplaySnapshotResult made = make_display_snapshot(*chunk, whole, DeviceScale{scale});
    if (!made.snapshot) return;

    PendingBake pending{};
    pending.armed = true;
    pending.ticket = (*slot.baker).submit(std::move(made.snapshot));
    pending.run = slot.run;
    pending.key.text.assign(subject.text);
    pending.key.face = face;
    pending.key.font = bake_lines_.font;
    pending.key.shadow = shadow;
    pending.key.scale = scale;
    pending_bake_ = std::move(pending);
}

void ToolTipPresenter::adopt_bake() {
    if (!pending_bake_.armed || !transient_bubble_) return;
    PendingBake pending = std::move(pending_bake_);
    pending_bake_ = PendingBake{};
    const SplashBakerSlot slot = current_splash_baker();
    if (slot.baker == nullptr || slot.run != pending.run) return;
    SplashBakeOutcome outcome = (*slot.baker).take(pending.ticket);
    // A bake still painting is left to the baker's bound; this show paints
    // live.
    if (outcome.state != SplashBakeState::finished || !outcome.raster) return;
    (*transient_bubble_).set_baked(std::move(outcome.raster), std::move(pending.key));
}

void ToolTipPresenter::transient_closed() {
    Showing* showing = std::get_if<Showing>(&state_);
    const bool shown_there = showing != nullptr && (*showing).overlay == nullptr;
    transient_closed_.disconnect();
    if (shown_there) raise(close(Idle{}));
}

std::optional<ToolTipEvent> ToolTipPresenter::close(State next) {
    Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr) {
        state_ = std::move(next);
        if (!std::holds_alternative<Waiting>(state_)) wake_.disconnect();
        return std::nullopt;
    }
    ToolTipEvent change = std::move(spare_event_);
    change.target_stable_id.clear();
    if (const Control::Ptr control = (*showing).subject.control.lock()) {
        change.target_stable_id.assign((*control).stable_id().value());
    }
    change.item_id.assign((*showing).subject.item_id);
    change.text.assign((*showing).subject.text);
    change.shown = false;
    // The subject's text storage is kept for the next program show.
    spare_text_ = std::move((*showing).subject.text);
    change.keyboard_initiated = (*showing).origin == ToolTipOrigin::keyboard;
    if ((*showing).overlay == nullptr && transient_) (*transient_).hide();
    // Moving the state out first leaves the presenter consistent while the
    // overlay's destructor detaches the overlay.
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
//
// In the Splash window the bubble may leave the window: the face is placed
// the same way without the client clamp, then kept inside the monitor's
// work area (detail::desktop_placement), the window holding the face and
// its shadow's outsets.
bool ToolTipPresenter::position(Showing& showing) {
    const bool anchored = showing.origin != ToolTipOrigin::pointer;
    const Rect anchor = showing.subject.anchor;
    if (showing.overlay == nullptr) {
        if (!transient_ || !transient_bubble_) return false;
        const Size face = (*transient_bubble_).face_size();
        const Insets outsets = (*transient_bubble_).visual_outsets();
        const double x = anchored ? anchor.x + 8.0 : pointer_.x + 14.0;
        const double y = anchored ? anchor.y + anchor.height + 7.0 : pointer_.y + 19.0;
        const Rect window_bounds{x - outsets.left, y - outsets.top, face.width + outsets.left + outsets.right,
                                 face.height + outsets.top + outsets.bottom};
        const std::optional<Rect> desktop = desktop_placement(window_.application_window_, window_bounds, placement_monitors_);
        if (!desktop.has_value()) return false;
        if (!transient_layer_) transient_layer_ = make_control<ToolTipLayer>(StableId("tooltip.splash.layer"));
        if ((*transient_bubble_).parent() != transient_layer_) (*transient_layer_).add_child(transient_bubble_);
        (*transient_bubble_).set_requested_bounds({outsets.left, outsets.top, face.width, face.height});
        if ((*transient_).shown()) return (*transient_).move(*desktop);
        const bool shown = (*transient_).show(transient_layer_, *desktop);
        return shown;
    }
    const std::shared_ptr<ToolTipBubble>& bubble = (*showing.overlay).bubble;
    const Rect current = (*bubble).requested_bounds();
    const Size client = window_.client_size();
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
    return true;
}

void ToolTipPresenter::raise(std::optional<ToolTipEvent> change) {
    if (!change) return;
    window_.tool_tip_request_changed(*change);
    window_.tool_tip_visibility_changed_.emit(*change);
    // Its storage serves the next event (a handler that opened or closed a
    // tooltip meanwhile took the spare and built its own).
    spare_event_ = std::move(*change);
}

void ToolTipPresenter::overlay_closed() {
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
    ToolTipSubject subject = resolve_pointer_subject(hovered, pointer_, window_.tool_tip_settings(), covers_);
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
        std::optional<ToolTipEvent> change = close(Idle{});
        pointer_wait(hovered, now);
        raise(std::move(change));
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
        const ToolTipSubject subject =
            resolve_pointer_subject(hovered, pointer_, window_.tool_tip_settings(), covers_);
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
        ToolTipSubject subject = resolve_focus_subject(focused, window_.tool_tip_settings(), covers_);
        keyboard_subject_ = subject;
        if (!subject.text.empty()) {
            if (!change) change = close(Idle{});
            wait(ToolTipOrigin::keyboard, std::move(subject), FrameClock::now());
        }
    }
    raise(std::move(change));
}

// A handled key may have moved the keyboard within the focused control
// (a strip's arrow keys) or shown the focus cue (Tab): the thing under
// focus is found again, and a different one waits to show.
void ToolTipPresenter::key_handled() {
    if (!focus_allowed() || !window_.focus_cue_visible()) return;
    const Control::Ptr focused = window_.focused_control();
    if (!focused) return;
    ToolTipSubject subject = resolve_focus_subject(focused, window_.tool_tip_settings(), covers_);
    if (same_tool_tip_thing(subject, keyboard_subject_) && keyboard_subject_.control.lock()) return;
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
    raise(std::move(change));
}

void ToolTipPresenter::root_arranged() {
    Showing* showing = std::get_if<Showing>(&state_);
    // In the Splash window nothing closes the bubble with its control, as
    // the overlay's owner rule does in the window: the layout after the
    // control left, hid or was disabled closes it here.
    if (showing != nullptr && (*showing).overlay == nullptr) {
        const Control::Ptr shown = (*showing).subject.control.lock();
        const bool stays = shown && (*shown).is_alive() && (*shown).attached_window() == &window_ &&
                           available(window_.tool_tip_settings(), *shown);
        if (!stays) {
            raise(close(Idle{}));
            return;
        }
    }
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
            control.tool_tip_text.read().empty()) {
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
    if (control.tool_tip_text.read().empty()) {
        raise(close(Idle{}));
        return;
    }
    const ToolTipOrigin origin = (*showing).origin;
    ToolTipSubject subject = (*showing).subject;
    subject.text = control.tool_tip_text.read();
    replace(origin, std::move(subject), std::nullopt);
}

void ToolTipPresenter::items_changed(const Control& control) {
    if (Showing* showing = std::get_if<Showing>(&state_)) {
        if ((*showing).subject.item_id.empty() || (*showing).subject.control.lock().get() != &control) return;
        const std::vector<SemanticNode> items = control.tool_tip_items();
        const SemanticNode* item = item_with_id(items, (*showing).subject.item_id);
        const ToolTipOrigin origin = (*showing).origin;
        if (item == nullptr || (*item).tool_tip.empty()) {
            std::optional<ToolTipEvent> change = close(Idle{});
            // The pointer may rest over another part of the control now.
            if (origin == ToolTipOrigin::pointer) pointer_wait(window_.hit_test(pointer_), last_motion_);
            raise(std::move(change));
            return;
        }
        if ((*item).tool_tip == (*showing).subject.text) return;
        ToolTipSubject subject = (*showing).subject;
        subject.text = (*item).tool_tip;
        replace(origin, std::move(subject), std::nullopt);
        return;
    }
    Control::Ptr hovered{};
    if (const Waiting* waiting = std::get_if<Waiting>(&state_)) {
        if ((*waiting).origin == ToolTipOrigin::pointer) hovered = (*waiting).hovered.lock();
    } else if (const Settled* settled = std::get_if<Settled>(&state_)) {
        hovered = (*settled).hovered.lock();
    }
    if (hovered.get() == &control) {
        // Found again from the moment the pointer came to rest: the delay
        // still counts from then.
        state_ = Idle{};
        wake_.disconnect();
        pointer_wait(hovered, last_motion_);
        return;
    }
    const Control::Ptr focused = window_.focused_control();
    if (focused.get() == &control) key_handled();
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
    std::string text = std::move(spare_text_);
    text.assign((*target).tool_tip_text.read());
    ToolTipSubject subject = tool_tip_subject_of(target, {}, std::move(text), (*target).absolute_bounds());
    replace(ToolTipOrigin::program, std::move(subject), duration);
}

void ToolTipPresenter::show_program_item(const Control::Ptr& control, std::string item_id, std::string text,
                                         Rect anchor, std::optional<std::chrono::milliseconds> duration) {
    ToolTipSubject subject = tool_tip_subject_of(control, std::move(item_id), std::move(text), anchor);
    replace(ToolTipOrigin::program, std::move(subject), duration);
}

void ToolTipPresenter::request(const Control::Ptr& owner, std::uint64_t id, std::string item_id,
                               const ToolTipRequest& content) {
    const std::string shown_item = item_id;
    show_program_item(owner, std::move(item_id), content.text, content.anchor, content.duration);
    const Showing* showing = std::get_if<Showing>(&state_);
    const bool shows = showing != nullptr && (*showing).subject.item_id == shown_item &&
                       (*showing).subject.control.lock() == owner;
    // An owner that cannot show one now (hidden, or disabled without
    // disabled tooltips) leaves the presenter idle.
    if (!shows) window_.answer_tool_tip_request(id, SurfaceRequestResult::declined);
}

void ToolTipPresenter::withdraw(std::string_view item_id) {
    const Showing* showing = std::get_if<Showing>(&state_);
    if (showing == nullptr || (*showing).subject.item_id != item_id) return;
    raise(close(Idle{}));
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
