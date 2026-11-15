#pragma once
// The window capsule (owner, 2026-11-15): a window's controls as one
// capsule in three sections, worked by gestures.
//
// - WindowCapsuleGesture is its gesture model, plain values in and out, so
//   every rule is tested headless on every platform
//   (tests/window_capsule_tests.cpp).
// - WindowCapsule is the control that draws it from its theme parts
//   (`window-capsule::part(left_half)` and the rest).
//   - The surface window manager shows one in every framed window's title
//     bar. The manager runs the gesture in surface coordinates and gives
//     the control its look.
//   - A program without a title bar bakes one into a corner of its own
//     content. The control then runs the gesture on its own pointer and key
//     input and raises `requested` for each command; the program carries
//     the command out on its window (ApplicationWindowHandle).
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
// Keyboard (every action has a path; the surface window manager focuses the
// capsule of the active window with Alt+Space, and a WindowCapsule in a
// program's content takes the keyboard when focused): Left and Right choose a section,
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

//
// One execution context: the UI thread (the manager's thread on a surface
// host).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

enum class WindowCapsuleSection : std::uint8_t {
    none,
    left,
    help,
    right,
};

// How far the close gesture has gone: one half armed, or both (the X).
enum class WindowCapsuleArm : std::uint8_t {
    idle,
    left,
    right,
    close,
};

// Where a move puts the window when it ends.
enum class WindowCapsuleSnap : std::uint8_t {
    // It floats where it was dropped (or, from a tiled or maximised state,
    // as a normal window).
    none,
    left_half,
    right_half,
};

// What the manager does for an input. One command per input at most.
enum class WindowCapsuleCommand : std::uint8_t {
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

struct GUI_FORMS_API WindowCapsuleOutcome final {
    WindowCapsuleCommand command{WindowCapsuleCommand::none};
    HostWindowId window{};
    Point origin{};
    Point point{};
    Point offset{};
    WindowCapsuleSnap snap{WindowCapsuleSnap::none};
    // The input was the capsule's (a key it consumed, a press on it).
    bool handled{};
    // The capsule's look changed: its strip paints again.
    bool redraw{};
};

// The gesture measures, logical units and milliseconds (from the desk
// theme, SurfaceFrameTheme).
struct GUI_FORMS_API WindowCapsuleMeasures final {
    // Movement within this distance of the press is still a click.
    double slop{4.0};
    double minimize_distance{40.0};
    double snap_edge{16.0};
    double snap_release{48.0};
    double animation_milliseconds{160.0};
    double nudge{16.0};
};

class GUI_FORMS_API WindowCapsuleGesture final {
public:
    // Non-finite or negative measures fall back to the defaults.
    void set_measures(const WindowCapsuleMeasures& measures) noexcept;
    [[nodiscard]] const WindowCapsuleMeasures& measures() const noexcept { return measures_; }

    // Pointer input, in one coordinate space for a whole press (the
    // surface's logical units in the window manager). press is called for a
    // primary press on `section` of `window`'s capsule with the press's
    // click count; drag for every move while that press lasts, `extent` the
    // surface's size, or an empty size where there are no snap targets (a
    // capsule in a program's content: the program decides where its window
    // goes); release with the section under the pointer (none off the
    // capsule); cancel when the pointer is lost.
    WindowCapsuleOutcome press(HostWindowId window, WindowCapsuleSection section, Point point,
                                std::uint32_t clicks) noexcept;
    WindowCapsuleOutcome drag(Point point, Size extent) noexcept;
    WindowCapsuleOutcome release(WindowCapsuleSection section) noexcept;
    WindowCapsuleOutcome cancel() noexcept;
    // The pointer is over `section` of `window`'s capsule, or off every
    // capsule (none). Leaving the capsule without a press disarms it.
    WindowCapsuleOutcome hover(HostWindowId window, WindowCapsuleSection section) noexcept;

    // Keyboard: focus gives `window`'s capsule the keyboard at its left
    // half; key is called for each key press while it has it.
    WindowCapsuleOutcome focus(HostWindowId window) noexcept;
    WindowCapsuleOutcome key(std::uint32_t physical_key, Modifier modifiers) noexcept;
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
    [[nodiscard]] WindowCapsuleArm arm() const noexcept { return arm_; }
    // The close intention as shown: 0 idle, 0.5 one half armed (the other
    // half green), 1 the X (red).
    [[nodiscard]] double intent() const noexcept { return intent_shown_; }
    // The left half's downward drag as shown, 0 to minimize_distance.
    [[nodiscard]] double minimize_offset() const noexcept { return offset_shown_; }
    [[nodiscard]] WindowCapsuleSnap snap() const noexcept { return snap_; }
    [[nodiscard]] bool moving() const noexcept { return mode_ == Mode::moving; }
    [[nodiscard]] bool pressing() const noexcept { return mode_ != Mode::idle; }
    [[nodiscard]] WindowCapsuleSection hovered() const noexcept { return hovered_; }
    [[nodiscard]] WindowCapsuleSection pressed() const noexcept { return pressed_; }
    [[nodiscard]] WindowCapsuleSection focused_section() const noexcept { return focused_; }
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
    void click(WindowCapsuleSection section, WindowCapsuleOutcome& outcome) noexcept;
    void set_arm(WindowCapsuleArm arm, WindowCapsuleOutcome& outcome) noexcept;
    [[nodiscard]] WindowCapsuleSnap next_snap(Point point, Size extent) const noexcept;
    void reset() noexcept;

    WindowCapsuleMeasures measures_{};
    HostWindowId window_{};
    WindowCapsuleArm arm_{WindowCapsuleArm::idle};
    Mode mode_{Mode::idle};
    WindowCapsuleSection pressed_{WindowCapsuleSection::none};
    WindowCapsuleSection hovered_{WindowCapsuleSection::none};
    WindowCapsuleSection focused_{WindowCapsuleSection::none};
    bool keyboard_{};
    Point origin_{};
    WindowCapsuleSnap snap_{WindowCapsuleSnap::none};
    // The minimise drag's live offset, and the shown one (equal while
    // dragging; springs back to 0 after).
    double offset_live_{};
    double offset_shown_{};
    double intent_shown_{};
};

// The capsule's own help: how to use it, in plain words.
[[nodiscard]] GUI_FORMS_API std::string_view window_capsule_help_text() noexcept;

// What a capsule shows: the gesture's state for its window
// (window_capsule_look), or the resting look.
struct GUI_FORMS_API WindowCapsuleLook final {
    WindowCapsuleArm arm{WindowCapsuleArm::idle};
    // WindowCapsuleGesture::intent and minimize_offset.
    double intent{};
    double minimize_offset{};
    WindowCapsuleSection hovered{WindowCapsuleSection::none};
    WindowCapsuleSection pressed{WindowCapsuleSection::none};
    // The section with the keyboard's focus ring, none without it.
    WindowCapsuleSection focused{WindowCapsuleSection::none};

    friend bool operator==(const WindowCapsuleLook& a, const WindowCapsuleLook& b) noexcept {
        const bool equal = a.arm == b.arm && a.intent == b.intent && a.minimize_offset == b.minimize_offset &&
                           a.hovered == b.hovered && a.pressed == b.pressed && a.focused == b.focused;
        return equal;
    }
};

// The look of `gesture` for `window`: its state while it concerns that
// window, else the resting look.
[[nodiscard]] GUI_FORMS_API WindowCapsuleLook window_capsule_look(const WindowCapsuleGesture& gesture,
                                                                  HostWindowId window) noexcept;

// The colours the capsule's fills and glyphs are drawn in, as authored
// colours over its theme parts. The defaults (2026-11-15, standing until a
// theme supplies capsule colours): a dark face, light glyphs, a green
// invitation (#2EA043) and a red X (#D1242F).
struct GUI_FORMS_API WindowCapsuleColors final {
    Color face{0x3F, 0x3F, 0x46, 0xFF};
    Color glyph{0xF0, 0xF0, 0xF0, 0xFF};
    Color ready{0x2E, 0xA0, 0x43, 0xFF};
    Color close{0xD1, 0x24, 0x2F, 0xFF};

    friend bool operator==(const WindowCapsuleColors& a, const WindowCapsuleColors& b) noexcept {
        const bool equal = a.face == b.face && a.glyph == b.glyph && a.ready == b.ready && a.close == b.close;
        return equal;
    }
};

// What a WindowCapsule in a program's content asks its program to do
// (WindowCapsule::requested). Points are in the capsule's client
// coordinates; a move's `point` stays where the window moves with the
// pointer, so a program moving its window reads the screen pointer
// (HostWindowing::pointer_position) and asks for the frame
// (ApplicationWindowHandle::request_frame), and chooses any snap itself.
struct GUI_FORMS_API WindowCapsuleRequest final {
    WindowCapsuleCommand command{WindowCapsuleCommand::none};
    Point origin{};
    Point point{};
    Point offset{};
    WindowCapsuleSnap snap{WindowCapsuleSnap::none};

    friend bool operator==(const WindowCapsuleRequest& a, const WindowCapsuleRequest& b) noexcept {
        const bool equal = a.command == b.command && a.origin == b.origin && a.point == b.point &&
                           a.offset == b.offset && a.snap == b.snap;
        return equal;
    }
};

// The window capsule control (owner, 2026-11-15; see the top of this
// header). Its anatomy: `region`, `face` (the whole capsule),
// `left_half`, `help` and `right_half` (the sections' fills and the
// question mark's face), `glyph` (the halves' marks and the X), `label`
// (the question mark) and `focus` (the keyboard's ring).
//
// Interactive (the default), it takes the pointer and, when focused, the
// keyboard, runs a WindowCapsuleGesture and raises `requested` for each
// command; it takes focus. Not interactive (the surface window manager's
// title strip), it only shows the look it is given and passes the pointer
// through.
//
// To assistive technology it is a group named "Window controls" (or its
// accessible name) whose three sections are buttons: "Maximise or
// minimise", "Help" and "Move", and "Close" while the X shows.
class GUI_FORMS_API WindowCapsule final : public Control {
public:
    explicit WindowCapsule(StableId stable_id);

    // The look shown; an interactive capsule's own gesture replaces it at
    // its next input. Paints again only when it changed.
    void set_look(const WindowCapsuleLook& look);
    [[nodiscard]] const WindowCapsuleLook& look() const noexcept { return look_; }
    void set_colors(const WindowCapsuleColors& colors);
    [[nodiscard]] const WindowCapsuleColors& colors() const noexcept { return colors_; }
    // The question mark's width: at least the capsule's height, so the
    // halves' rounded inner ends lie under it (20 by default).
    void set_help_width(double width);
    [[nodiscard]] double help_width() const noexcept { return help_width_; }
    // The gesture's measures (an interactive capsule's own gesture) and
    // the minimise distance its look scales the drag's light by.
    void set_measures(const WindowCapsuleMeasures& measures);
    void set_interactive(bool interactive);
    [[nodiscard]] bool interactive() const noexcept { return interactive_; }
    // The section at a client point, none outside the capsule.
    [[nodiscard]] WindowCapsuleSection section_at(Point local_point) const noexcept;
    // The sections' rectangles in client coordinates.
    [[nodiscard]] Rect section_bounds(WindowCapsuleSection section) const noexcept;

    // A command of an interactive capsule's gesture, for its program.
    [[nodiscard]] Event<const WindowCapsuleRequest&>& requested() noexcept { return requested_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

    // The anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry left_half;
        PartGeometry help;
        PartGeometry right_half;
        PartGeometry glyph;
        PartGeometry label;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_focus_changed(bool focused) override;

private:
    // Shows the gesture's look and raises its command.
    void take(const WindowCapsuleOutcome& outcome);
    // The capsule's own gesture keys its state by this identity.
    static constexpr HostWindowId own_window{1U};

    WindowCapsuleLook look_{};
    WindowCapsuleColors colors_{};
    double help_width_{20.0};
    bool interactive_{true};
    WindowCapsuleGesture gesture_{};
    Event<const WindowCapsuleRequest&> requested_{};
};

} // namespace gui_forms
