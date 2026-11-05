#include "gui_forms/controls/panel/date_time_picker/date_time_picker.hpp"
#include "gui_forms/detail/weak_member_callback.hpp"

#include "calendar_popup/calendar_popup.hpp"
#include "calendar_popup_layer/calendar_popup_layer.hpp"
#include "date_time_utilities.hpp"
#include "gui_forms/text.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace date_time_detail;

DateTimePicker::DateTimePicker(StableId stable_id) : Panel(std::move(stable_id)) {
    set_background(style_.paper);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
}

void DateTimePicker::set_value(DateTimeValue value) {
    require_mutable();
    if (!valid_date_time(value) || value < minimum_ || value > maximum_) {
        throw std::out_of_range("DateTimePicker value is invalid or outside its range");
    }
    if (value_ == value) return;
    value_ = value;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(value_changed_, value_);
}

void DateTimePicker::set_range(DateTimeValue minimum, DateTimeValue maximum) {
    require_mutable();
    if (!valid_date_time(minimum) || !valid_date_time(maximum) || minimum > maximum) {
        throw std::invalid_argument("DateTimePicker requires a valid ordered range");
    }
    if (minimum_ == minimum && maximum_ == maximum) return;
    minimum_ = minimum;
    maximum_ = maximum;
    const DateTimeValue clamped = std::clamp(value_, minimum_, maximum_);
    const bool value_changes = clamped != value_;
    value_ = clamped;
    if (dropped_down_) {
        close_drop_down();
    }
    invalidate(Dirty::paint | Dirty::semantics);
    if (value_changes) publish_change(value_changed_, value_);
}

void DateTimePicker::set_format(DateTimePickerFormat format) {
    require_mutable();
    if (format_ == format) return;
    format_ = format;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void DateTimePicker::set_custom_format(std::string format) {
    require_mutable();
    if (!validate_utf8(format).valid()) {
        throw std::invalid_argument("DateTimePicker custom format must be valid UTF-8");
    }
    if (custom_format_ == format) return;
    custom_format_ = std::move(format);
    if (format_ == DateTimePickerFormat::custom) {
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    }
}

void DateTimePicker::set_format_provider(DateTimeFormatProvider provider) {
    require_mutable();
    validate_provider(provider);
    format_provider_ = std::move(provider);
    if (dropped_down_) close_drop_down();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

namespace {

// The pattern a picker of `format` shows its value in: one of `provider`'s,
// or `custom`.
std::string_view format_pattern(DateTimePickerFormat format, const DateTimeFormatProvider& provider,
                                std::string_view custom) noexcept {
    switch (format) {
    case DateTimePickerFormat::long_date: return provider.long_date_pattern;
    case DateTimePickerFormat::short_date: return provider.short_date_pattern;
    case DateTimePickerFormat::time: return provider.time_pattern;
    case DateTimePickerFormat::custom: return custom;
    }
    // A format outside the enumeration shows nothing.
    return {};
}

} // namespace

std::string DateTimePicker::formatted_value() const {
    const std::string_view pattern = format_pattern(format_, format_provider_, custom_format_);
    return format_date_time(value_, pattern, format_provider_);
}

void DateTimePicker::set_show_check_box(bool show) {
    require_mutable();
    if (show_check_box_ == show) return;
    show_check_box_ = show;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DateTimePicker::set_checked(bool checked) {
    require_mutable();
    if (checked_ == checked) return;
    checked_ = checked;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(checked_changed_, checked_);
}

void DateTimePicker::set_show_up_down(bool show) {
    require_mutable();
    if (show_up_down_ == show) return;
    show_up_down_ = show;
    if (show && dropped_down_) close_drop_down();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DateTimePicker::set_dropped_down(bool dropped_down) {
    require_mutable();
    if (dropped_down == dropped_down_) return;
    if (dropped_down) open_drop_down(); else close_drop_down();
}

void DateTimePicker::set_drop_down_alignment(
    DateTimeDropDownAlignment alignment) {
    require_mutable();
    if (alignment != DateTimeDropDownAlignment::left &&
        alignment != DateTimeDropDownAlignment::right) {
        throw std::invalid_argument(
            "DateTimePicker drop-down alignment is invalid");
    }
    if (drop_down_alignment_ == alignment) return;
    drop_down_alignment_ = alignment;
    invalidate(Dirty::paint | Dirty::semantics);
}

void DateTimePicker::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("DateTimePicker font specification is invalid");
    }
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void DateTimePicker::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_authored_ && style_ == style) return;
    style_ = style;
    style_authored_ = true;
    set_background(style_.paper);
    invalidate(Dirty::paint);
}

DateTimePicker::HitPart DateTimePicker::part_at(Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute)) return HitPart::none;
    const double local_x = absolute.x - bounds.x;
    const double local_y = absolute.y - bounds.y;
    if (show_check_box_ && local_x < 25.0) return HitPart::check;
    if (local_x >= bounds.width - 24.0) {
        if (!show_up_down_) return HitPart::arrow_down;
        return local_y < bounds.height * 0.5 ? HitPart::arrow_up : HitPart::arrow_down;
    }
    return HitPart::body;
}

// The panel's well first; then the drop-down button (or the up-down pair),
// its divider and arrow marks, the check box's well and mark, the value's
// text and the focus ring are parts the theme's engine draws in the
// control's anatomy states, a pressed arrow pressed. An unchecked value's
// text is disabled text. A style set with set_style inks the text and the
// marks as authored colours (T4-1). The ring shows while the field has
// focus or its calendar is open.
void DateTimePicker::on_paint(Painter& painter, Rect damage) {
    Panel::on_paint(painter, damage);
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const AnatomyStateSet states = anatomy_states();
    std::optional<Color> ink{};
    if (style_authored_) ink = style_.text;
    const FontSpec font = text_font(font_);
    const double button_width = std::min(24.0, bounds.width);
    const double button_x = std::max(0.0, bounds.width - button_width);
    const double button_height = std::max(0.0, bounds.height - 2.0);
    const double center_x = button_x + button_width * 0.5;
    const double mark_left = std::floor(center_x) - 4.0;
    PartPaint button;
    button.family = ControlVisualRole::choice;
    button.role = PartRole::actuator;
    PartPaint mark;
    mark.family = ControlVisualRole::choice;
    mark.role = PartRole::disclosure;
    mark.authored_foreground = ink;
    PartPaint divider;
    divider.family = ControlVisualRole::choice;
    divider.role = PartRole::divider;
    divider.states = states;
    divider.bounds = {button_x - 0.5, 1.0, 1.0, button_height};
    if (show_up_down_) {
        const double middle = std::floor(bounds.height * 0.5);
        AnatomyStateSet up_states = states;
        AnatomyStateSet down_states = states;
        if (pressed_part_ == HitPart::arrow_up) up_states.insert(AnatomyState::pressed);
        if (pressed_part_ == HitPart::arrow_down) down_states.insert(AnatomyState::pressed);
        button.states = up_states;
        button.bounds = {button_x, 1.0, std::max(0.0, button_width - 1.0), std::max(0.0, middle - 1.0)};
        paint_part(painter, *this, button);
        button.states = down_states;
        button.bounds = {button_x, middle, std::max(0.0, button_width - 1.0),
                         std::max(0.0, bounds.height - 1.0 - middle)};
        paint_part(painter, *this, button);
        paint_part(painter, *this, divider);
        PartPaint rule = divider;
        rule.bounds = {button_x, middle - 0.5, button_width, 1.0};
        paint_part(painter, *this, rule);
        // The 7 x 4 arrows above and below the middle rule, as the old
        // strokes stood.
        mark.states = up_states;
        mark.mark = PartMark::arrow_up;
        mark.bounds = {mark_left, middle - 8.0, 8.0, 8.0};
        paint_part(painter, *this, mark);
        mark.states = down_states;
        mark.mark = PartMark::arrow_down;
        mark.bounds = {mark_left, middle + 1.0, 8.0, 8.0};
        paint_part(painter, *this, mark);
    } else {
        button.states = states;
        if (pressed_part_ == HitPart::arrow_down) button.states.insert(AnatomyState::pressed);
        button.bounds = {button_x, 1.0, std::max(0.0, button_width - 1.0), button_height};
        paint_part(painter, *this, button);
        paint_part(painter, *this, divider);
        // The arrow flips and moves when the calendar is open, as the combo
        // box's does.
        const double center_y = bounds.height * 0.5 + (dropped_down_ ? 2.0 : -1.0);
        const double square_top = dropped_down_ ? std::floor(center_y) - 3.0 : std::floor(center_y) - 4.0;
        mark.states = button.states;
        mark.mark = dropped_down_ ? PartMark::arrow_up : PartMark::arrow_down;
        mark.bounds = {mark_left, square_top, 8.0, 8.0};
        paint_part(painter, *this, mark);
    }
    double text_left = 7.0;
    AnatomyStateSet text_states = states;
    if (show_check_box_) {
        const double check_y = std::max(3.0, (bounds.height - 14.0) * 0.5);
        PartPaint check;
        check.family = ControlVisualRole::choice;
        check.role = PartRole::well;
        check.states = states;
        check.bounds = {5.0, check_y, 14.0, 14.0};
        paint_part(painter, *this, check);
        if (checked_) {
            check.role = PartRole::indicator;
            check.mark = PartMark::check;
            check.authored_foreground = ink;
            paint_part(painter, *this, check);
        } else {
            text_states.insert(AnatomyState::disabled);
        }
        text_left = 25.0;
    }
    PartPaint content;
    content.family = ControlVisualRole::editor;
    content.role = PartRole::content;
    content.states = text_states;
    content.font = font;
    content.authored_foreground = ink;
    const std::string text = formatted_value();
    content.text = text;
    const Size measured = painter.measure_text_utf8(text, font);
    const double box_height = std::max(measured.height, font.size * 1.2);
    const double box_ascent = std::max(font.size, box_height * 0.82);
    const double baseline = std::max(font.size, (bounds.height + font.size) * 0.5 - 1.0);
    content.bounds = {text_left, baseline - box_ascent, std::max(0.0, button_x - text_left - 3.0), box_height};
    content.measured_text = measured;
    paint_part(painter, *this, content);
    if (focused_ || dropped_down_) {
        PartPaint ring;
        ring.family = ControlVisualRole::editor;
        ring.role = PartRole::focus;
        ring.bounds = bounds;
        ring.states = states;
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
}

void DateTimePicker::on_pointer(PointerEvent& event) {
    if (!eligible_for_input() || event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        pressed_part_ = part_at(event.position);
        activation_part_ = HitPart::none;
        event.handled = pressed_part_ != HitPart::none;
        if (event.handled) invalidate(Dirty::paint);
    } else if (event.action == PointerAction::up && pressed_part_ != HitPart::none) {
        const HitPart released = part_at(event.position);
        if (released == pressed_part_) activation_part_ = pressed_part_;
        pressed_part_ = HitPart::none;
        event.handled = true;
        invalidate(Dirty::paint);
    }
}

void DateTimePicker::on_activate() {
    HitPart part = std::exchange(activation_part_, HitPart::none);
    if (part == HitPart::none) part = HitPart::body;
    if (part == HitPart::check) {
        set_checked(!checked_);
    } else if (show_up_down_ && part == HitPart::arrow_up) {
        step_days(1);
    } else if (show_up_down_ && part == HitPart::arrow_down) {
        step_days(-1);
    } else if (!show_up_down_) {
        set_dropped_down(!dropped_down_);
    }
}

void DateTimePicker::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.type() || event.action != KeyAction::down) return;
    if ((event.physical_key == PhysicalKey::f4 ||
         (event.physical_key == PhysicalKey::down &&
          includes(event.modifiers, Modifier::alt))) && !show_up_down_) {
        set_dropped_down(!dropped_down_);
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::up ||
               event.physical_key == PhysicalKey::right) {
        step_days(1);
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::down ||
               event.physical_key == PhysicalKey::left) {
        step_days(-1);
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::space && show_check_box_) {
        set_checked(!checked_);
        event.handled = true;
    }
}

void DateTimePicker::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void DateTimePicker::step_days(int days) {
    if (show_check_box_ && !checked_) set_checked(true);
    const DateTimeValue candidate = add_days(value_, days);
    if (!civil_in_range(candidate, minimum_, maximum_)) return;
    set_value(std::clamp(candidate, minimum_, maximum_));
}

void DateTimePicker::open_drop_down() {
    if (dropped_down_ || show_up_down_ || !attached() || window() == nullptr) return;
    const Control::Ptr owner = shared_from_this();
    const Size client = (*window()).client_size();
    const Rect picker = absolute_bounds();
    const double width = 286.0 * effective_text_scale();
    const double height = 260.0 * effective_text_scale();
    const double requested_x =
        drop_down_alignment_ == DateTimeDropDownAlignment::right
        ? picker.x + picker.width - width : picker.x;
    const double x = std::clamp(
        requested_x, 0.0, std::max(0.0, client.width - width));
    const double below = picker.y + picker.height;
    const double y = below + height <= client.height ? below
        : std::max(0.0, picker.y - height);
    const std::string prefix(stable_id().value());
    std::shared_ptr<gui_forms::CalendarPopupLayer> layer = make_control<CalendarPopupLayer>(StableId(prefix + ".popup.layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    std::shared_ptr<gui_forms::CalendarPopup> calendar = make_control<CalendarPopup>(
        StableId(prefix + ".popup.calendar"), value_, minimum_, maximum_,
        format_provider_);
    (*calendar).set_requested_bounds({x, y, width, height});
    (*layer).add_child(calendar);
    PopupToken token = (*window()).open_popup(owner, layer);

    popup_layer_ = layer;
    popup_calendar_ = calendar;
    popup_token_ = std::move(token);
    const std::weak_ptr<DateTimePicker> weak =
        std::static_pointer_cast<DateTimePicker>(shared_from_this());
    popup_commit_ = (*calendar).committed().subscribe(
        *this,
        detail::WeakMemberCallback<void (DateTimePicker::*)(DateTimeValue)>(
            weak, &DateTimePicker::commit_popup_value));
    popup_cancel_ = (*calendar).cancelled().subscribe(
        *this,
        detail::WeakMemberCallback<void (DateTimePicker::*)()>(
            weak, &DateTimePicker::close_drop_down));
    popup_dismiss_ = (*layer).dismissed().subscribe(
        *this,
        detail::WeakMemberCallback<void (DateTimePicker::*)()>(
            weak, &DateTimePicker::close_drop_down));
    if (Event<>* closed = popup_token_.closed_event()) {
        popup_revocation_ = (*closed).subscribe(
            *this,
            detail::WeakMemberCallback<void (DateTimePicker::*)()>(
                weak, &DateTimePicker::on_popup_revoked));
    }
    popup_scope_ = (*window()).begin_focus_scope(layer, calendar).value;
    dropped_down_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(drop_down_changed_, true);
}

void DateTimePicker::close_drop_down() {
    if (!dropped_down_ && !popup_layer_) return;
    closing_popup_ = true;
    popup_commit_.disconnect();
    popup_cancel_.disconnect();
    popup_dismiss_.disconnect();
    if (window() != nullptr && popup_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(FocusScopeId{popup_scope_}));
    }
    popup_scope_ = 0U;
    popup_token_.disconnect();
    popup_revocation_.disconnect();
    popup_calendar_.reset();
    popup_layer_.reset();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    closing_popup_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void DateTimePicker::on_popup_revoked() {
    if (closing_popup_) return;
    popup_commit_.disconnect();
    popup_cancel_.disconnect();
    popup_dismiss_.disconnect();
    popup_revocation_.disconnect();
    if (window() != nullptr && popup_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(
            FocusScopeId{popup_scope_}, FocusScopeCloseReason::owner_unavailable));
    }
    popup_scope_ = 0U;
    popup_calendar_.reset();
    popup_layer_.reset();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void DateTimePicker::commit_popup_value(DateTimeValue value) {
    set_value(value);
    close_drop_down();
}

SemanticDescriptor DateTimePicker::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::date_picker;
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.value = formatted_value();
    if (show_check_box_) descriptor.states |= SemanticState::checkable;
    if (show_check_box_ && checked_) descriptor.states |= SemanticState::checked;
    if (dropped_down_) descriptor.states |= SemanticState::expanded;
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement, SemanticAction::set_value};
    if (show_check_box_) {
        descriptor.actions.push_back(SemanticAction::press);
    }
    if (!show_up_down_) {
        descriptor.actions.push_back(dropped_down_ ? SemanticAction::collapse
                                                   : SemanticAction::expand);
    }
    descriptor.exposed = true;
    return descriptor;
}

bool DateTimePicker::on_semantic_action(SemanticAction action,
                                        std::string_view value) {
    if (action == SemanticAction::focus) {
        return Control::on_semantic_action(action, value);
    }
    if (action == SemanticAction::increment) {
        step_days(1);
        return true;
    }
    if (action == SemanticAction::decrement) {
        step_days(-1);
        return true;
    }
    if (action == SemanticAction::press && show_check_box_) {
        set_checked(!checked_);
        return true;
    }
    if (action == SemanticAction::expand && !show_up_down_) {
        set_dropped_down(true);
        return dropped_down_;
    }
    if (action == SemanticAction::collapse && !show_up_down_) {
        set_dropped_down(false);
        return !dropped_down_;
    }
    if (action == SemanticAction::set_value) {
        DateTimeValue parsed = value_;
        if (!parse_iso_date(value, parsed) || parsed < minimum_ || parsed > maximum_) {
            return false;
        }
        if (show_check_box_ && !checked_) set_checked(true);
        set_value(parsed);
        return true;
    }
    return false;
}

void DateTimePicker::on_detached_from_window() noexcept {
    try {
        close_drop_down();
    } catch (...) {
    }
}


namespace {

constexpr std::array<AnatomyState, 1> date_time_picker_pressable_states{{AnatomyState::pressed}};

constexpr std::array<PartField<DateTimePicker::Parts>, 9> date_time_picker_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &DateTimePicker::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &DateTimePicker::Parts::edge},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &DateTimePicker::Parts::content},
    {.declaration = {.name = "drop_down", .role = PartRole::actuator, .composed_from = "button"},
     .member = &DateTimePicker::Parts::drop_down},
    {.declaration = {.name = "disclosure", .role = PartRole::disclosure, .composed_from = {}},
     .member = &DateTimePicker::Parts::disclosure},
    {.declaration = {.name = "increment_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = date_time_picker_pressable_states},
     .member = &DateTimePicker::Parts::increment_arrow},
    {.declaration = {.name = "decrement_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = date_time_picker_pressable_states},
     .member = &DateTimePicker::Parts::decrement_arrow},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {},
                     .states = date_time_picker_pressable_states},
     .member = &DateTimePicker::Parts::well},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &DateTimePicker::Parts::indicator},
}};

constexpr std::array<StateField<DateTimePicker::States>, 10> date_time_picker_state_fields{{
    {.state = AnatomyState::normal, .member = &DateTimePicker::States::normal},
    {.state = AnatomyState::pressed, .member = &DateTimePicker::States::pressed},
    {.state = AnatomyState::captured, .member = &DateTimePicker::States::captured},
    {.state = AnatomyState::enabled, .member = &DateTimePicker::States::enabled},
    {.state = AnatomyState::disabled, .member = &DateTimePicker::States::disabled},
    {.state = AnatomyState::focused, .member = &DateTimePicker::States::focused},
    {.state = AnatomyState::focus_visible, .member = &DateTimePicker::States::focus_visible},
    {.state = AnatomyState::open, .member = &DateTimePicker::States::open},
    {.state = AnatomyState::closed, .member = &DateTimePicker::States::closed},
    {.state = AnatomyState::checked, .member = &DateTimePicker::States::checked},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 9>
    date_time_picker_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(date_time_picker_extra_part_fields));
constexpr std::array<AnatomyState, 10> date_time_picker_states =
    state_declarations(date_time_picker_state_fields);

constexpr ControlAnatomy date_time_picker_anatomy{
    .control_id = "date_time_picker",
    .parts = date_time_picker_parts,
    .states = date_time_picker_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& DateTimePicker::anatomy() const noexcept {
    return date_time_picker_anatomy;
}

DateTimePicker::Parts DateTimePicker::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), date_time_picker_extra_part_fields, published.parts(), record);
    return record;
}

DateTimePicker::States DateTimePicker::states() const {
    States record;
    read_states(date_time_picker_state_fields, anatomy_states(), record);
    return record;
}

void DateTimePicker::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = panel.face;
    record.edge = panel.edge;
    const double button_width = std::min(24.0, bounds.width);
    const double button_x = std::max(0.0, bounds.width - button_width);
    const Rect button{button_x, 1.0, std::max(0.0, button_width - 1.0),
                      std::max(0.0, bounds.height - 2.0)};
    const double center_x = button_x + button_width * 0.5;
    if (show_up_down_) {
        // The up/down actuators are the button's halves at the divider row.
        const double middle = std::floor(bounds.height * 0.5);
        const Rect upper{button.x, button.y, button.width, std::max(0.0, middle - button.y)};
        const Rect lower{button.x, middle, button.width,
                         std::max(0.0, button.bottom() - middle)};
        record.increment_arrow = PartGeometry::from_bounds(upper);
        record.decrement_arrow = PartGeometry::from_bounds(lower);
        if (pressed_part_ == HitPart::arrow_up) {
            record.increment_arrow.states.insert(AnatomyState::pressed);
        } else if (pressed_part_ == HitPart::arrow_down) {
            record.decrement_arrow.states.insert(AnatomyState::pressed);
        }
    } else {
        // The chevron's two one-unit strokes span 8 x 4 about its centre.
        const double center_y = bounds.height * 0.5 + (dropped_down_ ? 2.0 : -1.0);
        const Rect chevron{center_x - 4.5, center_y - 2.5, 9.0, 5.0};
        record.drop_down = PartGeometry::from_bounds(button);
        record.disclosure = PartGeometry::from_bounds(chevron);
    }
    double text_left = 7.0;
    if (show_check_box_) {
        const double check_y = std::max(3.0, (bounds.height - 14.0) * 0.5);
        const Rect box{5.0, check_y, 14.0, 14.0};
        record.well = PartGeometry::from_bounds(box);
        if (pressed_part_ == HitPart::check) {
            record.well.states.insert(AnatomyState::pressed);
        }
        if (checked_) {
            // Two-unit strokes through (8, 7), (11, 10) and (17, 3.5) from the box top.
            const Rect mark{7.0, check_y + 2.5, 11.0, 8.5};
            record.indicator = PartGeometry::from_bounds(mark);
        }
        text_left = 25.0;
    }
    const Rect content{text_left, 2.0, std::max(0.0, button_x - text_left - 3.0),
                       std::max(0.0, bounds.height - 4.0)};
    record.content = PartGeometry::from_bounds(content);
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), date_time_picker_extra_part_fields, record, geometry);
}

AnatomyStateSet DateTimePicker::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (dropped_down_) {
        states.insert(AnatomyState::open);
    } else {
        states.insert(AnatomyState::closed);
    }
    if (show_check_box_ && checked_) {
        states.insert(AnatomyState::checked);
    }
    return states;
}

namespace {

// The events DateTimePicker adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 3> date_time_picker_interactions{{
    {.name = "value_changed", .payload = "DateTimeValue", .mutable_payload = false},
    {.name = "checked_changed", .payload = "bool", .mutable_payload = false},
    {.name = "drop_down_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void DateTimePicker::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), date_time_picker_interactions.begin(), date_time_picker_interactions.end());
}

} // namespace gui_forms
