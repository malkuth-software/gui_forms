// Control's border (Control::set_active_border, active_border.hpp): the
// setter, the paint after content and the paint-gated timer of a moving
// pose.

#include "control_active_border.hpp"

#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>

namespace gui_forms {

void Control::set_active_border(stxlib::Optional<ActiveBorder> border) {
    require_mutable();
    if (border.present() && !active_border_valid(*border.value())) {
        throw std::invalid_argument(
            "an active border needs a width in (0, 8], an offset within 8, a corner radius in [0, 64] and a "
            "frequency of at least 0");
    }
    if (border.absent()) {
        if (!active_border_) return;
        (*active_border_).timer.stop();
        active_border_.reset();
        invalidate(Dirty::paint);
        return;
    }
    if (active_border_ && (*active_border_).border == *border.value()) return;
    if (!active_border_) active_border_ = std::make_unique<ActiveBorderState>();
    ActiveBorderState& state = *active_border_;
    state.border = *border.value();
    // The next paint resolves the new pose and restarts its motion lit.
    state.timer.stop();
    state.pose.reset();
    invalidate(Dirty::paint);
}

stxlib::Optional<ActiveBorder> Control::active_border() const {
    if (!active_border_) return stxlib::Optional<ActiveBorder>::none();
    const stxlib::Optional<ActiveBorder> border = stxlib::Optional<ActiveBorder>::of((*active_border_).border);
    return border;
}

stxlib::Optional<ActiveBorderPose> Control::active_border_pose() const noexcept {
    if (!active_border_) return stxlib::Optional<ActiveBorderPose>::none();
    return (*active_border_).pose;
}

bool Control::active_border_animating() const noexcept {
    const bool running = active_border_ && (*active_border_).timer.started();
    return running;
}

AnimationTickResult Control::ActiveBorderTick::operator()(FrameTime now) const {
    const std::shared_ptr<Control> live = control.lock();
    const bool ticked = live && (*live).on_active_border_tick(now);
    const AnimationTickResult result = ticked ? AnimationTickResult::damaged : AnimationTickResult::unchanged;
    return result;
}

bool Control::on_active_border_tick(FrameTime now) {
    if (!active_border_) return false;
    (*active_border_).shown = now;
    invalidate(Dirty::paint);
    return true;
}

void Control::paint_active_border(Painter& painter) {
    if (!active_border_) return;
    ActiveBorderState& state = *active_border_;
    // The timer waits for this paint after each damaging tick.
    state.timer.note_painted();
    const BasicControlStyle& style = effective_theme().basic_style();
    ActiveBorderConditions conditions{};
    conditions.theme_colour = style.accent;
    conditions.high_contrast_colour = style.text;
    conditions.rest = true;
    if (surface_root_ != nullptr) {
        const SurfaceRoot& root = *surface_root_;
        // Reduced motion, an occluded window or the motion switch off.
        conditions.rest = root.motion_clock().ambient_suppressed();
        conditions.high_contrast = root.presentation_settings().high_contrast;
    }
    const ActiveBorderPose pose = resolve_active_border(state.border, conditions);
    const bool moving = pose.motion != ActiveBorderMotion::steady;
    const bool changed = !state.pose || !(*state.pose == pose);
    if (!moving) {
        state.timer.stop();
    } else if (changed || !state.timer.started()) {
        // The motion starts lit, now.
        const FrameTime now = surface_root_ != nullptr ? (*surface_root_).motion_clock().now() : FrameClock::now();
        state.start = now;
        state.shown = now;
        const std::weak_ptr<Control> observed = shared_from_this();
        state.timer.start(*this, active_border_interval(pose), ActiveBorderTick{observed});
    }
    state.pose = pose;
    const std::chrono::duration<double> elapsed = state.shown - state.start;
    const Color colour = active_border_colour_at(pose, elapsed.count());
    if (colour.alpha == 0U) return;
    ActiveBorder drawn = state.border;
    drawn.width = pose.width;
    const ActiveBorderPath path = active_border_path(drawn, Size{arranged_bounds_.width, arranged_bounds_.height});
    if (path.rect.width <= 0.0 || path.rect.height <= 0.0) return;
    if (path.radius > 0.0) {
        painter.stroke_rounded_rect(path.rect, path.radius, colour, pose.width);
    } else {
        painter.stroke_rect(path.rect, colour, pose.width);
    }
}

} // namespace gui_forms
