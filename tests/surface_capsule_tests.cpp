// The window capsule's gesture model
// (src/host/surface/capsule/surface_capsule.hpp): each gesture (the left
// half's double click and downward drag, the question mark's click and
// hover, the right half's live move with its snap targets and their
// hysteresis), the three-click close and what disarms it, the keyboard
// paths, and the animation with and without reduced motion. Plain values;
// tested on every platform. The manager's wiring of the same gestures is
// tests/surface_window_manager_tests.cpp.

#include "../src/host/surface/capsule/surface_capsule.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using namespace gui_forms;
using namespace gui_forms::host::surface_detail;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const HostWindowId window{7U};
const HostWindowId other{8U};
const Size surface{1000.0, 700.0};

// A click: press and release on `section`.
SurfaceCapsuleOutcome click(SurfaceCapsuleGesture& gesture, SurfaceCapsuleSection section, Point point) {
    static_cast<void>(gesture.press(window, section, point, 1U));
    const SurfaceCapsuleOutcome outcome = gesture.release(section);
    return outcome;
}

void test_three_click_close() {
    SurfaceCapsuleGesture gesture;
    const SurfaceCapsuleOutcome first = click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0});
    require(first.command == SurfaceCapsuleCommand::none && gesture.arm() == SurfaceCapsuleArm::left,
            "the first click arms the left half");
    require(first.redraw && first.handled, "and is drawn");
    const SurfaceCapsuleOutcome second = click(gesture, SurfaceCapsuleSection::right, Point{380.0, 86.0});
    require(second.command == SurfaceCapsuleCommand::none && gesture.arm() == SurfaceCapsuleArm::close,
            "the second click, on the other half, makes the X");
    const SurfaceCapsuleOutcome third = click(gesture, SurfaceCapsuleSection::help, Point{356.0, 86.0});
    require(third.command == SurfaceCapsuleCommand::close && third.window == window,
            "the third click, anywhere on the X, closes");
    require(gesture.arm() == SurfaceCapsuleArm::idle, "and the capsule rests again");

    // The other order closes too.
    static_cast<void>(click(gesture, SurfaceCapsuleSection::right, Point{380.0, 86.0}));
    require(gesture.arm() == SurfaceCapsuleArm::right, "the right half arms first");
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    require(gesture.arm() == SurfaceCapsuleArm::close, "the left half completes the X");
    require(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}).command ==
                SurfaceCapsuleCommand::close,
            "and the X closes");
}

void test_close_disarms() {
    SurfaceCapsuleGesture gesture;
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{331.0, 86.0}));
    require(gesture.arm() == SurfaceCapsuleArm::left, "the same half twice stays one half");
    static_cast<void>(gesture.hover(window, SurfaceCapsuleSection::right));
    require(gesture.arm() == SurfaceCapsuleArm::left, "moving across the capsule keeps it armed");
    static_cast<void>(gesture.hover(HostWindowId{}, SurfaceCapsuleSection::none));
    require(gesture.arm() == SurfaceCapsuleArm::idle, "leaving the capsule disarms it");

    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::right, Point{380.0, 86.0}, 1U));
    require(gesture.release(SurfaceCapsuleSection::none).command == SurfaceCapsuleCommand::none,
            "a release off the capsule is no click");
    require(gesture.arm() == SurfaceCapsuleArm::left, "and arms nothing more");
    require(click(gesture, SurfaceCapsuleSection::help, Point{356.0, 86.0}).command ==
                SurfaceCapsuleCommand::open_help,
            "the question mark opens the help instead");
    require(gesture.arm() == SurfaceCapsuleArm::idle, "and disarms");

    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    require(gesture.forget(window) && gesture.arm() == SurfaceCapsuleArm::idle,
            "the window losing the capsule disarms it");
    require(!gesture.forget(other), "forgetting another window changes nothing");
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(gesture.press(other, SurfaceCapsuleSection::right, Point{80.0, 86.0}, 1U));
    static_cast<void>(gesture.release(SurfaceCapsuleSection::right));
    require(gesture.window() == other && gesture.arm() == SurfaceCapsuleArm::right,
            "another window's capsule starts afresh");
}

void test_left_double_click_maximizes() {
    SurfaceCapsuleGesture gesture;
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    const SurfaceCapsuleOutcome second = gesture.press(window, SurfaceCapsuleSection::left, Point{330.0, 86.0}, 2U);
    require(second.command == SurfaceCapsuleCommand::toggle_maximize, "a double click on the left half maximises");
    require(gesture.arm() == SurfaceCapsuleArm::idle && !gesture.pressing(),
            "and leaves the capsule disarmed, with no press pending");
    require(gesture.release(SurfaceCapsuleSection::left).command == SurfaceCapsuleCommand::none,
            "its release does nothing more");
    const SurfaceCapsuleOutcome right = gesture.press(window, SurfaceCapsuleSection::right, Point{380.0, 86.0}, 2U);
    require(right.command == SurfaceCapsuleCommand::none, "a double click on the right half does not");
}

void test_left_drag_minimizes() {
    SurfaceCapsuleGesture gesture;
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{331.0, 89.0}, surface));
    require(gesture.minimize_offset() == 0.0, "within the slop it is still a click");
    static_cast<void>(gesture.drag(Point{331.0, 106.0}, surface));
    require(gesture.minimize_offset() == 20.0, "down past the slop it follows the pointer");
    const SurfaceCapsuleOutcome short_release = gesture.release(SurfaceCapsuleSection::none);
    require(short_release.command == SurfaceCapsuleCommand::none, "released short of the distance, nothing");
    require(gesture.arm() == SurfaceCapsuleArm::idle, "and it was no click either");
    require(gesture.animating(), "the offset springs back");
    static_cast<void>(gesture.advance(80.0, false));
    require(gesture.minimize_offset() == 0.0, "within the animation");

    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{332.0, 200.0}, surface));
    require(gesture.minimize_offset() == 40.0, "the offset stops at the distance");
    require(gesture.release(SurfaceCapsuleSection::none).command == SurfaceCapsuleCommand::minimize,
            "released past it, the window minimises");

    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{400.0, 100.0}, surface));
    require(gesture.release(SurfaceCapsuleSection::left).command == SurfaceCapsuleCommand::none &&
                gesture.arm() == SurfaceCapsuleArm::idle,
            "a sideways drag of the left half is neither a minimise nor a click");
}

void test_help_and_title_hover() {
    SurfaceCapsuleGesture gesture;
    static_cast<void>(gesture.hover(window, SurfaceCapsuleSection::help));
    require(gesture.title_shown() && gesture.window() == window, "hovering the question mark shows the title");
    static_cast<void>(gesture.hover(window, SurfaceCapsuleSection::left));
    require(!gesture.title_shown(), "hovering a half does not");
    static_cast<void>(gesture.hover(window, SurfaceCapsuleSection::help));
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::help, Point{356.0, 86.0}, 1U));
    require(!gesture.title_shown(), "nor while pressed");
    require(gesture.release(SurfaceCapsuleSection::help).command == SurfaceCapsuleCommand::open_help,
            "a click on the question mark opens the help");
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::help, Point{356.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{356.0, 120.0}, surface));
    require(gesture.release(SurfaceCapsuleSection::help).command == SurfaceCapsuleCommand::none,
            "a drag from it opens nothing");
}

void test_right_drag_moves_and_snaps() {
    SurfaceCapsuleGesture gesture;
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::right, Point{380.0, 86.0}, 1U));
    require(gesture.drag(Point{383.0, 86.0}, surface).command == SurfaceCapsuleCommand::none,
            "within the slop nothing moves");
    const SurfaceCapsuleOutcome begun = gesture.drag(Point{420.0, 100.0}, surface);
    require(begun.command == SurfaceCapsuleCommand::begin_move && begun.origin == (Point{380.0, 86.0}) &&
                begun.point == (Point{420.0, 100.0}),
            "past the slop the live move begins at the press");
    require(gesture.moving() && gesture.snap() == SurfaceCapsuleSnap::none, "floating while mid-surface");
    const SurfaceCapsuleOutcome moved = gesture.drag(Point{500.0, 300.0}, surface);
    require(moved.command == SurfaceCapsuleCommand::move && moved.point == (Point{500.0, 300.0}), "the move goes on");
    const SurfaceCapsuleOutcome armed = gesture.drag(Point{10.0, 300.0}, surface);
    require(armed.snap == SurfaceCapsuleSnap::left_half && armed.redraw, "near the left edge the left half arms");
    static_cast<void>(gesture.drag(Point{50.0, 300.0}, surface));
    require(gesture.snap() == SurfaceCapsuleSnap::left_half, "it holds within the release distance");
    static_cast<void>(gesture.drag(Point{70.0, 300.0}, surface));
    require(gesture.snap() == SurfaceCapsuleSnap::none, "and lets go beyond it");
    static_cast<void>(gesture.drag(Point{50.0, 300.0}, surface));
    require(gesture.snap() == SurfaceCapsuleSnap::none, "coming back takes the edge zone again");
    static_cast<void>(gesture.drag(Point{995.0, 300.0}, surface));
    require(gesture.snap() == SurfaceCapsuleSnap::right_half, "the right edge arms the right half");
    const SurfaceCapsuleOutcome finished = gesture.release(SurfaceCapsuleSection::none);
    require(finished.command == SurfaceCapsuleCommand::finish_move &&
                finished.snap == SurfaceCapsuleSnap::right_half,
            "released there, the window takes the right half");
    require(!gesture.moving() && gesture.snap() == SurfaceCapsuleSnap::none, "and the move is over");
    require(gesture.arm() == SurfaceCapsuleArm::idle, "a move is no click");

    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::right, Point{380.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{500.0, 300.0}, surface));
    const SurfaceCapsuleOutcome floated = gesture.release(SurfaceCapsuleSection::none);
    require(floated.command == SurfaceCapsuleCommand::finish_move && floated.snap == SurfaceCapsuleSnap::none,
            "released mid-surface, it floats");
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::right, Point{380.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{500.0, 300.0}, surface));
    require(gesture.cancel().command == SurfaceCapsuleCommand::cancel_move, "a lost pointer ends the move");
    require(!gesture.pressing(), "and the press");

    // With the X shown, a drag is no move.
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(click(gesture, SurfaceCapsuleSection::right, Point{380.0, 86.0}));
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::right, Point{380.0, 86.0}, 1U));
    require(gesture.drag(Point{500.0, 300.0}, surface).command == SurfaceCapsuleCommand::none,
            "the X does not move the window");
    require(gesture.release(SurfaceCapsuleSection::right).command == SurfaceCapsuleCommand::none,
            "nor does a dragged press close it");
}

void test_keyboard_paths() {
    SurfaceCapsuleGesture gesture;
    require(!gesture.key(PhysicalKey::enter, Modifier::none).handled, "without focus the capsule takes no key");
    const SurfaceCapsuleOutcome focused = gesture.focus(window);
    require(focused.handled && gesture.keyboard() && gesture.focused_section() == SurfaceCapsuleSection::left,
            "focus starts at the left half");
    require(gesture.key(PhysicalKey::enter, Modifier::none).command == SurfaceCapsuleCommand::none,
            "Enter arms the left half");
    require(gesture.arm() == SurfaceCapsuleArm::left && gesture.focused_section() == SurfaceCapsuleSection::right,
            "and the focus moves to the other half");
    static_cast<void>(gesture.key(PhysicalKey::enter, Modifier::none));
    require(gesture.arm() == SurfaceCapsuleArm::close, "Enter again makes the X");
    require(gesture.key(PhysicalKey::enter, Modifier::none).command == SurfaceCapsuleCommand::close,
            "and a third Enter closes, as three clicks do");

    static_cast<void>(gesture.focus(window));
    static_cast<void>(gesture.key(PhysicalKey::enter, Modifier::none));
    require(gesture.key(PhysicalKey::escape, Modifier::none).handled && gesture.arm() == SurfaceCapsuleArm::idle &&
                gesture.keyboard(),
            "Escape disarms first");
    static_cast<void>(gesture.key(PhysicalKey::escape, Modifier::none));
    require(!gesture.keyboard(), "then leaves the capsule");

    static_cast<void>(gesture.focus(window));
    static_cast<void>(gesture.key(PhysicalKey::right, Modifier::none));
    require(gesture.focused_section() == SurfaceCapsuleSection::help, "Right moves to the question mark");
    static_cast<void>(gesture.key(PhysicalKey::right, Modifier::none));
    static_cast<void>(gesture.key(PhysicalKey::right, Modifier::none));
    require(gesture.focused_section() == SurfaceCapsuleSection::right, "and stops at the right half");
    static_cast<void>(gesture.key(PhysicalKey::left, Modifier::none));
    require(gesture.focused_section() == SurfaceCapsuleSection::help, "Left moves back");
    require(gesture.key(PhysicalKey::enter, Modifier::none).command == SurfaceCapsuleCommand::open_help,
            "Enter on the question mark opens the help");
    require(gesture.key(PhysicalKey::f1, Modifier::none).command == SurfaceCapsuleCommand::open_help,
            "and F1 does");
    require(gesture.key(PhysicalKey::up, Modifier::none).command == SurfaceCapsuleCommand::toggle_maximize,
            "Up maximises or restores");
    require(gesture.key(PhysicalKey::down, Modifier::none).command == SurfaceCapsuleCommand::minimize,
            "Down minimises");
    const SurfaceCapsuleOutcome left = gesture.key(PhysicalKey::left, Modifier::shift);
    require(left.command == SurfaceCapsuleCommand::tile && left.snap == SurfaceCapsuleSnap::left_half,
            "Shift+Left tiles to the left half");
    require(gesture.key(PhysicalKey::right, Modifier::shift).snap == SurfaceCapsuleSnap::right_half,
            "Shift+Right to the right half");
    const SurfaceCapsuleOutcome floated = gesture.key(PhysicalKey::down, Modifier::shift);
    require(floated.command == SurfaceCapsuleCommand::tile && floated.snap == SurfaceCapsuleSnap::none,
            "Shift+Down floats");
    const SurfaceCapsuleOutcome nudged = gesture.key(PhysicalKey::up, Modifier::control);
    require(nudged.command == SurfaceCapsuleCommand::nudge && nudged.offset == (Point{0.0, -16.0}),
            "Control with an arrow moves the window");
    const SurfaceCapsuleOutcome passed = gesture.key(PhysicalKey::a, Modifier::none);
    require(!passed.handled && !gesture.keyboard(), "any other key leaves the capsule for the window");

    static_cast<void>(gesture.focus(window));
    static_cast<void>(gesture.press(window, SurfaceCapsuleSection::left, Point{330.0, 86.0}, 1U));
    require(!gesture.keyboard(), "the pointer takes over from the keyboard");
    static_cast<void>(gesture.release(SurfaceCapsuleSection::left));
}

void test_animation_and_reduced_motion() {
    SurfaceCapsuleGesture gesture;
    static_cast<void>(click(gesture, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    require(gesture.intent() == 0.0 && gesture.animating(), "arming animates toward one half");
    require(gesture.advance(80.0, false) && gesture.intent() == 0.5, "the half step takes half the animation");
    require(!gesture.animating() && !gesture.advance(80.0, false), "then rests");
    static_cast<void>(click(gesture, SurfaceCapsuleSection::right, Point{380.0, 86.0}));
    static_cast<void>(gesture.advance(40.0, false));
    require(gesture.intent() == 0.75, "the X's red forms over time");
    static_cast<void>(gesture.advance(40.0, false));
    require(gesture.intent() == 1.0, "until it is whole");

    SurfaceCapsuleGesture reduced;
    static_cast<void>(click(reduced, SurfaceCapsuleSection::left, Point{330.0, 86.0}));
    require(reduced.advance(0.0, true) && reduced.intent() == 0.5 && !reduced.animating(),
            "with reduced motion the look takes its target at once");
    static_cast<void>(reduced.press(window, SurfaceCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(reduced.drag(Point{330.0, 110.0}, surface));
    static_cast<void>(reduced.release(SurfaceCapsuleSection::none));
    static_cast<void>(reduced.advance(0.0, true));
    require(reduced.minimize_offset() == 0.0 && !reduced.animating(), "and the spring-back is instant");

    SurfaceCapsuleMeasures odd{};
    odd.animation_milliseconds = -1.0;
    odd.slop = std::numeric_limits<double>::quiet_NaN();
    SurfaceCapsuleGesture defaults;
    defaults.set_measures(odd);
    require(defaults.measures().animation_milliseconds == 160.0 && defaults.measures().slop == 4.0,
            "measures that make no sense fall back to the defaults");
}

} // namespace

int main() {
    try {
        test_three_click_close();
        test_close_disarms();
        test_left_double_click_maximizes();
        test_left_drag_minimizes();
        test_help_and_title_hover();
        test_right_drag_moves_and_snaps();
        test_keyboard_paths();
        test_animation_and_reduced_motion();
    } catch (const std::exception& error) {
        std::cerr << "surface capsule tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "surface capsule tests passed\n";
    return EXIT_SUCCESS;
}
