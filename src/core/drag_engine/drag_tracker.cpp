#include "gui_forms/drag/drag_tracker/drag_tracker.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "drag_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <span>
#include <vector>

// The public drag client over the window's drag engine (drag_tracker.hpp).
// One DragTrackerSession per tracker is the engine's DragClient: it turns
// the owner's DragPlan into operators, its frames into DragTrackerFrames,
// and runs auto-scroll on one frame timer. Everything runs on the owner's
// root thread, from the engine's dispatch, the owner's on_pointer, or the
// scroll timer, which the session disconnects before it is destroyed.

namespace gui_forms {

static_assert(maximum_drag_targets == maximum_drag_candidates,
              "a plan's targets fit the engine's candidate storage");
static_assert(static_cast<std::size_t>(maximum_drag_target_groups) == maximum_drag_groups,
              "a target's group is one of the engine's groups");

namespace {

// A late scroll frame scrolls at most this many frames' worth, so a stalled
// window does not jump the content when it wakes.
constexpr double maximum_scroll_frames = 4.0;

bool finite_point(Point point) noexcept {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    return finite;
}

bool finite_rect(Rect rect) noexcept {
    const bool finite = std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
                        std::isfinite(rect.height) && rect.width >= 0.0 && rect.height >= 0.0;
    return finite;
}

bool empty_rect(Rect rect) noexcept {
    const bool empty = !(rect.width > 0.0) || !(rect.height > 0.0);
    return empty;
}

bool finite_size(Size size) noexcept {
    const bool finite = std::isfinite(size.width) && std::isfinite(size.height) && size.width >= 0.0 &&
                        size.height >= 0.0;
    return finite;
}

Point centre_of(Rect rect) noexcept {
    const Point centre{rect.x + rect.width * 0.5, rect.y + rect.height * 0.5};
    return centre;
}

// One axis of drag_auto_scroll_velocity: the viewport spans [low, low +
// extent) on it.
double axis_velocity(double position, double low, double extent, double band, double speed) noexcept {
    const double width = std::min(band, extent * 0.5);
    if (!(width > 0.0)) return 0.0;
    const double near_edge = low + width;
    const double far_edge = low + extent - width;
    if (position < near_edge) {
        const double depth = std::min(1.0, (near_edge - position) / width);
        const double velocity = -speed * depth;
        return velocity;
    }
    if (position > far_edge) {
        const double depth = std::min(1.0, (position - far_edge) / width);
        const double velocity = speed * depth;
        return velocity;
    }
    return 0.0;
}

DragMetric metric_along(DragAxis axis) noexcept {
    const DragMetric metric = axis == DragAxis::x ? DragMetric::axis_x : DragMetric::axis_y;
    return metric;
}

// The fault a builder refusal stands for.
DragPlanFault fault_of(DragPlanRefusal refusal) noexcept {
    switch (refusal) {
    case DragPlanRefusal::none:
        return DragPlanFault::none;
    case DragPlanRefusal::client_refused:
        return DragPlanFault::declined;
    case DragPlanRefusal::too_many_candidates:
        return DragPlanFault::too_many_targets;
    case DragPlanRefusal::too_many_operators:
    case DragPlanRefusal::too_many_groups:
    case DragPlanRefusal::bad_child:
    case DragPlanRefusal::bad_value:
    case DragPlanRefusal::empty:
    default:
        // The tracker builds at most a handful of operators in groups it
        // checked, so every other refusal is a value the owner gave.
        return DragPlanFault::bad_value;
    }
}

DragPlanRefusal refusal_of(DragPlanFault fault) noexcept {
    switch (fault) {
    case DragPlanFault::none:
        return DragPlanRefusal::none;
    case DragPlanFault::declined:
        return DragPlanRefusal::client_refused;
    case DragPlanFault::too_many_targets:
        return DragPlanRefusal::too_many_candidates;
    case DragPlanFault::bad_value:
    default:
        // Any other fault refuses the plan as a bad value.
        return DragPlanRefusal::bad_value;
    }
}

} // namespace

bool drag_end_commits(DragEnd end) noexcept {
    const bool commits = end == DragEnd::dropped || end == DragEnd::free;
    return commits;
}

Point drag_auto_scroll_velocity(const DragAutoScroll& scroll, Point pointer) noexcept {
    const Point still{};
    if (!finite_point(pointer) || !finite_rect(scroll.viewport) || empty_rect(scroll.viewport)) return still;
    if (!std::isfinite(scroll.band) || !std::isfinite(scroll.speed)) return still;
    if (!(scroll.band > 0.0) || !(scroll.speed > 0.0)) return still;
    const Rect view = scroll.viewport;
    const double x = axis_velocity(pointer.x, view.x, view.width, scroll.band, scroll.speed);
    const double y = axis_velocity(pointer.y, view.y, view.height, scroll.band, scroll.speed);
    const Point velocity{x, y};
    return velocity;
}

namespace detail {

class DragTrackerSession final : public DragClient {
public:
    DragTrackerSession(DragTracker& tracker, Control& owner, DragInteractionEvents* interactions) noexcept
        : tracker_(&tracker), owner_(&owner), interactions_(interactions) {}
    // The scroll timer, a member, stops as it is destroyed.
    ~DragTrackerSession() override = default;
    DragTrackerSession(const DragTrackerSession&) = delete;
    DragTrackerSession& operator=(const DragTrackerSession&) = delete;

    // -- the engine's client --
    [[nodiscard]] DragPlanRefusal plan_drag(DragPlanBuilder& builder, const DragSubject& subject) override;
    void preview_drag(const DragFrame& frame) override;
    void announce_drag(const DragFrame& frame, DragAnnouncement& announcement) override;
    void rewrite_drag_outcome(DragOutcome& outcome) override;
    void end_drag(const DragOutcome& outcome) override;
    [[nodiscard]] DragInteractionEvents* drag_interactions() noexcept override { return interactions_; }

    // -- the tracker's operations (drag_tracker.hpp) --
    [[nodiscard]] DragStartResult start(const DragStart& start, DragInputMode mode, const PointerEvent* press);
    bool pointer(const PointerEvent& event);
    void cancel(DragEnd end);
    bool withdraw(std::uint32_t id);
    void finish_settling();
    void detaching(SurfaceRoot& former_root) noexcept;
    // The tracker is being destroyed: a running session ends without a call.
    void abandon() noexcept;
    [[nodiscard]] DragPhase phase() const;
    void scroll_tick(FrameTime now);

    DragTrackerStatistics statistics{};
    DragPlanFault fault{DragPlanFault::none};
    Point velocity{};

private:
    // The scroll timer's target. The timer is the session's member and
    // skips the ticks of a destroyed timer, all on the window's thread, so
    // `session` is valid whenever it calls. A tick reports unchanged: the
    // tracker cannot see the owner's paints (the timer's paint gate needs
    // note_painted from on_paint), so it paces at the display interval and
    // the owner's scroll invalidates what moved.
    struct ScrollTick final {
        DragTrackerSession* session;
        AnimationTickResult operator()(FrameTime now) const {
            (*session).scroll_tick(now);
            return AnimationTickResult::unchanged;
        }
    };

    // Counts the owner's callbacks in progress (depth_), for the reentrancy
    // rules: no start, no pointer and no replan inside one.
    class CallbackScope final {
    public:
        explicit CallbackScope(int& depth) noexcept : depth_(&depth) { ++(*depth_); }
        ~CallbackScope() { --(*depth_); }
        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;

    private:
        int* depth_;
    };

    // The engine of the owner's root when it runs this session; null
    // otherwise (no session, the owner gone or detached).
    [[nodiscard]] DragEngine* running_engine() const;
    // The engine of the owner's root if it was made, running or not.
    [[nodiscard]] DragEngine* made_engine() const;
    [[nodiscard]] bool withdrawn(std::uint32_t id) const noexcept;
    [[nodiscard]] DragTrackerFrame frame_of(DragPhase phase, DragInputMode mode, std::uint32_t epoch, Point pointer,
                                            const DragSelection& selection, Rect landing) const noexcept;
    [[nodiscard]] DragNodeIndex build_placement(DragPlanBuilder& builder, const DragPlan& plan,
                                                const DragSubject& subject, DragPlanFault& found);
    [[nodiscard]] DragNodeIndex build_value(DragPlanBuilder& builder, const DragPlan& plan, DragPlanFault& found);
    [[nodiscard]] DragNodeIndex build_reorder(DragPlanBuilder& builder, const DragPlan& plan, DragPlanFault& found);
    void replan_now(DragEngine& engine, Point moved);
    // Asks the owner to announce the withdrawn target `id` of a keyboard
    // session: at once outside callbacks, else with the next frame's request.
    void announce_withdrawn(DragEngine& engine, std::uint32_t id);
    void update_scroll(Point pointer);
    void stop_scroll() noexcept;

    DragTracker* tracker_;
    // Borrowed until a start; observed from then on (observed_).
    Control* owner_;
    Control::WeakPtr observed_{};
    DragInteractionEvents* interactions_;

    // The session's terms, set at its start and its first plan.
    std::uint64_t key_{};
    DragKind kind_{DragKind::placement};
    // The placement's lock as the first plan resolved it (never dominant).
    DragAxisLock lock_{DragAxisLock::none};
    DragAutoScroll scroll_{};
    // The last pointer position and modifiers the owner handed in.
    Point pointer_{};
    Modifier modifiers_{Modifier::none};
    // Whether this start reached plan_drag (a refused start is then refused,
    // not bad input).
    bool planned_{};
    // A withdrawal inside a callback waits for the next event to plan again.
    bool replan_pending_{};
    // A keyboard withdrawal inside a callback, announced with the next frame.
    bool announce_withdrawal_{};
    std::uint32_t announced_id_{};
    int depth_{};
    // The landings of the placement's targets: capacity
    // maximum_drag_targets, reserved at the first start, never grown.
    std::vector<DragLanding> landings_{};
    // The ids withdrawn in this session: capacity
    // maximum_withdrawn_drag_targets, live count withdrawn_count_.
    std::array<std::uint32_t, maximum_withdrawn_drag_targets> withdrawn_{};
    std::size_t withdrawn_count_{};
    // Auto-scroll's frames: started when the pointer enters a band, stopped
    // when it leaves, at the content's end, at every session end, on detach
    // and destruction. A window that cancels its frame requests stops it
    // too; the next pointer move in a band starts it again.
    AnimationTimer scroll_timer_{};
    FrameTime last_tick_{};
};

// -- lookups ------------------------------------------------------------------------

DragEngine* DragTrackerSession::made_engine() const {
    const Control::Ptr owner = observed_.lock();
    if (!owner || !(*owner).is_alive()) return nullptr;
    SurfaceRoot* const root = (*owner).attached_surface_root();
    if (root == nullptr || (*root).drag_engine_if_made() == nullptr) return nullptr;
    DragEngine* const engine = &(*root).drag_engine();
    return engine;
}

DragEngine* DragTrackerSession::running_engine() const {
    DragEngine* const engine = made_engine();
    if (engine == nullptr || !(*engine).runs(*this)) return nullptr;
    return engine;
}

bool DragTrackerSession::withdrawn(std::uint32_t id) const noexcept {
    const std::span<const std::uint32_t> live(withdrawn_.data(), withdrawn_count_);
    const bool found = std::find(live.begin(), live.end(), id) != live.end();
    return found;
}

DragTrackerFrame DragTrackerSession::frame_of(DragPhase phase, DragInputMode mode, std::uint32_t epoch, Point pointer,
                                              const DragSelection& selection, Rect landing) const noexcept {
    DragTrackerFrame frame{};
    frame.phase = phase;
    frame.mode = mode;
    frame.key = key_;
    frame.epoch = epoch;
    frame.pointer = pointer;
    frame.modifiers = modifiers_;
    switch (selection.kind) {
    case DragSelectionKind::candidate:
        if (kind_ == DragKind::reorder) {
            frame.hit = DragHit::slot;
        } else if (withdrawn(selection.token.value)) {
            // A withdrawn target is no hit until the plan is redone.
            break;
        } else {
            frame.hit = DragHit::target;
        }
        frame.target = selection.token.value;
        frame.landing = landing;
        break;
    case DragSelectionKind::value:
        frame.hit = DragHit::value;
        frame.value = selection.value;
        break;
    case DragSelectionKind::point:
        frame.hit = DragHit::point;
        frame.point = selection.point;
        break;
    case DragSelectionKind::none:
    default:
        // Nothing selected (or a kind the engine does not make): no hit.
        break;
    }
    return frame;
}

// -- planning -----------------------------------------------------------------------

DragPlanRefusal DragTrackerSession::plan_drag(DragPlanBuilder& builder, const DragSubject& subject) {
    planned_ = true;
    ++statistics.plans;
    DragPlan plan{};
    plan.key = subject.key;
    plan.press = subject.press;
    plan.source = subject.source;
    plan.mode = subject.mode;
    plan.epoch = subject.epoch.value;
    if (subject.mode == DragInputMode::pointer) {
        plan.travel = Point{pointer_.x - subject.press.x, pointer_.y - subject.press.y};
    }
    {
        const CallbackScope scope(depth_);
        const Delegate<DragPlan&> callback = (*tracker_).callbacks.plan;
        callback(plan);
    }
    // The callback may have cancelled the session; the engine stops there.
    if (running_engine() == nullptr) return DragPlanRefusal::client_refused;
    DragPlanFault found = DragPlanFault::none;
    DragNodeIndex root = no_drag_node;
    if (plan.refused) {
        found = DragPlanFault::declined;
    } else {
        switch (plan.kind) {
        case DragKind::placement:
            root = build_placement(builder, plan, subject, found);
            break;
        case DragKind::value:
            root = build_value(builder, plan, found);
            break;
        case DragKind::reorder:
            root = build_reorder(builder, plan, found);
            break;
        default:
            // An unknown kind declares nothing the tracker can build.
            found = DragPlanFault::bad_value;
            break;
        }
    }
    if (found == DragPlanFault::none && root == no_drag_node) found = fault_of(builder.refusal());
    if (found == DragPlanFault::none && root == no_drag_node) found = DragPlanFault::bad_value;
    if (found != DragPlanFault::none) {
        fault = found;
        ++statistics.refused_plans;
        const DragPlanRefusal refusal = refusal_of(found);
        return refusal;
    }
    if (plan.container != nullptr) builder.group_container(DragGroup{}, *plan.container);
    builder.drop_motion(plan.settles);
    kind_ = plan.kind;
    const bool scrolls = subject.mode == DragInputMode::pointer;
    scroll_ = scrolls ? plan.auto_scroll : DragAutoScroll{};
    const DragPlanRefusal refusal = builder.finish(root);
    if (refusal != DragPlanRefusal::none) {
        fault = fault_of(refusal);
        ++statistics.refused_plans;
    }
    return refusal;
}

// The placement's operators: the free area (with the grid and its toggle),
// then the targets (contained or magnetic, optionally suppressed), the
// targets taking precedence over the area within the snap radius.
DragNodeIndex DragTrackerSession::build_placement(DragPlanBuilder& builder, const DragPlan& plan,
                                                  const DragSubject& subject, DragPlanFault& found) {
    const DragPlacement& terms = plan.placement;
    if (terms.targets.size() > maximum_drag_targets) {
        found = DragPlanFault::too_many_targets;
        return no_drag_node;
    }
    const bool radius = std::isfinite(terms.snap_radius) && terms.snap_radius >= 0.0;
    // Zero releases at the snap radius; otherwise the release is at least it.
    const double release = terms.release_radius > 0.0 ? terms.release_radius : terms.snap_radius;
    const bool released = std::isfinite(terms.release_radius) && release >= terms.snap_radius;
    const bool accepts_something = !terms.targets.empty() || terms.free_drop;
    if (!radius || !released || !finite_rect(terms.bounds) || !finite_size(terms.grid) || !accepts_something) {
        found = DragPlanFault::bad_value;
        return no_drag_node;
    }
    for (const DragTarget& target : terms.targets) {
        if (!finite_rect(target.bounds) || target.group >= maximum_drag_target_groups) {
            found = DragPlanFault::bad_value;
            return no_drag_node;
        }
    }
    // Empty bounds are the window's client area.
    Rect bounds = terms.bounds;
    if (empty_rect(bounds) && subject.control != nullptr) {
        SurfaceRoot* const root = (*subject.control).attached_surface_root();
        Control::Ptr top{};
        if (root != nullptr) top = (*root).root();
        if (top) bounds = (*top).absolute_bounds();
    }
    // The axis lock: dominant is resolved once, at the first plan, and kept.
    DragAxisLock lock = terms.axis_lock;
    if (lock == DragAxisLock::dominant) {
        if (subject.epoch.value != 1U) {
            lock = lock_;
        } else if (subject.mode == DragInputMode::keyboard) {
            lock = DragAxisLock::none;
        } else {
            const bool across = std::fabs(plan.travel.x) >= std::fabs(plan.travel.y);
            lock = across ? DragAxisLock::horizontal : DragAxisLock::vertical;
        }
    }
    lock_ = lock;
    const Rect source = subject.source;
    if (lock == DragAxisLock::horizontal) bounds = Rect{bounds.x, source.y, bounds.width, source.height};
    if (lock == DragAxisLock::vertical) bounds = Rect{source.x, bounds.y, source.width, bounds.height};

    DragFreeArea plain{};
    plain.bounds = bounds;
    DragFreeArea gridded = plain;
    gridded.grid = terms.grid;
    const bool has_grid = terms.grid.width > 0.0 || terms.grid.height > 0.0;
    DragNodeIndex area = no_drag_node;
    if (!has_grid) {
        area = builder.free(plain);
    } else if (terms.grid_toggle == Modifier::none) {
        area = builder.free(terms.grid_by_default ? gridded : plain);
    } else {
        // The usual area while the toggle is up, the other while it is held.
        const DragNodeIndex usual = builder.free(terms.grid_by_default ? gridded : plain);
        const DragNodeIndex other = builder.free(terms.grid_by_default ? plain : gridded);
        const DragNodeIndex unless_held = builder.suppress(usual, terms.grid_toggle);
        area = builder.fallback(unless_held, other, 0.0);
    }

    landings_.clear();
    for (const DragTarget& target : terms.targets) {
        if (withdrawn(target.id)) continue;
        DragLanding landing{};
        landing.rect = target.bounds;
        landing.anchor = centre_of(target.bounds);
        landing.token.value = target.id;
        landing.group.value = target.group;
        // Within the capacity reserved at the start (checked above).
        landings_.push_back(landing);
    }
    DragNodeIndex hit = no_drag_node;
    if (!landings_.empty()) {
        const std::span<const DragLanding> declared(landings_.data(), landings_.size());
        const DragCandidateRange range = builder.add_landings(declared);
        if (terms.snap_radius > 0.0) {
            const DragNodeIndex nearest = builder.nearest(range, DragMetric::edge, 0.0);
            hit = builder.snap(nearest, terms.snap_radius, DragChain::off, release);
        } else {
            hit = builder.target(range);
        }
        if (terms.targets_suppressed_by != Modifier::none) hit = builder.suppress(hit, terms.targets_suppressed_by);
    }
    // Without free_drop only the targets are accepted; once every target is
    // withdrawn the plan selects nothing and is refused.
    if (!terms.free_drop) return hit;
    if (hit == no_drag_node) return area;
    // A held target lies within the release radius, so the gap is that.
    const DragNodeIndex root = builder.fallback(hit, area, release);
    return root;
}

DragNodeIndex DragTrackerSession::build_value(DragPlanBuilder& builder, const DragPlan& plan, DragPlanFault& found) {
    const DragValue& terms = plan.value;
    DragProjection projection{};
    projection.axis = terms.axis;
    projection.origin_value = terms.origin_value;
    projection.scale = terms.scale;
    projection.minimum = terms.minimum;
    projection.maximum = terms.maximum;
    projection.step = terms.step;
    projection.keyboard_step = terms.keyboard_step;
    // The builder checks the terms (finite, ordered, a non-zero scale).
    const DragNodeIndex root = builder.project(projection);
    if (root == no_drag_node) found = fault_of(builder.refusal());
    return root;
}

DragNodeIndex DragTrackerSession::build_reorder(DragPlanBuilder& builder, const DragPlan& plan, DragPlanFault& found) {
    const DragReorder& terms = plan.reorder;
    if (terms.slots.size() > maximum_drag_targets) {
        found = DragPlanFault::too_many_targets;
        return no_drag_node;
    }
    if (terms.slots.empty() || terms.subject >= terms.slots.size()) {
        found = DragPlanFault::bad_value;
        return no_drag_node;
    }
    DragListGeometry list{};
    list.axis = terms.axis;
    list.slots = terms.slots;
    list.subject = terms.subject;
    const DragCandidateRange range = builder.add_list(list);
    const DragNodeIndex root = builder.nearest(range, metric_along(terms.axis), 0.0);
    if (root == no_drag_node) found = fault_of(builder.refusal());
    return root;
}

// -- frames and the end -------------------------------------------------------------

void DragTrackerSession::preview_drag(const DragFrame& frame) {
    ++statistics.previews;
    const DragTrackerFrame shown = frame_of(frame.phase, frame.mode, frame.epoch.value, frame.pointer,
                                            frame.selection, frame.landing);
    const CallbackScope scope(depth_);
    const Delegate<const DragTrackerFrame&> callback = (*tracker_).callbacks.preview;
    callback(shown);
}

void DragTrackerSession::announce_drag(const DragFrame& frame, DragAnnouncement& announcement) {
    const Delegate<DragAnnouncementRequest&> callback = (*tracker_).callbacks.announce;
    if (!callback) return;
    DragAnnouncementRequest request{};
    if (announce_withdrawal_) {
        request.reason = DragAnnouncementReason::target_withdrawn;
        request.withdrawn = announced_id_;
        announce_withdrawal_ = false;
    }
    request.frame = frame_of(frame.phase, frame.mode, frame.epoch.value, frame.pointer, frame.selection,
                             frame.landing);
    request.frame.end = frame.end;
    {
        const CallbackScope scope(depth_);
        callback(request);
    }
    if (!request.text.empty()) announcement.assign(request.text);
}

void DragTrackerSession::rewrite_drag_outcome(DragOutcome& outcome) {
    // A release on a target withdrawn since the last plan accepts nothing.
    const bool target = outcome.selection.kind == DragSelectionKind::candidate && kind_ != DragKind::reorder;
    if (target && withdrawn(outcome.selection.token.value)) outcome.end = DragEnd::rejected;
}

void DragTrackerSession::end_drag(const DragOutcome& outcome) {
    stop_scroll();
    replan_pending_ = false;
    // The selected landing's rectangle, from the engine's plan (kept until
    // the next session plans).
    Rect landing{};
    const DragEngine* const engine = made_engine();
    if (engine != nullptr && outcome.selection.kind == DragSelectionKind::candidate) {
        const std::span<const DragLanding> landings = (*engine).landings();
        if (outcome.selection.landing < landings.size()) landing = landings[outcome.selection.landing].rect;
    }
    const DragPhase phase = outcome.animate ? DragPhase::settling : DragPhase::idle;
    DragTrackerFrame last = frame_of(phase, outcome.mode, outcome.epoch.value, outcome.pointer, outcome.selection,
                                     landing);
    last.end = outcome.end;
    last.animate = outcome.animate;
    if (drag_end_commits(outcome.end)) {
        ++statistics.commits;
    } else {
        ++statistics.restores;
    }
    const CallbackScope scope(depth_);
    const Delegate<const DragTrackerFrame&> callback = (*tracker_).callbacks.finished;
    callback(last);
}

// -- operations ---------------------------------------------------------------------

DragStartResult DragTrackerSession::start(const DragStart& start, DragInputMode mode, const PointerEvent* press) {
    const DragTrackerCallbacks& callbacks = (*tracker_).callbacks;
    DragStartResult problem = DragStartResult::started;
    if (depth_ > 0) {
        problem = DragStartResult::busy;
    } else if (!callbacks.plan || !callbacks.finished) {
        problem = DragStartResult::bad_input;
    } else if (mode == DragInputMode::pointer) {
        const bool down = press != nullptr && (*press).action == PointerAction::down && finite_point((*press).position);
        const bool threshold = std::isfinite(start.threshold) && start.threshold >= 0.0;
        const bool source = !start.source || finite_rect(*start.source);
        if (!down || !threshold || !source) problem = DragStartResult::bad_input;
    }
    Control& subject = start.subject != nullptr ? *start.subject : *owner_;
    SurfaceRoot* const root = (*owner_).attached_surface_root();
    if (problem == DragStartResult::started) {
        SurfaceRoot* const subject_root = subject.attached_surface_root();
        if (root == nullptr || subject_root != root) problem = DragStartResult::detached;
    }
    if (problem == DragStartResult::started && (*root).drag_engine().phase() != DragPhase::idle) {
        problem = DragStartResult::busy;
    }
    if (problem != DragStartResult::started) {
        ++statistics.refused_starts;
        return problem;
    }
    DragEngine& engine = (*root).drag_engine();
    observed_ = (*owner_).weak_from_this();
    // Reserved once, at the cap (the engine's own storage likewise).
    if (landings_.capacity() < maximum_drag_targets) landings_.reserve(maximum_drag_targets);
    stop_scroll();
    key_ = start.key;
    kind_ = DragKind::placement;
    lock_ = DragAxisLock::none;
    scroll_ = DragAutoScroll{};
    planned_ = false;
    replan_pending_ = false;
    announce_withdrawal_ = false;
    withdrawn_count_ = 0U;
    fault = DragPlanFault::none;
    ++statistics.sessions;
    bool begun = false;
    if (mode == DragInputMode::pointer) {
        pointer_ = (*press).position;
        modifiers_ = (*press).modifiers;
        const Rect source = start.source ? *start.source : subject.absolute_bounds();
        begun = engine.arm(*this, *owner_, subject, start.key, pointer_, source, start.threshold);
    } else {
        modifiers_ = Modifier::none;
        begun = engine.begin_keyboard(*this, *owner_, subject, start.key);
    }
    if (begun) {
        const bool armed = engine.phase() == DragPhase::armed;
        const DragStartResult result = armed ? DragStartResult::armed : DragStartResult::started;
        return result;
    }
    ++statistics.refused_starts;
    const DragStartResult result = planned_ ? DragStartResult::refused : DragStartResult::bad_input;
    return result;
}

bool DragTrackerSession::pointer(const PointerEvent& event) {
    if (depth_ > 0) return false;
    DragEngine* engine = running_engine();
    if (engine == nullptr) return false;
    if (event.action == PointerAction::move || event.action == PointerAction::up) {
        pointer_ = event.position;
        modifiers_ = event.modifiers;
    }
    if (replan_pending_ && (*engine).phase() == DragPhase::dragging) {
        replan_now(*engine, Point{});
        engine = running_engine();
        // The redone plan was refused: the session ended, and took this event.
        if (engine == nullptr) return true;
    }
    const bool taken = (*engine).pointer(event);
    if (event.action == PointerAction::move) update_scroll(event.position);
    return taken;
}

void DragTrackerSession::cancel(DragEnd end) {
    if (end != DragEnd::model_changed && end != DragEnd::escaped) return;
    DragEngine* const engine = running_engine();
    if (engine == nullptr) return;
    (*engine).cancel(end);
}

bool DragTrackerSession::withdraw(std::uint32_t id) {
    DragEngine* const engine = running_engine();
    if (engine == nullptr || (*engine).phase() != DragPhase::dragging || withdrawn(id)) return false;
    if (withdrawn_count_ >= withdrawn_.size()) {
        (*engine).cancel(DragEnd::model_changed);
        return false;
    }
    withdrawn_[withdrawn_count_] = id;
    ++withdrawn_count_;
    ++statistics.withdrawn;
    // A keyboard session is never planned again (its stops would move): the
    // target stops being a stop, and the owner announces it.
    if ((*engine).mode() != DragInputMode::pointer) {
        DragCandidateToken token{};
        token.value = id;
        static_cast<void>((*engine).withdraw_keyboard_stop(*this, token));
        announce_withdrawn(*engine, id);
        return true;
    }
    if (depth_ > 0) {
        replan_pending_ = true;
        ++statistics.deferred_replans;
        return true;
    }
    replan_now(*engine, Point{});
    return true;
}

void DragTrackerSession::finish_settling() {
    DragEngine* const engine = made_engine();
    if (engine == nullptr || !(*engine).runs(*this)) return;
    (*engine).finish_settling();
}

void DragTrackerSession::detaching(SurfaceRoot& former_root) noexcept {
    stop_scroll();
    if (former_root.drag_engine_if_made() == nullptr) return;
    try {
        former_root.drag_engine().client_gone(*this);
    } catch (...) {
        // Only a call off the root's thread throws; detaching runs on it.
    }
}

void DragTrackerSession::abandon() noexcept {
    stop_scroll();
    try {
        DragEngine* const engine = made_engine();
        if (engine != nullptr) (*engine).client_gone(*this);
    } catch (...) {
        // Only a destruction off the root's thread throws here; the engine
        // then still observes the owner, and stops calling once it is gone.
    }
}

DragPhase DragTrackerSession::phase() const {
    const DragEngine* const engine = running_engine();
    if (engine == nullptr) return DragPhase::idle;
    const DragPhase current = (*engine).phase();
    return current;
}

void DragTrackerSession::announce_withdrawn(DragEngine& engine, std::uint32_t id) {
    const Delegate<DragAnnouncementRequest&> callback = (*tracker_).callbacks.announce;
    if (!callback) return;
    if (depth_ > 0) {
        announce_withdrawal_ = true;
        announced_id_ = id;
        return;
    }
    // The session as it stands: the selected landing, the virtual pointer
    // at its anchor (where the keyboard's pointer is for a candidate).
    const DragSelection& selection = engine.selection();
    Rect landing{};
    Point pointer{};
    const std::span<const DragLanding> landings = engine.landings();
    if (selection.kind == DragSelectionKind::candidate && selection.landing < landings.size()) {
        landing = landings[selection.landing].rect;
        pointer = landings[selection.landing].anchor;
    }
    DragAnnouncementRequest request{};
    request.reason = DragAnnouncementReason::target_withdrawn;
    request.withdrawn = id;
    request.frame = frame_of(engine.phase(), engine.mode(), engine.epoch().value, pointer, selection, landing);
    {
        const CallbackScope scope(depth_);
        callback(request);
    }
    if (!request.text.empty()) static_cast<void>(engine.set_announcement(*this, request.text));
}

void DragTrackerSession::replan_now(DragEngine& engine, Point moved) {
    replan_pending_ = false;
    ++statistics.replans;
    static_cast<void>(engine.replan(*this, moved));
}

// -- auto-scroll --------------------------------------------------------------------

void DragTrackerSession::update_scroll(Point pointer) {
    const DragEngine* const engine = running_engine();
    const bool scrolls = engine != nullptr && (*engine).phase() == DragPhase::dragging &&
                         (*engine).mode() == DragInputMode::pointer && (*tracker_).callbacks.scroll;
    if (!scrolls) {
        stop_scroll();
        return;
    }
    velocity = drag_auto_scroll_velocity(scroll_, pointer);
    if (velocity.x == 0.0 && velocity.y == 0.0) {
        stop_scroll();
        return;
    }
    if (scroll_timer_.started()) return;
    const Control::Ptr owner = observed_.lock();
    if (!owner) return;
    last_tick_ = FrameClock::now();
    // The interval is the display's and the target is set, so start does
    // not refuse them.
    scroll_timer_.start(*owner, display_frame_interval, ScrollTick{this});
}

void DragTrackerSession::stop_scroll() noexcept {
    scroll_timer_.stop();
    velocity = Point{};
}

void DragTrackerSession::scroll_tick(FrameTime now) {
    DragEngine* engine = running_engine();
    if (engine == nullptr || (*engine).phase() != DragPhase::dragging) {
        stop_scroll();
        return;
    }
    // A frame that arrives during one of the owner's callbacks waits for
    // the next.
    if (depth_ > 0) return;
    ++statistics.scroll_ticks;
    if (replan_pending_) {
        replan_now(*engine, Point{});
        engine = running_engine();
        if (engine == nullptr) {
            stop_scroll();
            return;
        }
    }
    const double frame_seconds = std::chrono::duration<double>(display_frame_interval).count();
    const double elapsed = std::chrono::duration<double>(now - last_tick_).count();
    const double seconds = std::clamp(elapsed, 0.0, frame_seconds * maximum_scroll_frames);
    // A tick on the clock's phase may fall at the start time: no time, no step.
    if (!(seconds > 0.0)) return;
    last_tick_ = now;
    velocity = drag_auto_scroll_velocity(scroll_, pointer_);
    if (velocity.x == 0.0 && velocity.y == 0.0) {
        stop_scroll();
        return;
    }
    DragAutoScrollStep step{};
    step.requested = Point{velocity.x * seconds, velocity.y * seconds};
    {
        const CallbackScope scope(depth_);
        const Delegate<DragAutoScrollStep&> callback = (*tracker_).callbacks.scroll;
        callback(step);
    }
    engine = running_engine();
    if (engine == nullptr) {
        stop_scroll();
        return;
    }
    const bool moved = finite_point(step.applied) && (step.applied.x != 0.0 || step.applied.y != 0.0);
    if (!moved) {
        // The content's end: stop until the pointer moves again.
        ++statistics.scroll_stops;
        stop_scroll();
        return;
    }
    ++statistics.scroll_steps;
    // The view moved right and down by `applied`, so the content moved left
    // and up in the window's coordinates.
    const Point content{-step.applied.x, -step.applied.y};
    replan_now(*engine, content);
}

} // namespace detail

// -- DragTracker --------------------------------------------------------------------

DragTracker::DragTracker(Control& owner, DragInteractionEvents* interactions)
    : session_(std::make_unique<detail::DragTrackerSession>(*this, owner, interactions)) {}

DragTracker::~DragTracker() { (*session_).abandon(); }

DragStartResult DragTracker::press(const PointerEvent& event, const DragStart& start) {
    const DragStartResult result = (*session_).start(start, DragInputMode::pointer, &event);
    return result;
}

bool DragTracker::pointer(const PointerEvent& event) {
    const bool taken = (*session_).pointer(event);
    return taken;
}

DragStartResult DragTracker::begin_keyboard(const DragStart& start) {
    const DragStartResult result = (*session_).start(start, DragInputMode::keyboard, nullptr);
    return result;
}

void DragTracker::cancel(DragEnd end) { (*session_).cancel(end); }

bool DragTracker::withdraw_target(std::uint32_t id) {
    const bool withdrawn = (*session_).withdraw(id);
    return withdrawn;
}

void DragTracker::finish_settling() { (*session_).finish_settling(); }

void DragTracker::owner_detaching(SurfaceRoot& former_root) noexcept { (*session_).detaching(former_root); }

DragPhase DragTracker::phase() const {
    const DragPhase current = (*session_).phase();
    return current;
}

bool DragTracker::running() const {
    const bool runs = (*session_).phase() != DragPhase::idle;
    return runs;
}

DragPlanFault DragTracker::last_fault() const noexcept { return (*session_).fault; }

Point DragTracker::scroll_velocity() const noexcept { return (*session_).velocity; }

const DragTrackerStatistics& DragTracker::statistics() const noexcept { return (*session_).statistics; }

} // namespace gui_forms
