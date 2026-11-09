#pragma once

// The drag engine (roadmap row 2.1, phase E1; the declarative dragging study,
// desks/coordinator/DECLARATIVE_DRAGGING_STUDY.md, with the owner's answers
// to its section 9 as relayed by the coordinator on 2026-11-08). Internal:
// clients are GUI.Forms controls, and nothing here is public API.
//
// A drag is declared at its start as a finite, bounded set of candidate
// edits. The client (DragClient) offers them through a DragPlanBuilder: each
// candidate is a landing (where the subject would lie under that edit) with
// the client's token for the edit; continuous drags are closed-form
// projections (project) and free placements (free). The builder's operator
// records form a small tree, compiled once; on every pointer event or key
// the engine evaluates that tree and hands the client a frame (its preview)
// and, at the end, an outcome. The engine never edits a model: only the
// client commits, once, for the `dropped` and `free` ends; every other end
// asks it to restore.
//
// Decisions in force (owner, 2026-11-08, section 9 of the study):
//  1. continuous drags use closed-form projections only; there is no
//     optimiser;
//  2. speculative layout, when phase E2 adds it, runs only in the nearest
//     container owning every affected element; phase E1 has analytic and
//     declared producers only;
//  3. chaining (several moves in one gesture) is admitted for the node
//     canvas and lists, never for docking: a docking client declares no
//     chained snap;
//  4. coinciding landings are resolved by declaration order (the earlier
//     wins) and counted in DragStatistics::ties;
//  5. caps: maximum_drag_candidates analytic or declared candidates and
//     maximum_drag_operators operators per plan, refused beyond, never
//     truncated;
//  6. OS drag and drop (SurfaceRoot::dispatch_drag) stays outside;
//  7. list reorder is off by default, with the insertion mark (client
//     properties; not the engine's);
// 10. the move key is Ctrl+Alt+Shift+M (is_drag_move_command), one-step
//     list moves Alt+Arrow (client keys).
//
// The session's states:
//
//   idle -arm-> armed -past the threshold-> planning -> dragging -> settling -> idle
//   idle -begin_keyboard-> planning -> dragging (stepping) -> settling -> idle
//
// planning is synchronous: the client declares its plan, refused candidates
// are removed, and, for a keyboard session, the candidates the pointer can
// reach are marked. A chained snap commits its candidate and re-enters
// planning with the next epoch. The ends are a closed sum (DragEnd). The
// session settles only when the plan asked for a drop motion and motion is
// not suppressed (reduced motion, an occluded window); the client then
// animates on its own MotionValue and calls finish_settling.
//
// Memory: the engine reserves its candidate, flag and trace storage at its
// first planning, at the caps, and never grows it; from the first dragging
// frame to the end nothing is allocated (DragStatistics::storage_growths
// stays zero, and the tests count allocations as well).
//
// Threads and surfaces: an engine belongs to one SurfaceRoot and is driven
// only from that root's dispatch (pointer, key, capture and activation
// hooks), on its thread. One session at a time. A threaded sub-surface's
// root has its own engine. Other surfaces contribute declared targets only
// (DragPlanBuilder::foreign_targets), translated into this root's
// coordinates by the client. foreign_targets returns an operator, not a
// range, so another surface's candidates cannot enter a nearest operator,
// and no layout runs across a thread.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class SurfaceRoot;
class DragEngine;
class DragPlanBuilder;

// -- limits ---------------------------------------------------------------------

inline constexpr std::size_t maximum_drag_operators = 32U;
inline constexpr std::size_t maximum_drag_candidates = 1024U;
inline constexpr std::size_t maximum_drag_groups = 16U;
// The trace ring's entries; older entries are overwritten.
inline constexpr std::size_t drag_trace_capacity = 256U;
// An announcement longer than this is cut at a character boundary.
inline constexpr std::size_t maximum_drag_announcement_bytes = 256U;
// Two distances within this (logical units) are a tie.
inline constexpr double drag_tie_tolerance = 1e-9;
// The keyboard step of project and free: Ctrl takes coarse steps, Shift fine.
inline constexpr double drag_coarse_factor = 10.0;
inline constexpr double drag_fine_factor = 0.1;

// -- identities -----------------------------------------------------------------

// The client's name for one candidate edit (an index into its own table).
struct DragCandidateToken final {
    std::uint32_t value{};
    friend bool operator==(DragCandidateToken left, DragCandidateToken right) noexcept {
        return left.value == right.value;
    }
};

// A group of candidates: one list, one pane, one edge. Tab steps groups;
// a group may name the container a rule's drop_candidate condition reads.
struct DragGroup final {
    std::uint16_t value{};
    friend bool operator==(DragGroup left, DragGroup right) noexcept { return left.value == right.value; }
};

// The plan's generation within one session: 1 at the first planning, one
// more at each chain point. Tokens of an older epoch are stale.
struct DragEpoch final {
    std::uint32_t value{};
    friend bool operator==(DragEpoch left, DragEpoch right) noexcept { return left.value == right.value; }
};

// An operator record's index in the plan. Children always precede their
// parent, so evaluation is acyclic and at most maximum_drag_operators deep.
struct DragNodeIndex final {
    std::uint8_t value{0xFFU};
    friend bool operator==(DragNodeIndex left, DragNodeIndex right) noexcept { return left.value == right.value; }
};
inline constexpr DragNodeIndex no_drag_node{};

// A contiguous run of the plan's landings, as add_landings or add_list
// stored it. `uniform` marks a list run whose anchors lie at a constant
// pitch along `axis` from `origin`: a nearest operator over it with that
// axis' metric finds its candidate in O(1).
enum class DragAxis : std::uint8_t { x, y };
struct DragCandidateRange final {
    std::uint32_t first{};
    std::uint32_t count{};
    bool uniform{};
    DragAxis axis{DragAxis::y};
    double origin{};
    double pitch{};
};

// -- vocabulary -----------------------------------------------------------------

enum class DragPhase : std::uint8_t { idle, armed, planning, dragging, settling };

// How a session ended. Only dropped (a candidate) and free (a projection's
// value or a free placement) commit; every other end restores.
enum class DragEnd : std::uint8_t {
    none,
    dropped,
    free,
    rejected,
    escaped,
    capture_lost,
    deactivated,
    model_changed,
    subject_gone,
};
inline constexpr std::size_t drag_end_count = 9U;

enum class DragInputMode : std::uint8_t { pointer, keyboard };

// The distance a nearest operator compares: from the grab-adjusted pointer
// to each landing's anchor (euclidean), to its rectangle (edge, zero inside),
// or along one axis only.
enum class DragMetric : std::uint8_t { euclidean, edge, axis_x, axis_y };

enum class DragChain : std::uint8_t { off, on };

// The closed operator vocabulary of phase E1. blend and spawn arrive with
// the speculative producer (E2) and the node canvas (E3).
enum class DragOperatorKind : std::uint8_t {
    none,
    // The candidate of a range whose landing is nearest under the metric,
    // keeping the current one while it is within `distance` (stickiness).
    nearest,
    // The first declared target of a range containing the pointer.
    target,
    // A closed-form projection of the pointer onto one parameter.
    project,
    // A free placement inside bounds, optionally on a grid.
    free,
    // `primary` while it selects within `distance` of the pointer, else
    // `secondary` (the paper's whenFar). A target selects at distance zero,
    // so fallback(target, x, 0) gives declared targets precedence.
    fallback,
    // `primary`'s candidate only within `distance` (the snap radius); with
    // `chain`, reaching a candidate other than the home one commits it and
    // plans again.
    snap,
    // Nothing while the modifiers `suppressed_by` are held (DK's Ctrl).
    suppress,
};

enum class DragSelectionKind : std::uint8_t { none, candidate, value, point };

// Why a plan was refused. The builder keeps the first refusal; later calls
// return no_drag_node and change nothing.
enum class DragPlanRefusal : std::uint8_t {
    none,
    too_many_operators,
    too_many_candidates,
    too_many_groups,
    // A child index that is no node or not an earlier node.
    bad_child,
    // A non-finite value, an inverted range, a negative distance, or a
    // candidate range the builder did not make.
    bad_value,
    // The plan selects nothing (no root).
    empty,
    // The client declined (it has nothing to drag now).
    client_refused,
};

// A landing: the rectangle the subject takes under the candidate's edit, in
// the root's coordinates, and its anchor (usually the centre).
struct DragLanding final {
    Rect rect{};
    Point anchor{};
    DragCandidateToken token{};
    DragGroup group{};
    // The subject's own place (no edit). A chained snap never commits it.
    bool home{};
};

// A closed-form projection: value = clamp(origin_value + scale * (pointer -
// press) along `axis`, minimum, maximum), then rounded to `step` above
// `minimum` when step is positive. The keyboard steps by keyboard_step in
// the direction the arrow moves the pointer.
struct DragProjection final {
    DragAxis axis{DragAxis::x};
    double origin_value{};
    // Value units per logical unit along the axis; negative when the value
    // grows as the pointer moves left or up.
    double scale{1.0};
    double minimum{};
    double maximum{};
    double step{};
    double keyboard_step{1.0};
    DragGroup group{};
};

// A free placement: the subject's top-left kept inside `bounds` (with the
// subject's size), on `grid` from bounds' origin when grid is not empty.
struct DragFreeArea final {
    Rect bounds{};
    Size grid{};
    DragGroup group{};
};

// A list or strip for the analytic producer: the items' rectangles in
// display order (the root's coordinates, along `axis`), the dragged item,
// and the token of "move the item to slot j", first_token + j. Moving item
// i to slot j shifts the run between them by the item's extent, so its
// landing starts where slot j starts (j <= i) or ends where slot j ends
// (j > i): O(n) for all n candidates, no layout.
struct DragListGeometry final {
    DragAxis axis{DragAxis::y};
    std::span<const Rect> slots{};
    std::size_t subject{};
    std::uint32_t first_token{};
    DragGroup group{};
};

// One operator record of a plan. The fields an operator kind does not read
// stay at their initial values.
//   range: nearest's and target's landings.
//   primary, secondary: fallback's children; snap's and suppress' child.
//   distance: nearest's stickiness, fallback's gap, snap's radius.
//   metric: nearest's metric.
//   chain: snap's chaining.
//   suppressed_by: suppress' modifiers.
//   projection: project's; free_area: free's.
//   foreign: target's landings came from another surface.
struct DragOperator final {
    DragOperatorKind kind{DragOperatorKind::none};
    DragCandidateRange range{};
    DragNodeIndex primary{};
    DragNodeIndex secondary{};
    double distance{};
    DragMetric metric{DragMetric::euclidean};
    DragChain chain{DragChain::off};
    Modifier suppressed_by{Modifier::none};
    DragProjection projection{};
    DragFreeArea free_area{};
    bool foreign{};
};

// What the plan selects for one pointer position or keyboard step.
//   landing, token: the candidate (candidate kind).
//   value: the projection's value (value kind).
//   point: the free placement's top-left (point kind).
//   group: the selected candidate's, projection's or area's group.
//   distance: the pointer's distance to the candidate under its metric.
//   decided_by: the operator that produced it.
struct DragSelection final {
    DragSelectionKind kind{DragSelectionKind::none};
    std::uint32_t landing{};
    DragCandidateToken token{};
    double value{};
    Point point{};
    DragGroup group{};
    double distance{};
    DragNodeIndex decided_by{};
    friend bool operator==(const DragSelection& left, const DragSelection& right) noexcept {
        return left.kind == right.kind && left.landing == right.landing && left.token == right.token &&
               left.value == right.value && left.point.x == right.point.x && left.point.y == right.point.y &&
               left.group == right.group;
    }
};

// What is dragged, as the client's plan_drag sees it. `control` is borrowed
// for that call only.
struct DragSubject final {
    Control* control{};
    std::uint64_t key{};
    // Where the drag was pressed (pointer) or the subject's centre
    // (keyboard), and the subject's rectangle then, in the root's
    // coordinates.
    Point press{};
    Rect source{};
    DragInputMode mode{DragInputMode::pointer};
    DragEpoch epoch{};
};

// One frame of a running session, handed to preview_drag and
// announce_drag. In a keyboard session `pointer` is where a pointer would
// be to make the same selection.
struct DragFrame final {
    DragPhase phase{DragPhase::idle};
    DragInputMode mode{DragInputMode::pointer};
    DragEpoch epoch{};
    Point pointer{};
    Modifier modifiers{Modifier::none};
    DragSelection selection{};
    // The selected candidate's landing rectangle; empty otherwise.
    Rect landing{};
    // At the end only: how the session ended.
    DragEnd end{DragEnd::none};
};

// How a session ended, handed to end_drag (and to commit_drag_chain at a
// chain point, with end dropped). `animate` is true when the session
// settles (a drop motion and motion not suppressed).
struct DragOutcome final {
    DragEnd end{DragEnd::none};
    DragInputMode mode{DragInputMode::pointer};
    DragEpoch epoch{};
    DragSelection selection{};
    Point pointer{};
    bool animate{};
};

// An announcement's text in fixed storage (no allocation while dragging).
class DragAnnouncement final {
public:
    // Replaces the text, cut to maximum_drag_announcement_bytes at a UTF-8
    // character boundary.
    void assign(std::string_view text) noexcept;
    void clear() noexcept { length_ = 0U; }
    [[nodiscard]] std::string_view view() const noexcept {
        const std::string_view text(bytes_.data(), length_);
        return text;
    }

private:
    std::array<char, maximum_drag_announcement_bytes> bytes_{};
    std::size_t length_{};
};

// The engine's counters, queryable (AGENTS.md: structured instrumentation).
//   storage_growths: times a reserved buffer had to grow after the first
//     planning; zero by construction (the tests' gate).
//   ends: by DragEnd, indexed by its value.
struct DragStatistics final {
    std::uint64_t sessions{};
    std::uint64_t plans{};
    std::uint64_t refused_plans{};
    std::uint64_t refused_candidates{};
    std::uint64_t evaluations{};
    std::uint64_t ties{};
    std::uint64_t chains{};
    std::uint64_t storage_growths{};
    std::array<std::uint64_t, drag_end_count> ends{};
};

enum class DragTraceEvent : std::uint8_t { arm, plan, move, key, chain, end };

// One trace entry, written on every step (DragEngine::trace).
struct DragTraceEntry final {
    std::uint64_t sequence{};
    DragTraceEvent event{DragTraceEvent::arm};
    DragPhase phase{DragPhase::idle};
    DragInputMode mode{DragInputMode::pointer};
    DragEpoch epoch{};
    Point pointer{};
    DragSelection selection{};
    Rect landing{};
    DragEnd end{DragEnd::none};
    std::uint64_t ties{};
};

// The text of one trace entry, appended to `out` with a newline: the event,
// phase, mode, epoch, pointer, selection, landing and end, coordinates
// rounded to 1/64 unit. Allocates; for tests and diagnostics, never called
// while dragging.
void append_drag_trace_line(const DragTraceEntry& entry, std::string& out);
[[nodiscard]] std::string_view drag_end_name(DragEnd end) noexcept;
[[nodiscard]] std::string_view drag_phase_name(DragPhase phase) noexcept;

// Whether `event` is the engine-wide move command (Ctrl+Alt+Shift+M, DK's;
// decision 10).
[[nodiscard]] bool is_drag_move_command(const KeyEvent& event) noexcept;

// -- the client -------------------------------------------------------------------

// A control that declares drags. Virtual dispatch is admitted here: the
// clients are different controls (house style section 7). Every call is
// made on the root's thread from the engine's dispatch; a call may end the
// session (the engine checks after each one) but must not start another.
class DragClient {
public:
    virtual ~DragClient() = default;
    // Declares the plan through `builder` and finishes it (builder.finish),
    // returning its result; client_refused when there is nothing to drag.
    [[nodiscard]] virtual DragPlanRefusal plan_drag(DragPlanBuilder& builder, const DragSubject& subject) = 0;
    // Whether a declared candidate is admitted (the study's refuse); asked
    // once per candidate at planning.
    [[nodiscard]] virtual bool accepts_drag_candidate(const DragLanding& landing);
    // Shows the frame: after each change of selection, and on every pointer
    // move while a value or a point is selected.
    virtual void preview_drag(const DragFrame& frame) = 0;
    // Writes the announcement for a frame whose selection changed, and for
    // the end. Nothing by default.
    virtual void announce_drag(const DragFrame& frame, DragAnnouncement& announcement);
    // Maps a committing outcome before end_drag (the study's rewrite).
    virtual void rewrite_drag_outcome(DragOutcome& outcome);
    // Commits a chained candidate; planning follows at once.
    virtual void commit_drag_chain(const DragOutcome& outcome);
    // Commits (dropped, free) or restores (every other end).
    virtual void end_drag(const DragOutcome& outcome) = 0;
};

// -- the plan builder -------------------------------------------------------------

// Writes a plan's landings and operator records into the engine's reserved
// storage during planning. Made only by the engine; valid for one plan_drag
// call.
class DragPlanBuilder final {
public:
    DragPlanBuilder(const DragPlanBuilder&) = delete;
    DragPlanBuilder& operator=(const DragPlanBuilder&) = delete;

    // Declared or client-computed landings, in declaration order.
    DragCandidateRange add_landings(std::span<const DragLanding> landings);
    // The analytic list producer (DragListGeometry).
    DragCandidateRange add_list(const DragListGeometry& list);
    DragNodeIndex nearest(const DragCandidateRange& range, DragMetric metric, double stickiness);
    DragNodeIndex target(const DragCandidateRange& range);
    // Declared targets another surface offered, in this root's coordinates.
    DragNodeIndex foreign_targets(std::span<const DragLanding> landings);
    DragNodeIndex project(const DragProjection& projection);
    DragNodeIndex free(const DragFreeArea& area);
    DragNodeIndex fallback(DragNodeIndex primary, DragNodeIndex secondary, double gap);
    DragNodeIndex snap(DragNodeIndex child, double radius, DragChain chain);
    DragNodeIndex suppress(DragNodeIndex child, Modifier suppressed_by);
    // Names the container of a group, for the drop_candidate rule condition.
    void group_container(DragGroup group, Control& container);
    // Asks for a settling phase at the end (the client animates it).
    void drop_motion(bool settles) noexcept;
    // Ends the declaration with `root`; the first refusal, if any.
    [[nodiscard]] DragPlanRefusal finish(DragNodeIndex root);

    [[nodiscard]] DragPlanRefusal refusal() const noexcept { return refusal_; }

private:
    friend class DragEngine;
    explicit DragPlanBuilder(DragEngine& engine) noexcept : engine_(&engine) {}

    [[nodiscard]] bool admit_operator() noexcept;
    [[nodiscard]] bool admit_range(const DragCandidateRange& range) noexcept;
    DragCandidateRange store(std::span<const DragLanding> landings);
    [[nodiscard]] bool valid_child(DragNodeIndex child) const noexcept;
    [[nodiscard]] DragNodeIndex append(const DragOperator& record);
    void refuse(DragPlanRefusal refusal) noexcept;

    DragEngine* engine_;
    DragPlanRefusal refusal_{DragPlanRefusal::none};
};

// -- the engine -------------------------------------------------------------------

class DragEngine final {
public:
    explicit DragEngine(SurfaceRoot& root) noexcept;
    DragEngine(const DragEngine&) = delete;
    DragEngine& operator=(const DragEngine&) = delete;

    // A pointer press on `subject` arms a session for `client`, whose
    // control `owner` is observed (its disposal ends the session without
    // calling the client). The engine takes the pointer capture for
    // `subject`. With a threshold of zero or less, planning runs at once;
    // otherwise once the pointer has moved `threshold` on either axis
    // (drag_gesture's rule). False, changing nothing, while a session runs
    // or when the subject is not attached to this root.
    bool arm(DragClient& client, Control& owner, Control& subject, std::uint64_t key, Point press, Rect source,
             double threshold);
    // A pointer move or release while armed or dragging; true when the
    // engine took it. Other actions are left to the caller.
    bool pointer(const PointerEvent& event);
    // Enters the keyboard mode on `subject` (the move command, or the
    // begin_keyboard_drag rule action). False while a session runs, or when
    // the plan is refused.
    bool begin_keyboard(DragClient& client, Control& owner, Control& subject, std::uint64_t key);
    // A key while a session runs: Escape cancels; in the keyboard mode the
    // arrows step (Ctrl coarse, Shift fine for values and points), Tab and
    // Shift+Tab step groups, Enter commits. True when the engine took it;
    // other keys go on to the window's rules and controls.
    bool key(const KeyEvent& event);
    // Ends a running session from outside: escaped, deactivated or
    // model_changed (the client's own edit during the drag). Other ends are
    // ignored.
    void cancel(DragEnd end);
    // The window's capture moved away from `control` (SurfaceRoot's hook).
    void note_capture_lost(const Control& control);
    // The client is going away: the session ends subject_gone without
    // calling it. A no-op for another client.
    void client_gone(const DragClient& client) noexcept;
    // The client's settling animation finished.
    void finish_settling() noexcept;

    [[nodiscard]] DragPhase phase() const noexcept { return phase_; }
    [[nodiscard]] DragInputMode mode() const noexcept { return mode_; }
    [[nodiscard]] DragEpoch epoch() const noexcept { return epoch_; }
    [[nodiscard]] const DragSelection& selection() const noexcept { return selection_; }
    [[nodiscard]] std::string_view announcement() const noexcept { return announcement_.view(); }
    [[nodiscard]] const DragStatistics& statistics() const noexcept { return statistics_; }
    // Whether `client` runs the current session (any phase but idle).
    [[nodiscard]] bool runs(const DragClient& client) const noexcept;
    // Whether a session is dragging (or planning, or settling) `control` or
    // one of its descendants: the rules' dragging condition.
    [[nodiscard]] bool drags(const Control& control) const noexcept;
    // Whether `control` is the container of the selected candidate's group:
    // the rules' drop_candidate condition.
    [[nodiscard]] bool drop_candidate(const Control& control) const noexcept;

    // The current plan's landings (after refusals) and operators.
    [[nodiscard]] std::span<const DragLanding> landings() const noexcept;
    [[nodiscard]] std::span<const DragOperator> operators() const noexcept;
    // Whether landing `index` is reachable by the keyboard (marked in a
    // keyboard session's planning, as the pointer reaches it at its anchor).
    [[nodiscard]] bool keyboard_reachable(std::size_t index) const noexcept;
    // What the plan selects with the pointer at `pointer` and `modifiers`,
    // without stickiness; the session is not changed. For tests and the
    // drop-zone map.
    [[nodiscard]] DragSelection probe(Point pointer, Modifier modifiers) const noexcept;
    // The drop-zone map (the study's visualiser): for each cell of `cell`
    // size over `area`, row by row, the selected landing's index, or -1.
    // `out` must hold columns times rows entries (false, writing nothing,
    // otherwise).
    bool sample_drop_zones(Rect area, Size cell, std::span<std::int32_t> out) const noexcept;
    // The trace, oldest first (at most drag_trace_capacity entries), copied
    // into `out` (cleared first). Allocates; never called while dragging.
    void copy_trace(std::vector<DragTraceEntry>& out) const;
    // Empties the trace ring (keeps its storage).
    void clear_trace() noexcept;

private:
    friend class DragPlanBuilder;

    // An evaluation's selection; `tied` when coinciding landings were
    // resolved by declaration order, `chain` when a chained snap engaged a
    // candidate other than the home one.
    struct Evaluation final {
        DragSelection selection{};
        bool tied{};
        bool chain{};
    };

    void reserve_storage();
    void note_growth(std::size_t landings_capacity, std::size_t flags_capacity, std::size_t trace_capacity) noexcept;
    [[nodiscard]] bool start_session(DragClient& client, Control& owner, Control& subject, std::uint64_t key,
                                     DragInputMode mode, Point press, Rect source);
    // Runs planning for the current session; false when it ended (refused).
    bool plan();
    void compact_refused();
    void mark_keyboard_reachable();
    void choose_keyboard_start();
    // Evaluates the plan at `pointer` and hands the client the frame.
    void drag_to(Point pointer, Modifier modifiers, DragTraceEvent event);
    [[nodiscard]] Evaluation evaluate(DragNodeIndex node, Point pointer, Modifier modifiers, bool sticky) const noexcept;
    [[nodiscard]] Evaluation evaluate_nearest(const DragOperator& record, Point adjusted, bool sticky) const noexcept;
    [[nodiscard]] Evaluation evaluate_target(const DragOperator& record, Point pointer) const noexcept;
    [[nodiscard]] DragSelection evaluate_project(const DragOperator& record, Point pointer) const noexcept;
    [[nodiscard]] DragSelection evaluate_free(const DragOperator& record, Point pointer) const noexcept;
    [[nodiscard]] double distance_to(const DragLanding& landing, Point adjusted, DragMetric metric) const noexcept;
    [[nodiscard]] Point adjusted(Point pointer) const noexcept;
    // The pointer that selects `selection` (the keyboard's virtual pointer).
    [[nodiscard]] Point pointer_for(const DragSelection& selection) const noexcept;
    // The first operator of `kind` the root reaches, or none.
    [[nodiscard]] DragNodeIndex find_operator(DragOperatorKind kind) const noexcept;
    bool step_spatial(double dx, double dy);
    bool step_group(bool forward);
    bool step_continuous(double dx, double dy, Modifier modifiers);
    void select(const DragSelection& selection, DragTraceEvent event);
    void announce(const DragFrame& frame);
    [[nodiscard]] DragFrame frame_now() const noexcept;
    // Ends the session with `end`, calling the client (unless it is gone).
    void end(DragEnd end);
    void reset_session() noexcept;
    // Leaves the armed state without a session end (no client call).
    void disarm();
    void release_capture();
    // Whether the session that was current when `serial` was read is still
    // running (a client call may have ended it).
    [[nodiscard]] bool still(std::uint64_t serial) const noexcept;
    [[nodiscard]] bool client_alive() const noexcept;
    void record(DragTraceEvent event, DragEnd ending = DragEnd::none) noexcept;

    SurfaceRoot* root_;
    DragPhase phase_{DragPhase::idle};
    DragInputMode mode_{DragInputMode::pointer};
    DragEpoch epoch_{};
    std::uint64_t serial_{};
    // The session's client (borrowed; valid while owner_ lives) and the
    // observed control that owns it, and the subject.
    DragClient* client_{};
    Control::WeakPtr owner_{};
    Control::WeakPtr subject_{};
    std::uint64_t key_{};
    Point press_{};
    Rect source_{};
    double threshold_{};
    Point pointer_{};
    Modifier modifiers_{Modifier::none};
    DragSelection selection_{};
    // Set while end() runs, so that its own capture release is not a loss.
    bool ending_{};
    bool settles_{};

    // The plan: operator records (live count operator_count_), the root,
    // the landings (capacity maximum_drag_candidates, reserved once), the
    // keyboard flags (one per landing) and the group containers.
    std::array<DragOperator, maximum_drag_operators> operators_{};
    std::size_t operator_count_{};
    DragNodeIndex root_node_{};
    std::vector<DragLanding> landings_{};
    std::vector<std::uint8_t> reachable_{};
    std::array<Control::WeakPtr, maximum_drag_groups> containers_{};
    // The ranges add_landings and add_list made, in order (disjoint and
    // ascending); nearest and target accept only these.
    std::array<DragCandidateRange, maximum_drag_operators> added_{};
    std::size_t added_count_{};

    DragAnnouncement announcement_{};
    DragStatistics statistics_{};
    // The trace ring: capacity drag_trace_capacity, reserved once; the next
    // entry goes to trace_next_ once it is full.
    std::vector<DragTraceEntry> trace_{};
    std::size_t trace_next_{};
    std::uint64_t trace_sequence_{};
    bool reserved_{};
};

} // namespace gui_forms
