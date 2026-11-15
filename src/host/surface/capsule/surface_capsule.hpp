#pragma once
// The window capsule (owner, 2026-11-15): a framed window's controls as
// one capsule in three sections, worked by gestures. This is its gesture
// model, plain values in and out, so every rule is tested headless on every
// platform (tests/surface_capsule_tests.cpp). The desk places the capsule
// (SurfaceDesk::box with the capsule parts), the manager feeds it pointer
// and key input and carries out its commands on the desk, and FrameChrome
// draws it from theme parts.
//
// - Left half: a double click maximises (or restores); dragged down at
//   least minimize_distance and released, it minimises.
// - Middle, a question mark: a click opens the help (the capsule's own
//   help, the business card and the diagnostic overlay, behind the
//   manager's help call point); hovering it shows the window's title.
// - Right half: dragged past the slop, it moves the window live; released
//   within snap_edge of the surface's left or right edge the window takes
//   that half of the surface, elsewhere it floats where it was dropped.
//   An armed snap holds until the pointer is snap_release beyond the edge
//   zone (the magnet hysteresis of DragTracker's placements, which cannot
//   drive this move: the window moves under the pointer, so the drag's
//   window-local position never changes).
// - Closing takes three clicks: a click on one half arms it (its half
//   turns into half an X, the other half lights green), a click on the
//   other half completes the X (the capsule becomes one X and turns red),
//   and a click anywhere on that X closes the window. Leaving the capsule,
//   Escape, or the window losing the capsule disarms it.
//
// Keyboard (every action has a path; the manager focuses the capsule of
// the active window with Alt+Space): Left and Right choose a section,
// Enter or Space click it (arming moves the focus to the other half, so
// Enter three times closes), Up maximises or restores, Down minimises,
// Shift+Left and Shift+Right tile to a half, Shift+Down floats, Control
// with an arrow moves the window by `nudge`, F1 opens the help, and Escape
// disarms, then leaves the capsule. Any other key leaves the capsule and
// goes to the window.
//
// Motion: the arming colour and the minimise drag's spring-back animate
// over animation_milliseconds; with reduced motion every value takes its
// target at once and nothing asks for frames.
//
// One execution context: the manager's thread.

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <string_view>

namespace gui_forms::host::surface_detail {

enum class SurfaceCapsuleSection : std::uint8_t {
    none,
    left,
    help,
    right,
};

// How far the close gesture has gone: one half armed, or both (the X).
enum class SurfaceCapsuleArm : std::uint8_t {
    idle,
    left,
    right,
    close,
};

// Where a move puts the window when it ends.
enum class SurfaceCapsuleSnap : std::uint8_t {
    // It floats where it was dropped (or, from a tiled or maximised state,
    // as a normal window).
    none,
    left_half,
    right_half,
};

// What the manager does for an input. One command per input at most.
enum class SurfaceCapsuleCommand : std::uint8_t {
    none,
    toggle_maximize,
    minimize,
    // The right half's drag passed the slop: a live move of the window
    // starts at `origin` (the press) and goes to `point`.
    begin_move,
    // The live move goes on to `point`.
    move,
    // The pointer was released: the move ends with `snap`.
    finish_move,
    // The pointer was cancelled: the move ends where it is, floating.
    cancel_move,
    // The keyboard tiles the window to `snap` (none floats it).
    tile,
    // The keyboard moves the window by `offset`.
    nudge,
    open_help,
    close,
};

struct SurfaceCapsuleOutcome final {
    SurfaceCapsuleCommand command{SurfaceCapsuleCommand::none};
    HostWindowId window{};
    Point origin{};
    Point point{};
    Point offset{};
    SurfaceCapsuleSnap snap{SurfaceCapsuleSnap::none};
    // The input was the capsule's (a key it consumed, a press on it).
    bool handled{};
    // The capsule's look changed: its strip paints again.
    bool redraw{};
};

// The gesture measures, logical units and milliseconds (from the desk
// theme, SurfaceFrameTheme).
struct SurfaceCapsuleMeasures final {
    // Movement within this distance of the press is still a click.
    double slop{4.0};
    double minimize_distance{40.0};
    double snap_edge{16.0};
    double snap_release{48.0};
    double animation_milliseconds{160.0};
    double nudge{16.0};
};

class SurfaceCapsuleGesture final {
public:
    // Non-finite or negative measures fall back to the defaults.
    void set_measures(const SurfaceCapsuleMeasures& measures) noexcept;
    [[nodiscard]] const SurfaceCapsuleMeasures& measures() const noexcept { return measures_; }

    // Pointer input, surface logical units. press is called for a primary
    // press on `section` of `window`'s capsule with the press's click count;
    // drag for every move while that press lasts (`extent` the surface's
    // size); release with the section under the pointer (none off the
    // capsule); cancel when the pointer is lost.
    SurfaceCapsuleOutcome press(HostWindowId window, SurfaceCapsuleSection section, Point point,
                                std::uint32_t clicks) noexcept;
    SurfaceCapsuleOutcome drag(Point point, Size extent) noexcept;
    SurfaceCapsuleOutcome release(SurfaceCapsuleSection section) noexcept;
    SurfaceCapsuleOutcome cancel() noexcept;
    // The pointer is over `section` of `window`'s capsule, or off every
    // capsule (none). Leaving the capsule without a press disarms it.
    SurfaceCapsuleOutcome hover(HostWindowId window, SurfaceCapsuleSection section) noexcept;

    // Keyboard: focus gives `window`'s capsule the keyboard at its left
    // half; key is called for each key press while it has it.
    SurfaceCapsuleOutcome focus(HostWindowId window) noexcept;
    SurfaceCapsuleOutcome key(std::uint32_t physical_key, Modifier modifiers) noexcept;
    [[nodiscard]] bool keyboard() const noexcept { return keyboard_; }

    // `window` closed, was minimised or lost the keyboard to another
    // window: a capsule of it forgets every gesture. Returns whether the
    // look changed.
    bool forget(HostWindowId window) noexcept;

    // Moves the animated values toward their targets by `milliseconds`;
    // with reduced motion they take them at once. Returns whether the look
    // changed.
    bool advance(double milliseconds, bool reduced_motion) noexcept;
    [[nodiscard]] bool animating() const noexcept;

    // The look. `window` is the capsule's window while any gesture, hover,
    // focus or animation concerns it.
    [[nodiscard]] HostWindowId window() const noexcept { return window_; }
    [[nodiscard]] SurfaceCapsuleArm arm() const noexcept { return arm_; }
    // The close intention as shown: 0 idle, 0.5 one half armed (the other
    // half green), 1 the X (red).
    [[nodiscard]] double intent() const noexcept { return intent_shown_; }
    // The left half's downward drag as shown, 0 to minimize_distance.
    [[nodiscard]] double minimize_offset() const noexcept { return offset_shown_; }
    [[nodiscard]] SurfaceCapsuleSnap snap() const noexcept { return snap_; }
    [[nodiscard]] bool moving() const noexcept { return mode_ == Mode::moving; }
    [[nodiscard]] bool pressing() const noexcept { return mode_ != Mode::idle; }
    [[nodiscard]] SurfaceCapsuleSection hovered() const noexcept { return hovered_; }
    [[nodiscard]] SurfaceCapsuleSection pressed() const noexcept { return pressed_; }
    [[nodiscard]] SurfaceCapsuleSection focused_section() const noexcept { return focused_; }
    // Hovering the help section shows the window's title.
    [[nodiscard]] bool title_shown() const noexcept;

private:
    enum class Mode : std::uint8_t {
        idle,
        // A press not yet past the slop.
        pressed,
        // The left half dragged downward.
        minimizing,
        // The right half moving the window.
        moving,
        // Dragged off in another direction: no click, no command.
        abandoned,
    };

    [[nodiscard]] double intent_target() const noexcept;
    // A click (press and release on the same section, or Enter) on
    // `section`; fills `outcome`.
    void click(SurfaceCapsuleSection section, SurfaceCapsuleOutcome& outcome) noexcept;
    void set_arm(SurfaceCapsuleArm arm, SurfaceCapsuleOutcome& outcome) noexcept;
    [[nodiscard]] SurfaceCapsuleSnap next_snap(Point point, Size extent) const noexcept;
    void reset() noexcept;

    SurfaceCapsuleMeasures measures_{};
    HostWindowId window_{};
    SurfaceCapsuleArm arm_{SurfaceCapsuleArm::idle};
    Mode mode_{Mode::idle};
    SurfaceCapsuleSection pressed_{SurfaceCapsuleSection::none};
    SurfaceCapsuleSection hovered_{SurfaceCapsuleSection::none};
    SurfaceCapsuleSection focused_{SurfaceCapsuleSection::none};
    bool keyboard_{};
    Point origin_{};
    SurfaceCapsuleSnap snap_{SurfaceCapsuleSnap::none};
    // The minimise drag's live offset, and the shown one (equal while
    // dragging; springs back to 0 after).
    double offset_live_{};
    double offset_shown_{};
    double intent_shown_{};
};

// The call point of the capsule's help (WINDOW_MANAGER_SPEC: the business
// card and the diagnostic overlay, unified across GUI.Forms programs, come
// from a separate task and attach here). `open` is called on the manager's
// thread with `context` and the window whose capsule asked; `context` must
// outlive the run or be cleared first. Without one the manager shows the
// capsule's own help (surface_capsule_help_text) in a message box over the
// window.
struct SurfaceCapsuleHelpTarget final {
    void* context{};
    void (*open)(void* context, HostWindowId window){};
};

// The capsule's own help: how to use it, in plain words.
[[nodiscard]] std::string_view surface_capsule_help_text() noexcept;

} // namespace gui_forms::host::surface_detail
