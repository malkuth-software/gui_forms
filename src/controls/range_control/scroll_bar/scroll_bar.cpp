#include "gui_forms/controls/scroll_bar.hpp"

#include "../range_control_rendering.hpp"
#include "scroll_bar_anatomy.hpp"
#include "scroll_bar_painting.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "../../basic/number_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

ScrollBar::ScrollBar(StableId stable_id, Orientation initial_orientation)
    : RangeControl(std::move(stable_id)) {
    orientation = initial_orientation;
    focusable = true;
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
    // The minimum's end: the left, or the right of a mirrored bar.
    if (mirrored_axis()) return Rect{std::max(0.0, bounds.width - extent), 0.0, extent, bounds.height};
    return orientation.read() == Orientation::horizontal
        ? Rect{0.0, 0.0, extent, bounds.height}
        : Rect{0.0, 0.0, bounds.width, extent};
}

Rect ScrollBar::increment_button_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const double axis = orientation.read() == Orientation::horizontal
        ? bounds.width : bounds.height;
    const double extent = arrow_extent(axis);
    if (mirrored_axis()) return Rect{0.0, 0.0, extent, bounds.height};
    return orientation.read() == Orientation::horizontal
        ? Rect{std::max(0.0, bounds.width - extent), 0.0, extent, bounds.height}
        : Rect{0.0, std::max(0.0, bounds.height - extent), bounds.width, extent};
}

Rect ScrollBar::track_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect decrement = decrement_button_bounds();
    const Rect increment = increment_button_bounds();
    if (mirrored_axis()) {
        // Between the increment button (left) and the decrement (right).
        const Rect between{increment.width, 0.0, std::max(0.0, decrement.x - increment.width), bounds.height};
        return between;
    }
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
    if (track_extent <= 0.0) return Rect{track.x, track.y, 0.0, 0.0};
    const double range = maximum.read() - minimum.read();
    const double proportion = large_change.read() / (range + large_change.read());
    const scroll_bar_detail::ThumbPlacement placed = scroll_bar_detail::place_thumb(
        track_extent, proportion, normalized_value(), minimum_thumb_extent.read());
    if (mirrored_axis()) {
        // The offset runs from the right end.
        const Rect thumb{track.x + track.width - placed.offset - placed.extent, track.y, placed.extent, track.height};
        return thumb;
    }
    return orientation.read() == Orientation::horizontal
        ? Rect{track.x + placed.offset, track.y, placed.extent, track.height}
        : Rect{track.x, track.y + placed.offset, track.width, placed.extent};
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
    // Before the thumb is toward the minimum, unless the bar is mirrored.
    const bool before = coordinate < thumb_start;
    const bool decrementing = before != mirrored_axis();
    return decrementing ? ScrollBarPart::decrement_track : ScrollBarPart::increment_track;
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
    const Rect decrement_button = decrement_button_bounds();
    const Rect increment_button = increment_button_bounds();
    record.decrement_arrow = PartGeometry::from_bounds(decrement_button);
    record.increment_arrow = PartGeometry::from_bounds(increment_button);
    record.decrement_glyph = PartGeometry::from_bounds(scroll_bar_detail::arrow_mark_bounds(decrement_button));
    record.increment_glyph = PartGeometry::from_bounds(scroll_bar_detail::arrow_mark_bounds(increment_button));
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
    // The part under the pointer carries the hover.
    if (hot_part_ == ScrollBarPart::decrement_button) {
        record.decrement_arrow.states.insert(AnatomyState::hot);
    } else if (hot_part_ == ScrollBarPart::increment_button) {
        record.increment_arrow.states.insert(AnatomyState::hot);
    } else if (hot_part_ == ScrollBarPart::decrement_track ||
               hot_part_ == ScrollBarPart::increment_track) {
        record.track.states.insert(AnatomyState::hot);
    } else if (hot_part_ == ScrollBarPart::thumb) {
        record.thumb.states.insert(AnatomyState::hot);
    }
    // Each mark takes its button's states.
    record.decrement_glyph.states = record.decrement_arrow.states;
    record.increment_glyph.states = record.increment_arrow.states;
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
    const double from_left = std::clamp((coordinate - track_start) / movable, 0.0, 1.0);
    // A mirrored bar's thumb at the left edge is at its maximum.
    const double ratio = mirrored_axis() ? 1.0 - from_left : from_left;
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

// The trough, track, arrow buttons and thumb are drawn as every scroll bar
// draws them (scroll_bar_detail::paint_scroll_bar_parts): only the pressed
// part shows pressed and only the part under the pointer hot. The focus cue
// rings the thumb, or the track while the bar is too short for a thumb.
void ScrollBar::on_paint(Painter& painter, Rect) {
    scroll_bar_detail::ScrollBarPaintParts parts;
    parts.orientation = orientation.read();
    parts.bar = local_bounds();
    parts.track = track_bounds();
    parts.decrement = decrement_button_bounds();
    parts.increment = increment_button_bounds();
    parts.thumb = thumb_bounds();
    parts.states = anatomy_states();
    parts.pressed = pressed_part_;
    parts.hot = hot_part_;
    parts.thumb_captured = tracking_thumb_;
    parts.mirrored = mirrored_axis();
    scroll_bar_detail::paint_scroll_bar_parts(painter, *this, parts);
    if (focused_ || tracking_thumb_) {
        PartPaint focus;
        focus.family = ControlVisualRole::button;
        focus.role = PartRole::focus;
        focus.bounds = parts.thumb.empty() ? parts.track : parts.thumb;
        focus.states = parts.states;
        focus.states.insert(AnatomyState::focus_visible);
        focus.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, focus);
    }
}

// Moves the hover onto `part`, repainting the bar when it changes.
void ScrollBar::set_hot_part(ScrollBarPart part) {
    if (hot_part_ == part) return;
    hot_part_ = part;
    invalidate(Dirty::paint);
}

void ScrollBar::on_mouse_leave() {
    set_hot_part(ScrollBarPart::none);
    RangeControl::on_mouse_leave();
}

void ScrollBar::on_mouse_wheel(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.wheel_delta.y != 0.0) {
        const bool decrementing = arguments.wheel_delta.y > 0.0;
        set_value_from_input(value.read() + (decrementing ? -small_change.read() : small_change.read()),
                             decrementing ? RangeAction::small_decrement
                                          : RangeAction::small_increment);
        arguments.handled = true;
    }
    RangeControl::on_mouse_wheel(arguments);
}

void ScrollBar::on_mouse_down(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary) press_part(arguments);
    RangeControl::on_mouse_down(arguments);
}

void ScrollBar::on_mouse_move(MouseEventArgs& arguments) {
    if (eligible_for_input()) {
        const Point local = arguments.location;
        // The hover follows the pointer, except that a dragged thumb keeps it.
        if (!tracking_thumb_) set_hot_part(part_at(local));
        if (tracking_thumb_) {
            const double coordinate = axis_coordinate(local) - thumb_drag_offset_;
            set_value_from_input(value_from_thumb_coordinate(coordinate),
                                 RangeAction::thumb_track);
            arguments.handled = true;
        } else if (pressed_part_ != ScrollBarPart::none) {
            repeat_pointer_ = local;
            arguments.handled = true;
        }
    }
    RangeControl::on_mouse_move(arguments);
}

void ScrollBar::on_mouse_up(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary &&
        pressed_part_ != ScrollBarPart::none) {
        const bool was_thumb = tracking_thumb_;
        if (has_pointer_capture()) set_pointer_capture(false);
        stop_interaction();
        if (was_thumb) invalidate(Dirty::paint | Dirty::semantics);
        else invalidate(Dirty::paint);
        arguments.handled = true;
    }
    RangeControl::on_mouse_up(arguments);
}

// A primary press focuses the bar and presses the part under the pointer:
// the thumb starts a drag, any other part applies at once and repeats.
void ScrollBar::press_part(MouseEventArgs& arguments) {
    if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
    const Point local = arguments.location;
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
    arguments.handled = true;
}

void ScrollBar::on_key_down(KeyEventArgs& arguments) {
    RangeControl::on_key_down(arguments);
    if (arguments.handled || !focused_) return;
    double next = value.read();
    RangeAction action = RangeAction::thumb_position;
    bool recognized = true;
    // Left and Right move the thumb that way: on a mirrored bar Left
    // increments.
    std::uint32_t key = arguments.physical_key;
    if (mirrored_axis() && key == PhysicalKey::left) key = PhysicalKey::right;
    else if (mirrored_axis() && key == PhysicalKey::right) key = PhysicalKey::left;
    switch (key) {
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
        arguments.handled = true;
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
        double parsed{};
        if (!number_text::parse_finite_decimal(value_text, parsed)) return false;
        // A refusal by a bound value's validation fails the client's request.
        try {
            return set_value_from_input(parsed, RangeAction::thumb_position) || parsed == value.read();
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
    if (incoming < scroll_thumb_extent_least || incoming > scroll_thumb_extent_most) throw std::invalid_argument("scrollbar thumb extent must be between 8 and 128");
}

void ScrollBar::written(const decltype(minimum_thumb_extent)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end ScrollBar

namespace scroll_bar_detail {

namespace {

// `states` with the press, capture and hover moved onto one part: each only
// while its flag is true.
AnatomyStateSet part_states(AnatomyStateSet states, bool pressed, bool captured, bool hot) {
    states.erase(AnatomyState::pressed);
    states.erase(AnatomyState::captured);
    states.erase(AnatomyState::hot);
    if (pressed) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
    }
    if (captured) states.insert(AnatomyState::captured);
    if (hot) states.insert(AnatomyState::hot);
    return states;
}

bool track_part(ScrollBarPart part) noexcept {
    const bool track = part == ScrollBarPart::decrement_track || part == ScrollBarPart::increment_track;
    return track;
}

// One arrow button: its actuator part and, where the button holds the
// mark's square, its arrow mark centred in it.
void paint_arrow_button(Painter& painter, const Control& control, const ScrollBarPaintParts& drawn,
                        Rect button, bool incrementing) {
    if (button.empty()) return;
    const ScrollBarPart part = incrementing ? ScrollBarPart::increment_button : ScrollBarPart::decrement_button;
    const AnatomyStateSet states = part_states(drawn.states, drawn.pressed == part, false, drawn.hot == part);
    PartPaint actuator;
    actuator.family = ControlVisualRole::button;
    actuator.role = PartRole::actuator;
    actuator.bounds = button;
    actuator.states = states;
    actuator.key = PartKey{incrementing ? "increment_arrow" : "decrement_arrow", drawn.item, {}};
    actuator.control_type = drawn.control_type;
    paint_part(painter, control, actuator);
    const Rect mark = arrow_mark_bounds(button);
    if (mark.empty()) return;
    const bool horizontal = drawn.orientation == Orientation::horizontal;
    PartPaint arrow;
    arrow.family = ControlVisualRole::button;
    arrow.role = PartRole::glyph;
    if (horizontal) {
        // The button at the right end points right, in either direction.
        const bool points_right = incrementing != drawn.mirrored;
        arrow.mark = points_right ? PartMark::arrow_right : PartMark::arrow_left;
    } else {
        arrow.mark = incrementing ? PartMark::arrow_down : PartMark::arrow_up;
    }
    arrow.bounds = mark;
    arrow.states = states;
    arrow.key = PartKey{incrementing ? "increment_glyph" : "decrement_glyph", drawn.item, {}};
    arrow.control_type = drawn.control_type;
    paint_part(painter, control, arrow);
}

} // namespace

Rect arrow_mark_bounds(Rect button) noexcept {
    if (button.width < scroll_arrow_mark_extent || button.height < scroll_arrow_mark_extent) return Rect{};
    const double half = scroll_arrow_mark_extent * 0.5;
    const Rect mark{std::floor(button.x + button.width * 0.5) - half,
                    std::floor(button.y + button.height * 0.5) - half,
                    scroll_arrow_mark_extent, scroll_arrow_mark_extent};
    return mark;
}

void paint_scroll_bar_parts(Painter& painter, const Control& control, const ScrollBarPaintParts& drawn) {
    PartPaint trough;
    trough.family = ControlVisualRole::panel;
    trough.role = PartRole::track;
    trough.bounds = drawn.bar;
    trough.states = part_states(drawn.states, false, false, false);
    trough.control_type = drawn.control_type;
    paint_part(painter, control, trough);
    PartPaint track;
    track.family = ControlVisualRole::editor;
    track.role = PartRole::track;
    track.bounds = drawn.track;
    track.states = part_states(drawn.states, track_part(drawn.pressed), false, track_part(drawn.hot));
    track.key = PartKey{"track", drawn.item, {}};
    track.control_type = drawn.control_type;
    paint_part(painter, control, track);
    paint_arrow_button(painter, control, drawn, drawn.decrement, false);
    paint_arrow_button(painter, control, drawn, drawn.increment, true);
    if (drawn.thumb.empty()) return;
    PartPaint thumb;
    thumb.family = ControlVisualRole::button;
    thumb.role = PartRole::thumb;
    thumb.bounds = drawn.thumb;
    thumb.states = part_states(drawn.states, drawn.pressed == ScrollBarPart::thumb, drawn.thumb_captured,
                               drawn.hot == ScrollBarPart::thumb);
    thumb.key = PartKey{"thumb", drawn.item, {}};
    thumb.control_type = drawn.control_type;
    paint_part(painter, control, thumb);
}

ThumbPlacement place_thumb(double track_extent, double proportion, double fraction,
                           double minimum_extent) noexcept {
    ThumbPlacement placed;
    // A share or fraction that is not a number (an empty range) reads as
    // the whole content and the start.
    const double along = fraction >= 0.0 ? std::min(fraction, 1.0) : 0.0;
    if (!(track_extent > 0.0)) return placed;
    if (track_extent < minimum_extent) {
        // No room for a thumb: none is shown; the value's place still
        // divides the track into its paging halves.
        placed.offset = along * track_extent;
        return placed;
    }
    const double share = proportion >= 0.0 ? std::min(proportion, 1.0) : 1.0;
    placed.extent = std::clamp(track_extent * share, minimum_extent, track_extent);
    placed.offset = along * (track_extent - placed.extent);
    return placed;
}

} // namespace scroll_bar_detail

} // namespace gui_forms
