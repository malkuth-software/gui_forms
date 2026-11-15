// The window capsule's gesture model (gui_forms/controls/window_capsule.hpp).
#include "gui_forms/controls/window_capsule.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {
namespace {

double measure_or(double value, double fallback) noexcept {
    const double result = std::isfinite(value) && value >= 0.0 ? value : fallback;
    return result;
}

bool finite_point(Point point) noexcept {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    return finite;
}

// `shown` moved toward `target` by at most `step`.
double approach(double shown, double target, double step) noexcept {
    if (shown < target) return std::min(target, shown + step);
    const double result = std::max(target, shown - step);
    return result;
}

} // namespace

void WindowCapsuleGesture::set_measures(const WindowCapsuleMeasures& measures) noexcept {
    const WindowCapsuleMeasures defaults{};
    measures_.slop = measure_or(measures.slop, defaults.slop);
    measures_.minimize_distance = measure_or(measures.minimize_distance, defaults.minimize_distance);
    measures_.snap_edge = measure_or(measures.snap_edge, defaults.snap_edge);
    measures_.snap_release = measure_or(measures.snap_release, defaults.snap_release);
    measures_.animation_milliseconds = measure_or(measures.animation_milliseconds, defaults.animation_milliseconds);
    measures_.nudge = measure_or(measures.nudge, defaults.nudge);
}

void WindowCapsuleGesture::reset() noexcept {
    window_ = {};
    arm_ = WindowCapsuleArm::idle;
    mode_ = Mode::idle;
    pressed_ = WindowCapsuleSection::none;
    hovered_ = WindowCapsuleSection::none;
    focused_ = WindowCapsuleSection::none;
    keyboard_ = false;
    origin_ = {};
    snap_ = WindowCapsuleSnap::none;
    offset_live_ = 0.0;
    offset_shown_ = 0.0;
    intent_shown_ = 0.0;
}

double WindowCapsuleGesture::intent_target() const noexcept {
    switch (arm_) {
    case WindowCapsuleArm::left:
    case WindowCapsuleArm::right:
        return 0.5;
    case WindowCapsuleArm::close:
        return 1.0;
    case WindowCapsuleArm::idle:
    default:
        return 0.0;
    }
}

void WindowCapsuleGesture::set_arm(WindowCapsuleArm arm, WindowCapsuleOutcome& outcome) noexcept {
    if (arm == arm_) return;
    arm_ = arm;
    outcome.redraw = true;
}

void WindowCapsuleGesture::click(WindowCapsuleSection section, WindowCapsuleOutcome& outcome) noexcept {
    outcome.window = window_;
    if (arm_ == WindowCapsuleArm::close) {
        // The whole capsule is the X.
        outcome.command = WindowCapsuleCommand::close;
        set_arm(WindowCapsuleArm::idle, outcome);
        return;
    }
    switch (section) {
    case WindowCapsuleSection::left:
        set_arm(arm_ == WindowCapsuleArm::right ? WindowCapsuleArm::close : WindowCapsuleArm::left, outcome);
        break;
    case WindowCapsuleSection::right:
        set_arm(arm_ == WindowCapsuleArm::left ? WindowCapsuleArm::close : WindowCapsuleArm::right, outcome);
        break;
    case WindowCapsuleSection::help:
        outcome.command = WindowCapsuleCommand::open_help;
        set_arm(WindowCapsuleArm::idle, outcome);
        break;
    case WindowCapsuleSection::none:
    default:
        break;
    }
}

WindowCapsuleSnap WindowCapsuleGesture::next_snap(Point point, Size extent) const noexcept {
    const double edge = measures_.snap_edge;
    const double held = measures_.snap_edge + measures_.snap_release;
    const double width = extent.width;
    // An empty extent has no snap targets.
    if (!(width > 0.0) || !std::isfinite(width)) return WindowCapsuleSnap::none;
    // An armed snap holds through the release distance (hysteresis).
    if (snap_ == WindowCapsuleSnap::left_half && point.x <= held) return WindowCapsuleSnap::left_half;
    if (snap_ == WindowCapsuleSnap::right_half && point.x >= width - held) return WindowCapsuleSnap::right_half;
    if (point.x <= edge) return WindowCapsuleSnap::left_half;
    if (point.x >= width - edge) return WindowCapsuleSnap::right_half;
    return WindowCapsuleSnap::none;
}

WindowCapsuleOutcome WindowCapsuleGesture::press(HostWindowId window, WindowCapsuleSection section, Point point,
                                                   std::uint32_t clicks) noexcept {
    WindowCapsuleOutcome outcome{};
    if (section == WindowCapsuleSection::none || !window.valid() || !finite_point(point)) return outcome;
    if (window != window_) reset();
    window_ = window;
    outcome.window = window;
    outcome.handled = true;
    outcome.redraw = true;
    // The pointer takes over from the keyboard.
    keyboard_ = false;
    focused_ = WindowCapsuleSection::none;
    if (section == WindowCapsuleSection::left && clicks == 2U && arm_ != WindowCapsuleArm::close) {
        // The double click's first click armed the left half; the double
        // click maximises instead and disarms.
        set_arm(WindowCapsuleArm::idle, outcome);
        outcome.command = WindowCapsuleCommand::toggle_maximize;
        mode_ = Mode::idle;
        pressed_ = WindowCapsuleSection::none;
        return outcome;
    }
    mode_ = Mode::pressed;
    pressed_ = section;
    origin_ = point;
    offset_live_ = 0.0;
    return outcome;
}

WindowCapsuleOutcome WindowCapsuleGesture::drag(Point point, Size extent) noexcept {
    WindowCapsuleOutcome outcome{};
    outcome.window = window_;
    if (!finite_point(point)) return outcome;
    const double dx = point.x - origin_.x;
    const double dy = point.y - origin_.y;
    switch (mode_) {
    case Mode::pressed: {
        outcome.handled = true;
        if (std::max(std::abs(dx), std::abs(dy)) <= measures_.slop) return outcome;
        if (arm_ == WindowCapsuleArm::close || pressed_ == WindowCapsuleSection::help) {
            mode_ = Mode::abandoned;
            return outcome;
        }
        if (pressed_ == WindowCapsuleSection::left) {
            if (dy <= std::abs(dx)) {
                mode_ = Mode::abandoned;
                return outcome;
            }
            mode_ = Mode::minimizing;
            offset_live_ = std::clamp(dy, 0.0, measures_.minimize_distance);
            offset_shown_ = offset_live_;
            outcome.redraw = true;
            return outcome;
        }
        mode_ = Mode::moving;
        snap_ = next_snap(point, extent);
        outcome.command = WindowCapsuleCommand::begin_move;
        outcome.origin = origin_;
        outcome.point = point;
        outcome.snap = snap_;
        outcome.redraw = true;
        return outcome;
    }
    case Mode::minimizing: {
        outcome.handled = true;
        const double offset = std::clamp(dy, 0.0, measures_.minimize_distance);
        outcome.redraw = offset != offset_shown_;
        offset_live_ = offset;
        offset_shown_ = offset;
        return outcome;
    }
    case Mode::moving: {
        outcome.handled = true;
        const WindowCapsuleSnap snap = next_snap(point, extent);
        outcome.redraw = snap != snap_;
        snap_ = snap;
        outcome.command = WindowCapsuleCommand::move;
        outcome.point = point;
        outcome.snap = snap_;
        return outcome;
    }
    case Mode::abandoned:
        outcome.handled = true;
        return outcome;
    case Mode::idle:
    default:
        return outcome;
    }
}

WindowCapsuleOutcome WindowCapsuleGesture::release(WindowCapsuleSection section) noexcept {
    WindowCapsuleOutcome outcome{};
    outcome.window = window_;
    if (mode_ == Mode::idle) return outcome;
    outcome.handled = true;
    outcome.redraw = true;
    const Mode ended = mode_;
    const WindowCapsuleSection pressed = pressed_;
    mode_ = Mode::idle;
    pressed_ = WindowCapsuleSection::none;
    switch (ended) {
    case Mode::pressed: {
        // A click: released on the pressed section, or anywhere on the X.
        const bool on_x = arm_ == WindowCapsuleArm::close && section != WindowCapsuleSection::none;
        if (section == pressed || on_x) click(section, outcome);
        break;
    }
    case Mode::minimizing:
        if (offset_live_ >= measures_.minimize_distance) {
            outcome.command = WindowCapsuleCommand::minimize;
            set_arm(WindowCapsuleArm::idle, outcome);
        }
        // The shown offset springs back (advance).
        offset_live_ = 0.0;
        break;
    case Mode::moving:
        outcome.command = WindowCapsuleCommand::finish_move;
        outcome.snap = snap_;
        snap_ = WindowCapsuleSnap::none;
        set_arm(WindowCapsuleArm::idle, outcome);
        break;
    case Mode::abandoned:
    case Mode::idle:
    default:
        break;
    }
    return outcome;
}

WindowCapsuleOutcome WindowCapsuleGesture::cancel() noexcept {
    WindowCapsuleOutcome outcome{};
    outcome.window = window_;
    if (mode_ == Mode::idle) return outcome;
    outcome.handled = true;
    outcome.redraw = true;
    if (mode_ == Mode::moving) outcome.command = WindowCapsuleCommand::cancel_move;
    mode_ = Mode::idle;
    pressed_ = WindowCapsuleSection::none;
    snap_ = WindowCapsuleSnap::none;
    offset_live_ = 0.0;
    return outcome;
}

WindowCapsuleOutcome WindowCapsuleGesture::hover(HostWindowId window, WindowCapsuleSection section) noexcept {
    WindowCapsuleOutcome outcome{};
    // A press keeps its capsule until the release.
    if (mode_ != Mode::idle) return outcome;
    if (section != WindowCapsuleSection::none && window.valid() && window != window_) {
        const bool kept = keyboard_ || arm_ != WindowCapsuleArm::idle;
        if (kept || window_.valid()) outcome.redraw = true;
        reset();
        window_ = window;
    }
    outcome.window = window_;
    if (section != hovered_) {
        hovered_ = section;
        outcome.redraw = true;
    }
    // Leaving the capsule disarms it, unless the keyboard holds it.
    if (section == WindowCapsuleSection::none && !keyboard_) set_arm(WindowCapsuleArm::idle, outcome);
    return outcome;
}

WindowCapsuleOutcome WindowCapsuleGesture::focus(HostWindowId window) noexcept {
    WindowCapsuleOutcome outcome{};
    if (!window.valid()) return outcome;
    if (window != window_) reset();
    window_ = window;
    keyboard_ = true;
    focused_ = WindowCapsuleSection::left;
    outcome.window = window;
    outcome.handled = true;
    outcome.redraw = true;
    return outcome;
}

WindowCapsuleOutcome WindowCapsuleGesture::key(std::uint32_t physical_key, Modifier modifiers) noexcept {
    WindowCapsuleOutcome outcome{};
    outcome.window = window_;
    if (!keyboard_ || mode_ != Mode::idle) return outcome;
    outcome.handled = true;
    const bool plain = modifiers == Modifier::none;
    const bool shift = modifiers == Modifier::shift;
    const bool control = modifiers == Modifier::control;
    const bool arrow = physical_key == PhysicalKey::left || physical_key == PhysicalKey::right ||
                       physical_key == PhysicalKey::up || physical_key == PhysicalKey::down;
    if (physical_key == PhysicalKey::escape && plain) {
        if (arm_ != WindowCapsuleArm::idle) {
            set_arm(WindowCapsuleArm::idle, outcome);
        } else {
            keyboard_ = false;
            focused_ = WindowCapsuleSection::none;
            outcome.redraw = true;
        }
        return outcome;
    }
    if (plain && (physical_key == PhysicalKey::left || physical_key == PhysicalKey::right)) {
        const bool forward = physical_key == PhysicalKey::right;
        WindowCapsuleSection next = focused_;
        if (focused_ == WindowCapsuleSection::left) next = forward ? WindowCapsuleSection::help : focused_;
        if (focused_ == WindowCapsuleSection::help)
            next = forward ? WindowCapsuleSection::right : WindowCapsuleSection::left;
        if (focused_ == WindowCapsuleSection::right) next = forward ? focused_ : WindowCapsuleSection::help;
        outcome.redraw = next != focused_;
        focused_ = next;
        return outcome;
    }
    if (plain && physical_key == PhysicalKey::up) {
        outcome.command = WindowCapsuleCommand::toggle_maximize;
        set_arm(WindowCapsuleArm::idle, outcome);
        return outcome;
    }
    if (plain && physical_key == PhysicalKey::down) {
        outcome.command = WindowCapsuleCommand::minimize;
        set_arm(WindowCapsuleArm::idle, outcome);
        return outcome;
    }
    if (shift && (physical_key == PhysicalKey::left || physical_key == PhysicalKey::right ||
                  physical_key == PhysicalKey::down)) {
        outcome.command = WindowCapsuleCommand::tile;
        outcome.snap = WindowCapsuleSnap::none;
        if (physical_key == PhysicalKey::left) outcome.snap = WindowCapsuleSnap::left_half;
        if (physical_key == PhysicalKey::right) outcome.snap = WindowCapsuleSnap::right_half;
        set_arm(WindowCapsuleArm::idle, outcome);
        return outcome;
    }
    if (control && arrow) {
        const double step = measures_.nudge;
        outcome.command = WindowCapsuleCommand::nudge;
        if (physical_key == PhysicalKey::left) outcome.offset = Point{-step, 0.0};
        if (physical_key == PhysicalKey::right) outcome.offset = Point{step, 0.0};
        if (physical_key == PhysicalKey::up) outcome.offset = Point{0.0, -step};
        if (physical_key == PhysicalKey::down) outcome.offset = Point{0.0, step};
        return outcome;
    }
    if (plain && (physical_key == PhysicalKey::enter || physical_key == PhysicalKey::space)) {
        click(focused_, outcome);
        // Arming one half moves the focus to the other, so Enter three
        // times closes, as three clicks do.
        if (arm_ == WindowCapsuleArm::left) focused_ = WindowCapsuleSection::right;
        if (arm_ == WindowCapsuleArm::right) focused_ = WindowCapsuleSection::left;
        outcome.redraw = true;
        return outcome;
    }
    if (plain && physical_key == PhysicalKey::f1) {
        outcome.command = WindowCapsuleCommand::open_help;
        set_arm(WindowCapsuleArm::idle, outcome);
        return outcome;
    }
    // Any other key leaves the capsule and goes to the window.
    keyboard_ = false;
    focused_ = WindowCapsuleSection::none;
    set_arm(WindowCapsuleArm::idle, outcome);
    outcome.handled = false;
    outcome.redraw = true;
    return outcome;
}

bool WindowCapsuleGesture::forget(HostWindowId window) noexcept {
    if (!window.valid() || window != window_) return false;
    reset();
    return true;
}

bool WindowCapsuleGesture::advance(double milliseconds, bool reduced_motion) noexcept {
    const double intent_goal = intent_target();
    const double offset_goal = mode_ == Mode::minimizing ? offset_live_ : 0.0;
    const double before_intent = intent_shown_;
    const double before_offset = offset_shown_;
    const double duration = measures_.animation_milliseconds;
    const bool instant = reduced_motion || !(duration > 0.0) || !std::isfinite(milliseconds);
    if (instant) {
        intent_shown_ = intent_goal;
        offset_shown_ = offset_goal;
    } else {
        // The whole range of each value takes `duration`.
        const double fraction = std::max(0.0, milliseconds) / duration;
        intent_shown_ = approach(intent_shown_, intent_goal, fraction);
        offset_shown_ = approach(offset_shown_, offset_goal, fraction * measures_.minimize_distance);
    }
    const bool changed = before_intent != intent_shown_ || before_offset != offset_shown_;
    return changed;
}

bool WindowCapsuleGesture::animating() const noexcept {
    const double offset_goal = mode_ == Mode::minimizing ? offset_live_ : 0.0;
    const bool moving_values = intent_shown_ != intent_target() || offset_shown_ != offset_goal;
    return moving_values;
}

bool WindowCapsuleGesture::title_shown() const noexcept {
    const bool shown = hovered_ == WindowCapsuleSection::help && mode_ == Mode::idle &&
                       arm_ != WindowCapsuleArm::close;
    return shown;
}

WindowCapsuleLook window_capsule_look(const WindowCapsuleGesture& gesture, HostWindowId window) noexcept {
    WindowCapsuleLook look{};
    if (!window.valid() || gesture.window() != window) return look;
    look.arm = gesture.arm();
    look.intent = gesture.intent();
    look.minimize_offset = gesture.minimize_offset();
    look.hovered = gesture.hovered();
    look.pressed = gesture.pressed();
    look.focused = gesture.keyboard() ? gesture.focused_section() : WindowCapsuleSection::none;
    return look;
}

std::string_view window_capsule_help_text() noexcept {
    const std::string_view text =
        "The window capsule. Left half: double-click to maximise or restore; drag it down to minimise. "
        "Question mark: this help; hover it to see the window's title. Right half: drag to move the window, "
        "to the left or right edge for half the screen, to the middle to float. To close, click the left half, "
        "then the right half, then the X they become. Keyboard: Alt+Space, then the arrows, Enter and Escape.";
    return text;
}

} // namespace gui_forms
