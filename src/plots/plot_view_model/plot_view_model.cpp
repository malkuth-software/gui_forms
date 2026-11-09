#include "gui_forms/plots/plot_view_model/plot_view_model.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::plots {

namespace {

// The axis's transform (its kind's transformed space), through a mapping
// over a unit device interval: only transform and untransform are used.
ScaleMapping transform_of(const Axis& axis) noexcept {
    Axis unit = axis;
    // A range the kind refuses (a log axis reaching zero) falls back to a
    // valid one for the transform alone; transform does not read it.
    if (!admissible_range(unit, unit.range)) unit.range = PlotRange{1.0, 10.0};
    const ScaleMappingResult made = ScaleMapping::make(axis_scale(unit, 0.0, 1.0));
    return made.mapping;
}

bool set_checked(Axis& axis, PlotRange range) noexcept {
    if (!admissible_range(axis, range) || range == axis.range) return false;
    if (locks_minimum(axis.lock) && range.minimum != axis.range.minimum) return false;
    if (locks_maximum(axis.lock) && range.maximum != axis.range.maximum) return false;
    axis.range = range;
    return true;
}

PlotRange ordered(double first, double second) noexcept {
    const PlotRange range{std::min(first, second), std::max(first, second)};
    return range;
}

} // namespace

void PlotViewModel::set_axes(std::span<const Axis> x_axes, std::span<const Axis> y_axes) noexcept {
    x_count_ = std::min(x_axes.size(), maximum_axes_per_orientation);
    y_count_ = std::min(y_axes.size(), maximum_axes_per_orientation);
    for (std::size_t index = 0U; index < x_count_; ++index) {
        // An axis that newly follows the tail starts following; one that
        // already did keeps its state (a user's pan stopped it).
        const bool newly = !x_axes_[index].follow.tail;
        const bool following = x_axes[index].follow.tail && (newly || x_following_[index]);
        x_axes_[index] = x_axes[index];
        x_following_[index] = following;
    }
    for (std::size_t index = 0U; index < y_count_; ++index) y_axes_[index] = y_axes[index];
}

std::span<const Axis> PlotViewModel::x_axes() const noexcept {
    const std::span<const Axis> result(x_axes_.data(), x_count_);
    return result;
}

std::span<const Axis> PlotViewModel::y_axes() const noexcept {
    const std::span<const Axis> result(y_axes_.data(), y_count_);
    return result;
}

bool PlotViewModel::x_following(std::size_t index) const noexcept {
    if (index >= x_count_) return false;
    return x_following_[index];
}

std::optional<Rect> PlotViewModel::drag_box() const noexcept {
    if (gesture_ != PlotGesture::box_zoom && gesture_ != PlotGesture::query) return std::nullopt;
    const double left = std::min(gesture_start_.x, gesture_last_.x);
    const double top = std::min(gesture_start_.y, gesture_last_.y);
    const Rect box{left, top, std::fabs(gesture_last_.x - gesture_start_.x),
                   std::fabs(gesture_last_.y - gesture_start_.y)};
    const Rect clipped = Rect::intersection(box, plot_);
    return clipped;
}

bool PlotViewModel::set_x_range(std::size_t index, PlotRange range) noexcept {
    if (index >= x_count_) return false;
    const bool changed = set_checked(x_axes_[index], range);
    return changed;
}

bool PlotViewModel::set_y_range(std::size_t index, PlotRange range) noexcept {
    if (index >= y_count_) return false;
    const bool changed = set_checked(y_axes_[index], range);
    return changed;
}

void PlotViewModel::clear_selection() noexcept {
    selection_.reset();
}

// -- axis arithmetic ---------------------------------------------------------------

bool PlotViewModel::pan_axis(Axis& axis, double delta, double extent, bool horizontal) noexcept {
    if (axis.lock != AxisLock::none || !(extent > 0.0)) return false;
    const ScaleMapping mapping = transform_of(axis);
    const double low = mapping.transform(axis.range.minimum);
    const double high = mapping.transform(axis.range.maximum);
    // Content moving right (or down) shows smaller (or, on a y axis whose
    // values grow upwards, larger) values; inversion flips it.
    const double sign = horizontal != axis.inverted ? -1.0 : 1.0;
    const double shift = sign * (high - low) * delta / extent;
    const PlotRange moved = ordered(mapping.untransform(low + shift), mapping.untransform(high + shift));
    const bool changed = set_checked(axis, moved);
    return changed;
}

bool PlotViewModel::zoom_axis(Axis& axis, double factor, double anchor_fraction) noexcept {
    if (axis.lock == AxisLock::both || !(factor > 0.0) || !std::isfinite(factor)) return false;
    double anchor = std::clamp(anchor_fraction, 0.0, 1.0);
    if (locks_minimum(axis.lock)) anchor = 0.0;
    if (locks_maximum(axis.lock)) anchor = 1.0;
    const ScaleMapping mapping = transform_of(axis);
    const double low = mapping.transform(axis.range.minimum);
    const double high = mapping.transform(axis.range.maximum);
    const double pivot = low + anchor * (high - low);
    const double new_low = anchor == 0.0 ? low : pivot - (pivot - low) / factor;
    const double new_high = anchor == 1.0 ? high : pivot + (high - pivot) / factor;
    // A locked end is kept exactly, not recomputed through the transform.
    PlotRange zoomed{mapping.untransform(new_low), mapping.untransform(new_high)};
    if (anchor == 0.0) zoomed.minimum = axis.range.minimum;
    if (anchor == 1.0) zoomed.maximum = axis.range.maximum;
    const bool changed = set_checked(axis, zoomed);
    return changed;
}

bool PlotViewModel::fit_axis(Axis& axis, PlotRange data) noexcept {
    if (axis.lock == AxisLock::both) return false;
    if (!std::isfinite(data.minimum) || !std::isfinite(data.maximum) || data.minimum > data.maximum) return false;
    const ScaleMapping mapping = transform_of(axis);
    PlotRange source = data;
    if (axis.kind == ScaleKind::log10 && !(source.minimum > 0.0)) return false;
    double low = mapping.transform(source.minimum);
    double high = mapping.transform(source.maximum);
    if (low == high) {
        // A single value: a unit around it (a decade for log).
        low = low - 0.5;
        high = high + 0.5;
    } else {
        const double padding = (high - low) * fit_padding;
        low = low - padding;
        high = high + padding;
    }
    PlotRange fitted{mapping.untransform(low), mapping.untransform(high)};
    if (locks_minimum(axis.lock)) fitted.minimum = axis.range.minimum;
    if (locks_maximum(axis.lock)) fitted.maximum = axis.range.maximum;
    if (!admissible_range(axis, fitted) || fitted == axis.range) return false;
    axis.range = fitted;
    return true;
}

double PlotViewModel::fraction_at(const Axis& axis, double coordinate, bool horizontal) const noexcept {
    double fraction = horizontal ? (coordinate - plot_.x) / plot_.width : (plot_.bottom() - coordinate) / plot_.height;
    if (axis.inverted) fraction = 1.0 - fraction;
    return fraction;
}

PlotRange PlotViewModel::data_interval(const Axis& axis, double low, double high, bool horizontal) const noexcept {
    const ScaleMapping mapping = transform_of(axis);
    const double start = mapping.transform(axis.range.minimum);
    const double end = mapping.transform(axis.range.maximum);
    const double first = start + fraction_at(axis, low, horizontal) * (end - start);
    const double second = start + fraction_at(axis, high, horizontal) * (end - start);
    const PlotRange range = ordered(mapping.untransform(first), mapping.untransform(second));
    return range;
}

PlotViewChange PlotViewModel::pan_all(Point delta, PlotZone zone) noexcept {
    PlotViewChange change{};
    const bool all = zone.kind == PlotZoneKind::plot;
    for (std::size_t index = 0U; index < x_count_; ++index) {
        if (!all && !(zone.kind == PlotZoneKind::x_axis && zone.axis == index)) continue;
        if (pan_axis(x_axes_[index], delta.x, plot_.width, true)) {
            change.view = true;
            x_following_[index] = false;
        }
    }
    for (std::size_t index = 0U; index < y_count_; ++index) {
        if (!all && !(zone.kind == PlotZoneKind::y_axis && zone.axis == index)) continue;
        if (pan_axis(y_axes_[index], delta.y, plot_.height, false)) change.view = true;
    }
    return change;
}

PlotViewChange PlotViewModel::zoom_all(double factor, Point anchor, PlotZone zone) noexcept {
    PlotViewChange change{};
    const bool all = zone.kind == PlotZoneKind::plot;
    for (std::size_t index = 0U; index < x_count_; ++index) {
        if (!all && !(zone.kind == PlotZoneKind::x_axis && zone.axis == index)) continue;
        const double fraction = fraction_at(x_axes_[index], anchor.x, true);
        if (zoom_axis(x_axes_[index], factor, fraction)) {
            change.view = true;
            x_following_[index] = false;
        }
    }
    for (std::size_t index = 0U; index < y_count_; ++index) {
        if (!all && !(zone.kind == PlotZoneKind::y_axis && zone.axis == index)) continue;
        const double fraction = fraction_at(y_axes_[index], anchor.y, false);
        if (zoom_axis(y_axes_[index], factor, fraction)) change.view = true;
    }
    return change;
}

PlotViewChange PlotViewModel::apply_box() noexcept {
    PlotViewChange change{};
    const std::optional<Rect> box = drag_box();
    if (!box || (*box).empty()) return change;
    const Rect area = *box;
    if (gesture_ == PlotGesture::query) {
        if (x_count_ == 0U || y_count_ == 0U) return change;
        PlotSelection selection{};
        selection.x = data_interval(x_axes_[0], area.left(), area.right(), true);
        selection.y = data_interval(y_axes_[0], area.top(), area.bottom(), false);
        selection_ = selection;
        change.selection = true;
        return change;
    }
    for (std::size_t index = 0U; index < x_count_; ++index) {
        const PlotRange range = data_interval(x_axes_[index], area.left(), area.right(), true);
        if (set_checked(x_axes_[index], range)) {
            change.view = true;
            x_following_[index] = false;
        }
    }
    for (std::size_t index = 0U; index < y_count_; ++index) {
        const PlotRange range = data_interval(y_axes_[index], area.top(), area.bottom(), false);
        if (set_checked(y_axes_[index], range)) change.view = true;
    }
    return change;
}

// -- input ---------------------------------------------------------------------------

PlotViewChange PlotViewModel::pointer_pressed(Point position, PlotPointerButton button, PlotModifiers modifiers,
                                              PlotZone zone) noexcept {
    PlotViewChange change{};
    if (gesture_ != PlotGesture::none || zone.kind == PlotZoneKind::outside) return change;
    PlotGesture chosen = PlotGesture::none;
    const bool in_plot = zone.kind == PlotZoneKind::plot;
    if (binding_ == BoxZoomBinding::secondary_drag) {
        if (button == PlotPointerButton::primary) chosen = PlotGesture::pan;
        if (button == PlotPointerButton::secondary && in_plot) {
            chosen = modifiers.control ? PlotGesture::query : PlotGesture::box_zoom;
        }
    } else {
        if (button == PlotPointerButton::primary) {
            chosen = modifiers.control && in_plot ? PlotGesture::box_zoom : PlotGesture::pan;
        }
        if (button == PlotPointerButton::secondary && modifiers.control && in_plot) chosen = PlotGesture::query;
    }
    if (chosen == PlotGesture::none) return change;
    gesture_ = chosen;
    gesture_zone_ = zone;
    gesture_start_ = position;
    gesture_last_ = position;
    gesture_moved_ = false;
    return change;
}

PlotViewChange PlotViewModel::pointer_moved(Point position) noexcept {
    PlotViewChange change{};
    if (gesture_ == PlotGesture::none) return change;
    const double travelled = std::hypot(position.x - gesture_start_.x, position.y - gesture_start_.y);
    if (!gesture_moved_ && travelled < drag_threshold) return change;
    gesture_moved_ = true;
    if (gesture_ == PlotGesture::pan) {
        const Point delta{position.x - gesture_last_.x, position.y - gesture_last_.y};
        gesture_last_ = position;
        change = pan_all(delta, gesture_zone_);
        return change;
    }
    gesture_last_ = position;
    change.transient = true;
    return change;
}

PlotViewChange PlotViewModel::pointer_released(Point position) noexcept {
    PlotViewChange change{};
    if (gesture_ == PlotGesture::none) return change;
    change = pointer_moved(position);
    if ((gesture_ == PlotGesture::box_zoom || gesture_ == PlotGesture::query) && gesture_moved_) {
        const PlotViewChange applied = apply_box();
        change.view = change.view || applied.view;
        change.selection = change.selection || applied.selection;
        change.transient = true;
    }
    gesture_ = PlotGesture::none;
    return change;
}

PlotViewChange PlotViewModel::cancel_gesture() noexcept {
    PlotViewChange change{};
    if (gesture_ == PlotGesture::none) return change;
    change.transient = gesture_ != PlotGesture::pan;
    gesture_ = PlotGesture::none;
    return change;
}

PlotViewChange PlotViewModel::wheel(Point position, double notches, PlotZone zone) noexcept {
    PlotViewChange change{};
    if (zone.kind == PlotZoneKind::outside || !std::isfinite(notches) || notches == 0.0) return change;
    double factor = 1.0;
    const double steps = std::fabs(notches);
    // Whole notches multiply exactly; a fraction of one scales linearly.
    double remaining = steps;
    while (remaining >= 1.0) {
        factor = factor * wheel_zoom_per_notch;
        remaining = remaining - 1.0;
    }
    factor = factor * (1.0 + (wheel_zoom_per_notch - 1.0) * remaining);
    if (notches < 0.0) factor = 1.0 / factor;
    change = zoom_all(factor, position, zone);
    return change;
}

PlotViewChange PlotViewModel::key(PlotKey pressed, PlotModifiers modifiers, const PlotDataBounds& bounds) noexcept {
    PlotViewChange change{};
    const Point centre{plot_.x + plot_.width * 0.5, plot_.y + plot_.height * 0.5};
    const double fraction = modifiers.shift ? 1.0 : key_pan_fraction;
    PlotZone x_zone{PlotZoneKind::x_axis, 0U};
    PlotZone y_zone{PlotZoneKind::y_axis, 0U};
    switch (pressed) {
    case PlotKey::left:
    case PlotKey::right: {
        const bool forward = pressed == PlotKey::right;
        if (modifiers.control) {
            for (std::size_t index = 0U; index < x_count_; ++index) {
                x_zone.axis = index;
                const PlotViewChange zoomed = zoom_all(forward ? key_zoom_factor : 1.0 / key_zoom_factor, centre, x_zone);
                change.view = change.view || zoomed.view;
            }
            return change;
        }
        // Right shows later values: the content moves left.
        const Point delta{(forward ? -1.0 : 1.0) * plot_.width * fraction, 0.0};
        for (std::size_t index = 0U; index < x_count_; ++index) {
            x_zone.axis = index;
            const PlotViewChange panned = pan_all(delta, x_zone);
            change.view = change.view || panned.view;
        }
        return change;
    }
    case PlotKey::up:
    case PlotKey::down: {
        const bool upward = pressed == PlotKey::up;
        if (modifiers.control) {
            for (std::size_t index = 0U; index < y_count_; ++index) {
                y_zone.axis = index;
                const PlotViewChange zoomed = zoom_all(upward ? key_zoom_factor : 1.0 / key_zoom_factor, centre, y_zone);
                change.view = change.view || zoomed.view;
            }
            return change;
        }
        // Up shows larger values: the content moves down.
        const Point delta{0.0, (upward ? 1.0 : -1.0) * plot_.height * fraction};
        for (std::size_t index = 0U; index < y_count_; ++index) {
            y_zone.axis = index;
            const PlotViewChange panned = pan_all(delta, y_zone);
            change.view = change.view || panned.view;
        }
        return change;
    }
    case PlotKey::zoom_in:
    case PlotKey::zoom_out: {
        const double factor = pressed == PlotKey::zoom_in ? key_zoom_factor : 1.0 / key_zoom_factor;
        const PlotZone everywhere{PlotZoneKind::plot, 0U};
        change = zoom_all(factor, centre, everywhere);
        return change;
    }
    case PlotKey::home:
        change = fit(bounds);
        return change;
    case PlotKey::end:
        for (std::size_t index = 0U; index < x_count_; ++index) {
            if (x_axes_[index].follow.tail && !x_following_[index]) {
                x_following_[index] = true;
                change.transient = true;
            }
        }
        return change;
    }
    return change;
}

PlotViewChange PlotViewModel::fit(const PlotDataBounds& bounds) noexcept {
    PlotViewChange change{};
    for (std::size_t index = 0U; index < x_count_; ++index) {
        if (fit_axis(x_axes_[index], bounds.x[index])) change.view = true;
        x_following_[index] = x_axes_[index].follow.tail;
    }
    for (std::size_t index = 0U; index < y_count_; ++index) {
        if (fit_axis(y_axes_[index], bounds.y[index])) change.view = true;
    }
    return change;
}

bool PlotViewModel::follow_tail(double newest) noexcept {
    if (!std::isfinite(newest)) return false;
    bool changed = false;
    for (std::size_t index = 0U; index < x_count_; ++index) {
        Axis& axis = x_axes_[index];
        if (!x_following_[index] || !axis.follow.tail || !(axis.follow.span > 0.0)) continue;
        if (axis.lock != AxisLock::none) continue;
        const PlotRange range{newest - axis.follow.span, newest};
        if (set_checked(axis, range)) changed = true;
    }
    return changed;
}

} // namespace gui_forms::plots
