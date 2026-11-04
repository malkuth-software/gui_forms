#include "gui_forms/controls/range_control/scroll_bar/scroll_bar.hpp"

#include "../range_control_rendering.hpp"
#include "scroll_bar_anatomy.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

ScrollBar::ScrollBar(StableId stable_id, Orientation orientation)
    : RangeControl(std::move(stable_id)) {
    set_orientation(orientation);
    set_focusable(true);
    set_cursor(CursorKind::arrow);
    // What a form document sets besides the range (stage FD6.3); it raises
    // no change event.
    PropertyDescriptor large_change;
    large_change.name = "large_change";
    large_change.kind = BindingValueKind::number;
    large_change.category = "Behavior";
    large_change.description = "How far a click on the track or Page Up/Down moves the value.";
    large_change.default_value = BindingValue{10.0};
    large_change.invalidation_effects = Dirty::paint | Dirty::semantics;
    large_change.bindable = false;
    define_bindable_property({
        std::move(large_change),
        detail::BindingNoexceptMethodGetter<ScrollBar, double>(*this, &ScrollBar::large_change),
        detail::DirectPropertySetter<ScrollBar, double>(*this, &ScrollBar::set_large_change),
        {}, {}, {}});
}

void ScrollBar::set_button_extent(double extent) {
    require_mutable();
    require_finite(extent, "scrollbar button extent must be finite");
    if (extent < 8.0 || extent > 64.0) {
        throw std::invalid_argument("scrollbar button extent must be between 8 and 64");
    }
    if (button_extent_ == extent) return;
    button_extent_ = extent;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ScrollBar::set_minimum_thumb_extent(double extent) {
    require_mutable();
    require_finite(extent, "scrollbar thumb extent must be finite");
    if (extent < 8.0 || extent > 128.0) {
        throw std::invalid_argument("scrollbar thumb extent must be between 8 and 128");
    }
    if (minimum_thumb_extent_ == extent) return;
    minimum_thumb_extent_ = extent;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ScrollBar::set_initial_repeat_delay(FrameInterval delay) {
    require_mutable();
    if (delay < std::chrono::milliseconds(100) ||
        delay > std::chrono::seconds(2)) {
        throw std::invalid_argument(
            "scrollbar initial repeat delay must be between 100ms and 2s");
    }
    initial_repeat_delay_ = delay;
}

void ScrollBar::set_repeat_interval(FrameInterval interval) {
    require_mutable();
    if (interval < std::chrono::milliseconds(16) ||
        interval > std::chrono::milliseconds(500)) {
        throw std::invalid_argument(
            "scrollbar repeat interval must be between 16ms and 500ms");
    }
    repeat_interval_ = interval;
}

Rect ScrollBar::decrement_button_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const double axis = orientation() == Orientation::horizontal
        ? bounds.width : bounds.height;
    const double extent = std::min(button_extent_, axis * 0.5);
    return orientation() == Orientation::horizontal
        ? Rect{0.0, 0.0, extent, bounds.height}
        : Rect{0.0, 0.0, bounds.width, extent};
}

Rect ScrollBar::increment_button_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const double axis = orientation() == Orientation::horizontal
        ? bounds.width : bounds.height;
    const double extent = std::min(button_extent_, axis * 0.5);
    return orientation() == Orientation::horizontal
        ? Rect{std::max(0.0, bounds.width - extent), 0.0, extent, bounds.height}
        : Rect{0.0, std::max(0.0, bounds.height - extent), bounds.width, extent};
}

Rect ScrollBar::track_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect decrement = decrement_button_bounds();
    const Rect increment = increment_button_bounds();
    return orientation() == Orientation::horizontal
        ? Rect{decrement.width, 0.0,
               std::max(0.0, increment.x - decrement.width), bounds.height}
        : Rect{0.0, decrement.height, bounds.width,
               std::max(0.0, increment.y - decrement.height)};
}

Rect ScrollBar::thumb_bounds() const noexcept {
    const Rect track = track_bounds();
    const double track_extent = orientation() == Orientation::horizontal
        ? track.width : track.height;
    if (track_extent <= 0.0) return track;
    const double range = maximum() - minimum();
    const double proportion = large_change() / (range + large_change());
    const double thumb_extent = std::clamp(track_extent * proportion,
        std::min(minimum_thumb_extent_, track_extent), track_extent);
    const double movable = std::max(0.0, track_extent - thumb_extent);
    const double position = normalized_value() * movable;
    return orientation() == Orientation::horizontal
        ? Rect{track.x + position, track.y, thumb_extent, track.height}
        : Rect{track.x, track.y + position, track.width, thumb_extent};
}

double ScrollBar::axis_coordinate(Point local_point) const noexcept {
    return orientation() == Orientation::horizontal ? local_point.x : local_point.y;
}

ScrollBarPart ScrollBar::part_at(Point local_point) const noexcept {
    if (!local_bounds().contains(local_point)) return ScrollBarPart::none;
    if (decrement_button_bounds().contains(local_point)) {
        return ScrollBarPart::decrement_button;
    }
    if (increment_button_bounds().contains(local_point)) {
        return ScrollBarPart::increment_button;
    }
    const Rect thumb = thumb_bounds();
    if (thumb.contains(local_point)) return ScrollBarPart::thumb;
    const double coordinate = axis_coordinate(local_point);
    const double thumb_start = orientation() == Orientation::horizontal
        ? thumb.x : thumb.y;
    return coordinate < thumb_start ? ScrollBarPart::decrement_track
                                    : ScrollBarPart::increment_track;
}

namespace {

constexpr ControlAnatomy scroll_bar_anatomy{
    .control_id = "scroll_bar",
    .parts = scroll_bar_detail::parts,
    .states = scroll_bar_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ScrollBar::anatomy() const noexcept {
    return scroll_bar_anatomy;
}

void ScrollBar::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect thumb = thumb_bounds();
    Parts record;
    record.region = PartGeometry::from_bounds(local_bounds());
    record.decrement_arrow = PartGeometry::from_bounds(decrement_button_bounds());
    record.increment_arrow = PartGeometry::from_bounds(increment_button_bounds());
    record.track = PartGeometry::from_bounds(track_bounds());
    record.thumb = PartGeometry::from_bounds(thumb);
    // The scroll bar shows focus on its thumb.
    record.focus = PartGeometry::from_bounds(thumb);
    // The pressed part carries the pressed state; the page regions either
    // side of the thumb are the track.
    if (pressed_part_ == ScrollBarPart::decrement_button) {
        record.decrement_arrow.states.insert(AnatomyState::pressed);
    } else if (pressed_part_ == ScrollBarPart::increment_button) {
        record.increment_arrow.states.insert(AnatomyState::pressed);
    } else if (pressed_part_ == ScrollBarPart::decrement_track ||
               pressed_part_ == ScrollBarPart::increment_track) {
        record.track.states.insert(AnatomyState::pressed);
    } else if (pressed_part_ == ScrollBarPart::thumb) {
        record.thumb.states.insert(AnatomyState::pressed);
    }
    if (tracking_thumb_) {
        record.thumb.states.insert(AnatomyState::captured);
    }
    write_parts(anatomy(), scroll_bar_detail::part_fields, record, geometry);
}

AnatomyStateSet ScrollBar::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (pressed_part_ != ScrollBarPart::none) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
    }
    if (tracking_thumb_) {
        states.insert(AnatomyState::captured);
    }
    return states;
}

ScrollBar::Parts ScrollBar::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scroll_bar_detail::part_fields, published.parts(), record);
    return record;
}

ScrollBar::States ScrollBar::states() const {
    States record;
    read_states(scroll_bar_detail::state_fields, anatomy_states(), record);
    return record;
}

double ScrollBar::value_from_thumb_coordinate(double coordinate) const noexcept {
    const Rect track = track_bounds();
    const Rect thumb = thumb_bounds();
    const double track_start = orientation() == Orientation::horizontal
        ? track.x : track.y;
    const double track_extent = orientation() == Orientation::horizontal
        ? track.width : track.height;
    const double thumb_extent = orientation() == Orientation::horizontal
        ? thumb.width : thumb.height;
    const double movable = std::max(0.0, track_extent - thumb_extent);
    if (movable <= 0.0) return minimum();
    const double ratio = std::clamp((coordinate - track_start) / movable, 0.0, 1.0);
    return minimum() + ratio * (maximum() - minimum());
}

bool ScrollBar::apply_part(ScrollBarPart part) {
    switch (part) {
    case ScrollBarPart::decrement_button:
        return set_value_from_input(value() - small_change(),
                                    RangeAction::small_decrement);
    case ScrollBarPart::increment_button:
        return set_value_from_input(value() + small_change(),
                                    RangeAction::small_increment);
    case ScrollBarPart::decrement_track:
        return set_value_from_input(value() - large_change(),
                                    RangeAction::large_decrement);
    case ScrollBarPart::increment_track:
        return set_value_from_input(value() + large_change(),
                                    RangeAction::large_increment);
    case ScrollBarPart::none:
    case ScrollBarPart::thumb:
        return false;
    }
    return false;
}

void ScrollBar::begin_repeat(ScrollBarPart part, Point pointer) {
    repeat_frames_.disconnect();
    repeat_pointer_ = pointer;
    if (window() == nullptr || part == ScrollBarPart::none ||
        part == ScrollBarPart::thumb) return;
    const FrameTime now = FrameClock::now();
    const std::shared_ptr<ScrollBar> self =
        std::static_pointer_cast<ScrollBar>(shared_from_this());
    repeat_frames_ = (*window()).schedule_ui_timer(
        *this, repeat_interval_, now + initial_repeat_delay_, RepeatTick{self});
}

void ScrollBar::RepeatTick::operator()(FrameTime now) const {
    static_cast<void>(now);
    const std::shared_ptr<ScrollBar> live = bar.lock();
    if (live) (*live).repeat_step();
}

void ScrollBar::stop_interaction() noexcept {
    repeat_frames_.disconnect();
    pressed_part_ = ScrollBarPart::none;
    tracking_thumb_ = false;
    thumb_drag_offset_ = 0.0;
}

Size ScrollBar::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred = orientation() == Orientation::horizontal
        ? Size{requested.width > 0.0 ? requested.width : 180.0,
               requested.height > 0.0 ? requested.height : 18.0}
        : Size{requested.width > 0.0 ? requested.width : 18.0,
               requested.height > 0.0 ? requested.height : 180.0};
    return {std::min(available.width, preferred.width),
            std::min(available.height, preferred.height)};
}

void ScrollBar::paint_button(Painter& painter, Rect button,
                             ScrollBarPart part, bool incrementing,
                             const BasicControlStyle& colors) {
    paint_thumb(painter, button, colors, pressed_part_ == part);
    const double cx = button.x + button.width * 0.5;
    const double cy = button.y + button.height * 0.5;
    const Color arrow = enabled() ? colors.dark_border : colors.disabled_text;
    if (orientation() == Orientation::horizontal) {
        const double direction = incrementing ? 1.0 : -1.0;
        painter.draw_line({cx - 2.0 * direction, cy - 4.0},
                          {cx + 2.0 * direction, cy}, arrow, 1.5);
        painter.draw_line({cx + 2.0 * direction, cy},
                          {cx - 2.0 * direction, cy + 4.0}, arrow, 1.5);
    } else {
        const double direction = incrementing ? 1.0 : -1.0;
        painter.draw_line({cx - 4.0, cy - 2.0 * direction},
                          {cx, cy + 2.0 * direction}, arrow, 1.5);
        painter.draw_line({cx, cy + 2.0 * direction},
                          {cx + 4.0, cy - 2.0 * direction}, arrow, 1.5);
    }
}

void ScrollBar::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const BasicControlStyle& colors = style();
    const Rect track = track_bounds();
    painter.fill_rect(bounds, colors.face);
    paint_sunken(painter, track, colors, colors.face_light);

    paint_button(painter, decrement_button_bounds(),
                 ScrollBarPart::decrement_button, false, colors);
    paint_button(painter, increment_button_bounds(),
                 ScrollBarPart::increment_button, true, colors);
    paint_thumb(painter, thumb_bounds(), colors, focused_ || tracking_thumb_);
}

void ScrollBar::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    if (event.action == PointerAction::wheel && event.wheel_delta.y != 0.0) {
        const bool decrementing = event.wheel_delta.y > 0.0;
        set_value_from_input(value() + (decrementing ? -small_change() : small_change()),
                             decrementing ? RangeAction::small_decrement
                                          : RangeAction::small_increment);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
        pressed_part_ = part_at(local);
        if (pressed_part_ == ScrollBarPart::none) return;
        set_pointer_capture(true);
        if (pressed_part_ == ScrollBarPart::thumb) {
            const Rect thumb = thumb_bounds();
            const double thumb_start = orientation() == Orientation::horizontal
                ? thumb.x : thumb.y;
            thumb_drag_offset_ = axis_coordinate(local) - thumb_start;
            tracking_thumb_ = true;
        } else {
            static_cast<void>(apply_part(pressed_part_));
            begin_repeat(pressed_part_, local);
        }
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move && tracking_thumb_) {
        const double coordinate = axis_coordinate(local) - thumb_drag_offset_;
        set_value_from_input(value_from_thumb_coordinate(coordinate),
                             RangeAction::thumb_track);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move && pressed_part_ != ScrollBarPart::none) {
        repeat_pointer_ = local;
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up &&
        event.button == PointerButton::primary &&
        pressed_part_ != ScrollBarPart::none) {
        const bool was_thumb = tracking_thumb_;
        if (has_pointer_capture()) set_pointer_capture(false);
        stop_interaction();
        if (was_thumb) invalidate(Dirty::paint | Dirty::semantics);
        else invalidate(Dirty::paint);
        event.handled = true;
    }
}

void ScrollBar::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down) return;
    double next = value();
    RangeAction action = RangeAction::thumb_position;
    bool recognized = true;
    switch (event.physical_key) {
    case PhysicalKey::left:
    case PhysicalKey::up:
        next -= small_change(); action = RangeAction::small_decrement; break;
    case PhysicalKey::right:
    case PhysicalKey::down:
        next += small_change(); action = RangeAction::small_increment; break;
    case PhysicalKey::page_up:
        next -= large_change(); action = RangeAction::large_decrement; break;
    case PhysicalKey::page_down:
        next += large_change(); action = RangeAction::large_increment; break;
    case PhysicalKey::home:
        next = minimum(); action = RangeAction::first; break;
    case PhysicalKey::end:
        next = maximum(); action = RangeAction::last; break;
    default:
        recognized = false; break;
    }
    if (recognized) {
        static_cast<void>(set_value_from_input(next, action));
        event.handled = true;
    }
}

void ScrollBar::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) {
        if (has_pointer_capture()) set_pointer_capture(false);
        stop_interaction();
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void ScrollBar::repeat_step() {
    if (pressed_part_ == ScrollBarPart::decrement_track ||
        pressed_part_ == ScrollBarPart::increment_track) {
        if (part_at(repeat_pointer_) != pressed_part_) {
            repeat_frames_.disconnect();
            return;
        }
    }
    if (!apply_part(pressed_part_)) repeat_frames_.disconnect();
}

void ScrollBar::on_detached_from_window() noexcept {
    stop_interaction();
    RangeControl::on_detached_from_window();
}

SemanticDescriptor ScrollBar::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::scroll_bar;
    descriptor.name = accessible_name();
    descriptor.description = accessible_description();
    std::ostringstream value_text;
    value_text << value();
    descriptor.value = value_text.str();
    descriptor.numeric_value = value();
    descriptor.minimum_value = minimum();
    descriptor.maximum_value = maximum();
    // The arrow keys move by the small change, the page keys by the large.
    descriptor.small_step = small_change();
    descriptor.large_step = large_change();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement, SemanticAction::set_value};
    descriptor.exposed = true;
    return descriptor;
}

bool ScrollBar::on_semantic_action(SemanticAction action,
                                   std::string_view value_text) {
    if (action == SemanticAction::increment || action == SemanticAction::decrement) {
        const bool increasing = action == SemanticAction::increment;
        set_value_from_input(value() + (increasing ? small_change() : -small_change()),
                             increasing ? RangeAction::small_increment
                                        : RangeAction::small_decrement);
        return true;
    }
    if (action == SemanticAction::set_value) {
        try {
            std::size_t consumed{};
            const double parsed = std::stod(std::string(value_text), &consumed);
            if (consumed != value_text.size() || !std::isfinite(parsed)) return false;
            return set_value_from_input(parsed, RangeAction::thumb_position) ||
                   parsed == value();
        } catch (const std::exception&) {
            return false;
        }
    }
    return RangeControl::on_semantic_action(action, value_text);
}

} // namespace gui_forms
