#pragma once
// The public drag client (cp29 phase 2b; roadmap 2.1; NODE_GRAPH_SPEC gaps 9
// to 11; owner, 2026-11-12: public APIs before 1.0). An application or a
// control holds a DragTracker to start, track, constrain, auto-scroll,
// hit-test drop targets for, preview and cancel a drag inside one window.
// The window's drag engine runs the session (src/core/drag_engine, internal);
// the tracker is that engine's client and translates the plan an owner
// declares through named callbacks (Delegate: a member function and the
// object it is called on) into the engine's operators. Platform drag and
// drop between applications is not this: it stays behind the host's drag
// capabilities (SurfaceRoot::dispatch_drag, Control::on_drag).
//
// A drag in four steps:
//  1. The owner's on_pointer hands a press to press() (or a rule or key to
//     begin_keyboard()). The engine takes the pointer capture for the
//     subject and the session is armed.
//  2. Once the pointer has moved `threshold` on either axis (at once for a
//     threshold of zero, and for the keyboard) the session plans: the
//     tracker calls `callbacks.plan` with a DragPlan to fill. The plan names
//     one kind: a placement (a free position inside bounds, with an axis
//     lock and a grid, and drop targets that take precedence), a value (one
//     number along an axis) or a reorder (the subject among slots).
//  3. Every later pointer move hands the owner a DragTrackerFrame through
//     `callbacks.preview` (after each change of hit, and on every move while
//     a point or a value is shown). Near a declared viewport's edges the
//     tracker auto-scrolls through `callbacks.scroll` and plans again.
//  4. The session ends once: `callbacks.finished` receives the last frame
//     with its DragEnd. The owner commits for dropped and free
//     (drag_end_commits) and restores for every other end.
//
// The session's states (DragPhase):
//
//   idle -press-> armed -past the threshold-> planning -> dragging -> idle
//   idle -begin_keyboard-> planning -> dragging -> idle
//   dragging -end with DragPlan::settles-> settling -finish_settling-> idle
//
// What ends a session, in any state but idle (an armed session that ends
// was never planned, so `finished` is not called for it):
//   - the release: dropped on a target or slot, free for a point or a
//     value, rejected where nothing is accepted;
//   - Escape (the window routes it to the engine first): escaped;
//   - the subject losing the pointer capture: capture_lost;
//   - the window's deactivation: deactivated;
//   - cancel(): model_changed (the owner's model changed under the drag);
//   - a refused plan: rejected (`finished` is called, nothing to restore);
//   - the owner's disposal, owner_detaching() or the tracker's destruction:
//     subject_gone, and no callback is called.
// Any other input in a state is ignored: a press while a session runs is
// refused (DragStartResult::busy); pointer() outside a session returns
// false; withdraw_target() outside a dragging session changes nothing.
//
// Reentrancy: callbacks run on the window's thread from its dispatch. A
// callback may call cancel() (the session ends at once, `finished` is
// called once, and nothing further is called for it) and withdraw_target()
// (the target is hidden at once; the plan is redone at the next pointer
// event or auto-scroll step, never inside the callback). A callback may not
// start a drag (press and begin_keyboard return busy), hand in pointer
// events (pointer returns false) or destroy the tracker. A target withdrawn while it is hit is reported as no hit; a
// release on it before the plan is redone ends rejected.
//
// Memory: the first press reserves the tracker's target storage at
// maximum_drag_targets and the engine's at its caps; neither grows. Entering
// an auto-scroll band schedules one frame timer (one allocation in the
// window's scheduler); a pointer move or frame inside a dragging session
// allocates nothing else.
//
// Threads: every member runs on the owner's window's thread (a call from
// another thread throws WrongThreadError, as the window does), one session
// per window at a time.
//
// Lifetime: the tracker borrows its owner, which must outlive it (a member
// of the owner, or destroyed first). After the first press the tracker only
// observes the owner: once the owner is disposed nothing is called. An
// owner that can leave its window during a drag calls owner_detaching()
// from on_detaching_from_window.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/events/drag_events/drag_events.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

class Control;
class SurfaceRoot;

namespace detail {
class DragTrackerSession;
} // namespace detail

// -- limits -----------------------------------------------------------------------

// The most targets (or slots) one plan declares; more is refused, never
// truncated. A provider over a large scene offers the nearest ones (the
// node graph's bounded candidate provider). Equal to the engine's cap.
inline constexpr std::size_t maximum_drag_targets = 1024U;
// Target groups (keyboard Tab steps groups): a group is below this.
inline constexpr std::uint16_t maximum_drag_target_groups = 16U;
// The most targets one session withdraws; one more cancels the drag
// (model_changed).
inline constexpr std::size_t maximum_withdrawn_drag_targets = 64U;
// The press travel, on either axis, before a pointer drag starts (logical
// units; the framework's drags use 4).
inline constexpr double default_drag_threshold = 4.0;
// Auto-scroll defaults: the edge band's depth (logical units) and the speed
// at the viewport's edge (logical units per second). Starting choices, not
// measured values.
inline constexpr double default_drag_scroll_band = 24.0;
inline constexpr double default_drag_scroll_speed = 1200.0;

// -- vocabulary -------------------------------------------------------------------

// The session's state (the engine's; see the diagram above).
enum class DragPhase : std::uint8_t { idle, armed, planning, dragging, settling };

// An axis of the window's coordinates.
enum class DragAxis : std::uint8_t { x, y };

// Which axes a placement moves on. dominant locks to the axis of the larger
// travel when the threshold is passed (horizontal on a tie; none for the
// keyboard, whose arrows move one axis at a time anyway).
enum class DragAxisLock : std::uint8_t { none, horizontal, vertical, dominant };

// What a plan declares.
enum class DragKind : std::uint8_t {
    // A free position inside bounds; declared targets take precedence.
    placement,
    // One number along an axis (a splitter, a scrubbed value).
    value,
    // The subject moved to one of several slots along an axis (a list).
    reorder,
};

// What a frame shows.
enum class DragHit : std::uint8_t { none, target, slot, point, value };

// Whether a session that ended with `end` commits (dropped or free).
[[nodiscard]] GUI_FORMS_API bool drag_end_commits(DragEnd end) noexcept;

// One drop target, in the window's coordinates. `id` is the owner's name
// for it (reported in DragTrackerFrame::target, and what withdraw_target
// takes); `group` is its keyboard group, below maximum_drag_target_groups.
struct GUI_FORMS_API DragTarget final {
    Rect bounds{};
    std::uint32_t id{};
    std::uint16_t group{};
};

// A placement: the subject's top-left kept inside `bounds` (with the
// subject's size; empty bounds use the window's client area), moved only on
// the axes `axis_lock` leaves, on `grid` from bounds' origin when the grid
// is on. The grid is on when `grid_by_default` is true, and the reverse
// while the modifiers `grid_toggle` are held (none: no toggle); a grid with
// a zero side is off on that axis.
//
// Targets: the owner writes them into `targets` (borrowed for the plan
// call). With `snap_radius` zero a target is hit when it contains the
// pointer; above zero the nearest target within that distance of the
// subject's centre (zero inside, the grab kept) attracts it, a magnet. Of
// overlapping or equidistant targets the earlier one wins. While the
// modifiers `targets_suppressed_by` are held (none: never) no target is hit.
// A release off every target places the subject (free) when `free_drop`,
// and is rejected otherwise; without targets `free_drop` must be true.
struct GUI_FORMS_API DragPlacement final {
    Rect bounds{};
    DragAxisLock axis_lock{DragAxisLock::none};
    Size grid{};
    bool grid_by_default{true};
    Modifier grid_toggle{Modifier::none};
    std::span<const DragTarget> targets{};
    double snap_radius{};
    Modifier targets_suppressed_by{Modifier::none};
    bool free_drop{true};
};

// A value: clamp(origin_value + scale * travel along `axis`, minimum,
// maximum), rounded to `step` above minimum when step is positive. `scale`
// is value units per logical unit, negative when the value grows as the
// pointer moves left or up. The keyboard's arrows step by keyboard_step
// (Ctrl ten times, Shift a tenth).
struct GUI_FORMS_API DragValue final {
    DragAxis axis{DragAxis::x};
    double origin_value{};
    double scale{1.0};
    double minimum{};
    double maximum{};
    double step{};
    double keyboard_step{1.0};
};

// A reorder: the items' rectangles in display order along `axis` (borrowed
// for the plan call) and the dragged item's index. The frame's `target` is
// the slot the subject would take (DragHit::slot); moving it later shifts
// the items between by its extent, the engine's list rule.
struct GUI_FORMS_API DragReorder final {
    DragAxis axis{DragAxis::y};
    std::span<const Rect> slots{};
    std::size_t subject{};
};

// Auto-scroll while dragging: within `band` of an edge of `viewport` (the
// window's coordinates; empty: off) the tracker asks the owner to scroll,
// faster the deeper the pointer is, `speed` at and beyond the edge
// (drag_auto_scroll_velocity). Pointer sessions only.
struct GUI_FORMS_API DragAutoScroll final {
    Rect viewport{};
    double band{default_drag_scroll_band};
    double speed{default_drag_scroll_speed};
};

// The velocity auto-scroll asks for with the pointer at `pointer`, in
// logical units per second, positive right and down, each axis apart: zero
// outside the bands; inside one, `speed` times the depth into it over its
// width; at or past the edge, `speed`. A band wider than half the viewport
// is narrowed to half. Zero for an empty viewport, a band or speed that is
// not positive, or a non-finite input.
[[nodiscard]] GUI_FORMS_API Point drag_auto_scroll_velocity(const DragAutoScroll& scroll, Point pointer) noexcept;

// The plan callback's record: the tracker fills the inputs, the owner the
// rest. Only the terms of `kind` are read (placement, value or reorder);
// the others keep their initial values.
//   key, press, source, mode, epoch: the drag (inputs). press is where the
//     pointer was pressed, or the subject's centre for the keyboard; source
//     the subject's rectangle; both move with the content when an
//     auto-scroll step plans again. epoch is 1 at the first plan and one
//     more at each later one.
//   travel: the pointer's travel from the press when the plan is made
//     (input; zero for the keyboard).
//   auto_scroll: read for pointer sessions; replanned each step.
//   container: borrowed for the call; the rules' drop_candidate condition
//     names it while a session runs (null: none).
//   settles: asks for a settling phase after the end (the owner animates
//     and calls finish_settling).
//   refused: the owner has nothing to drag now; the session ends rejected.
struct GUI_FORMS_API DragPlan final {
    std::uint64_t key{};
    Point press{};
    Rect source{};
    DragInputMode mode{DragInputMode::pointer};
    std::uint32_t epoch{};
    Point travel{};

    DragKind kind{DragKind::placement};
    DragPlacement placement{};
    DragValue value{};
    DragReorder reorder{};
    DragAutoScroll auto_scroll{};
    Control* container{};
    bool settles{};
    bool refused{};
};

// One frame of a session, for preview and finished.
//   key, epoch, mode, pointer, modifiers: the drag now (pointer is where a
//     pointer would be in a keyboard session).
//   hit: what the plan selects. target: the target's id (target) or the
//     slot's index (slot). landing: the target's bounds or the slot's
//     landing rectangle. point: the placement's top-left. value: the value.
//   end: none while running; how it ended in finished.
//   animate: finished only; the session settles until finish_settling.
struct GUI_FORMS_API DragTrackerFrame final {
    DragPhase phase{DragPhase::idle};
    DragInputMode mode{DragInputMode::pointer};
    std::uint64_t key{};
    std::uint32_t epoch{};
    Point pointer{};
    Modifier modifiers{Modifier::none};
    DragHit hit{DragHit::none};
    std::uint32_t target{};
    Rect landing{};
    Point point{};
    double value{};
    DragEnd end{DragEnd::none};
    bool animate{};
};

// The announce callback's record: the owner points `text` at storage that
// stays valid until the callback returns (the tracker copies it, cut to the
// engine's 256 bytes at a character boundary). Empty text keeps the last
// announcement.
struct GUI_FORMS_API DragAnnouncementRequest final {
    DragTrackerFrame frame{};
    std::string_view text{};
};

// The scroll callback's record: the owner scrolls its content by
// `requested` (logical units; positive moves the view right and down, so
// the content moves left and up) and writes in `applied` how far it did
// (less at the content's end; zero there). An applied step plans again
// with the content moved; a step applying nothing on both axes stops
// auto-scroll until the pointer moves.
struct GUI_FORMS_API DragAutoScrollStep final {
    Point requested{};
    Point applied{};
};

// The named callbacks. plan and finished are required; the rest are
// optional (auto-scroll is off without scroll). Each is read when it is
// called, so a change takes effect at the next call.
struct GUI_FORMS_API DragTrackerCallbacks final {
    Delegate<DragPlan&> plan;
    Delegate<const DragTrackerFrame&> preview;
    Delegate<const DragTrackerFrame&> finished;
    Delegate<DragAnnouncementRequest&> announce;
    Delegate<DragAutoScrollStep&> scroll;
};

// What starts a drag. `subject`: the control dragged (borrowed for the
// call; null: the owner); it holds the pointer capture, so its pointer
// events must reach pointer(). `key`: the owner's name for what is dragged.
// `source`: the subject's rectangle in the window's coordinates (none: the
// subject's bounds; the keyboard always uses them). `threshold`: the press
// travel before the drag starts, zero or more (pointer only).
struct GUI_FORMS_API DragStart final {
    Control* subject{};
    std::uint64_t key{};
    std::optional<Rect> source{};
    double threshold{default_drag_threshold};
};

enum class DragStartResult : std::uint8_t {
    // A press armed the session; it plans past the threshold.
    armed,
    // The session planned and is dragging (zero threshold, or keyboard).
    started,
    // Another session runs in the window, or the call came from one of
    // this tracker's callbacks.
    busy,
    // The owner is not in a window, or the subject is not in the owner's.
    detached,
    // The plan was refused (last_fault says why); finished was called.
    refused,
    // A press that is not a pointer down, a non-finite press, source or
    // threshold, a negative threshold, or no plan or finished callback.
    bad_input,
};

// Why the last plan was refused.
enum class DragPlanFault : std::uint8_t {
    none,
    // DragPlan::refused.
    declined,
    too_many_targets,
    // A target group out of range, a non-finite or inverted value, a
    // negative radius, a reorder subject past its slots, no targets
    // without free_drop, or an unknown kind.
    bad_value,
};

// The tracker's counters (AGENTS.md: structured instrumentation).
//   withdrawn: targets withdrawn while dragging; deferred_replans: those
//     whose replan waited for the next event (withdrawn inside a callback).
//   scroll_ticks: auto-scroll frames; scroll_steps: frames that scrolled;
//   scroll_stops: frames that applied nothing (the content's end).
struct GUI_FORMS_API DragTrackerStatistics final {
    std::uint64_t sessions{};
    std::uint64_t refused_starts{};
    std::uint64_t plans{};
    std::uint64_t refused_plans{};
    std::uint64_t replans{};
    std::uint64_t previews{};
    std::uint64_t commits{};
    std::uint64_t restores{};
    std::uint64_t withdrawn{};
    std::uint64_t deferred_replans{};
    std::uint64_t scroll_ticks{};
    std::uint64_t scroll_steps{};
    std::uint64_t scroll_stops{};
};

// -- the tracker ------------------------------------------------------------------

class GUI_FORMS_API DragTracker final {
public:
    // `owner` is borrowed (see Lifetime above). `interactions`, when not
    // null, receives the typed drag interactions the engine raises
    // (drag_started, drag_target_changed, drag_dropped, drag_ended), after
    // the tracker's own callbacks; it belongs to the owner and lives as
    // long as the tracker.
    explicit DragTracker(Control& owner, DragInteractionEvents* interactions = nullptr);
    // Ends a running session subject_gone without calling back, and stops
    // auto-scroll.
    ~DragTracker();
    DragTracker(const DragTracker&) = delete;
    DragTracker& operator=(const DragTracker&) = delete;
    DragTracker(DragTracker&&) = delete;
    DragTracker& operator=(DragTracker&&) = delete;

    DragTrackerCallbacks callbacks{};

    // Arms a pointer drag from `event`, a pointer down in the window's
    // coordinates. Nothing changes unless armed or started.
    [[nodiscard]] DragStartResult press(const PointerEvent& event, const DragStart& start);
    // Hands a pointer move or release of the subject to the session; true
    // when the session took it (the owner marks the event handled). A
    // release while armed is the press's click: false, and the session
    // ends without a plan. False outside a session of this tracker.
    bool pointer(const PointerEvent& event);
    // Starts a keyboard drag of start.subject (the move command or the
    // begin_keyboard_drag rule action): arrows step, Tab steps target
    // groups, Enter commits, Escape cancels (the window routes the keys).
    [[nodiscard]] DragStartResult begin_keyboard(const DragStart& start);
    // Ends the running session with `end`: model_changed (the default) or
    // escaped; any other end is ignored. Nothing outside a session.
    void cancel(DragEnd end = DragEnd::model_changed);
    // Hides the target `id` for the rest of the session: it is no longer
    // hit, and a pointer session's plan is redone without it (at once, or at
    // the next pointer event or auto-scroll frame when called from a
    // callback; a keyboard session only hides it). Once every target of a
    // plan without free_drop is withdrawn, the redone plan selects nothing
    // and the session ends rejected.
    // False, changing nothing, outside a dragging session or for an id
    // already withdrawn; past maximum_withdrawn_drag_targets the session is
    // cancelled (model_changed) and the result is false.
    bool withdraw_target(std::uint32_t id);
    // The owner's settling animation finished (DragTrackerFrame::animate).
    void finish_settling();
    // The owner is leaving `former_root` (call it from
    // on_detaching_from_window): a session there ends subject_gone without
    // calling back.
    void owner_detaching(SurfaceRoot& former_root) noexcept;

    [[nodiscard]] DragPhase phase() const;
    // Whether this tracker runs the window's session (any phase but idle).
    [[nodiscard]] bool running() const;
    [[nodiscard]] DragPlanFault last_fault() const noexcept;
    // The auto-scroll velocity now (zero when not scrolling).
    [[nodiscard]] Point scroll_velocity() const noexcept;
    [[nodiscard]] const DragTrackerStatistics& statistics() const noexcept;

private:
    friend class detail::DragTrackerSession;

    std::unique_ptr<detail::DragTrackerSession> session_;
};

} // namespace gui_forms
