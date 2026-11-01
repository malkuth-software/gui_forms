#include "splitter_grip.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

namespace {

constexpr double expanded_actuator_width = 12.0;
constexpr double expanded_actuator_extent = 42.0;
constexpr double near_actuator_width = 9.0;
constexpr double near_actuator_extent = 34.0;
constexpr double rest_actuator_width = 7.0;
constexpr double rest_actuator_extent = 28.0;
constexpr Point engaged_shadow_offset{0.0, 2.0};
constexpr double engaged_shadow_blur = 3.0;
constexpr double engaged_shadow_reach = engaged_shadow_blur * 3.0;

AnimationSpec seam_transition_spec(FrameInterval duration) {
    AnimationSpec specification;
    specification.duration = duration;
    specification.easing = EasingCurve::ease_out;
    return specification;
}

} // namespace

SplitterGrip::SplitterGrip(StableId stable_id)
    : Control(std::move(stable_id)),
      transition_timeline_(seam_transition_spec(transition_duration_)) {
    set_focusable(true);
}

void SplitterGrip::set_orientation(Orientation orientation) {
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    set_cursor(orientation == Orientation::vertical
                   ? CursorKind::resize_horizontal
                   : CursorKind::resize_vertical);
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void SplitterGrip::set_visible_geometry(double origin, double width) {
    if (visible_origin_ == origin && visible_width_ == width) return;
    visible_origin_ = origin;
    visible_width_ = width;
    invalidate(Dirty::paint);
}

void SplitterGrip::set_collapse_appearance(SplitFixedPanel panel,
                                            bool collapsed) {
    if (collapse_panel_ == panel && collapsed_ == collapsed) return;
    collapse_panel_ = panel;
    collapsed_ = collapsed;
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void SplitterGrip::set_dragging(const bool dragging) {
    if (dragging_ == dragging) return;
    dragging_ = dragging;
    retarget_transition();
    invalidate(Dirty::paint);
}

void SplitterGrip::set_actuator_pressed(const bool pressed) {
    if (actuator_pressed_ == pressed) return;
    actuator_pressed_ = pressed;
    retarget_transition();
    invalidate(Dirty::paint);
}

void SplitterGrip::set_transition_duration(FrameInterval duration) {
    require_mutable();
    if (duration < FrameInterval::zero() ||
        duration > std::chrono::milliseconds(1000)) {
        throw std::invalid_argument(
            "split seam transition duration must be between zero and 1000 milliseconds");
    }
    if (transition_duration_ == duration) return;
    transition_duration_ = duration;
    if (duration == FrameInterval::zero()) {
        complete_transition();
        invalidate(Dirty::paint);
        return;
    }
    transition_timeline_.set_specification(seam_transition_spec(duration));
    if (transition_active_) {
        transition_from_ = presented_actuator_;
        transition_progress_ = 0.0;
        transition_timeline_.start(FrameClock::now());
        update_transition_registration();
    }
}

void SplitterGrip::reset_interaction() noexcept {
    near_ = false;
    hot_ = false;
    dragging_ = false;
    actuator_pressed_ = false;
    focused_ = false;
    transition_frames_.stop();
    transition_active_ = false;
    transition_progress_ = 1.0;
    transition_to_ = target_actuator_size();
    transition_from_ = transition_to_;
    presented_actuator_ = transition_to_;
}

SplitSeamState SplitterGrip::seam_state() const noexcept {
    if (!effectively_enabled()) return SplitSeamState::disabled;
    if (dragging_ && has_pointer_capture()) return SplitSeamState::dragging;
    if (focused_) return SplitSeamState::focused;
    if (hot_) return SplitSeamState::hot;
    if (near_) return SplitSeamState::near;
    if (collapsed_) return SplitSeamState::collapsed;
    return SplitSeamState::idle;
}

Rect SplitterGrip::actuator_bounds() const noexcept {
    return actuator_bounds(presented_actuator_.width,
                           presented_actuator_.height);
}

Insets SplitterGrip::visual_outsets() const noexcept {
    // Theme and renderer damage law conservatively treats an outer shadow as
    // blur*3 plus its offset. Declare the union of that envelope and the
    // engaged actuator without widening layout or hit testing.
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const Rect tab = actuator_bounds(expanded_actuator_width,
                                     expanded_actuator_extent);
    const Rect shadow{
        tab.x + engaged_shadow_offset.x - engaged_shadow_reach,
        tab.y + engaged_shadow_offset.y - engaged_shadow_reach,
        tab.width + engaged_shadow_reach * 2.0,
        tab.height + engaged_shadow_reach * 2.0};
    const double left = std::min(tab.x, shadow.x);
    const double top = std::min(tab.y, shadow.y);
    const double right = std::max(tab.x + tab.width,
                                  shadow.x + shadow.width);
    const double bottom = std::max(tab.y + tab.height,
                                   shadow.y + shadow.height);
    return {std::max(0.0, -left), std::max(0.0, -top),
            std::max(0.0, right - bounds.width),
            std::max(0.0, bottom - bounds.height)};
}

void SplitterGrip::on_pointer(PointerEvent& event) {
    if (event.action != PointerAction::enter &&
        event.action != PointerAction::move &&
        event.action != PointerAction::leave) return;
    const bool near = event.action != PointerAction::leave;
    const bool hot = near && point_in_proximity(event.position);
    if (near_ == near && hot_ == hot) return;
    near_ = near;
    hot_ = hot;
    retarget_transition();
    invalidate(Dirty::paint);
}

void SplitterGrip::on_focus_changed(bool focused) {
    focused_ = focused;
    retarget_transition();
    invalidate(invalidation::focus);
}

void SplitterGrip::on_focus_cue_changed(bool) {
    if (!focused_) return;
    retarget_transition();
    invalidate(invalidation::focus);
}

Size SplitterGrip::target_actuator_size() const noexcept {
    // The owner publishes pressed/dragging immediately before capture in the
    // same routed event. Geometry may retarget from those retained flags; the
    // painted engaged state still requires actual capture below.
    const bool focus_cue = focused_ && window() != nullptr &&
        (*window()).focus_cue_visible();
    if (hot_ || dragging_ || actuator_pressed_ || focus_cue) {
        return {expanded_actuator_width, expanded_actuator_extent};
    }
    if (near_) return {near_actuator_width, near_actuator_extent};
    return {rest_actuator_width, rest_actuator_extent};
}

void SplitterGrip::complete_transition() noexcept {
    transition_frames_.stop();
    transition_active_ = false;
    transition_progress_ = 1.0;
    presented_actuator_ = transition_to_;
    transition_from_ = transition_to_;
}

void SplitterGrip::update_transition_registration() {
    transition_frames_.stop();
    if (!transition_active_ || window() == nullptr) return;
    const std::shared_ptr<SplitterGrip> self =
        std::static_pointer_cast<SplitterGrip>(shared_from_this());
    transition_frames_.start(*this, display_frame_interval, TransitionTick{self});
}

AnimationTickResult SplitterGrip::TransitionTick::operator()(FrameTime now) const {
    const std::shared_ptr<SplitterGrip> live = grip.lock();
    const AnimationTickResult result = live
        ? (*live).on_transition_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult SplitterGrip::on_transition_tick(FrameTime now) {
    // The actuator's stroke reaches half a pixel outside its bounds.
    const Rect before = actuator_bounds();
    advance_animation(now);
    const Rect after = actuator_bounds();
    const Rect moved = Rect::united(before, after);
    invalidate({moved.x - 1.0, moved.y - 1.0, moved.width + 2.0, moved.height + 2.0});
    return AnimationTickResult::damaged;
}

void SplitterGrip::retarget_transition() {
    const Size target = target_actuator_size();
    if (target == transition_to_ && transition_active_) return;
    transition_from_ = presented_actuator_;
    transition_to_ = target;
    const bool reduced = window() != nullptr &&
        (*window()).presentation_settings().reduced_motion;
    if (transition_from_ == transition_to_ || window() == nullptr || reduced ||
        transition_duration_ == FrameInterval::zero()) {
        complete_transition();
        return;
    }
    transition_progress_ = 0.0;
    transition_active_ = true;
    transition_timeline_.start(FrameClock::now());
    update_transition_registration();
}

void SplitterGrip::on_attached_to_window() {
    Control::on_attached_to_window();
    transition_to_ = target_actuator_size();
    transition_from_ = transition_to_;
    presented_actuator_ = transition_to_;
    transition_progress_ = 1.0;
    transition_active_ = false;
}

void SplitterGrip::on_detached_from_window() noexcept {
    transition_frames_.stop();
    transition_active_ = false;
    Control::on_detached_from_window();
}

void SplitterGrip::advance_animation(FrameTime now) {
    if (!transition_active_) return;
    if (window() != nullptr &&
        (*window()).presentation_settings().reduced_motion) {
        complete_transition();
        invalidate(Dirty::paint);
        return;
    }
    const AnimationSample sample = transition_timeline_.sample(now);
    transition_progress_ = std::clamp(sample.progress, 0.0, 1.0);
    presented_actuator_ = {
        transition_from_.width +
            (transition_to_.width - transition_from_.width) * transition_progress_,
        transition_from_.height +
            (transition_to_.height - transition_from_.height) * transition_progress_};
    if (sample.finished) complete_transition();
    // The transition timer's tick damages the actuator's old and new place.
}

bool SplitterGrip::engaged() const noexcept {
    return hot_ || (dragging_ && has_pointer_capture()) ||
        (actuator_pressed_ && has_pointer_capture()) ||
        (focused_ && window() != nullptr && (*window()).focus_cue_visible());
}

Rect SplitterGrip::actuator_bounds(double cross_extent,
                                   double axis_extent) const noexcept {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const double seam_center = visible_origin_ + visible_width_ * 0.5;
    if (orientation_ == Orientation::vertical) {
        const double height = std::min(axis_extent, bounds.height);
        return {seam_center - cross_extent * 0.5,
                std::max(0.0, (bounds.height - height) * 0.5),
                cross_extent, height};
    }
    const double width = std::min(axis_extent, bounds.width);
    return {std::max(0.0, (bounds.width - width) * 0.5),
            seam_center - cross_extent * 0.5, width, cross_extent};
}

bool SplitterGrip::point_in_proximity(const Point window_point) const noexcept {
    const Point local = point_from_window(window_point);
    return actuator_bounds(expanded_actuator_width,
                           expanded_actuator_extent).contains(local);
}

void SplitterGrip::on_paint(Painter& painter, Rect) {
    transition_frames_.note_painted();
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const BasicControlStyle style;
    const SplitSeamState state = seam_state();
    const bool disabled = state == SplitSeamState::disabled;
    const bool dragging = state == SplitSeamState::dragging;
    if (orientation_ == Orientation::vertical) {
        const double width = std::min(visible_width_, bounds.width);
        const double x = std::clamp(visible_origin_, 0.0,
                                    std::max(0.0, bounds.width - width));
        painter.fill_rect({x, 0.0, width, bounds.height},
                          disabled ? style.face_light
                                   : (dragging ? style.accent_light : style.face));
        if (width >= 2.0) {
            painter.draw_line({x, 0.0}, {x, bounds.height},
                              disabled ? style.border : style.highlight, 1.0);
            painter.draw_line({x + width - 1.0, 0.0},
                              {x + width - 1.0, bounds.height},
                              disabled ? style.border : style.dark_border,
                              1.0);
        }
    } else {
        const double height = std::min(visible_width_, bounds.height);
        const double y = std::clamp(visible_origin_, 0.0,
                                    std::max(0.0, bounds.height - height));
        painter.fill_rect({0.0, y, bounds.width, height},
                          disabled ? style.face_light
                                   : (dragging ? style.accent_light : style.face));
        if (height >= 2.0) {
            painter.draw_line({0.0, y}, {bounds.width, y},
                              disabled ? style.border : style.highlight, 1.0);
            painter.draw_line({0.0, y + height - 1.0},
                              {bounds.width, y + height - 1.0},
                              disabled ? style.border : style.dark_border,
                              1.0);
        }
    }
    if (collapse_panel_ == SplitFixedPanel::none) return;

    const bool expanded_grip = engaged();
    const bool first = collapse_panel_ == SplitFixedPanel::first;
    std::string arrow;
    const Rect tab = actuator_bounds();
    Point text_origin;
    if (orientation_ == Orientation::vertical) {
        arrow = first ? (collapsed_ ? "▶" : "◀")
                      : (collapsed_ ? "◀" : "▶");
        text_origin = {tab.x + (tab.width - 7.0) * 0.5,
                       tab.y + tab.height * 0.5 + 4.0};
    } else {
        arrow = first ? (collapsed_ ? "▼" : "▲")
                      : (collapsed_ ? "▲" : "▼");
        text_origin = {tab.x + tab.width * 0.5 - 4.0,
                       tab.y + tab.height * 0.5 + 4.0};
    }
    if (expanded_grip && !disabled) {
        painter.draw_box_shadow(
            tab, 0.0, engaged_shadow_offset, engaged_shadow_blur, 0.0,
            Color::rgba(style.dark_border.red, style.dark_border.green,
                        style.dark_border.blue, 102));
    }
    painter.fill_rect(tab, disabled ? style.face_light
                                    : (expanded_grip ? style.accent_light
                                                     : style.face_light));
    painter.stroke_rect({tab.x + 0.5, tab.y + 0.5,
                         std::max(0.0, tab.width - 1.0),
                         std::max(0.0, tab.height - 1.0)},
                        disabled ? style.border
                                 : (expanded_grip ? style.accent : style.border),
                        1.0);
    if (expanded_grip) {
        painter.draw_line({tab.x + 1.0, tab.y + 1.0},
                          {tab.x + tab.width - 1.0, tab.y + 1.0},
                          style.highlight, 1.0);
    }
    if (dragging) {
        if (orientation_ == Orientation::vertical) {
            painter.draw_line({tab.x + tab.width * 0.5 - 2.0,
                               tab.y + tab.height * 0.5},
                              {tab.x + tab.width * 0.5 + 2.0,
                               tab.y + tab.height * 0.5},
                              style.dark_border, 1.0);
        } else {
            painter.draw_line({tab.x + tab.width * 0.5,
                               tab.y + tab.height * 0.5 - 2.0},
                              {tab.x + tab.width * 0.5,
                               tab.y + tab.height * 0.5 + 2.0},
                              style.dark_border, 1.0);
        }
    }
    painter.draw_text_utf8(text_origin, arrow,
                           effective_font(
                               {FontRole::control, 8.0, 700, false}),
                           style.text);
}

} // namespace gui_forms
