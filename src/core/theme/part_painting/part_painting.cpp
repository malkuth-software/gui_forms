#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "built_in_recipe.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gui_forms {
namespace {

// Bounds whose arithmetic is meaningful: finite and not empty.
bool paintable(Rect bounds) noexcept {
    const bool result = bounds.finite() && !bounds.empty();
    return result;
}

// Roles painted as a surface of the recipe's material: the ones an accent
// tints.
bool surface_role(PartRole role) noexcept {
    switch (role) {
    case PartRole::face:
    case PartRole::well:
    case PartRole::track:
    case PartRole::fill:
    case PartRole::thumb:
    case PartRole::actuator:
    case PartRole::chip:
    case PartRole::cap:
    case PartRole::activity:
    case PartRole::selection:
        return true;
    case PartRole::region:
    case PartRole::shadow:
    case PartRole::edge:
    case PartRole::keyline:
    case PartRole::perimeter:
    case PartRole::sheen:
    case PartRole::seam:
    case PartRole::divider:
    case PartRole::caption:
    case PartRole::indicator:
    case PartRole::glyph:
    case PartRole::icon:
    case PartRole::label:
    case PartRole::content:
    case PartRole::caret:
    case PartRole::disclosure:
    case PartRole::grip:
    case PartRole::focus:
    case PartRole::preselect:
    case PartRole::validation:
    case PartRole::drop:
    case PartRole::image:
    case PartRole::count:
        return false;
    }
    // A role outside the enumeration is not a surface.
    return false;
}

// The corner radius a surface part is drawn with: its own, when set (never
// negative), else its recipe's material's.
double part_corner_radius(const PartPaint& part, const ControlVisualRecipe& recipe) noexcept {
    if (part.corner_radius) {
        const double result = std::max(0.0, *part.corner_radius);
        return result;
    }
    return recipe.material.corner_radius;
}

// The overlay shadow's layers around `part.bounds`, in theme order, before
// the casting surface is painted over them.
void paint_overlay_shadow(Painter& painter, const PartPaint& part, const ControlVisualRecipe& recipe,
                          const std::vector<MaterialShadow>& layers) {
    const double radius = part_corner_radius(part, recipe);
    for (const MaterialShadow& shadow : layers) {
        painter.draw_box_shadow(part.bounds, radius, shadow.offset, shadow.blur_radius, shadow.spread,
                                shadow.color);
    }
}

bool any_state(AnatomyStateSet states, AnatomyState first, AnatomyState second) noexcept {
    const bool result = states.contains(first) || states.contains(second);
    return result;
}

// The surface of a part whose role is a plate, well or mark ground.
void paint_surface(Painter& painter, const PartPaint& part, const ControlVisualRecipe& recipe) {
    if (!part.corner_radius) {
        paint_surface_material(painter, part.bounds, recipe.material);
        return;
    }
    SurfaceMaterial shaped = recipe.material;
    shaped.corner_radius = std::max(0.0, *part.corner_radius);
    paint_surface_material(painter, part.bounds, shaped);
}

// A divider or seam: one line across the long axis of `bounds`, centred.
void paint_rule(Painter& painter, Rect bounds, const ControlVisualRecipe& recipe) {
    const Color color = recipe.material.border ? (*recipe.material.border).color : recipe.muted_text;
    if (bounds.width >= bounds.height) {
        const double y = bounds.y + bounds.height * 0.5;
        painter.draw_line({bounds.x, y}, {bounds.x + bounds.width, y}, color, 1.0);
    } else {
        const double x = bounds.x + bounds.width * 0.5;
        painter.draw_line({x, bounds.y}, {x, bounds.y + bounds.height}, color, 1.0);
    }
}

// The preselect (default) ring, inside the part's bounds.
void paint_preselect(Painter& painter, Rect bounds, const ControlVisualRecipe& recipe) {
    const double width = recipe.default_width;
    if (width <= 0.0 || bounds.width <= width * 2.0 || bounds.height <= width * 2.0) return;
    const double inset = width * 0.5;
    const Rect ring{bounds.x + inset, bounds.y + inset,
                    bounds.width - inset * 2.0, bounds.height - inset * 2.0};
    painter.stroke_rounded_rect(ring, std::max(0.0, recipe.material.corner_radius - inset),
                                recipe.default_ring, width);
}

// One side of a dotted ring: `count` square dots `width` across, the first
// with its top-left corner at `start`, each next one two widths further
// along `step` (a unit axis).
void paint_dot_run(Painter& painter, Point start, Point step, std::size_t count, double width, Color colour) {
    const double pitch = width * 2.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double along = pitch * static_cast<double>(index);
        const Rect dot{start.x + step.x * along, start.y + step.y * along, width, width};
        painter.fill_rect(dot, colour);
    }
}

// A dotted ring whose stroke is centred on `ring`'s edges: square dots one
// width across, one every two widths, clockwise from the top-left corner,
// which always holds a dot. Each side holds the dots that start before the
// next corner, so the corners join without doubling a dot.
void paint_dotted_ring(Painter& painter, Rect ring, double width, Color colour) {
    const double half = width * 0.5;
    // The span the dots' top-left corners run along on each axis: from the
    // stroke's outer corner to its far side less one width.
    const double across = ring.width;
    const double down = ring.height;
    if (across <= 0.0 || down <= 0.0) return;
    const double pitch = width * 2.0;
    const std::size_t across_count = static_cast<std::size_t>(std::ceil(across / pitch));
    const std::size_t down_count = static_cast<std::size_t>(std::ceil(down / pitch));
    const double left = ring.x - half;
    const double top = ring.y - half;
    const double right = left + across;
    const double bottom = top + down;
    paint_dot_run(painter, Point{left, top}, Point{1.0, 0.0}, across_count, width, colour);
    paint_dot_run(painter, Point{right, top}, Point{0.0, 1.0}, down_count, width, colour);
    paint_dot_run(painter, Point{right, bottom}, Point{-1.0, 0.0}, across_count, width, colour);
    paint_dot_run(painter, Point{left, bottom}, Point{0.0, -1.0}, down_count, width, colour);
}

// The focus ring: the recipe's authored outline outside the bounds, or the
// house ring inset inside them; dotted when the recipe says so.
void paint_focus_ring(Painter& painter, Rect bounds, const ControlVisualRecipe& recipe) {
    if (recipe.focus_width <= 0.0 || bounds.width <= 8.0 || bounds.height <= 8.0) return;
    Rect ring{};
    double radius = 0.0;
    if (recipe.authored_focus_outline) {
        const double outset = recipe.focus_offset + recipe.focus_width * 0.5;
        ring = Rect{bounds.x - outset, bounds.y - outset, bounds.width + outset * 2.0, bounds.height + outset * 2.0};
        radius = std::max(0.0, recipe.material.corner_radius + outset);
    } else {
        const double inset = std::max(3.0, recipe.default_width + 1.0);
        ring = Rect{bounds.x + inset, bounds.y + inset, bounds.width - inset * 2.0, bounds.height - inset * 2.0};
        radius = std::max(0.0, recipe.material.corner_radius - inset);
    }
    if (ring.empty()) return;
    if (recipe.dotted_focus) {
        paint_dotted_ring(painter, ring, recipe.focus_width, recipe.focus_ring);
        return;
    }
    painter.stroke_rounded_rect(ring, radius, recipe.focus_ring, recipe.focus_width);
}

// One arrow kernel: `rows` one-unit strips across the arrow's axis, strip
// `row` at `first + step * row` along it and spanning `row` units either side
// of `centre` across it. `horizontal_strips` lays the strips along x (an
// arrow pointing up or down); otherwise along y. Every value is a whole
// number, so the strips land on whole units on every host.
void fill_arrow_strips(Painter& painter, bool horizontal_strips, double centre, double first,
                       double step, int rows, Color color) {
    for (int row = 0; row < rows; ++row) {
        const double extent = static_cast<double>(row);
        const double position = first + step * static_cast<double>(row);
        const double length = extent * 2.0 + 1.0;
        if (horizontal_strips) {
            painter.fill_rect({centre - extent, position, length, 1.0}, color);
        } else {
            painter.fill_rect({position, centre - extent, 1.0, length}, color);
        }
    }
}

// A solid triangle pointing `direction`, filled one device-independent strip
// at a time so every host rasterizes the same spans. The tip is the one-unit
// strip; each strip after it reaches one unit further on either side. The
// triangle is `half` strips deep, with its middle strip at the box's centre.
void fill_arrow(Painter& painter, Rect bounds, PartMark direction, Color color) {
    const double side = std::min(bounds.width, bounds.height);
    const double half = std::floor(side * 0.5);
    if (half < 1.0) return;
    const double centre_x = std::floor(bounds.x + bounds.width * 0.5);
    const double centre_y = std::floor(bounds.y + bounds.height * 0.5);
    const int rows = static_cast<int>(half);
    const double middle_row = std::floor(half * 0.5);
    switch (direction) {
    case PartMark::arrow_down:
        // Tip at the bottom: strips climb from centre_y + middle_row.
        fill_arrow_strips(painter, true, centre_x, centre_y + middle_row, -1.0, rows, color);
        return;
    case PartMark::arrow_up:
        // Tip at the top: strips descend from centre_y - middle_row - 1.
        fill_arrow_strips(painter, true, centre_x, centre_y - middle_row - 1.0, 1.0, rows, color);
        return;
    case PartMark::arrow_right:
        fill_arrow_strips(painter, false, centre_y, centre_x + middle_row, -1.0, rows, color);
        return;
    default:
        // arrow_left, the one direction left.
        fill_arrow_strips(painter, false, centre_y, centre_x - middle_row - 1.0, 1.0, rows, color);
        return;
    }
}

// The largest square that fits `box`, centred, on whole units.
Rect square_in(Rect box) noexcept {
    const double side = std::floor(std::min(box.width, box.height));
    const Rect square{std::floor(box.x + (box.width - side) * 0.5),
                      std::floor(box.y + (box.height - side) * 0.5), side, side};
    return square;
}

// A ribbon group's dialog launcher: the top-left corner of a small window
// and an arrow leaving it towards the bottom right, in one-unit strokes.
void paint_dialog_launcher(Painter& painter, Rect box, Color ink) {
    const Rect square = square_in(box);
    const double side = square.width;
    if (side < 4.0) return;
    const double corner = side * 0.6;
    const Point origin{square.x + 0.5, square.y + 0.5};
    painter.draw_line(origin, {origin.x + corner, origin.y}, ink, 1.0);
    painter.draw_line(origin, {origin.x, origin.y + corner}, ink, 1.0);
    const Point tip{square.x + side - 0.5, square.y + side - 0.5};
    const double head = side * 0.4;
    painter.draw_line({origin.x + side * 0.3, origin.y + side * 0.3}, tip, ink, 1.0);
    painter.draw_line({tip.x - head, tip.y}, tip, ink, 1.0);
    painter.draw_line({tip.x, tip.y - head}, tip, ink, 1.0);
}

// A gallery's "more" button: a bar across the top third and a solid arrow
// pointing down beneath it.
void paint_more(Painter& painter, Rect box, Color ink) {
    const Rect square = square_in(box);
    const double side = square.width;
    if (side < 4.0) return;
    const double bar = std::max(1.0, std::floor(side / 8.0));
    const double bar_top = std::floor(side * 0.2);
    painter.fill_rect({square.x, square.y + bar_top, side, bar}, ink);
    const double top = bar_top + bar + 1.0;
    fill_arrow(painter, {square.x, square.y + top, side, side - top}, PartMark::arrow_down, ink);
}

// Overflow: a double chevron pointing to where the hidden items went, the
// two chevrons a third of the square apart.
void paint_overflow(Painter& painter, Rect box, Color ink) {
    const Rect square = square_in(box);
    const double side = square.width;
    if (side < 4.0) return;
    const double half = side * 0.25;
    const double middle = square.y + side * 0.5;
    const double gap = side / 3.0;
    const double first_tip = square.x + side * 0.5 - gap * 0.5 + half * 0.5;
    for (int chevron = 0; chevron < 2; ++chevron) {
        const double tip = first_tip + gap * static_cast<double>(chevron);
        painter.draw_line({tip - half, middle - half}, {tip, middle}, ink, 1.5);
        painter.draw_line({tip, middle}, {tip - half, middle + half}, ink, 1.5);
    }
}

// A Quick Access Toolbar's customize button: a short bar over an open
// chevron pointing down. It differs from "more" by the open chevron and the
// shorter bar.
void paint_customize(Painter& painter, Rect box, Color ink) {
    const Rect square = square_in(box);
    const double side = square.width;
    if (side < 4.0) return;
    const double bar = std::max(1.0, std::floor(side / 8.0));
    const double inset = std::floor(side * 0.2);
    painter.fill_rect({square.x + inset, square.y + std::floor(side * 0.25), side - inset * 2.0, bar}, ink);
    const double half = side * 0.25;
    const Point middle{square.x + side * 0.5, square.y + side * 0.75};
    painter.draw_line({middle.x - half, middle.y - half * 0.75}, middle, ink, 1.5);
    painter.draw_line(middle, {middle.x + half, middle.y - half * 0.75}, ink, 1.5);
}

// One point of the pin mark, given in the upright pin's frame as fractions
// of `square` (`across` from the left, `down` from the top). Upright, the
// pin stands needle down; lying, the same pin is turned a quarter so that
// the needle points left. Points land on half units so one-unit strokes
// stay crisp.
Point pin_point(Rect square, bool upright, double across, double down) noexcept {
    const double side = square.width;
    double x = square.x + across * side;
    double y = square.y + down * side;
    if (!upright) {
        x = square.x + (1.0 - down) * side;
        y = square.y + across * side;
    }
    const Point point{std::floor(x) + 0.5, std::floor(y) + 0.5};
    return point;
}

// A pushpin: a barrel (three sides of a small block), a collar across its
// open end and a needle beyond the collar. Pinned stands it upright, needle
// down, as a docked panel's caption shows it; unpinned lays it on its side
// with the needle pointing left (owner decision DK-35, J1).
void paint_pin(Painter& painter, Rect box, bool pinned, Color ink) {
    const Rect square = square_in(box);
    if (square.width < 6.0) return;
    const Point barrel_top_left = pin_point(square, pinned, 0.3, 0.05);
    const Point barrel_top_right = pin_point(square, pinned, 0.7, 0.05);
    const Point barrel_bottom_left = pin_point(square, pinned, 0.3, 0.5);
    const Point barrel_bottom_right = pin_point(square, pinned, 0.7, 0.5);
    painter.draw_line(barrel_top_left, barrel_top_right, ink, 1.0);
    painter.draw_line(barrel_top_left, barrel_bottom_left, ink, 1.0);
    painter.draw_line(barrel_top_right, barrel_bottom_right, ink, 1.0);
    const Point collar_start = pin_point(square, pinned, 0.1, 0.5);
    const Point collar_end = pin_point(square, pinned, 0.9, 0.5);
    painter.draw_line(collar_start, collar_end, ink, 1.0);
    const Point needle_start = pin_point(square, pinned, 0.5, 0.5);
    const Point needle_end = pin_point(square, pinned, 0.5, 0.95);
    painter.draw_line(needle_start, needle_end, ink, 1.0);
}

// The part of a dock shape's interior a drop region fills: a half of the
// interior for a side, or the middle half of it each way for tab_into.
Rect dock_shape_fill(Rect interior, DockShapeRegion region) noexcept {
    const double half_width = std::floor(interior.width * 0.5);
    const double half_height = std::floor(interior.height * 0.5);
    Rect filled = interior;
    switch (region) {
    case DockShapeRegion::top:
        filled.height = half_height;
        return filled;
    case DockShapeRegion::bottom:
        filled.y = interior.y + interior.height - half_height;
        filled.height = half_height;
        return filled;
    case DockShapeRegion::left:
        filled.width = half_width;
        return filled;
    case DockShapeRegion::right:
        filled.x = interior.x + interior.width - half_width;
        filled.width = half_width;
        return filled;
    case DockShapeRegion::tab_into:
        filled.x = interior.x + std::floor(interior.width * 0.25);
        filled.y = interior.y + std::floor(interior.height * 0.25);
        filled.width = half_width;
        filled.height = half_height;
        return filled;
    }
    // A region outside the enumeration fills the whole interior.
    return interior;
}

// A docking target's shape (owner decision DK-35, J2): a small window, its
// caption a two-unit bar along the top and its other three edges one-unit
// strokes, with the region a drop there gives the panel filled inside it,
// one unit clear of the edges (dock_shape_fill).
void paint_dock_shape(Painter& painter, Rect box, DockShapeRegion region, Color ink) {
    const Rect square = square_in(box);
    const double side = square.width;
    if (side < 8.0) return;
    painter.fill_rect({square.x, square.y, side, 2.0}, ink);
    const double left = square.x + 0.5;
    const double right = square.x + side - 0.5;
    const double bottom = square.y + side - 0.5;
    painter.draw_line({left, square.y + 2.0}, {left, bottom}, ink, 1.0);
    painter.draw_line({right, square.y + 2.0}, {right, bottom}, ink, 1.0);
    painter.draw_line({left, bottom}, {right, bottom}, ink, 1.0);
    const Rect interior{square.x + 2.0, square.y + 3.0, side - 4.0, side - 5.0};
    const Rect filled = dock_shape_fill(interior, region);
    painter.fill_rect(filled, ink);
}

void paint_mark(Painter& painter, const PartPaint& part, const ControlVisualRecipe& recipe) {
    const Rect box = part.bounds;
    const Color ink = recipe.glyph;
    switch (part.mark) {
    case PartMark::none:
    case PartMark::count:
        // Nothing to draw: no mark, or the count sentinel.
        return;
    case PartMark::check: {
        // Two strokes through fixed fractions of the mark's box: the house
        // check is 12 by 9 units with strokes (1, 5), (4, 8), (11, 1).
        const Point start{box.x + box.width * (1.0 / 12.0), box.y + box.height * (5.0 / 9.0)};
        const Point corner{box.x + box.width * (4.0 / 12.0), box.y + box.height * (8.0 / 9.0)};
        const Point end{box.x + box.width * (11.0 / 12.0), box.y + box.height * (1.0 / 9.0)};
        painter.draw_line(start, corner, ink, 2.0);
        painter.draw_line(corner, end, ink, 2.0);
        return;
    }
    case PartMark::indeterminate:
        painter.fill_rect(box, ink);
        return;
    case PartMark::radio_dot:
        painter.fill_rounded_rect(box, std::min(box.width, box.height) * 0.5, ink);
        return;
    case PartMark::toggle_knob:
        painter.fill_rounded_rect(box, std::min(box.width, box.height) * 0.5, recipe.text);
        return;
    case PartMark::arrow_up:
    case PartMark::arrow_down:
    case PartMark::arrow_left:
    case PartMark::arrow_right:
        fill_arrow(painter, box, part.mark, ink);
        return;
    case PartMark::chevron_down: {
        const double side = std::min(box.width, box.height) * 0.5;
        const Point middle{box.x + box.width * 0.5, box.y + box.height * 0.5 + side * 0.25};
        painter.draw_line({middle.x - side * 0.5, middle.y - side * 0.5}, middle, ink, 1.5);
        painter.draw_line(middle, {middle.x + side * 0.5, middle.y - side * 0.5}, ink, 1.5);
        return;
    }
    case PartMark::chevron_right: {
        const double side = std::min(box.width, box.height) * 0.5;
        const Point middle{box.x + box.width * 0.5 + side * 0.25, box.y + box.height * 0.5};
        painter.draw_line({middle.x - side * 0.5, middle.y - side * 0.5}, middle, ink, 1.5);
        painter.draw_line(middle, {middle.x - side * 0.5, middle.y + side * 0.5}, ink, 1.5);
        return;
    }
    case PartMark::close: {
        const double side = std::min(box.width, box.height) * 0.6;
        const Point centre{box.x + box.width * 0.5, box.y + box.height * 0.5};
        const double half = side * 0.5;
        painter.draw_line({centre.x - half, centre.y - half}, {centre.x + half, centre.y + half}, ink, 1.5);
        painter.draw_line({centre.x - half, centre.y + half}, {centre.x + half, centre.y - half}, ink, 1.5);
        return;
    }
    case PartMark::grip_dots: {
        // Two-unit dots on a four-unit pitch, filling the box.
        for (double y = box.y + 1.0; y + 2.0 <= box.y + box.height; y += 4.0) {
            for (double x = box.x + 1.0; x + 2.0 <= box.x + box.width; x += 4.0) {
                painter.fill_rect({x, y, 2.0, 2.0}, recipe.muted_text);
            }
        }
        return;
    }
    case PartMark::dialog_launcher:
        paint_dialog_launcher(painter, box, ink);
        return;
    case PartMark::more:
        paint_more(painter, box, ink);
        return;
    case PartMark::overflow:
        paint_overflow(painter, box, ink);
        return;
    case PartMark::customize:
        paint_customize(painter, box, ink);
        return;
    case PartMark::pin:
        paint_pin(painter, box, part.states.contains(AnatomyState::checked), ink);
        return;
    case PartMark::dock_shape:
        paint_dock_shape(painter, box, part.dock_region, ink);
        return;
    case PartMark::pencil: {
        // A two-unit shaft from the lower left towards the upper right, its
        // point a one-unit stroke at the lower left.
        const Rect square = square_in(box);
        const double side = square.width;
        const Point tip{square.x + side * 0.1, square.y + side * 0.9};
        const Point heel{square.x + side * 0.3, square.y + side * 0.7};
        const Point top{square.x + side * 0.9, square.y + side * 0.1};
        painter.draw_line(tip, heel, ink, 1.0);
        painter.draw_line(heel, top, ink, 2.0);
        return;
    }
    case PartMark::asterisk: {
        // Three strokes through the centre: upright and two at 60 degrees.
        const Rect square = square_in(box);
        const double half = square.width * 0.45;
        const Point centre{square.x + square.width * 0.5, square.y + square.height * 0.5};
        const double along = half * 0.866;
        const double across = half * 0.5;
        painter.draw_line({centre.x, centre.y - half}, {centre.x, centre.y + half}, ink, 1.5);
        painter.draw_line({centre.x - along, centre.y - across}, {centre.x + along, centre.y + across}, ink, 1.5);
        painter.draw_line({centre.x - along, centre.y + across}, {centre.x + along, centre.y - across}, ink, 1.5);
        return;
    }
    case PartMark::error: {
        // A one-unit ring holding an exclamation mark: a bar and a dot.
        const Rect square = square_in(box);
        const double side = square.width;
        if (side < 5.0) return;
        const Rect ring{square.x + 0.5, square.y + 0.5, side - 1.0, side - 1.0};
        painter.stroke_rounded_rect(ring, (side - 1.0) * 0.5, ink, 1.0);
        const double bar_x = std::floor(square.x + side * 0.5 - 0.5);
        painter.fill_rect({bar_x, square.y + side * 0.22, 1.5, side * 0.38}, ink);
        painter.fill_rect({bar_x, square.y + side * 0.68, 1.5, 1.5}, ink);
        return;
    }
    case PartMark::underline: {
        // A one-unit rule whose lower edge is the box's bottom edge.
        const double y = box.y + box.height - 0.5;
        painter.draw_line({box.x, y}, {box.x + box.width, y}, ink, 1.0);
        return;
    }
    }
    // A mark outside the enumeration draws nothing.
    return;
}

// One line of text in the part's bounds, its line box centred across the
// reading direction and the run aligned along it, clipped to the bounds.
// The origin is the start of the baseline, as Painter::draw_text_utf8 and
// draw_turned_text_utf8 take it. A turned run reads along the bounds'
// height with the glyph tops towards the left (bottom_to_top) or the right
// (top_to_bottom), so its line box is centred across the width.
void paint_text(Painter& painter, const PartPaint& part, Color color) {
    if (part.text.empty()) return;
    const Size measured = painter.measure_text_utf8(part.text, part.font);
    const double line_height = std::max(measured.height, part.font.size * 1.2);
    const double ascent = std::max(part.font.size, line_height * 0.82);
    const bool turned = part.orientation != TextOrientation::horizontal;
    const double run_extent = turned ? part.bounds.height : part.bounds.width;
    const double cross_extent = turned ? part.bounds.width : part.bounds.height;
    // Distances along the reading direction from the end reading starts at,
    // and across it from the edge the glyph tops face.
    double along = 0.0;
    // A run aligned at the start stays at the reading start (0).
    if (part.alignment == PartTextAlignment::center) {
        along = (run_extent - measured.width) * 0.5;
    } else if (part.alignment == PartTextAlignment::end) {
        along = run_extent - measured.width;
    }
    const double line_top = (cross_extent - line_height) * 0.5;
    Point origin{part.bounds.x + along, part.bounds.y + line_top + ascent};
    // Horizontal text keeps this origin; a turned run's is turned with it.
    if (part.orientation == TextOrientation::bottom_to_top) {
        origin = {part.bounds.x + line_top + ascent, part.bounds.y + part.bounds.height - along};
    } else if (part.orientation == TextOrientation::top_to_bottom) {
        origin = {part.bounds.x + part.bounds.width - line_top - ascent, part.bounds.y + along};
    }
    painter.save();
    painter.clip_rect(part.bounds);
    if (turned) {
        painter.draw_turned_text_utf8(origin, part.text, part.font, color, part.orientation);
    } else {
        painter.draw_text_utf8(origin, part.text, part.font, color);
    }
    painter.restore();
}

// A fill part's content colour (owner decision W-7): flat, in the part's
// shape, as given; a disabled part's at 45 % of its opacity, as a disabled
// icon is drawn.
// Content colour is drawn exactly as given, disabled or not (T2 GIVEN
// E2-1): a disabled part shows through what surrounds it.
void paint_content_color(Painter& painter, const PartPaint& part, Color color) {
    const double radius = part.corner_radius ? std::max(0.0, *part.corner_radius) : 0.0;
    if (radius > 0.0) {
        painter.fill_rounded_rect(part.bounds, radius, color);
        return;
    }
    painter.fill_rect(part.bounds, color);
}

// Whether `part` paints a ground: a surface role, or an indicator or glyph
// without a mark (paint_part_from_recipe paints those as surfaces too).
bool ground_role(const PartPaint& part) noexcept {
    if (surface_role(part.role)) return true;
    const bool marked = part.mark != PartMark::none;
    const bool ground = (part.role == PartRole::indicator || part.role == PartRole::glyph) && !marked;
    return ground;
}

// `recipe` with the part's authored colours in place (owner decision T4-1;
// PartPaint::authored_background), for one paint: the background as the
// material's only fill on a ground, and the foreground, unless the part is
// disabled, as the text, the mark ink or the rule's colour by role. An
// important text colour (`important_text`) keeps its place.
void apply_authored_colours(const PartPaint& part, bool disabled, bool important_text,
                            ControlVisualRecipe& recipe) {
    if (part.authored_background && ground_role(part)) {
        recipe.material.fills.assign(1U, MaterialFillLayer::solid(*part.authored_background));
    }
    if (!part.authored_foreground || disabled) return;
    const Color ink = *part.authored_foreground;
    switch (part.role) {
    case PartRole::caption:
    case PartRole::label:
    case PartRole::content:
    case PartRole::caret:
        if (!important_text) recipe.text = ink;
        return;
    case PartRole::indicator:
    case PartRole::glyph:
    case PartRole::disclosure:
    case PartRole::grip:
        recipe.glyph = ink;
        return;
    case PartRole::keyline:
    case PartRole::divider:
    case PartRole::seam:
        // paint_rule draws in the border's colour, else the muted text's.
        if (recipe.material.border) {
            (*recipe.material.border).color = ink;
        } else {
            recipe.muted_text = ink;
        }
        return;
    default:
        // Surfaces, cues, images and the silent roles read no foreground.
        return;
    }
}

} // namespace

bool authored_background_applies(const PartPaint& part) noexcept {
    const bool applies = part.authored_background.has_value() && ground_role(part);
    return applies;
}

ControlVisualContext part_visual_context(const Control& control, AnatomyStateSet states) noexcept {
    const bool hovered = states.contains(AnatomyState::hot);
    const bool pressed = any_state(states, AnatomyState::pressed, AnatomyState::captured);
    const bool selected = any_state(states, AnatomyState::selected, AnatomyState::checked) ||
                          any_state(states, AnatomyState::current, AnatomyState::open) ||
                          states.contains(AnatomyState::active_tool);
    const bool focused = states.contains(AnatomyState::focus_visible);
    const bool defaulted = states.contains(AnatomyState::preselect);
    ControlVisualContext context = control.visual_context(hovered, pressed, selected, focused, defaulted);
    if (any_state(states, AnatomyState::disabled, AnatomyState::unavailable)) {
        context.surface = ControlSurfaceState::disabled;
    }
    return context;
}

std::string_view part_mark_name(PartMark mark) noexcept {
    switch (mark) {
    case PartMark::none: return "none";
    case PartMark::check: return "check";
    case PartMark::indeterminate: return "indeterminate";
    case PartMark::radio_dot: return "radio_dot";
    case PartMark::toggle_knob: return "toggle_knob";
    case PartMark::arrow_up: return "arrow_up";
    case PartMark::arrow_down: return "arrow_down";
    case PartMark::arrow_left: return "arrow_left";
    case PartMark::arrow_right: return "arrow_right";
    case PartMark::chevron_down: return "chevron_down";
    case PartMark::chevron_right: return "chevron_right";
    case PartMark::close: return "close";
    case PartMark::grip_dots: return "grip_dots";
    case PartMark::dialog_launcher: return "dialog_launcher";
    case PartMark::more: return "more";
    case PartMark::overflow: return "overflow";
    case PartMark::customize: return "customize";
    case PartMark::pin: return "pin";
    case PartMark::dock_shape: return "dock_shape";
    case PartMark::underline: return "underline";
    case PartMark::pencil: return "pencil";
    case PartMark::asterisk: return "asterisk";
    case PartMark::error: return "error";
    case PartMark::count: return {};
    }
    return {};
}

Insets overlay_shadow_outsets(const Control& control) noexcept {
    const ControlVisualContext context = part_visual_context(control, AnatomyStateSet{});
    const Theme& theme = control.effective_theme();
    const Insets result = box_shadow_visual_outsets(theme.overlay_shadow(context.high_contrast));
    return result;
}

bool part_bounds_paintable(Rect bounds) noexcept {
    const bool result = paintable(bounds);
    return result;
}

void paint_part(Painter& painter, const Control& control, const PartPaint& part) {
    const Theme& theme = control.effective_theme();
    // The control's authored colours (Control::set_back_color and
    // set_fore_color) under the part's own: its own background fills every
    // ground, an inherited one every ground but a well.
    std::optional<Color> background = part.authored_background;
    if (!background) background = control.back_color();
    if (!background && part.role != PartRole::well) background = control.ambient_back_color();
    std::optional<Color> foreground = part.authored_foreground;
    if (!foreground) foreground = control.ambient_fore_color();
    const bool unchanged = background == part.authored_background && foreground == part.authored_foreground;
    if (unchanged) {
        theme.engine().paint(painter, control, part);
        return;
    }
    PartPaint coloured = part;
    coloured.authored_background = background;
    coloured.authored_foreground = foreground;
    theme.engine().paint(painter, control, coloured);
}

void paint_part_with_built_in_recipe(Painter& painter, const Control& control, const PartPaint& part) {
    if (!paintable(part.bounds)) return;
    const ControlVisualContext context = part_visual_context(control, part.states);
    const Theme& theme = control.effective_theme();
    // The control's recipe fields (T3 P6f) in the part's states: a label
    // part takes the control's text colour.
    const ResolvedControlRecipe resolved = theme.resolve(control, part.family, context, part.states);
    const ControlVisualRecipe& recipe = resolved.recipe();
    if (part.role == PartRole::shadow) {
        paint_overlay_shadow(painter, part, recipe, theme.overlay_shadow(context.high_contrast));
        return;
    }
    // Authored colours (owner decision T4-1) stand in a copy of the recipe,
    // made only for a part that carries them, and never in high contrast.
    const bool authored = (part.authored_background || part.authored_foreground) && !context.high_contrast;
    if (authored) {
        ControlVisualRecipe styled = recipe;
        const bool disabled = context.surface == ControlSurfaceState::disabled;
        apply_authored_colours(part, disabled, resolved.important_text(), styled);
        paint_part_from_recipe(painter, part, styled, context);
    } else {
        paint_part_from_recipe(painter, part, recipe, context);
    }
    if (!part.accent || !surface_role(part.role)) return;
    // Provisional accent (design language §8.5): the ramp colour, which
    // carries its own translucency, laid over the part's material in the
    // part's shape. T4 replaces this with the engine's tint.
    const Color tint = theme.accent(*part.accent);
    const double radius = part_corner_radius(part, recipe);
    painter.fill_rounded_rect(part.bounds, radius, tint);
}

void paint_part_from_recipe(Painter& painter, const PartPaint& part,
                            const ControlVisualRecipe& recipe, ControlVisualContext context) {
    if (!paintable(part.bounds)) return;
    const bool disabled = context.surface == ControlSurfaceState::disabled;
    switch (part.role) {
    case PartRole::region:
    case PartRole::shadow:
    case PartRole::edge:
    case PartRole::perimeter:
    case PartRole::sheen:
    case PartRole::count:
        // The built-in recipe has no paint of its own for these (a shadow is
        // painted by paint_part_with_built_in_recipe; count is no role).
        return;
    case PartRole::fill:
        if (part.content_color) {
            paint_content_color(painter, part, *part.content_color);
            return;
        }
        paint_surface(painter, part, recipe);
        return;
    case PartRole::face:
    case PartRole::well:
    case PartRole::track:
    case PartRole::thumb:
    case PartRole::actuator:
    case PartRole::chip:
    case PartRole::cap:
    case PartRole::activity:
    case PartRole::selection:
    case PartRole::drop:
    case PartRole::validation:
        paint_surface(painter, part, recipe);
        return;
    case PartRole::indicator:
    case PartRole::glyph:
        if (part.mark == PartMark::none) {
            paint_surface(painter, part, recipe);
        } else {
            paint_mark(painter, part, recipe);
        }
        return;
    case PartRole::keyline:
    case PartRole::divider:
    case PartRole::seam:
        paint_rule(painter, part.bounds, recipe);
        return;
    case PartRole::grip: {
        PartPaint dots = part;
        dots.mark = PartMark::grip_dots;
        paint_mark(painter, dots, recipe);
        return;
    }
    case PartRole::caption:
    case PartRole::label:
    case PartRole::content:
        // The recipe is resolved in the disabled state, so its text is the
        // theme's own disabled text (owner ruling, 2026-11-02).
        paint_text(painter, part, recipe.text);
        return;
    case PartRole::icon:
        if (part.image.value != 0U) painter.draw_image(part.image, part.bounds, disabled ? 0.45 : 1.0);
        return;
    case PartRole::image:
        // Content, drawn exactly as given, disabled or not (GIVEN H1-1).
        if (part.image.value != 0U) painter.draw_image(part.image, part.bounds, 1.0);
        return;
    case PartRole::caret:
        painter.fill_rect({part.bounds.x, part.bounds.y, std::max(1.0, part.bounds.width),
                           part.bounds.height}, recipe.text);
        return;
    case PartRole::disclosure:
        paint_mark(painter, part, recipe);
        return;
    case PartRole::focus:
        if (context.focused) paint_focus_ring(painter, part.bounds, recipe);
        return;
    case PartRole::preselect:
        if (context.defaulted) paint_preselect(painter, part.bounds, recipe);
        return;
    }
    // A role outside the enumeration paints nothing.
    return;
}

} // namespace gui_forms
