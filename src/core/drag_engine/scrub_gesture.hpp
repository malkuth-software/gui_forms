#pragma once

// The scrub gesture (the node engine and value bar study, sections 3.2 and
// 3.4; the declarative dragging study's step M2): press on a value and drag
// sideways to change it, a client of the window's drag engine. Shared by
// ValueBar and NumericUpDown's scrub mode; internal, not public API.
//
// The gesture arms the engine on a press with a threshold of
// scrub_threshold logical units. A release before it is a click
// (ScrubPointerResult::clicked), which the owner turns into its own
// action (a ValueBar opens its editor). Past it the engine plans one
// projection along x from the start value, clamped to the scrub range, and
// the gesture previews each frame:
//   value = clamp(value + travel * rate * (Shift ? scrub_fine_factor : 1),
//                 low, high)
// accumulated from move to move, so Shift slows the scrub from where it is
// pressed, without a jump; with Ctrl the shown value is that value snapped
// to the range's snap step from its minimum (the accumulated value is
// kept, so releasing Ctrl resumes it). The range is the owner's soft range
// widened to hold the start value, so a value typed outside the soft range
// does not jump into it when scrubbed. The release commits the last value
// (ScrubEnd::committed, once); Escape, a right press, a lost capture or the
// window's deactivation restore the start value (ScrubEnd::restored). In
// the keyboard mode (begin_keyboard) the engine steps the projection: the
// arrows by the keyboard step, Ctrl coarse, Shift fine; Enter commits,
// Escape restores.
//
// Pointer lock (study 3.4; owner question 12) is the host's capability,
// reported per session by the owner (ScrubPointerLock): with relative
// motion the pointer is hidden while scrubbing (ScrubState::pointer_hidden,
// for the host) and the owner says where it reappears; clamped, the
// pointer stops at the window's edge, the rate is unchanged and
// ScrubState::at_edge is set while it is there, so the owner can show the
// edge state. Both are recorded host behaviour, not core policy.
//
// Threads and memory: the owner's root's thread, from its dispatch; one
// session at a time per window (the engine's rule). Nothing is allocated
// from the first scrub frame to the end, the target's own work aside.

#include "drag_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {

// The press travel past which a press scrubs (study 3.2: 4 px).
inline constexpr double scrub_threshold = 4.0;
// Shift's rate while scrubbing (study 3.4: x0.1).
inline constexpr double scrub_fine_factor = 0.1;

// One scrub's terms, from the owner when it starts.
//   start: the value when pressed.
//   minimum, maximum: the soft range (the gesture widens it to the start).
//   rate: value units per logical unit of travel; positive and finite.
//   snap_step: Ctrl's grid from minimum; zero or less snaps nothing.
//   keyboard_step: the keyboard mode's arrow step; positive.
//   reversed: travel to the left raises the value, for a target that reads
//     right to left (Control::right_to_left).
struct ScrubRange final {
    double start{};
    double minimum{};
    double maximum{1.0};
    double rate{0.01};
    double snap_step{};
    double keyboard_step{1.0};
    bool reversed{};
};

// The host's pointer lock for this scrub (HostCapability
// relative_pointer_motion reported, or not).
enum class ScrubPointerLock : std::uint8_t { clamped, relative };

enum class ScrubEnd : std::uint8_t { committed, restored };

// What a pointer event did: nothing for the gesture, taken by it, or the
// release of a press that never passed the threshold.
enum class ScrubPointerResult : std::uint8_t { ignored, taken, clicked };

// The gesture's state, for the owner's paint and the host's pointer.
//   scrubbing: a session past its threshold (or in the keyboard mode).
//   pointer_hidden: relative pointer lock while scrubbing by pointer.
//   at_edge: clamped pointer lock with the pointer on the window's edge.
//   value: the value shown now.
struct ScrubState final {
    bool scrubbing{};
    bool pointer_hidden{};
    bool at_edge{};
    double value{};
};

// The gesture's counters (AGENTS.md: structured instrumentation).
struct ScrubStatistics final {
    std::uint64_t presses{};
    std::uint64_t clicks{};
    std::uint64_t scrubs{};
    std::uint64_t previews{};
    std::uint64_t commits{};
    std::uint64_t restores{};
    std::uint64_t edge_frames{};
};

// The owner's side of a scrub: a named target (house style 8). Every call
// is on the root's thread from the engine's dispatch; a call may end the
// session but must not start another.
class ScrubTarget {
public:
    virtual ~ScrubTarget() = default;
    // Fills `range` for a scrub starting now; false when there is nothing to
    // scrub (the session is refused and nothing else is called).
    [[nodiscard]] virtual bool begin_scrub(ScrubRange& range) = 0;
    // Shows `value` live, on every change while scrubbing.
    virtual void preview_scrub(double value) = 0;
    // The end: `committed` with the final value, or `restored` with the
    // start value. Called once per scrub that began.
    virtual void end_scrub(ScrubEnd end, double value) = 0;
    // Writes `value` as an announcement's text into `out` and returns its
    // length (at most out.size()); must not allocate. The default writes
    // the number in its shortest form.
    [[nodiscard]] virtual std::size_t describe_scrub(double value, std::span<char> out) const;
};

class ScrubGesture final : public DragClient {
public:
    // `owner` is the control pressed and captured (observed by the engine);
    // `target` is borrowed and must live as long as the gesture (the owner
    // owns both).
    ScrubGesture(Control& owner, ScrubTarget& target) noexcept : owner_(&owner), target_(&target) {}
    ScrubGesture(const ScrubGesture&) = delete;
    ScrubGesture& operator=(const ScrubGesture&) = delete;

    // A primary press at `event.position` (window coordinates): arms the
    // owner's window's engine. False, changing nothing, when no window, a
    // session already runs, or the event is not a primary press.
    bool press(const PointerEvent& event, ScrubPointerLock lock);
    // A move, a release or a right press while this gesture's session runs.
    ScrubPointerResult pointer(const PointerEvent& event);
    // A key while this gesture's session runs (Escape, and the keyboard
    // mode's keys); true when taken.
    bool key(const KeyEvent& event);
    // Enters the keyboard mode; false when a session runs or the target
    // refuses.
    bool begin_keyboard();
    // Whether this gesture's session runs (armed, scrubbing or settling).
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const ScrubState& state() const noexcept { return state_; }
    [[nodiscard]] const ScrubStatistics& statistics() const noexcept { return statistics_; }

    // DragClient.
    [[nodiscard]] DragPlanRefusal plan_drag(DragPlanBuilder& builder, const DragSubject& subject) override;
    void preview_drag(const DragFrame& frame) override;
    void announce_drag(const DragFrame& frame, DragAnnouncement& announcement) override;
    void end_drag(const DragOutcome& outcome) override;

private:
    // The window's engine, or null without a window.
    [[nodiscard]] DragEngine* engine() const noexcept;
    // Accumulates the travel from the last pointer to `pointer` under
    // `modifiers` and returns the value shown.
    double advance_to(Point pointer, Modifier modifiers) noexcept;
    [[nodiscard]] double snapped(double value) const noexcept;
    [[nodiscard]] bool on_edge(Point pointer) const noexcept;

    Control* owner_;
    ScrubTarget* target_;
    ScrubPointerLock lock_{ScrubPointerLock::clamped};
    ScrubRange range_{};
    // The scrub range after widening to the start value.
    double low_{};
    double high_{};
    // The accumulated (unsnapped) value, the last pointer and modifiers.
    double raw_{};
    Point last_pointer_{};
    Modifier modifiers_{Modifier::none};
    // Whether begin_scrub accepted the current session (so end_scrub is
    // owed).
    bool begun_{};
    ScrubState state_{};
    ScrubStatistics statistics_{};
};

} // namespace gui_forms
