#include "calendar_overlay.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

namespace gui_forms {

CalendarOverlay::~CalendarOverlay() {}

namespace {

// A mark's square, `side` units across on whole units, centred on `centre`.
Rect mark_square(Point centre, double side) noexcept {
    const Rect square{std::floor(centre.x - side * 0.5), std::floor(centre.y - side * 0.5), side, side};
    return square;
}

} // namespace

// The overlay lies over the window's content, so it casts the theme's overlay
// shadow from its square face (owner decision RB-37). The face is the
// panel's; the month title, weekday names and hint are labels, the names
// and the hint in the secondary tone; each month arrow is a menu item's
// actuator and arrow mark, unavailable at the end of the range. A day is a
// menu item's plate (selected, hot under the pointer, disabled outside the
// range) with its number as content: days of the neighbouring months are
// secondary text. The focus ring marks the selected day while the overlay has
// focus.
void CalendarOverlay::on_paint(Painter& painter, Rect) {
    const Rect arranged = committed_arranged_bounds();
    const double s = effective_text_scale();
    const Rect body{0.0, 0.0, std::max(0.0, arranged.width - 3.0 * s),
                    std::max(0.0, arranged.height - 3.0 * s)};
    PartPaint surface;
    surface.family = ControlVisualRole::panel;
    surface.role = PartRole::shadow;
    surface.bounds = body;
    surface.corner_radius = 0.0;
    paint_part(painter, *this, surface);
    surface.role = PartRole::face;
    paint_part(painter, *this, surface);

    PartPaint label;
    label.family = ControlVisualRole::panel;
    label.role = PartRole::label;
    label.alignment = PartTextAlignment::center;
    label.font = effective_font({FontRole::control, 12.0, 700, false});
    label.bounds = {40.0 * s, 8.0 * s, std::max(0.0, body.width - 80.0 * s), 24.0 * s};
    const std::string title = provider_.month_names[display_month_ - 1U] + " " +
                              std::to_string(display_year_);
    label.text = title;
    paint_part(painter, *this, label);
    label.text = {};

    const double mark_side = std::round(8.0 * s);
    PartPaint arrow;
    arrow.family = ControlVisualRole::menu_item;
    arrow.role = PartRole::actuator;
    arrow.bounds = {8.0 * s, 8.0 * s, 24.0 * s, 24.0 * s};
    if (!can_change_month(-1)) arrow.states.insert(AnatomyState::unavailable);
    paint_part(painter, *this, arrow);
    arrow.role = PartRole::glyph;
    arrow.mark = PartMark::arrow_left;
    arrow.bounds = mark_square({20.0 * s, 20.0 * s}, mark_side);
    paint_part(painter, *this, arrow);
    arrow.states = {};
    if (!can_change_month(1)) arrow.states.insert(AnatomyState::unavailable);
    arrow.role = PartRole::actuator;
    arrow.mark = PartMark::none;
    arrow.bounds = {body.width - 32.0 * s, 8.0 * s, 24.0 * s, 24.0 * s};
    paint_part(painter, *this, arrow);
    arrow.role = PartRole::glyph;
    arrow.mark = PartMark::arrow_right;
    arrow.bounds = mark_square({body.width - 20.0 * s, 20.0 * s}, mark_side);
    paint_part(painter, *this, arrow);

    constexpr std::array<std::string_view, 7> narrow_days{"S", "M", "T", "W", "T", "F", "S"};
    label.tone = PartTextTone::secondary;
    label.font = effective_font({FontRole::control, 10.0, 700, false});
    for (std::size_t column = 0U; column < narrow_days.size(); ++column) {
        const Rect cell = calendar_detail::month_grid_cell_bounds(calendar_overlay_grid, column, s);
        label.bounds = {cell.x, 44.0 * s, cell.width, 18.0 * s};
        label.text = narrow_days[column];
        paint_part(painter, *this, label);
    }
    label.text = {};

    const std::array<DateTimeValue, 42> dates = cell_dates();
    const FontSpec day_font = effective_font({FontRole::content, 11.0, 400, false});
    FontSpec selected_font = day_font;
    selected_font.weight = 700;
    PartPaint day;
    day.family = ControlVisualRole::menu_item;
    day.alignment = PartTextAlignment::center;
    for (std::size_t cell = 0U; cell < dates.size(); ++cell) {
        const DateTimeValue date = dates[cell];
        const bool in_range = civil_in_range(date, minimum_, maximum_);
        const bool in_month = date.month == display_month_ && date.year == display_year_;
        const bool selected = date.year == selected_.year && date.month == selected_.month &&
                              date.day == selected_.day;
        day.states = {};
        if (selected) day.states.insert(AnatomyState::selected);
        if (!selected && in_range && hovered_cell_ == cell) day.states.insert(AnatomyState::hot);
        if (!in_range) day.states.insert(AnatomyState::disabled);
        day.bounds = calendar_detail::month_grid_cell_bounds(calendar_overlay_grid, cell, s);
        day.role = PartRole::face;
        paint_part(painter, *this, day);
        day.role = PartRole::content;
        day.font = selected ? selected_font : day_font;
        day.tone = in_month || selected ? PartTextTone::ordinary : PartTextTone::secondary;
        const std::string number = std::to_string(date.day);
        day.text = number;
        paint_part(painter, *this, day);
        day.text = {};
        if (focused_ && selected) {
            day.role = PartRole::focus;
            day.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, day);
        }
    }

    PartPaint rule;
    rule.family = ControlVisualRole::panel;
    rule.role = PartRole::divider;
    rule.bounds = {8.0 * s, 231.0 * s - 0.5, std::max(0.0, body.width - 16.0 * s), 1.0};
    paint_part(painter, *this, rule);
    label.alignment = PartTextAlignment::start;
    label.font = effective_font({FontRole::content, 9.0, 400, false});
    label.text = "Arrows move · Enter selects · Esc cancels";
    paint_text_part_at(painter, *this, label, {10.0 * s, 248.0 * s}, std::max(0.0, body.width - 20.0 * s));
}

Insets CalendarOverlay::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

} // namespace gui_forms
