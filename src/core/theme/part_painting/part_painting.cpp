#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "built_in_recipe.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
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
    case PartRole::plot:
    case PartRole::legend:
    case PartRole::color_scale:
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
    case PartRole::graticule:
    case PartRole::trace:
    case PartRole::axis:
    case PartRole::tick:
    case PartRole::tick_label:
    case PartRole::grid_major:
    case PartRole::grid_minor:
    case PartRole::legend_entry:
    case PartRole::marker:
    case PartRole::crosshair:
    case PartRole::annotation:
    case PartRole::series:
    case PartRole::arc:
    case PartRole::needle:
    case PartRole::beak:
    case PartRole::scrim:
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
    const bool frameless = part.frame == PartFrame::none;
    if (!part.corner_radius && !frameless) {
        paint_surface_material(painter, part.bounds, recipe.material);
        return;
    }
    // A copy for this paint: the part's corner radius, and a frameless
    // part's ground without the material's borders (PartPaint::frame).
    SurfaceMaterial shaped = recipe.material;
    if (part.corner_radius) shaped.corner_radius = std::max(0.0, *part.corner_radius);
    if (frameless) {
        shaped.border.reset();
        shaped.border_edges = MaterialBorderEdges{};
    }
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

// The preselect (default) ring, inside the part's bounds, following a
// surface whose corners have `radius`.
void paint_preselect(Painter& painter, Rect bounds, const ControlVisualRecipe& recipe, double radius) {
    const double width = recipe.default_width;
    if (width <= 0.0 || bounds.width <= width * 2.0 || bounds.height <= width * 2.0) return;
    const double inset = width * 0.5;
    const Rect ring{bounds.x + inset, bounds.y + inset,
                    bounds.width - inset * 2.0, bounds.height - inset * 2.0};
    painter.stroke_rounded_rect(ring, std::max(0.0, radius - inset), recipe.default_ring, width);
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
// house ring inset inside them; dotted when the recipe says so. It follows a
// surface whose corners have `corner_radius`.
void paint_focus_ring(Painter& painter, Rect bounds, const ControlVisualRecipe& recipe, double corner_radius) {
    if (recipe.focus_width <= 0.0 || bounds.width <= 8.0 || bounds.height <= 8.0) return;
    Rect ring{};
    double radius = 0.0;
    if (recipe.authored_focus_outline) {
        const double outset = recipe.focus_offset + recipe.focus_width * 0.5;
        ring = Rect{bounds.x - outset, bounds.y - outset, bounds.width + outset * 2.0, bounds.height + outset * 2.0};
        radius = std::max(0.0, corner_radius + outset);
    } else {
        const double inset = std::max(3.0, recipe.default_width + 1.0);
        ring = Rect{bounds.x + inset, bounds.y + inset, bounds.width - inset * 2.0, bounds.height - inset * 2.0};
        radius = std::max(0.0, corner_radius - inset);
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
    case PartMark::chevron_left: {
        const double side = std::min(box.width, box.height) * 0.5;
        const Point middle{box.x + box.width * 0.5 - side * 0.25, box.y + box.height * 0.5};
        painter.draw_line({middle.x + side * 0.5, middle.y - side * 0.5}, middle, ink, 1.5);
        painter.draw_line(middle, {middle.x + side * 0.5, middle.y + side * 0.5}, ink, 1.5);
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
    case PartMark::search: {
        // A one-unit ring over the upper left two thirds of the square and a
        // one-and-a-half-unit handle from it to the lower right corner.
        const Rect square = square_in(box);
        const double side = square.width;
        if (side < 5.0) return;
        const double lens = side * 0.66;
        const Rect ring{square.x + 0.5, square.y + 0.5, lens - 1.0, lens - 1.0};
        painter.stroke_rounded_rect(ring, (lens - 1.0) * 0.5, ink, 1.0);
        const Point heel{square.x + lens * 0.85, square.y + lens * 0.85};
        const Point end{square.x + side - 0.5, square.y + side - 0.5};
        painter.draw_line(heel, end, ink, 1.5);
        return;
    }
    case PartMark::information: {
        // A one-unit ring holding an `i`: a dot over a bar (error's
        // exclamation mark upside down).
        const Rect square = square_in(box);
        const double side = square.width;
        if (side < 5.0) return;
        const Rect ring{square.x + 0.5, square.y + 0.5, side - 1.0, side - 1.0};
        painter.stroke_rounded_rect(ring, (side - 1.0) * 0.5, ink, 1.0);
        const double bar_x = std::floor(square.x + side * 0.5 - 0.5);
        painter.fill_rect({bar_x, square.y + side * 0.22, 1.5, 1.5}, ink);
        painter.fill_rect({bar_x, square.y + side * 0.40, 1.5, side * 0.38}, ink);
        return;
    }
    case PartMark::warning: {
        // A one-unit triangle, apex up, holding an exclamation mark.
        const Rect square = square_in(box);
        const double side = square.width;
        if (side < 5.0) return;
        const Point apex{square.x + side * 0.5, square.y + 0.5};
        const Point left{square.x + 0.5, square.y + side - 0.5};
        const Point right{square.x + side - 0.5, square.y + side - 0.5};
        painter.draw_line(apex, left, ink, 1.0);
        painter.draw_line(left, right, ink, 1.0);
        painter.draw_line(right, apex, ink, 1.0);
        const double bar_x = std::floor(square.x + side * 0.5 - 0.5);
        painter.fill_rect({bar_x, square.y + side * 0.36, 1.5, side * 0.32}, ink);
        painter.fill_rect({bar_x, square.y + side * 0.76, 1.5, 1.5}, ink);
        return;
    }
    case PartMark::success: {
        // A one-unit ring holding the house check (see `check`), scaled into
        // the ring's middle half.
        const Rect square = square_in(box);
        const double side = square.width;
        if (side < 5.0) return;
        const Rect ring{square.x + 0.5, square.y + 0.5, side - 1.0, side - 1.0};
        painter.stroke_rounded_rect(ring, (side - 1.0) * 0.5, ink, 1.0);
        const Rect inner{square.x + side * 0.25, square.y + side * 0.3, side * 0.5, side * 0.4};
        const Point start{inner.x + inner.width * (1.0 / 12.0), inner.y + inner.height * (5.0 / 9.0)};
        const Point corner{inner.x + inner.width * (4.0 / 12.0), inner.y + inner.height * (8.0 / 9.0)};
        const Point end{inner.x + inner.width * (11.0 / 12.0), inner.y + inner.height * (1.0 / 9.0)};
        painter.draw_line(start, corner, ink, 1.5);
        painter.draw_line(corner, end, ink, 1.5);
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
    const Size measured = part.measured_text ? *part.measured_text
                                             : painter.measure_text_utf8(part.text, part.font);
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

// -- the dial and overlay roles (row 2.5; owner option (a)) ---------------------

constexpr double whole_turn = 6.283185307179586476925;

// The circle inscribed in a part's bounds: centred in them, its radius half
// their shorter side.
struct InscribedCircle final {
    Point centre{};
    double radius{};
};

InscribedCircle inscribed_circle(Rect bounds) noexcept {
    InscribedCircle result{};
    result.centre = Point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    result.radius = std::min(bounds.width, bounds.height) * 0.5;
    return result;
}

// The point `radius` from `centre` at `angle` radians clockwise from twelve
// o'clock, with the engine's sine and cosine, so that a dial is the same on
// every host.
Point point_at_angle(Point centre, double radius, double angle) noexcept {
    const EngineSineCosine turned = engine_sine_cosine(angle);
    const Point result{centre.x + radius * turned.sine, centre.y - radius * turned.cosine};
    return result;
}

// A round dot `side` across centred on `centre`.
void fill_round_dot(Painter& painter, Point centre, double side, Color colour) {
    const double half = side * 0.5;
    const Rect dot{centre.x - half, centre.y - half, side, side};
    painter.fill_rounded_rect(dot, half, colour);
}

// The colour a material's face shows at its top (`top`) or bottom: a solid
// fill's colour, or a gradient's first or last stop; transparent where the
// material has no fill.
Color material_face_colour(const SurfaceMaterial& material, bool top) noexcept {
    if (material.fills.empty()) return Color::rgba(0, 0, 0, 0);
    const MaterialFillLayer& fill = material.fills.front();
    if (fill.kind == MaterialFillKind::solid || fill.stops.empty()) return fill.color;
    const Color result = top ? fill.stops.front().color : fill.stops.back().color;
    return result;
}

// An arc: a stroke `recipe.stroke_width` thick along the inscribed circle,
// inset by half the stroke so that it lies inside the bounds. An unselected
// arc (a track, an alarm segment) is stroked in the border's colour, else
// the glyph's; a selected arc (the value) in the face's colour, the plate a
// selected progress fill shows. The circle is drawn as straight segments,
// one per three units of arc length (2 to 128), and a thick stroke's joints
// are closed with round dots of its width (a translucent colour is
// therefore slightly deeper at the joints).
void paint_arc(Painter& painter, const PartPaint& part, const ControlVisualRecipe& recipe) {
    if (!part.arc) return;
    const double width = recipe.stroke_width;
    const double start = (*part.arc).start;
    const double sweep = std::clamp((*part.arc).sweep, -whole_turn, whole_turn);
    if (width <= 0.0 || !std::isfinite(start) || !std::isfinite(sweep) || sweep == 0.0) return;
    const InscribedCircle circle = inscribed_circle(part.bounds);
    const double radius = circle.radius - width * 0.5;
    if (radius <= 0.0) return;
    Color colour = recipe.glyph;
    if (part.states.contains(AnatomyState::selected)) {
        colour = material_face_colour(recipe.material, false);
    } else if (recipe.material.border) {
        colour = (*recipe.material.border).color;
    }
    const double length = std::abs(sweep) * radius;
    const double wanted = std::ceil(length / 3.0);
    const std::size_t segments = static_cast<std::size_t>(std::clamp(wanted, 2.0, 128.0));
    const double step = sweep / static_cast<double>(segments);
    const bool joined = width > 2.0;
    Point from = point_at_angle(circle.centre, radius, start);
    for (std::size_t index = 1U; index <= segments; ++index) {
        const double angle = start + step * static_cast<double>(index);
        const Point to = point_at_angle(circle.centre, radius, angle);
        painter.draw_line(from, to, colour, width);
        if (joined && index < segments) fill_round_dot(painter, to, width, colour);
        from = to;
    }
}

// A needle: from the inscribed circle's centre out to the circle at the
// arc's start, in the glyph's colour, shaped by `recipe.needle_shape`. A
// blade is drawn as eight sections narrowing from the stroke width towards
// one unit.
void paint_needle(Painter& painter, const PartPaint& part, const ControlVisualRecipe& recipe) {
    if (!part.arc) return;
    const double width = recipe.stroke_width;
    const double angle = (*part.arc).start;
    if (width <= 0.0 || !std::isfinite(angle)) return;
    const InscribedCircle circle = inscribed_circle(part.bounds);
    if (circle.radius <= 0.0) return;
    const Color colour = recipe.glyph;
    switch (recipe.needle_shape) {
    case NeedleShape::dot: {
        const double reach = std::max(0.0, circle.radius - width * 0.5);
        fill_round_dot(painter, point_at_angle(circle.centre, reach, angle), width, colour);
        return;
    }
    case NeedleShape::line: {
        const Point tip = point_at_angle(circle.centre, circle.radius, angle);
        painter.draw_line(circle.centre, tip, colour, width);
        break;
    }
    case NeedleShape::tapered: {
        constexpr std::size_t sections = 8U;
        Point from = circle.centre;
        for (std::size_t index = 0U; index < sections; ++index) {
            const double reached = static_cast<double>(index + 1U) / static_cast<double>(sections);
            const Point to = point_at_angle(circle.centre, circle.radius * reached, angle);
            const double remaining = 1.0 - (static_cast<double>(index) + 0.5) / static_cast<double>(sections);
            const double section_width = std::max(1.0, width * remaining);
            painter.draw_line(from, to, colour, section_width);
            from = to;
        }
        break;
    }
    default:
        // A shape outside the enumeration, which valid_recipe refuses,
        // draws nothing.
        return;
    }
    fill_round_dot(painter, circle.centre, width * 1.5, colour);
}

// The three corners of a beak in `box` pointing as `mark` names.
struct BeakCorners final {
    Point tip{};
    Point first{};
    Point second{};
};

BeakCorners beak_corners(Rect box, PartMark mark) noexcept {
    const double right = box.x + box.width;
    const double bottom = box.y + box.height;
    const double middle_x = box.x + box.width * 0.5;
    const double middle_y = box.y + box.height * 0.5;
    BeakCorners result{};
    // Geometry, not a vocabulary mapping: up, down and left have their own
    // tips; arrow_right and every mark without a direction point right.
    if (mark == PartMark::arrow_up) {
        result = BeakCorners{{middle_x, box.y}, {box.x, bottom}, {right, bottom}};
    } else if (mark == PartMark::arrow_down) {
        result = BeakCorners{{middle_x, bottom}, {box.x, box.y}, {right, box.y}};
    } else if (mark == PartMark::arrow_left) {
        result = BeakCorners{{box.x, middle_y}, {right, box.y}, {right, bottom}};
    } else {
        result = BeakCorners{{right, middle_y}, {box.x, box.y}, {box.x, bottom}};
    }
    return result;
}

// A beak: a triangle filling the part's bounds, its tip at the middle of
// the edge the arrow mark names and its base along the opposite edge, in
// the colour of the face side it meets (the top for a beak pointing up,
// else the bottom), its two slanted sides in the border's colour. It is
// laid as at most 64 strips across the direction it points. A beak with no
// arrow mark draws nothing.
void paint_beak(Painter& painter, const PartPaint& part, const ControlVisualRecipe& recipe) {
    const Rect box = part.bounds;
    const PartMark mark = part.mark;
    const bool up = mark == PartMark::arrow_up;
    const bool down = mark == PartMark::arrow_down;
    const bool left = mark == PartMark::arrow_left;
    const bool right = mark == PartMark::arrow_right;
    if (!up && !down && !left && !right) return;
    const bool vertical = up || down;
    const Color face = material_face_colour(recipe.material, up);
    const double depth = vertical ? box.height : box.width;
    const double span = vertical ? box.width : box.height;
    const std::size_t strips = static_cast<std::size_t>(std::clamp(std::ceil(depth), 1.0, 64.0));
    const double strip = depth / static_cast<double>(strips);
    for (std::size_t index = 0U; index < strips; ++index) {
        // How far the strip's middle lies from the tip, as a fraction of
        // the depth, and so its half width.
        const double from_tip = (static_cast<double>(index) + 0.5) / static_cast<double>(strips);
        const double half = span * 0.5 * from_tip;
        const double offset = strip * static_cast<double>(index);
        const double across_x = box.x + box.width * 0.5 - half;
        const double across_y = box.y + box.height * 0.5 - half;
        Rect band{};
        if (up) band = Rect{across_x, box.y + offset, half * 2.0, strip};
        if (down) band = Rect{across_x, box.y + box.height - offset - strip, half * 2.0, strip};
        if (left) band = Rect{box.x + offset, across_y, strip, half * 2.0};
        if (right) band = Rect{box.x + box.width - offset - strip, across_y, strip, half * 2.0};
        painter.fill_rect(band, face);
    }
    if (!recipe.material.border) return;
    const Color edge = (*recipe.material.border).color;
    const double edge_width = std::max(1.0, (*recipe.material.border).width);
    const BeakCorners corners = beak_corners(box, mark);
    painter.draw_line(corners.first, corners.tip, edge, edge_width);
    painter.draw_line(corners.second, corners.tip, edge, edge_width);
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

// Whether `part` paints a ground: a surface role, or an indicator, trace or
// glyph without a mark (paint_part_from_recipe paints those as surfaces too).
bool ground_role(const PartPaint& part) noexcept {
    if (surface_role(part.role)) return true;
    const bool marked = part.mark != PartMark::none;
    const bool pointer_like = part.role == PartRole::indicator || part.role == PartRole::trace ||
                              part.role == PartRole::glyph || part.role == PartRole::marker ||
                              part.role == PartRole::series;
    const bool ground = pointer_like && !marked;
    return ground;
}

// `recipe` with the part's authored colours in place (owner decision T4-1;
// PartPaint::authored_background), for one paint: the background as the
// material's only fill on a ground, and the foreground, unless the part is
// disabled, as the text, the mark ink or the rule's colour by role. An
// important text colour (`important_text`) and an important glyph colour
// (`important_glyph`, for the marks) keep their places.
void apply_authored_colours(const PartPaint& part, bool disabled, bool important_text, bool important_glyph,
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
    case PartRole::tick_label:
    case PartRole::legend_entry:
    case PartRole::annotation:
        if (!important_text) {
            recipe.text = ink;
            recipe.muted_text = ink;
        }
        return;
    case PartRole::indicator:
    case PartRole::trace:
    case PartRole::marker:
    case PartRole::series:
    case PartRole::glyph:
    case PartRole::disclosure:
    case PartRole::grip:
    case PartRole::needle:
        if (!important_glyph) recipe.glyph = ink;
        return;
    case PartRole::arc:
        // paint_arc strokes an unselected arc in the border's colour, else
        // the glyph's.
        if (recipe.material.border) {
            (*recipe.material.border).color = ink;
        } else {
            recipe.glyph = ink;
        }
        return;
    case PartRole::keyline:
    case PartRole::divider:
    case PartRole::seam:
    case PartRole::graticule:
    case PartRole::axis:
    case PartRole::tick:
    case PartRole::grid_major:
    case PartRole::grid_minor:
    case PartRole::crosshair:
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
    VisualInteraction interaction{};
    interaction.hovered = states.contains(AnatomyState::hot);
    interaction.pressed = any_state(states, AnatomyState::pressed, AnatomyState::captured);
    interaction.selected = any_state(states, AnatomyState::selected, AnatomyState::checked) ||
                           any_state(states, AnatomyState::current, AnatomyState::open) ||
                           states.contains(AnatomyState::active_tool);
    interaction.focused = states.contains(AnatomyState::focus_visible);
    interaction.defaulted = states.contains(AnatomyState::preselect);
    ControlVisualContext context = control.visual_context(interaction);
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
    case PartMark::chevron_left: return "chevron_left";
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
    case PartMark::search: return "search";
    case PartMark::information: return "information";
    case PartMark::warning: return "warning";
    case PartMark::success: return "success";
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

namespace {

// The strips a pointed face's nose is cut into: each is clipped to the
// nose's height at its leading edge, so the nose is a bounded staircase
// whose outline the edge rules then draw.
constexpr std::size_t pointed_face_strips = 9U;

// The nose length of a pointed part, within its bounds (zero when none).
double pointed_length(const PartPaint& part) noexcept {
    if (!part.point || !surface_role(part.role)) return 0.0;
    const double length = std::clamp(*part.point, 0.0, std::max(0.0, part.bounds.width));
    return length;
}

// A pointed part (PartPaint::point): the whole part drawn once clipped to
// its body and once to each strip of its nose, so whatever draws the part
// (a primitive or the built-in recipe) is cut to the shape; then the nose's
// two edges in the material's border colour. The part is resolved and
// baked once and its drawing repeated under the clips (N91).
void paint_part_clipped(Painter& painter, const Control& control, const PartPaint& part,
                        std::span<const Rect> clips);

void paint_pointed_part(Painter& painter, const Control& control, const PartPaint& part, double length) {
    PartPaint whole = part;
    whole.point.reset();
    const Rect bounds = part.bounds;
    const double shoulder = bounds.x + bounds.width - length;
    std::array<Rect, pointed_face_strips + 1U> clips{};
    clips[0] = Rect{bounds.x, bounds.y, bounds.width - length, bounds.height};
    for (std::size_t strip = 0U; strip < pointed_face_strips; ++strip) {
        const double left = length * static_cast<double>(strip) / static_cast<double>(pointed_face_strips);
        const double right = length * static_cast<double>(strip + 1U) / static_cast<double>(pointed_face_strips);
        const double inset = bounds.height * 0.5 * left / length;
        clips[strip + 1U] = Rect{shoulder + left, bounds.y + inset, right - left, bounds.height - inset * 2.0};
    }
    paint_part_clipped(painter, control, whole, clips);
    const ControlVisualContext context = part_visual_context(control, part.states);
    const ControlVisualRecipe& recipe = control.effective_theme().resolve(part.family, context);
    if (part.frame == PartFrame::none || !recipe.material.border) return;
    const Color edge = (*recipe.material.border).color;
    const Point tip{bounds.x + bounds.width - 0.5, bounds.y + bounds.height * 0.5};
    painter.draw_line({shoulder, bounds.y + 0.5}, tip, edge, 1.0);
    painter.draw_line(tip, {shoulder, bounds.y + bounds.height - 0.5}, edge, 1.0);
}

// The strips a shaped part's taper, lip and edge shadow are cut into, each
// a rectangle, so the shape is a bounded staircase (as the pointed face's
// nose is).
constexpr std::size_t part_profile_strips = 12U;

// Whether `part` carries a profile that draws anything.
bool profiled(const PartPaint& part) noexcept {
    if (!part.profile || part.point || !surface_role(part.role)) return false;
    const PartProfile& profile = *part.profile;
    const bool any = profile.taper > 0.0 || profile.flare > 0.0 || profile.edge_shadow > 0.0;
    return any;
}

// The rows a profiled part's trailing edge is drawn in: whole logical
// units, so that at an integer scale no antialiased clip edge is painted
// twice (a seam). One-unit rows through the top corner, where the outline
// curves, then about a twelfth of the height each. At most this many rows.
constexpr std::size_t maximum_profile_rows = part_profile_strips + 2U * static_cast<std::size_t>(maximum_part_shape);

// One row of a profiled part: its top and bottom and where its outline
// stands, the least x of the outline over the row, so that the face cut
// at it never runs past the outline and the shadow begins exactly there.
struct ProfileRow final {
    double top{};
    double bottom{};
    double outline{};
};

// The trailing outline at height `y`: the taper's straight lean, rounded
// into the top edge by the part's top-right radius as the face's corner
// would be.
double profile_outline(Rect bounds, double taper, double radius, double y) noexcept {
    const double right = bounds.x + bounds.width;
    const double down = std::clamp(y - bounds.y, 0.0, bounds.height);
    const double lean = bounds.height > 0.0 ? taper * (1.0 - down / bounds.height) : 0.0;
    double inset = 0.0;
    if (radius > 0.0 && down < radius) {
        const double rise = radius - down;
        inset = radius - std::sqrt(std::max(0.0, radius * radius - rise * rise));
    }
    const double outline = right - lean - inset;
    return outline;
}

// Fills `rows` (capacity maximum_profile_rows) and returns the live count.
std::size_t profile_rows(Rect bounds, double taper, double radius, std::span<ProfileRow> rows) noexcept {
    std::size_t count = 0U;
    const double corner = std::min(std::ceil(radius), std::floor(bounds.height));
    double top = bounds.y;
    for (double step = 0.0; step < corner && count < rows.size(); step += 1.0) {
        rows[count] = ProfileRow{top, top + 1.0, profile_outline(bounds, taper, radius, top)};
        ++count;
        top += 1.0;
    }
    const double rest = bounds.y + bounds.height - top;
    for (std::size_t strip = 0U; strip < part_profile_strips && count < rows.size(); ++strip) {
        const double start = top + std::round(rest * static_cast<double>(strip) / static_cast<double>(part_profile_strips));
        const double end =
            top + std::round(rest * static_cast<double>(strip + 1U) / static_cast<double>(part_profile_strips));
        if (end <= start) continue;
        rows[count] = ProfileRow{start, end, profile_outline(bounds, taper, radius, start)};
        ++count;
    }
    return count;
}

// A profiled part (PartPaint::profile), in order: the edge shadow beyond the
// trailing outline, the part itself (once clipped to its body and once to
// each row of the outline), the outline's line, and the lip at its bottom
// corners. The outline at height y leans from `right - taper` at the top to
// `right` at the bottom and rounds into the top edge by the part's top-right
// radius (owner, 2026-11-09): the shadow starts in each row exactly where
// the face ends, so the face's edge and the shadow's falloff are one
// continuous edge, the top corner included.
void paint_profiled_part(Painter& painter, const Control& control, const PartPaint& part) {
    const PartProfile& profile = *part.profile;
    PartPaint whole = part;
    whole.profile.reset();
    const Rect bounds = part.bounds;
    const double right = bounds.x + bounds.width;
    const double bottom = bounds.y + bounds.height;
    const double taper = std::clamp(profile.taper, 0.0, std::min(bounds.width, maximum_part_shape));
    const double flare = std::clamp(profile.flare, 0.0, std::min(bounds.height, maximum_part_shape));
    const double falloff = std::clamp(profile.edge_shadow, 0.0, maximum_part_shape);
    const ControlVisualContext context = part_visual_context(control, part.states);
    const std::string_view part_name = part.key ? (*part.key).part : std::string_view{};
    const ResolvedControlRecipe resolved =
        control.effective_theme().resolve(control, part.family, context, part.states, part_name, part.control_type);
    const SurfaceMaterial& material = resolved.recipe().material;
    double radius = part.corner_radius ? std::max(0.0, *part.corner_radius) : material.corner_radius;
    if (!part.corner_radius && material.corner_radii) radius = (*material.corner_radii).top_right;
    radius = std::clamp(radius, 0.0, std::min(maximum_part_shape, bounds.height));
    // Without a taper the outline is the straight right edge; its rounded
    // top corner is the face's own and only the shadow follows it.
    std::array<ProfileRow, maximum_profile_rows> rows{};
    const std::size_t row_count = profile_rows(bounds, taper, radius, rows);
    if (falloff > 0.0) {
        const Color clear = Color::rgba(profile.edge_shadow_colour.red, profile.edge_shadow_colour.green,
                                        profile.edge_shadow_colour.blue, 0);
        const std::array<GradientStop, 2> stops{GradientStop{0.0, profile.edge_shadow_colour}, GradientStop{1.0, clear}};
        for (std::size_t index = 0U; index < row_count; ++index) {
            const ProfileRow& row = rows[index];
            const double middle = (row.top + row.bottom) * 0.5;
            const Rect band{row.outline, row.top, falloff, row.bottom - row.top};
            painter.fill_linear_gradient(band, {row.outline, middle}, {row.outline + falloff, middle}, stops);
        }
    }
    if (taper <= 0.0) {
        paint_part(painter, control, whole);
    } else {
        const double shoulder = std::floor(right - taper - radius);
        painter.save();
        painter.clip_rect({bounds.x, bounds.y, std::max(0.0, shoulder - bounds.x), bounds.height});
        paint_part(painter, control, whole);
        painter.restore();
        for (std::size_t index = 0U; index < row_count; ++index) {
            const ProfileRow& row = rows[index];
            if (row.outline <= shoulder) continue;
            painter.save();
            painter.clip_rect({shoulder, row.top, row.outline - shoulder, row.bottom - row.top});
            paint_part(painter, control, whole);
            painter.restore();
        }
        if (profile.taper_colour && part.frame != PartFrame::none) {
            // The line follows the outline: one segment per corner row,
            // then the straight lean to the foot.
            Point from{profile_outline(bounds, taper, radius, bounds.y) - 0.5, bounds.y + 0.5};
            const double corner_end = bounds.y + std::min(std::ceil(radius), std::floor(bounds.height));
            for (double y = bounds.y + 1.0; y <= corner_end; y += 1.0) {
                const Point to{profile_outline(bounds, taper, radius, y) - 0.5, y};
                painter.draw_line(from, to, *profile.taper_colour, 1.0);
                from = to;
            }
            painter.draw_line(from, {right - 0.5, bottom}, *profile.taper_colour, 1.0);
        }
    }
    if (flare <= 0.0) return;
    // The lip: a concave quarter at each bottom corner, flat where it meets
    // what lies below the part.
    const double lip_strip = flare / static_cast<double>(part_profile_strips);
    for (std::size_t strip = 0U; strip < part_profile_strips; ++strip) {
        const double top = bottom - flare + lip_strip * static_cast<double>(strip);
        const double depth = (top + lip_strip * 0.5) - (bottom - flare);
        const double reach = flare - std::sqrt(std::max(0.0, flare * flare - depth * depth));
        if (reach <= 0.0) continue;
        painter.fill_rect({bounds.x - reach, top, reach, lip_strip}, profile.flare_colour);
        painter.fill_rect({right, top, reach, lip_strip}, profile.flare_colour);
    }
}

} // namespace

namespace {

// Whether the built-in recipe fills all of `part`'s bounds opaquely with
// `material`: a surface role painted with the material, square, unpointed,
// and the material opaque (surface_material_opaque).
bool opaque_surface_part(const PartPaint& part, const SurfaceMaterial& material) noexcept {
    // A fill with a content colour paints that colour, not the material.
    const bool content = part.role == PartRole::fill && part.content_color;
    if (!surface_role(part.role) || content || part.point) return false;
    if (part.corner_radius && *part.corner_radius != 0.0) return false;
    const bool opaque = surface_material_opaque(material);
    return opaque;
}

} // namespace

bool pointed_face_contains(Rect bounds, double point, Point position) noexcept {
    if (!bounds.contains(position)) return false;
    const double half_height = bounds.height * 0.5;
    if (point <= 0.0 || half_height <= 0.0) return true;
    const double tip = std::min(point, bounds.width);
    const double distance = std::abs(position.y - bounds.y - half_height);
    const double edge = bounds.x + bounds.width - tip * distance / half_height;
    const bool inside = position.x <= edge;
    return inside;
}

namespace {

// The provisional accent (design language §8.5) over a surface part a
// theme rule's primitive drew: the built-in recipe lays it on itself
// (paint_part_with_built_in_recipe), a primitive does not, so a contextual
// tab or plane a theme styles keeps its tint (RB 25). The ramp colour
// carries its own translucency and follows the recipe's corners.
// The accent fill a part's primitive takes, or none.
struct AccentFill final {
    double radius{};
    Color color{};
};

std::optional<AccentFill> primitive_accent_fill(const Control& control, const PartPaint& part) {
    if (!part.accent || !surface_role(part.role) || !paintable(part.bounds)) return std::nullopt;
    const Theme& theme = control.effective_theme();
    const ThemeEngine::PartPlan decided = theme.engine().plan(control, part);
    if (decided.drawing != ThemeEngine::PartDrawing::primitive) return std::nullopt;
    const ControlVisualContext context = part_visual_context(control, part.states);
    const std::string_view part_name = part.key ? (*part.key).part : std::string_view{};
    const ResolvedControlRecipe resolved =
        theme.resolve(control, part.family, context, part.states, part_name, part.control_type);
    AccentFill fill{};
    fill.radius = part_corner_radius(part, resolved.recipe());
    fill.color = theme.accent(*part.accent);
    return fill;
}

// Fills the part's accent under each of `clips`, or once without any.
void paint_primitive_accent_clipped(Painter& painter, const Control& control, const PartPaint& part,
                                    const std::span<const Rect> clips) {
    // Resolved once; a part with no accent records nothing, as before.
    const std::optional<AccentFill> fill = primitive_accent_fill(control, part);
    if (!fill) return;
    if (clips.empty()) {
        painter.fill_rounded_rect(part.bounds, (*fill).radius, (*fill).color);
        return;
    }
    for (const Rect clip : clips) {
        painter.save();
        painter.clip_rect(clip);
        painter.fill_rounded_rect(part.bounds, (*fill).radius, (*fill).color);
        painter.restore();
    }
}

} // namespace

namespace {

// paint_part with what it draws repeated under each of `clips` (N91). A
// pointed or profiled part, which cuts itself by clips of its own, is
// painted whole under each clip.
void paint_part_clipped(Painter& painter, const Control& control, const PartPaint& part,
                        const std::span<const Rect> clips) {
    // Drawn in GUI.Forms' own theme in place of the application's (T4
    // theme variants): colours chosen against the other theme are not
    // applied, the part's own included.
    const bool suspended = control.application_colours_suspended();
    if (suspended && (part.authored_background || part.authored_foreground)) {
        PartPaint plain = part;
        plain.authored_background.reset();
        plain.authored_foreground.reset();
        paint_part_clipped(painter, control, plain, clips);
        return;
    }
    const double length = pointed_length(part);
    if (length > 0.0 && paintable(part.bounds)) {
        if (clips.empty()) {
            paint_pointed_part(painter, control, part, length);
            return;
        }
        for (const Rect clip : clips) {
            painter.save();
            painter.clip_rect(clip);
            paint_pointed_part(painter, control, part, length);
            painter.restore();
        }
        return;
    }
    if (profiled(part) && paintable(part.bounds)) {
        if (clips.empty()) {
            paint_profiled_part(painter, control, part);
            return;
        }
        for (const Rect clip : clips) {
            painter.save();
            painter.clip_rect(clip);
            paint_profiled_part(painter, control, part);
            painter.restore();
        }
        return;
    }
    const Theme& theme = control.effective_theme();
    // The control's authored colours (Control::set_back_color and
    // set_fore_color) under the part's own: its own background fills every
    // ground, an inherited one every ground but a well.
    std::optional<Color> background = part.authored_background;
    if (!background && !suspended) background = control.back_color.read();
    if (!background && !suspended && part.role != PartRole::well) background = control.ambient_back_color();
    std::optional<Color> foreground = part.authored_foreground;
    if (!foreground && !suspended) foreground = control.ambient_fore_color();
    const bool unchanged = background == part.authored_background && foreground == part.authored_foreground;
    if (unchanged) {
        theme.engine().paint_clipped(painter, control, part, clips);
        paint_primitive_accent_clipped(painter, control, part, clips);
        return;
    }
    PartPaint coloured = part;
    coloured.authored_background = background;
    coloured.authored_foreground = foreground;
    theme.engine().paint_clipped(painter, control, coloured, clips);
    paint_primitive_accent_clipped(painter, control, coloured, clips);
}

} // namespace

void paint_part(Painter& painter, const Control& control, const PartPaint& part) {
    paint_part_clipped(painter, control, part, std::span<const Rect>{});
}

void paint_text_part_at(Painter& painter, const Control& control, PartPaint part, Point origin,
                        double width) {
    if (part.text.empty()) return;
    // The line box paint_text centres the text in: its height and ascent.
    const Size measured = part.measured_text ? *part.measured_text
                                             : painter.measure_text_utf8(part.text, part.font);
    const double line_height = std::max(measured.height, part.font.size * 1.2);
    const double ascent = std::max(part.font.size, line_height * 0.82);
    part.alignment = PartTextAlignment::start;
    part.orientation = TextOrientation::horizontal;
    part.bounds = {origin.x, origin.y - ascent, std::max(width, measured.width), line_height};
    part.measured_text = measured;
    paint_part(painter, control, part);
}

void paint_part_with_built_in_recipe(Painter& painter, const Control& control, const PartPaint& part,
                                     const SurfaceMaterial* resolved_material) {
    if (!paintable(part.bounds)) return;
    const ControlVisualContext context = part_visual_context(control, part.states);
    const Theme& theme = control.effective_theme();
    // The control's recipe fields (T3 P6f) in the part's states: a label
    // part takes the control's text colour, or its own where a rule names
    // the part.
    const std::string_view part_name = part.key ? (*part.key).part : std::string_view{};
    const ResolvedControlRecipe resolved = theme.resolve(control, part.family, context, part.states, part_name,
                                                              part.control_type);
    const ControlVisualRecipe& recipe = resolved.recipe();
    if (part.role == PartRole::shadow) {
        paint_overlay_shadow(painter, part, recipe, theme.overlay_shadow(context.high_contrast));
        return;
    }
    // Authored colours (owner decision T4-1) stand in a copy of the recipe,
    // made only for a part that carries them, and never in high contrast;
    // so does the engine's resolved material, made only for a part that has
    // one.
    const bool authored = (part.authored_background || part.authored_foreground) && !context.high_contrast;
    const bool resolved_ground = resolved_material != nullptr;
    bool opaque_face = false;
    if (authored || resolved_ground) {
        ControlVisualRecipe styled = recipe;
        if (resolved_ground) styled.material = *resolved_material;
        const bool disabled = context.surface == ControlSurfaceState::disabled;
        if (authored) {
            apply_authored_colours(part, disabled, resolved.important_text(), resolved.important_glyph(), styled);
        }
        paint_part_from_recipe(painter, part, styled, context);
        opaque_face = opaque_surface_part(part, styled.material);
    } else {
        paint_part_from_recipe(painter, part, recipe, context);
        opaque_face = opaque_surface_part(part, recipe.material);
    }
    // An opaque engine face (owner, 2026-11-05): what lies beneath it, a
    // window's backdrop included, need not be painted. The window keeps the
    // cover only when the face was drawn untranslated and unclipped.
    if (opaque_face) control.report_opaque_cover(painter, part.bounds);
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
    case PartRole::plot:
    case PartRole::legend:
    case PartRole::color_scale:
        paint_surface(painter, part, recipe);
        return;
    case PartRole::indicator:
    case PartRole::trace:
    case PartRole::marker:
    case PartRole::series:
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
    case PartRole::graticule:
    case PartRole::axis:
    case PartRole::tick:
    case PartRole::grid_major:
    case PartRole::grid_minor:
    case PartRole::crosshair:
        paint_rule(painter, part.bounds, recipe);
        return;
    case PartRole::arc:
        paint_arc(painter, part, recipe);
        return;
    case PartRole::needle:
        paint_needle(painter, part, recipe);
        return;
    case PartRole::beak:
        paint_beak(painter, part, recipe);
        return;
    case PartRole::scrim:
        // A scrim is drawn in its colour as given, disabled or not.
        painter.fill_rect(part.bounds, recipe.scrim);
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
    case PartRole::tick_label:
    case PartRole::legend_entry:
    case PartRole::annotation:
        // The recipe is resolved in the disabled state, so its text is the
        // theme's own disabled text (owner ruling, 2026-11-02).
        // A secondary text is drawn in the muted ink; disabled text of either
        // tone is the disabled recipe's text.
        paint_text(painter, part,
                   part.tone == PartTextTone::secondary && !disabled ? recipe.muted_text : recipe.text);
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
        if (context.focused) paint_focus_ring(painter, part.bounds, recipe, part_corner_radius(part, recipe));
        return;
    case PartRole::preselect:
        if (context.defaulted) paint_preselect(painter, part.bounds, recipe, part_corner_radius(part, recipe));
        return;
    }
    // A role outside the enumeration paints nothing.
    return;
}

} // namespace gui_forms
