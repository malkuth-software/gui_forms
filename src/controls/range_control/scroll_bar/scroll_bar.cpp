#include "gui_forms/controls/range_control/scroll_bar/scroll_bar.hpp"

#include "../range_control_rendering.hpp"
#include "scroll_bar_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

ScrollBar::ScrollBar(StableId stable_id, Orientation initial_orientation)
    : RangeControl(std::move(stable_id)) {
    orientation = initial_orientation;
    set_focusable(true);
    cursor = CursorKind::arrow;
}

void ScrollBar::prepare_write(const decltype(initial_repeat_delay)&, FrameInterval& incoming) {
    require_mutable();
    if (incoming < std::chrono::milliseconds(100) || incoming > std::chrono::seconds(2)) {
        throw std::invalid_argument("scrollbar initial repeat delay must be between 100ms and 2s");
    }
}

// The delay is read when a press starts repeating; nothing is drawn from it.
void ScrollBar::written(const decltype(initial_repeat_delay)&) {}

void ScrollBar::prepare_write(const decltype(repeat_interval)&, FrameInterval& incoming) {
    require_mutable();
    if (incoming < std::chrono::milliseconds(16) || incoming > std::chrono::milliseconds(500)) {
        throw std::invalid_argument("scrollbar repeat interval must be between 16ms and 500ms");
    }
}

void ScrollBar::written(const decltype(repeat_interval)&) {}

// The arrow buttons' extent along the bar: none where the theme draws no
// scroll arrows (ThemeGeometryTokens::scroll_arrows), else the button
// extent, at most half the bar.
double ScrollBar::arrow_extent(double axis) const noexcept {
    if (!effective_theme().structure().geometry.scroll_arrows) return 0.0;
    const double extent = std::min(button_extent.read(), axis * 0.5);
    return extent;
}

Rect ScrollBar::decrement_button_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const double axis = orientation.read() == Orientation::horizontal
        ? bounds.width : bounds.height;
    const double extent = arrow_extent(axis);
    return orientation.read() == Orientation::horizontal
        ? Rect{0.0, 0.0, extent, bounds.height}
        : Rect{0.0, 0.0, bounds.width, extent};
}

Rect ScrollBar::increment_button_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const double axis = orientation.read() == Orientation::horizontal
        ? bounds.width : bounds.height;
    const double extent = arrow_extent(axis);
    return orientation.read() == Orientation::horizontal
        ? Rect{std::max(0.0, bounds.width - extent), 0.0, extent, bounds.height}
        : Rect{0.0, std::max(0.0, bounds.height - extent), bounds.width, extent};
}

Rect ScrollBar::track_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect decrement = decrement_button_bounds();
    const Rect increment = increment_button_bounds();
    return orientation.read() == Orientation::horizontal
        ? Rect{decrement.width, 0.0,
               std::max(0.0, increment.x - decrement.width), bounds.height}
        : Rect{0.0, decrement.height, bounds.width,
               std::max(0.0, increment.y - decrement.height)};
}

Rect ScrollBar::thumb_bounds() const noexcept {
    const Rect track = track_bounds();
    const double track_extent = orientation.read() == Orientation::horizontal
        ? track.width : track.height;
    if (track_extent <= 0.0) return track;
    const double range = maximum.read() - minimum.read();
    const double proportion = large_change.read() / (range + large_change.read());
    const double thumb_extent = std::clamp(track_extent * proportion,
        std::min(minimum_thumb_extent.read(), track_extent), track_extent);
    const double movable = std::max(0.0, track_extent - thumb_extent);
    const double position = normalized_value() * movable;
    return orientation.read() == Orientation::horizontal
        ? Rect{track.x + position, track.y, thumb_extent, track.height}
        : Rect{track.x, track.y + position, track.width, thumb_extent};
}

double ScrollBar::axis_coordinate(Point local_point) const noexcept {
    return orientation.read() == Orientation::horizontal ? local_point.x : local_point.y;
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
    const double thumb_start = orientation.read() == Orientation::horizontal
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
    const double track_start = orientation.read() == Orientation::horizontal
        ? track.x : track.y;
    const double track_extent = orientation.read() == Orientation::horizontal
        ? track.width : track.height;
    const double thumb_extent = orientation.read() == Orientation::horizontal
        ? thumb.width : thumb.height;
    const double movable = std::max(0.0, track_extent - thumb_extent);
    if (movable <= 0.0) return minimum.read();
    const double ratio = std::clamp((coordinate - track_start) / movable, 0.0, 1.0);
    return minimum.read() + ratio * (maximum.read() - minimum.read());
}

bool ScrollBar::apply_part(ScrollBarPart part) {
    switch (part) {
    case ScrollBarPart::decrement_button:
        return set_value_from_input(value.read() - small_change.read(),
                                    RangeAction::small_decrement);
    case ScrollBarPart::increment_button:
        return set_value_from_input(value.read() + small_change.read(),
                                    RangeAction::small_increment);
    case ScrollBarPart::decrement_track:
        return set_value_from_input(value.read() - large_change.read(),
                                    RangeAction::large_decrement);
    case ScrollBarPart::increment_track:
        return set_value_from_input(value.read() + large_change.read(),
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
        *this, repeat_interval.read(), now + initial_repeat_delay.read(), RepeatTick{self});
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
    const Size preferred = orientation.read() == Orientation::horizontal
        ? Size{requested.width > 0.0 ? requested.width : 180.0,
               requested.height > 0.0 ? requested.height : 18.0}
        : Size{requested.width > 0.0 ? requested.width : 18.0,
               requested.height > 0.0 ? requested.height : 180.0};
    return {std::min(available.width, preferred.width),
            std::min(available.height, preferred.height)};
}

namespace {

// `states` with the press moved onto one part: pressed (and captured, for
// the thumb being dragged) only while `pressed` is true.
AnatomyStateSet scroll_part_states(AnatomyStateSet states, bool pressed, bool captured) {
    states.erase(AnatomyState::pressed);
    states.erase(AnatomyState::captured);
    if (pressed) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
    }
    if (captured) states.insert(AnatomyState::captured);
    return states;
}

} // namespace

void ScrollBar::paint_button(Painter& painter, Rect button,
                             ScrollBarPart part, bool incrementing,
                             AnatomyStateSet states) {
    const bool pressed = pressed_part_ == part;
    const AnatomyStateSet button_states = scroll_part_states(states, pressed, false);
    PartPaint actuator;
    actuator.family = ControlVisualRole::button;
    actuator.role = PartRole::actuator;
    actuator.bounds = button;
    actuator.states = button_states;
    actuator.key = PartKey{incrementing ? "increment_arrow" : "decrement_arrow", {}, {}};
    paint_part(painter, *this, actuator);
    // The arrow mark draws half its square's side in rows: an 8-unit square
    // centred on the button.
    const bool horizontal = orientation.read() == Orientation::horizontal;
    PartPaint arrow;
    arrow.family = ControlVisualRole::button;
    arrow.role = PartRole::glyph;
    if (horizontal) {
        arrow.mark = incrementing ? PartMark::arrow_right : PartMark::arrow_left;
    } else {
        arrow.mark = incrementing ? PartMark::arrow_down : PartMark::arrow_up;
    }
    arrow.bounds = {std::floor(button.x + button.width * 0.5) - 4.0,
                    std::floor(button.y + button.height * 0.5) - 4.0, 8.0, 8.0};
    arrow.states = button_states;
    paint_part(painter, *this, arrow);
}

// The trough under the whole bar, the track between the buttons, the two
// arrow buttons and the thumb are parts the theme's engine draws; only the
// pressed part shows pressed. The focus cue rings the thumb.
void ScrollBar::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const AnatomyStateSet states = anatomy_states();
    const AnatomyStateSet rest = scroll_part_states(states, false, false);
    PartPaint trough;
    trough.family = ControlVisualRole::panel;
    trough.role = PartRole::track;
    trough.bounds = bounds;
    trough.states = rest;
    paint_part(painter, *this, trough);
    PartPaint track;
    track.family = ControlVisualRole::editor;
    track.role = PartRole::track;
    track.bounds = track_bounds();
    track.states = scroll_part_states(states, pressed_part_ == ScrollBarPart::decrement_track ||
                                                  pressed_part_ == ScrollBarPart::increment_track,
                                      false);
    track.key = PartKey{"track", {}, {}};
    paint_part(painter, *this, track);

    // A theme without scroll arrows leaves the buttons empty: not drawn.
    const Rect decrement = decrement_button_bounds();
    const Rect increment = increment_button_bounds();
    if (!decrement.empty()) paint_button(painter, decrement, ScrollBarPart::decrement_button, false, states);
    if (!increment.empty()) paint_button(painter, increment, ScrollBarPart::increment_button, true, states);
    PartPaint thumb;
    thumb.family = ControlVisualRole::button;
    thumb.role = PartRole::thumb;
    thumb.bounds = thumb_bounds();
    thumb.states = scroll_part_states(states, pressed_part_ == ScrollBarPart::thumb, tracking_thumb_);
    thumb.key = PartKey{"thumb", {}, {}};
    paint_part(painter, *this, thumb);
    if (focused_ || tracking_thumb_) {
        PartPaint focus = thumb;
        focus.role = PartRole::focus;
        focus.states.insert(AnatomyState::focus_visible);
        focus.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, focus);
    }
}

void ScrollBar::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    if (event.action == PointerAction::wheel && event.wheel_delta.y != 0.0) {
        const bool decrementing = event.wheel_delta.y > 0.0;
        set_value_from_input(value.read() + (decrementing ? -small_change.read() : small_change.read()),
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
            const double thumb_start = orientation.read() == Orientation::horizontal
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
    double next = value.read();
    RangeAction action = RangeAction::thumb_position;
    bool recognized = true;
    switch (event.physical_key) {
    case PhysicalKey::left:
    case PhysicalKey::up:
        next -= small_change.read(); action = RangeAction::small_decrement; break;
    case PhysicalKey::right:
    case PhysicalKey::down:
        next += small_change.read(); action = RangeAction::small_increment; break;
    case PhysicalKey::page_up:
        next -= large_change.read(); action = RangeAction::large_decrement; break;
    case PhysicalKey::page_down:
        next += large_change.read(); action = RangeAction::large_increment; break;
    case PhysicalKey::home:
        next = minimum.read(); action = RangeAction::first; break;
    case PhysicalKey::end:
        next = maximum.read(); action = RangeAction::last; break;
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
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    std::ostringstream value_text;
    value_text << value.read();
    descriptor.value = value_text.str();
    descriptor.numeric_value = value.read();
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    // The arrow keys move by the small change, the page keys by the large.
    descriptor.small_step = small_change.read();
    descriptor.large_step = large_change.read();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement, SemanticAction::set_value};
    descriptor.exposed = true;
    return descriptor;
}

bool ScrollBar::on_semantic_action(SemanticAction action,
                                   std::string_view value_text) {
    if (action == SemanticAction::increment || action == SemanticAction::decrement) {
        const bool increasing = action == SemanticAction::increment;
        set_value_from_input(value.read() + (increasing ? small_change.read() : -small_change.read()),
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
                   parsed == value.read();
        } catch (const std::exception&) {
            return false;
        }
    }
    return RangeControl::on_semantic_action(action, value_text);
}

namespace {

// RangeControl's large_change, which a scroll bar's form document sets
// besides the range (stage FD6.3): published in ScrollBar's table alone
// (tools/property_families/scroll_bar.json, extra_entries). It raises no
// change event.
PropertyDescriptor scroll_bar_large_change_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "large_change";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "How far a click on the track or Page Up/Down moves the value.";
    descriptor.default_value = BindingValue{10.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_scroll_bar_large_change() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scroll_bar_large_change_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

// property_codemod begin ScrollBar (tools/property_families/scroll_bar.json; generated, do not edit)

const std::array<PropertyEntry, ScrollBar::scroll_bar_property_count>& ScrollBar::scroll_bar_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"large_change", &describe_scroll_bar_large_change,
         &read_cell_entry<ScrollBar, decltype(&ScrollBar::large_change), &ScrollBar::large_change>,
         &write_cell_entry<ScrollBar, decltype(&ScrollBar::large_change), &ScrollBar::large_change>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ScrollBar's property table names a property twice");
    static const std::array<PropertyEntry, scroll_bar_property_count> entries =
        join_property_tables(RangeControl::range_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ScrollBar::property_table() const noexcept {
    const std::array<PropertyEntry, scroll_bar_property_count>& entries = scroll_bar_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ScrollBar::prepare_write(const decltype(button_extent)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "scrollbar button extent must be finite");
    if (incoming < 8.0 || incoming > 64.0) throw std::invalid_argument("scrollbar button extent must be between 8 and 64");
}

void ScrollBar::written(const decltype(button_extent)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ScrollBar::prepare_write(const decltype(minimum_thumb_extent)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "scrollbar thumb extent must be finite");
    if (incoming < 8.0 || incoming > 128.0) throw std::invalid_argument("scrollbar thumb extent must be between 8 and 128");
}

void ScrollBar::written(const decltype(minimum_thumb_extent)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end ScrollBar

} // namespace gui_forms
