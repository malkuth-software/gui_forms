// The window capsule (include/gui_forms/controls/window_capsule.hpp).
// Its gesture model: each gesture (the left half's double click and
// downward drag, the question mark's click and hover, the right half's
// live move with its snap targets and their hysteresis, and no snap
// without an extent), the three-click close and what disarms it, the
// keyboard paths, and the animation with and without reduced motion. The
// WindowCapsule control: its sections, anatomy and parts, the requests an
// interactive capsule in a program's content raises for the pointer and
// for assistive technology, and that a non-interactive one is never hit
// and refuses actions. Tested on every platform. The surface window
// manager's wiring of the same gestures is
// tests/surface_window_manager_tests.cpp.

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/window_capsule.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const HostWindowId window{7U};
const HostWindowId other{8U};
const Size surface{1000.0, 700.0};

// A click: press and release on `section`.
WindowCapsuleOutcome click(WindowCapsuleGesture& gesture, WindowCapsuleSection section, Point point) {
    static_cast<void>(gesture.press(window, section, point, 1U));
    const WindowCapsuleOutcome outcome = gesture.release(section);
    return outcome;
}

void test_three_click_close() {
    WindowCapsuleGesture gesture;
    const WindowCapsuleOutcome first = click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0});
    require(first.command == WindowCapsuleCommand::none && gesture.arm() == WindowCapsuleArm::left,
            "the first click arms the left half");
    require(first.redraw && first.handled, "and is drawn");
    const WindowCapsuleOutcome second = click(gesture, WindowCapsuleSection::right, Point{380.0, 86.0});
    require(second.command == WindowCapsuleCommand::none && gesture.arm() == WindowCapsuleArm::close,
            "the second click, on the other half, makes the X");
    const WindowCapsuleOutcome third = click(gesture, WindowCapsuleSection::help, Point{356.0, 86.0});
    require(third.command == WindowCapsuleCommand::close && third.window == window,
            "the third click, anywhere on the X, closes");
    require(gesture.arm() == WindowCapsuleArm::idle, "and the capsule rests again");

    // The other order closes too.
    static_cast<void>(click(gesture, WindowCapsuleSection::right, Point{380.0, 86.0}));
    require(gesture.arm() == WindowCapsuleArm::right, "the right half arms first");
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    require(gesture.arm() == WindowCapsuleArm::close, "the left half completes the X");
    require(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}).command ==
                WindowCapsuleCommand::close,
            "and the X closes");
}

void test_close_disarms() {
    WindowCapsuleGesture gesture;
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{331.0, 86.0}));
    require(gesture.arm() == WindowCapsuleArm::left, "the same half twice stays one half");
    static_cast<void>(gesture.hover(window, WindowCapsuleSection::right));
    require(gesture.arm() == WindowCapsuleArm::left, "moving across the capsule keeps it armed");
    static_cast<void>(gesture.hover(HostWindowId{}, WindowCapsuleSection::none));
    require(gesture.arm() == WindowCapsuleArm::idle, "leaving the capsule disarms it");

    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(gesture.press(window, WindowCapsuleSection::right, Point{380.0, 86.0}, 1U));
    require(gesture.release(WindowCapsuleSection::none).command == WindowCapsuleCommand::none,
            "a release off the capsule is no click");
    require(gesture.arm() == WindowCapsuleArm::left, "and arms nothing more");
    require(click(gesture, WindowCapsuleSection::help, Point{356.0, 86.0}).command ==
                WindowCapsuleCommand::open_help,
            "the question mark opens the help instead");
    require(gesture.arm() == WindowCapsuleArm::idle, "and disarms");

    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    require(gesture.forget(window) && gesture.arm() == WindowCapsuleArm::idle,
            "the window losing the capsule disarms it");
    require(!gesture.forget(other), "forgetting another window changes nothing");
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(gesture.press(other, WindowCapsuleSection::right, Point{80.0, 86.0}, 1U));
    static_cast<void>(gesture.release(WindowCapsuleSection::right));
    require(gesture.window() == other && gesture.arm() == WindowCapsuleArm::right,
            "another window's capsule starts afresh");
}

void test_left_double_click_maximizes() {
    WindowCapsuleGesture gesture;
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    const WindowCapsuleOutcome second = gesture.press(window, WindowCapsuleSection::left, Point{330.0, 86.0}, 2U);
    require(second.command == WindowCapsuleCommand::toggle_maximize, "a double click on the left half maximises");
    require(gesture.arm() == WindowCapsuleArm::idle && !gesture.pressing(),
            "and leaves the capsule disarmed, with no press pending");
    require(gesture.release(WindowCapsuleSection::left).command == WindowCapsuleCommand::none,
            "its release does nothing more");
    const WindowCapsuleOutcome right = gesture.press(window, WindowCapsuleSection::right, Point{380.0, 86.0}, 2U);
    require(right.command == WindowCapsuleCommand::none, "a double click on the right half does not");
}

void test_left_drag_minimizes() {
    WindowCapsuleGesture gesture;
    static_cast<void>(gesture.press(window, WindowCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{331.0, 89.0}, surface));
    require(gesture.minimize_offset() == 0.0, "within the slop it is still a click");
    static_cast<void>(gesture.drag(Point{331.0, 106.0}, surface));
    require(gesture.minimize_offset() == 20.0, "down past the slop it follows the pointer");
    const WindowCapsuleOutcome short_release = gesture.release(WindowCapsuleSection::none);
    require(short_release.command == WindowCapsuleCommand::none, "released short of the distance, nothing");
    require(gesture.arm() == WindowCapsuleArm::idle, "and it was no click either");
    require(gesture.animating(), "the offset springs back");
    static_cast<void>(gesture.advance(80.0, false));
    require(gesture.minimize_offset() == 0.0, "within the animation");

    static_cast<void>(gesture.press(window, WindowCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{332.0, 200.0}, surface));
    require(gesture.minimize_offset() == 40.0, "the offset stops at the distance");
    require(gesture.release(WindowCapsuleSection::none).command == WindowCapsuleCommand::minimize,
            "released past it, the window minimises");

    static_cast<void>(gesture.press(window, WindowCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{400.0, 100.0}, surface));
    require(gesture.release(WindowCapsuleSection::left).command == WindowCapsuleCommand::none &&
                gesture.arm() == WindowCapsuleArm::idle,
            "a sideways drag of the left half is neither a minimise nor a click");
}

void test_help_and_title_hover() {
    WindowCapsuleGesture gesture;
    static_cast<void>(gesture.hover(window, WindowCapsuleSection::help));
    require(gesture.title_shown() && gesture.window() == window, "hovering the question mark shows the title");
    static_cast<void>(gesture.hover(window, WindowCapsuleSection::left));
    require(!gesture.title_shown(), "hovering a half does not");
    static_cast<void>(gesture.hover(window, WindowCapsuleSection::help));
    static_cast<void>(gesture.press(window, WindowCapsuleSection::help, Point{356.0, 86.0}, 1U));
    require(!gesture.title_shown(), "nor while pressed");
    require(gesture.release(WindowCapsuleSection::help).command == WindowCapsuleCommand::open_help,
            "a click on the question mark opens the help");
    static_cast<void>(gesture.press(window, WindowCapsuleSection::help, Point{356.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{356.0, 120.0}, surface));
    require(gesture.release(WindowCapsuleSection::help).command == WindowCapsuleCommand::none,
            "a drag from it opens nothing");
}

void test_right_drag_moves_and_snaps() {
    WindowCapsuleGesture gesture;
    static_cast<void>(gesture.press(window, WindowCapsuleSection::right, Point{380.0, 86.0}, 1U));
    require(gesture.drag(Point{383.0, 86.0}, surface).command == WindowCapsuleCommand::none,
            "within the slop nothing moves");
    const WindowCapsuleOutcome begun = gesture.drag(Point{420.0, 100.0}, surface);
    require(begun.command == WindowCapsuleCommand::begin_move && begun.origin == (Point{380.0, 86.0}) &&
                begun.point == (Point{420.0, 100.0}),
            "past the slop the live move begins at the press");
    require(gesture.moving() && gesture.snap() == WindowCapsuleSnap::none, "floating while mid-surface");
    const WindowCapsuleOutcome moved = gesture.drag(Point{500.0, 300.0}, surface);
    require(moved.command == WindowCapsuleCommand::move && moved.point == (Point{500.0, 300.0}), "the move goes on");
    const WindowCapsuleOutcome armed = gesture.drag(Point{10.0, 300.0}, surface);
    require(armed.snap == WindowCapsuleSnap::left_half && armed.redraw, "near the left edge the left half arms");
    static_cast<void>(gesture.drag(Point{50.0, 300.0}, surface));
    require(gesture.snap() == WindowCapsuleSnap::left_half, "it holds within the release distance");
    static_cast<void>(gesture.drag(Point{70.0, 300.0}, surface));
    require(gesture.snap() == WindowCapsuleSnap::none, "and lets go beyond it");
    static_cast<void>(gesture.drag(Point{50.0, 300.0}, surface));
    require(gesture.snap() == WindowCapsuleSnap::none, "coming back takes the edge zone again");
    static_cast<void>(gesture.drag(Point{995.0, 300.0}, surface));
    require(gesture.snap() == WindowCapsuleSnap::right_half, "the right edge arms the right half");
    const WindowCapsuleOutcome finished = gesture.release(WindowCapsuleSection::none);
    require(finished.command == WindowCapsuleCommand::finish_move &&
                finished.snap == WindowCapsuleSnap::right_half,
            "released there, the window takes the right half");
    require(!gesture.moving() && gesture.snap() == WindowCapsuleSnap::none, "and the move is over");
    require(gesture.arm() == WindowCapsuleArm::idle, "a move is no click");

    static_cast<void>(gesture.press(window, WindowCapsuleSection::right, Point{380.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{500.0, 300.0}, surface));
    const WindowCapsuleOutcome floated = gesture.release(WindowCapsuleSection::none);
    require(floated.command == WindowCapsuleCommand::finish_move && floated.snap == WindowCapsuleSnap::none,
            "released mid-surface, it floats");
    static_cast<void>(gesture.press(window, WindowCapsuleSection::right, Point{380.0, 86.0}, 1U));
    static_cast<void>(gesture.drag(Point{500.0, 300.0}, surface));
    require(gesture.cancel().command == WindowCapsuleCommand::cancel_move, "a lost pointer ends the move");
    require(!gesture.pressing(), "and the press");

    // With the X shown, a drag is no move.
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    static_cast<void>(click(gesture, WindowCapsuleSection::right, Point{380.0, 86.0}));
    static_cast<void>(gesture.press(window, WindowCapsuleSection::right, Point{380.0, 86.0}, 1U));
    require(gesture.drag(Point{500.0, 300.0}, surface).command == WindowCapsuleCommand::none,
            "the X does not move the window");
    require(gesture.release(WindowCapsuleSection::right).command == WindowCapsuleCommand::none,
            "nor does a dragged press close it");
}

void test_keyboard_paths() {
    WindowCapsuleGesture gesture;
    require(!gesture.key(PhysicalKey::enter, Modifier::none).handled, "without focus the capsule takes no key");
    const WindowCapsuleOutcome focused = gesture.focus(window);
    require(focused.handled && gesture.keyboard() && gesture.focused_section() == WindowCapsuleSection::left,
            "focus starts at the left half");
    require(gesture.key(PhysicalKey::enter, Modifier::none).command == WindowCapsuleCommand::none,
            "Enter arms the left half");
    require(gesture.arm() == WindowCapsuleArm::left && gesture.focused_section() == WindowCapsuleSection::right,
            "and the focus moves to the other half");
    static_cast<void>(gesture.key(PhysicalKey::enter, Modifier::none));
    require(gesture.arm() == WindowCapsuleArm::close, "Enter again makes the X");
    require(gesture.key(PhysicalKey::enter, Modifier::none).command == WindowCapsuleCommand::close,
            "and a third Enter closes, as three clicks do");

    static_cast<void>(gesture.focus(window));
    static_cast<void>(gesture.key(PhysicalKey::enter, Modifier::none));
    require(gesture.key(PhysicalKey::escape, Modifier::none).handled && gesture.arm() == WindowCapsuleArm::idle &&
                gesture.keyboard(),
            "Escape disarms first");
    static_cast<void>(gesture.key(PhysicalKey::escape, Modifier::none));
    require(!gesture.keyboard(), "then leaves the capsule");

    static_cast<void>(gesture.focus(window));
    static_cast<void>(gesture.key(PhysicalKey::right, Modifier::none));
    require(gesture.focused_section() == WindowCapsuleSection::help, "Right moves to the question mark");
    static_cast<void>(gesture.key(PhysicalKey::right, Modifier::none));
    static_cast<void>(gesture.key(PhysicalKey::right, Modifier::none));
    require(gesture.focused_section() == WindowCapsuleSection::right, "and stops at the right half");
    static_cast<void>(gesture.key(PhysicalKey::left, Modifier::none));
    require(gesture.focused_section() == WindowCapsuleSection::help, "Left moves back");
    require(gesture.key(PhysicalKey::enter, Modifier::none).command == WindowCapsuleCommand::open_help,
            "Enter on the question mark opens the help");
    require(gesture.key(PhysicalKey::f1, Modifier::none).command == WindowCapsuleCommand::open_help,
            "and F1 does");
    require(gesture.key(PhysicalKey::up, Modifier::none).command == WindowCapsuleCommand::toggle_maximize,
            "Up maximises or restores");
    require(gesture.key(PhysicalKey::down, Modifier::none).command == WindowCapsuleCommand::minimize,
            "Down minimises");
    const WindowCapsuleOutcome left = gesture.key(PhysicalKey::left, Modifier::shift);
    require(left.command == WindowCapsuleCommand::tile && left.snap == WindowCapsuleSnap::left_half,
            "Shift+Left tiles to the left half");
    require(gesture.key(PhysicalKey::right, Modifier::shift).snap == WindowCapsuleSnap::right_half,
            "Shift+Right to the right half");
    const WindowCapsuleOutcome floated = gesture.key(PhysicalKey::down, Modifier::shift);
    require(floated.command == WindowCapsuleCommand::tile && floated.snap == WindowCapsuleSnap::none,
            "Shift+Down floats");
    const WindowCapsuleOutcome nudged = gesture.key(PhysicalKey::up, Modifier::control);
    require(nudged.command == WindowCapsuleCommand::nudge && nudged.offset == (Point{0.0, -16.0}),
            "Control with an arrow moves the window");
    const WindowCapsuleOutcome passed = gesture.key(PhysicalKey::a, Modifier::none);
    require(!passed.handled && !gesture.keyboard(), "any other key leaves the capsule for the window");

    static_cast<void>(gesture.focus(window));
    static_cast<void>(gesture.press(window, WindowCapsuleSection::left, Point{330.0, 86.0}, 1U));
    require(!gesture.keyboard(), "the pointer takes over from the keyboard");
    static_cast<void>(gesture.release(WindowCapsuleSection::left));
}

void test_animation_and_reduced_motion() {
    WindowCapsuleGesture gesture;
    static_cast<void>(click(gesture, WindowCapsuleSection::left, Point{330.0, 86.0}));
    require(gesture.intent() == 0.0 && gesture.animating(), "arming animates toward one half");
    require(gesture.advance(80.0, false) && gesture.intent() == 0.5, "the half step takes half the animation");
    require(!gesture.animating() && !gesture.advance(80.0, false), "then rests");
    static_cast<void>(click(gesture, WindowCapsuleSection::right, Point{380.0, 86.0}));
    static_cast<void>(gesture.advance(40.0, false));
    require(gesture.intent() == 0.75, "the X's red forms over time");
    static_cast<void>(gesture.advance(40.0, false));
    require(gesture.intent() == 1.0, "until it is whole");

    WindowCapsuleGesture reduced;
    static_cast<void>(click(reduced, WindowCapsuleSection::left, Point{330.0, 86.0}));
    require(reduced.advance(0.0, true) && reduced.intent() == 0.5 && !reduced.animating(),
            "with reduced motion the look takes its target at once");
    static_cast<void>(reduced.press(window, WindowCapsuleSection::left, Point{330.0, 86.0}, 1U));
    static_cast<void>(reduced.drag(Point{330.0, 110.0}, surface));
    static_cast<void>(reduced.release(WindowCapsuleSection::none));
    static_cast<void>(reduced.advance(0.0, true));
    require(reduced.minimize_offset() == 0.0 && !reduced.animating(), "and the spring-back is instant");

    WindowCapsuleMeasures odd{};
    odd.animation_milliseconds = -1.0;
    odd.slop = std::numeric_limits<double>::quiet_NaN();
    WindowCapsuleGesture defaults;
    defaults.set_measures(odd);
    require(defaults.measures().animation_milliseconds == 160.0 && defaults.measures().slop == 4.0,
            "measures that make no sense fall back to the defaults");
}

void test_no_snap_without_extent() {
    WindowCapsuleGesture gesture;
    static_cast<void>(gesture.press(window, WindowCapsuleSection::right, Point{60.0, 10.0}, 1U));
    const WindowCapsuleOutcome begun = gesture.drag(Point{2.0, 300.0}, Size{});
    require(begun.command == WindowCapsuleCommand::begin_move && begun.snap == WindowCapsuleSnap::none,
            "without an extent the move begins and nothing arms");
    static_cast<void>(gesture.drag(Point{1.0, 1.0}, Size{}));
    require(gesture.snap() == WindowCapsuleSnap::none, "not even at the origin's corner");
    const WindowCapsuleOutcome finished = gesture.release(WindowCapsuleSection::none);
    require(finished.command == WindowCapsuleCommand::finish_move && finished.snap == WindowCapsuleSnap::none,
            "and it ends floating");
}

// Records each request of a capsule.
class RequestRecorder final : public Component {
public:
    void record(const WindowCapsuleRequest& request) { requests.push_back(request); }
    std::vector<WindowCapsuleRequest> requests{};
};

// A capsule at (8, 8) in a panel in a window of its own.
struct CapsuleWindow final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<WindowCapsule> capsule;
    std::unique_ptr<Window> window;
};

CapsuleWindow capsule_window() {
    CapsuleWindow made;
    made.root = make_control<Panel>(StableId("root"));
    made.capsule = make_control<WindowCapsule>(StableId("root.capsule"));
    (*made.capsule).set_requested_bounds({8.0, 8.0, 76.0, 20.0});
    (*made.root).add_child(made.capsule);
    made.window = std::make_unique<Window>(made.root, Size{200.0, 120.0});
    (*made.window).set_active(true);
    (*made.window).perform_layout();
    return made;
}

void send(Window& target, PointerAction action, Point point) {
    PointerEvent event{};
    event.action = action;
    event.button = PointerButton::primary;
    event.position = point;
    static_cast<void>(target.dispatch_pointer(event));
}

void test_control_sections_and_anatomy() {
    const CapsuleWindow made = capsule_window();
    const WindowCapsule& capsule = *made.capsule;
    require(capsule.interactive() && capsule.help_width() == 20.0, "a new capsule is interactive, its help 20 wide");
    require(capsule.section_bounds(WindowCapsuleSection::left) == (Rect{0.0, 0.0, 28.0, 20.0}) &&
                capsule.section_bounds(WindowCapsuleSection::help) == (Rect{28.0, 0.0, 20.0, 20.0}) &&
                capsule.section_bounds(WindowCapsuleSection::right) == (Rect{48.0, 0.0, 28.0, 20.0}),
            "the halves share what the question mark leaves");
    require(capsule.section_at(Point{4.0, 10.0}) == WindowCapsuleSection::left &&
                capsule.section_at(Point{38.0, 10.0}) == WindowCapsuleSection::help &&
                capsule.section_at(Point{70.0, 10.0}) == WindowCapsuleSection::right &&
                capsule.section_at(Point{80.0, 10.0}) == WindowCapsuleSection::none,
            "each point finds its section, none outside");
    require(capsule.hit_test_local(Point{38.0, 10.0}), "an interactive capsule is hit");

    const ControlAnatomy& anatomy = capsule.anatomy();
    require(anatomy.control_id == "window_capsule" && anatomy.parts.size() == 8U,
            "the anatomy declares region, face, the sections, glyph, label and focus");
    const WindowCapsule::Parts parts = capsule.parts();
    require(parts.left_half.bounds.width > 0.0 && parts.help.bounds.width > 0.0 &&
                parts.right_half.bounds.width > 0.0,
            "each section publishes its geometry");

    const SemanticDescriptor described = capsule.semantic_descriptor();
    require(described.role == SemanticRole::group && described.name == "Window controls" && described.exposed,
            "to assistive technology it is a group named Window controls");
    const std::vector<SemanticNode> children = capsule.semantic_virtual_children();
    require(children.size() == 3U && children[0].stable_id == "root.capsule.left-half" &&
                children[1].stable_id == "root.capsule.help" && children[2].stable_id == "root.capsule.right-half",
            "its three sections are its children");
    require(children[1].role == SemanticRole::button && children[1].name == "Help" &&
                has_semantic_state(children[1].states, SemanticState::enabled),
            "each an enabled button");
}

void test_control_requests() {
    CapsuleWindow made = capsule_window();
    WindowCapsule& capsule = *made.capsule;
    RequestRecorder recorder;
    on(capsule.requested(), recorder, &RequestRecorder::record);

    require(capsule.on_semantic_child_action("root.capsule.help", SemanticAction::press, ""),
            "pressing the help button is taken");
    require(recorder.requests.size() == 1U && recorder.requests[0].command == WindowCapsuleCommand::open_help,
            "and asks for the help");
    require(!capsule.on_semantic_child_action("root.capsule.nothing", SemanticAction::press, ""),
            "an unknown child is refused");

    // A drag from the right half, in window coordinates: the capsule's
    // right half starts at x = 8 + 48.
    recorder.requests.clear();
    send(*made.window, PointerAction::down, Point{70.0, 18.0});
    send(*made.window, PointerAction::move, Point{110.0, 60.0});
    send(*made.window, PointerAction::up, Point{110.0, 60.0});
    require(recorder.requests.size() == 2U, "a drag past the slop begins a move and ends it");
    if (recorder.requests.size() == 2U) {
        require(recorder.requests[0].command == WindowCapsuleCommand::begin_move &&
                    recorder.requests[0].origin == (Point{62.0, 10.0}) &&
                    recorder.requests[0].point == (Point{102.0, 52.0}),
                "the move begins at the press, in the capsule's coordinates");
        require(recorder.requests[1].command == WindowCapsuleCommand::finish_move &&
                    recorder.requests[1].snap == WindowCapsuleSnap::none,
                "and ends floating: a program chooses any snap itself");
    }
    require(capsule.look().arm == WindowCapsuleArm::idle, "a move arms nothing");
}

void test_non_interactive_control() {
    CapsuleWindow made = capsule_window();
    WindowCapsule& capsule = *made.capsule;
    RequestRecorder recorder;
    on(capsule.requested(), recorder, &RequestRecorder::record);
    capsule.set_interactive(false);
    require(!capsule.hit_test_local(Point{38.0, 10.0}), "a capsule that only shows a look is never hit");
    require(!capsule.on_semantic_child_action("root.capsule.help", SemanticAction::press, ""),
            "and refuses actions");
    send(*made.window, PointerAction::down, Point{70.0, 18.0});
    send(*made.window, PointerAction::up, Point{70.0, 18.0});
    require(recorder.requests.empty(), "the pointer passes through it");
    const std::vector<SemanticNode> children = capsule.semantic_virtual_children();
    require(children.size() == 3U && !has_semantic_state(children[0].states, SemanticState::enabled),
            "its buttons are not enabled");
    WindowCapsuleLook shown{};
    shown.arm = WindowCapsuleArm::close;
    capsule.set_look(shown);
    require(capsule.look().arm == WindowCapsuleArm::close && capsule.semantic_virtual_children()[2].name == "Close",
            "it shows the look it is given, Close while the X shows");
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
        test_no_snap_without_extent();
        test_control_sections_and_anatomy();
        test_control_requests();
        test_non_interactive_control();
    } catch (const std::exception& error) {
        std::cerr << "window capsule tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "window capsule tests passed\n";
    return EXIT_SUCCESS;
}
