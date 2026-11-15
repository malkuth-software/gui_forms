// The window capsule's gesture model (surface_capsule.hpp).
#include "surface_capsule.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::host::surface_detail {
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

void SurfaceCapsuleGesture::set_measures(const SurfaceCapsuleMeasures& measures) noexcept {
    const SurfaceCapsuleMeasures defaults{};
    measures_.slop = measure_or(measures.slop, defaults.slop);
    measures_.minimize_distance = measure_or(measures.minimize_distance, defaults.minimize_distance);
    measures_.snap_edge = measure_or(measures.snap_edge, defaults.snap_edge);
    measures_.snap_release = measure_or(measures.snap_release, defaults.snap_release);
    measures_.animation_milliseconds = measure_or(measures.animation_milliseconds, defaults.animation_milliseconds);
    measures_.nudge = measure_or(measures.nudge, defaults.nudge);
}

void SurfaceCapsuleGesture::reset() noexcept {
    window_ = {};
    arm_ = SurfaceCapsuleArm::idle;
    mode_ = Mode::idle;
    pressed_ = SurfaceCapsuleSection::none;
    hovered_ = SurfaceCapsuleSection::none;
    focused_ = SurfaceCapsuleSection::none;
    keyboard_ = false;
    origin_ = {};
    snap_ = SurfaceCapsuleSnap::none;
    offset_live_ = 0.0;
    offset_shown_ = 0.0;
    intent_shown_ = 0.0;
}

double SurfaceCapsuleGesture::intent_target() const noexcept {
    switch (arm_) {
    case SurfaceCapsuleArm::left:
    case SurfaceCapsuleArm::right:
        return 0.5;
    case SurfaceCapsuleArm::close:
        return 1.0;
    case SurfaceCapsuleArm::idle:
    default:
        return 0.0;
    }
}

void SurfaceCapsuleGesture::set_arm(SurfaceCapsuleArm arm, SurfaceCapsuleOutcome& outcome) noexcept {
    if (arm == arm_) return;
    arm_ = arm;
    outcome.redraw = true;
}

void SurfaceCapsuleGesture::click(SurfaceCapsuleSection section, SurfaceCapsuleOutcome& outcome) noexcept {
    outcome.window = window_;
    if (arm_ == SurfaceCapsuleArm::close) {
        // The whole capsule is the X.
        outcome.command = SurfaceCapsuleCommand::close;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        return;
    }
    switch (section) {
    case SurfaceCapsuleSection::left:
        set_arm(arm_ == SurfaceCapsuleArm::right ? SurfaceCapsuleArm::close : SurfaceCapsuleArm::left, outcome);
        break;
    case SurfaceCapsuleSection::right:
        set_arm(arm_ == SurfaceCapsuleArm::left ? SurfaceCapsuleArm::close : SurfaceCapsuleArm::right, outcome);
        break;
    case SurfaceCapsuleSection::help:
        outcome.command = SurfaceCapsuleCommand::open_help;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        break;
    case SurfaceCapsuleSection::none:
    default:
        break;
    }
}

SurfaceCapsuleSnap SurfaceCapsuleGesture::next_snap(Point point, Size extent) const noexcept {
    const double edge = measures_.snap_edge;
    const double held = measures_.snap_edge + measures_.snap_release;
    const double width = extent.width;
    // An armed snap holds through the release distance (hysteresis).
    if (snap_ == SurfaceCapsuleSnap::left_half && point.x <= held) return SurfaceCapsuleSnap::left_half;
    if (snap_ == SurfaceCapsuleSnap::right_half && point.x >= width - held) return SurfaceCapsuleSnap::right_half;
    if (point.x <= edge) return SurfaceCapsuleSnap::left_half;
    if (point.x >= width - edge) return SurfaceCapsuleSnap::right_half;
    return SurfaceCapsuleSnap::none;
}

SurfaceCapsuleOutcome SurfaceCapsuleGesture::press(HostWindowId window, SurfaceCapsuleSection section, Point point,
                                                   std::uint32_t clicks) noexcept {
    SurfaceCapsuleOutcome outcome{};
    if (section == SurfaceCapsuleSection::none || !window.valid() || !finite_point(point)) return outcome;
    if (window != window_) reset();
    window_ = window;
    outcome.window = window;
    outcome.handled = true;
    outcome.redraw = true;
    // The pointer takes over from the keyboard.
    keyboard_ = false;
    focused_ = SurfaceCapsuleSection::none;
    if (section == SurfaceCapsuleSection::left && clicks == 2U && arm_ != SurfaceCapsuleArm::close) {
        // The double click's first click armed the left half; the double
        // click maximises instead and disarms.
        set_arm(SurfaceCapsuleArm::idle, outcome);
        outcome.command = SurfaceCapsuleCommand::toggle_maximize;
        mode_ = Mode::idle;
        pressed_ = SurfaceCapsuleSection::none;
        return outcome;
    }
    mode_ = Mode::pressed;
    pressed_ = section;
    origin_ = point;
    offset_live_ = 0.0;
    return outcome;
}

SurfaceCapsuleOutcome SurfaceCapsuleGesture::drag(Point point, Size extent) noexcept {
    SurfaceCapsuleOutcome outcome{};
    outcome.window = window_;
    if (!finite_point(point)) return outcome;
    const double dx = point.x - origin_.x;
    const double dy = point.y - origin_.y;
    switch (mode_) {
    case Mode::pressed: {
        outcome.handled = true;
        if (std::max(std::abs(dx), std::abs(dy)) <= measures_.slop) return outcome;
        if (arm_ == SurfaceCapsuleArm::close || pressed_ == SurfaceCapsuleSection::help) {
            mode_ = Mode::abandoned;
            return outcome;
        }
        if (pressed_ == SurfaceCapsuleSection::left) {
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
        outcome.command = SurfaceCapsuleCommand::begin_move;
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
        const SurfaceCapsuleSnap snap = next_snap(point, extent);
        outcome.redraw = snap != snap_;
        snap_ = snap;
        outcome.command = SurfaceCapsuleCommand::move;
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

SurfaceCapsuleOutcome SurfaceCapsuleGesture::release(SurfaceCapsuleSection section) noexcept {
    SurfaceCapsuleOutcome outcome{};
    outcome.window = window_;
    if (mode_ == Mode::idle) return outcome;
    outcome.handled = true;
    outcome.redraw = true;
    const Mode ended = mode_;
    const SurfaceCapsuleSection pressed = pressed_;
    mode_ = Mode::idle;
    pressed_ = SurfaceCapsuleSection::none;
    switch (ended) {
    case Mode::pressed: {
        // A click: released on the pressed section, or anywhere on the X.
        const bool on_x = arm_ == SurfaceCapsuleArm::close && section != SurfaceCapsuleSection::none;
        if (section == pressed || on_x) click(section, outcome);
        break;
    }
    case Mode::minimizing:
        if (offset_live_ >= measures_.minimize_distance) {
            outcome.command = SurfaceCapsuleCommand::minimize;
            set_arm(SurfaceCapsuleArm::idle, outcome);
        }
        // The shown offset springs back (advance).
        offset_live_ = 0.0;
        break;
    case Mode::moving:
        outcome.command = SurfaceCapsuleCommand::finish_move;
        outcome.snap = snap_;
        snap_ = SurfaceCapsuleSnap::none;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        break;
    case Mode::abandoned:
    case Mode::idle:
    default:
        break;
    }
    return outcome;
}

SurfaceCapsuleOutcome SurfaceCapsuleGesture::cancel() noexcept {
    SurfaceCapsuleOutcome outcome{};
    outcome.window = window_;
    if (mode_ == Mode::idle) return outcome;
    outcome.handled = true;
    outcome.redraw = true;
    if (mode_ == Mode::moving) outcome.command = SurfaceCapsuleCommand::cancel_move;
    mode_ = Mode::idle;
    pressed_ = SurfaceCapsuleSection::none;
    snap_ = SurfaceCapsuleSnap::none;
    offset_live_ = 0.0;
    return outcome;
}

SurfaceCapsuleOutcome SurfaceCapsuleGesture::hover(HostWindowId window, SurfaceCapsuleSection section) noexcept {
    SurfaceCapsuleOutcome outcome{};
    // A press keeps its capsule until the release.
    if (mode_ != Mode::idle) return outcome;
    if (section != SurfaceCapsuleSection::none && window.valid() && window != window_) {
        const bool kept = keyboard_ || arm_ != SurfaceCapsuleArm::idle;
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
    if (section == SurfaceCapsuleSection::none && !keyboard_) set_arm(SurfaceCapsuleArm::idle, outcome);
    return outcome;
}

SurfaceCapsuleOutcome SurfaceCapsuleGesture::focus(HostWindowId window) noexcept {
    SurfaceCapsuleOutcome outcome{};
    if (!window.valid()) return outcome;
    if (window != window_) reset();
    window_ = window;
    keyboard_ = true;
    focused_ = SurfaceCapsuleSection::left;
    outcome.window = window;
    outcome.handled = true;
    outcome.redraw = true;
    return outcome;
}

SurfaceCapsuleOutcome SurfaceCapsuleGesture::key(std::uint32_t physical_key, Modifier modifiers) noexcept {
    SurfaceCapsuleOutcome outcome{};
    outcome.window = window_;
    if (!keyboard_ || mode_ != Mode::idle) return outcome;
    outcome.handled = true;
    const bool plain = modifiers == Modifier::none;
    const bool shift = modifiers == Modifier::shift;
    const bool control = modifiers == Modifier::control;
    const bool arrow = physical_key == PhysicalKey::left || physical_key == PhysicalKey::right ||
                       physical_key == PhysicalKey::up || physical_key == PhysicalKey::down;
    if (physical_key == PhysicalKey::escape && plain) {
        if (arm_ != SurfaceCapsuleArm::idle) {
            set_arm(SurfaceCapsuleArm::idle, outcome);
        } else {
            keyboard_ = false;
            focused_ = SurfaceCapsuleSection::none;
            outcome.redraw = true;
        }
        return outcome;
    }
    if (plain && (physical_key == PhysicalKey::left || physical_key == PhysicalKey::right)) {
        const bool forward = physical_key == PhysicalKey::right;
        SurfaceCapsuleSection next = focused_;
        if (focused_ == SurfaceCapsuleSection::left) next = forward ? SurfaceCapsuleSection::help : focused_;
        if (focused_ == SurfaceCapsuleSection::help)
            next = forward ? SurfaceCapsuleSection::right : SurfaceCapsuleSection::left;
        if (focused_ == SurfaceCapsuleSection::right) next = forward ? focused_ : SurfaceCapsuleSection::help;
        outcome.redraw = next != focused_;
        focused_ = next;
        return outcome;
    }
    if (plain && physical_key == PhysicalKey::up) {
        outcome.command = SurfaceCapsuleCommand::toggle_maximize;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        return outcome;
    }
    if (plain && physical_key == PhysicalKey::down) {
        outcome.command = SurfaceCapsuleCommand::minimize;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        return outcome;
    }
    if (shift && (physical_key == PhysicalKey::left || physical_key == PhysicalKey::right ||
                  physical_key == PhysicalKey::down)) {
        outcome.command = SurfaceCapsuleCommand::tile;
        outcome.snap = SurfaceCapsuleSnap::none;
        if (physical_key == PhysicalKey::left) outcome.snap = SurfaceCapsuleSnap::left_half;
        if (physical_key == PhysicalKey::right) outcome.snap = SurfaceCapsuleSnap::right_half;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        return outcome;
    }
    if (control && arrow) {
        const double step = measures_.nudge;
        outcome.command = SurfaceCapsuleCommand::nudge;
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
        if (arm_ == SurfaceCapsuleArm::left) focused_ = SurfaceCapsuleSection::right;
        if (arm_ == SurfaceCapsuleArm::right) focused_ = SurfaceCapsuleSection::left;
        outcome.redraw = true;
        return outcome;
    }
    if (plain && physical_key == PhysicalKey::f1) {
        outcome.command = SurfaceCapsuleCommand::open_help;
        set_arm(SurfaceCapsuleArm::idle, outcome);
        return outcome;
    }
    // Any other key leaves the capsule and goes to the window.
    keyboard_ = false;
    focused_ = SurfaceCapsuleSection::none;
    set_arm(SurfaceCapsuleArm::idle, outcome);
    outcome.handled = false;
    outcome.redraw = true;
    return outcome;
}

bool SurfaceCapsuleGesture::forget(HostWindowId window) noexcept {
    if (!window.valid() || window != window_) return false;
    reset();
    return true;
}

bool SurfaceCapsuleGesture::advance(double milliseconds, bool reduced_motion) noexcept {
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

bool SurfaceCapsuleGesture::animating() const noexcept {
    const double offset_goal = mode_ == Mode::minimizing ? offset_live_ : 0.0;
    const bool moving_values = intent_shown_ != intent_target() || offset_shown_ != offset_goal;
    return moving_values;
}

bool SurfaceCapsuleGesture::title_shown() const noexcept {
    const bool shown = hovered_ == SurfaceCapsuleSection::help && mode_ == Mode::idle &&
                       arm_ != SurfaceCapsuleArm::close;
    return shown;
}

std::string_view surface_capsule_help_text() noexcept {
    const std::string_view text =
        "The window capsule. Left half: double-click to maximise or restore; drag it down to minimise. "
        "Question mark: this help; hover it to see the window's title. Right half: drag to move the window, "
        "to the left or right edge for half the screen, to the middle to float. To close, click the left half, "
        "then the right half, then the X they become. Keyboard: Alt+Space, then the arrows, Enter and Escape.";
    return text;
}

} // namespace gui_forms::host::surface_detail
