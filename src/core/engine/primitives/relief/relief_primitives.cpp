#include "relief_primitives.hpp"

#include "../kernel/pixel_kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// The colour a vertical gradient from `top` to `bottom` has at `t` in
// [0, 1], each straight channel interpolated.
Color gradient_at(Color top, Color bottom, double t) noexcept {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const double red = static_cast<double>(top.red) +
                       (static_cast<double>(bottom.red) - static_cast<double>(top.red)) * clamped;
    const double green = static_cast<double>(top.green) +
                         (static_cast<double>(bottom.green) - static_cast<double>(top.green)) * clamped;
    const double blue = static_cast<double>(top.blue) +
                        (static_cast<double>(bottom.blue) - static_cast<double>(top.blue)) * clamped;
    const double alpha = static_cast<double>(top.alpha) +
                         (static_cast<double>(bottom.alpha) - static_cast<double>(top.alpha)) * clamped;
    const Color colour{static_cast<std::uint8_t>(std::floor(red + 0.5)),
                       static_cast<std::uint8_t>(std::floor(green + 0.5)),
                       static_cast<std::uint8_t>(std::floor(blue + 0.5)),
                       static_cast<std::uint8_t>(std::floor(alpha + 0.5))};
    return colour;
}

Rect inset_box(Rect box, double amount) noexcept {
    const Rect inset{box.x + amount, box.y + amount, std::max(0.0, box.width - amount * 2.0),
                     std::max(0.0, box.height - amount * 2.0)};
    return inset;
}

// A shape whose radius shrinks with its box: the face inside a border.
PartShape inset_shape(PartShape shape, double amount) noexcept {
    PartShape inset = shape;
    inset.corner_radius = std::max(0.0, shape.corner_radius - amount);
    return inset;
}

double number_at(const PrimitiveParameterBlock& parameters, std::size_t index) noexcept {
    const double value = parameters.values[index].number;
    return value;
}

Color colour_at(const PrimitiveParameterBlock& parameters, std::size_t index) noexcept {
    const Color colour = parameters.values[index].colour;
    return colour;
}

// A shape from a `shape` choice and a `corner_radius` in logical units, the
// radius scaled to device pixels.
PartShape shape_from(const PrimitiveParameterBlock& parameters, std::size_t shape_index, std::size_t radius_index,
                     double scale) noexcept {
    PartShape shape{};
    shape.kind = static_cast<PartShapeKind>(static_cast<std::uint8_t>(number_at(parameters, shape_index)));
    shape.corner_radius = number_at(parameters, radius_index) * scale;
    return shape;
}

// The plate's layers for one pixel, the inputs of plate_pixel.
struct PlateLook final {
    PartShape shape{};
    // In device pixels: the plate's box, its face, and the line width.
    Rect plate{};
    Rect face{};
    double line{};
    // The ring of the default action around the plate, when drawn.
    bool perimeter{};
    Rect ring{};
    Color perimeter_colour{};
    bool shadow{};
    Color shadow_colour{};
    Color border{};
    Color face_top{};
    Color face_bottom{};
    // The inner lines: top, left, right, bottom, each drawn only when set.
    bool top_line{};
    Color top_colour{};
    bool left_line{};
    Color left_colour{};
    bool right_line{};
    Color right_colour{};
    bool bottom_line{};
    Color bottom_colour{};
};

Unit plate_pixel(const PlateLook& look, std::int64_t x, std::int64_t y) noexcept {
    Unit pixel{};
    const double px = static_cast<double>(x);
    const double py = static_cast<double>(y);
    if (look.shadow) {
        const Rect below{look.plate.x, look.plate.y + look.line, look.plate.width, look.plate.height};
        blend_over(pixel, premultiply(look.shadow_colour, shape_coverage(look.shape, below, 1.0, x, y)));
    }
    if (look.perimeter) {
        blend_over(pixel, premultiply(look.perimeter_colour, shape_coverage(look.shape, look.ring, 1.0, x, y)));
    }
    blend_over(pixel, premultiply(look.border, shape_coverage(look.shape, look.plate, 1.0, x, y)));
    const PartShape face_shape = inset_shape(look.shape, look.line);
    const double face = shape_coverage(face_shape, look.face, 1.0, x, y);
    if (face <= 0.0) return pixel;
    const double t = look.face.height > 0.0 ? (py + 0.5 - look.face.y) / look.face.height : 0.0;
    blend_over(pixel, premultiply(gradient_at(look.face_top, look.face_bottom, t), face));
    const Rect& f = look.face;
    if (look.top_line) {
        const double band = interval_overlap(py, f.y, f.y + look.line);
        blend_over(pixel, premultiply(look.top_colour, face * band));
    }
    if (look.left_line) {
        const double band = interval_overlap(px, f.x, f.x + look.line);
        blend_over(pixel, premultiply(look.left_colour, face * band));
    }
    if (look.right_line) {
        const double right = f.x + f.width;
        const double band = interval_overlap(px, right - look.line, right);
        blend_over(pixel, premultiply(look.right_colour, face * band));
    }
    if (look.bottom_line) {
        const double bottom = f.y + f.height;
        const double band = interval_overlap(py, bottom - look.line, bottom);
        blend_over(pixel, premultiply(look.bottom_colour, face * band));
    }
    return pixel;
}

// The plate's box in device pixels: the part's box without its bottom
// line, where the contact shadow lies.
Rect plate_box(const PrimitiveTarget& target, double line) noexcept {
    const double width = target.logical_size.width * target.scale.value;
    const double height = target.logical_size.height * target.scale.value;
    const Rect plate{0.0, 0.0, width, std::max(0.0, height - line)};
    return plate;
}

void fill_plate(const PlateLook& look, const PartRasterView& destination) noexcept {
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            const Unit pixel = plate_pixel(look, static_cast<std::int64_t>(column), static_cast<std::int64_t>(row));
            store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
        }
    }
}

constexpr Color white{255, 255, 255, 255};

// How far the plate's rounding reaches into its box on each side, in
// logical units: `half` is half the box's shorter side, and `across` says
// the box is at least as wide as it is high.
Insets plate_rounding(PartShape shape, double width, double plate_height, double half, bool across) noexcept {
    // 1 - 1/sqrt(2): how far a rounded corner's arc lies inside its box's
    // corner along each side, as a fraction of the radius.
    constexpr double arc_fraction = 0.29289321881345247;
    switch (shape.kind) {
    case PartShapeKind::rounded_rectangle: {
        const double cut = std::clamp(shape.corner_radius, 0.0, half) * arc_fraction;
        const Insets rounded{cut, cut, cut, cut};
        return rounded;
    }
    case PartShapeKind::circle: {
        // The square inscribed in the centred circle.
        const double cut = half * arc_fraction;
        const double across_gap = (width - half * 2.0) * 0.5;
        const double down_gap = (plate_height - half * 2.0) * 0.5;
        const Insets inscribed{across_gap + cut, down_gap + cut, across_gap + cut, down_gap + cut};
        return inscribed;
    }
    case PartShapeKind::pill:
        return across ? Insets{half, 0.0, half, 0.0} : Insets{0.0, half, 0.0, half};
    case PartShapeKind::cap_start:
        return across ? Insets{half, 0.0, 0.0, 0.0} : Insets{0.0, half, 0.0, 0.0};
    case PartShapeKind::cap_end:
        return across ? Insets{0.0, 0.0, half, 0.0} : Insets{0.0, 0.0, 0.0, half};
    case PartShapeKind::count:
        // Not a shape: the square box, unrounded.
        return Insets{};
    }
    // A kind outside the enumeration is the square box, unrounded.
    return Insets{};
}

// A plate's text slot insets (see relief_primitives.hpp), in logical units.
Insets plate_content_insets(PartShape shape, Size logical, DeviceScale scale, double padding,
                            double text_scale) noexcept {
    const double line = keyline_width(scale.value) / scale.value;
    // The plate's box: the part's less its bottom line (plate_box).
    const double plate_height = std::max(0.0, logical.height - line);
    const double half = std::min(logical.width, plate_height) * 0.5;
    const bool across = logical.width >= plate_height;
    const Insets rounding = plate_rounding(shape, logical.width, plate_height, half, across);
    const double lines = line * 2.0;
    const double side = padding * text_scale;
    const Insets insets{rounding.left + lines + side, rounding.top + lines, rounding.right + lines + side,
                        rounding.bottom + lines + line};
    return insets;
}

// -- the pill plate --------------------------------------------------------------

const PartRole plate_roles[] = {PartRole::face, PartRole::actuator, PartRole::thumb, PartRole::chip};
const AnatomyState pill_states[] = {AnatomyState::hot, AnatomyState::pressed, AnatomyState::disabled,
                                    AnatomyState::preselect};

enum PillParameter : std::size_t {
    pill_shape,
    pill_corner_radius,
    pill_face_top,
    pill_face_bottom,
    pill_border,
    pill_upper,
    pill_lower,
    pill_shadow,
    pill_hot_top,
    pill_hot_bottom,
    pill_hot_border,
    pill_pressed_top,
    pill_pressed_bottom,
    pill_pressed_upper,
    pill_pressed_lower,
    pill_disabled_top,
    pill_disabled_bottom,
    pill_disabled_border,
    pill_disabled_upper,
    pill_perimeter,
    pill_text_padding,
    pill_parameter_count,
};

const PrimitiveParameterDeclaration pill_parameters[] = {
    {"shape", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, part_shape_names},
    {"corner_radius", PrimitiveParameterKind::number, 0.0, 64.0, 3.0, {}},
    {"face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {210, 228, 251, 255}},
    {"face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {163, 193, 236, 255}},
    {"border", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {99, 132, 178, 255}},
    {"upper_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 230}},
    {"lower_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {49, 86, 134, 135}},
    {"contact_shadow", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {41, 75, 119, 85}},
    {"hot_face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {218, 233, 252, 255}},
    {"hot_face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {177, 204, 242, 255}},
    {"hot_border", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {53, 94, 150, 255}},
    {"pressed_face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {152, 181, 218, 255}},
    {"pressed_face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {181, 204, 235, 255}},
    {"pressed_upper_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {44, 86, 132, 130}},
    {"pressed_lower_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 155}},
    {"disabled_face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {190, 207, 228, 255}},
    {"disabled_face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {181, 199, 222, 255}},
    {"disabled_border", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {143, 166, 196, 100}},
    {"disabled_upper_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 100}},
    {"perimeter", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {33, 75, 129, 255}},
    {"text_padding", PrimitiveParameterKind::number, 0.0, 32.0, 4.0, {}},
};
static_assert(std::size(pill_parameters) == pill_parameter_count, "one declaration per pill parameter");

// Seven anti-aliased layers of a few operations each, and the gradient.
const PrimitiveDescriptor pill_descriptor{
    "pill_plate", EngineVersion{0, 2}, plate_roles, pill_states, pill_parameters, PrimitiveCost{256U, 64U, 0U},
    Insets{}, 0U};

// -- the gold plate --------------------------------------------------------------

const PartRole gold_roles[] = {PartRole::face, PartRole::actuator, PartRole::chip};
const AnatomyState gold_states[] = {AnatomyState::pressed};

enum GoldParameter : std::size_t {
    gold_shape,
    gold_corner_radius,
    gold_face_top,
    gold_face_bottom,
    gold_border,
    gold_pressed_top,
    gold_pressed_bottom,
    gold_upper,
    gold_left,
    gold_lower,
    gold_text_padding,
    gold_parameter_count,
};

const PrimitiveParameterDeclaration gold_parameters[] = {
    {"shape", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, part_shape_names},
    {"corner_radius", PrimitiveParameterKind::number, 0.0, 64.0, 2.0, {}},
    {"face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 237, 186, 255}},
    {"face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 207, 102, 255}},
    {"border", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {196, 147, 50, 255}},
    {"pressed_face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {246, 193, 91, 255}},
    {"pressed_face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 226, 151, 255}},
    {"upper_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 240, 255}},
    {"left_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 240, 180}},
    {"lower_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {160, 109, 28, 35}},
    {"text_padding", PrimitiveParameterKind::number, 0.0, 32.0, 4.0, {}},
};
static_assert(std::size(gold_parameters) == gold_parameter_count, "one declaration per gold parameter");

const PrimitiveDescriptor gold_descriptor{
    "gold_plate", EngineVersion{0, 2}, gold_roles, gold_states, gold_parameters, PrimitiveCost{192U, 64U, 0U},
    Insets{}, 0U};

// -- the inset well --------------------------------------------------------------

// An instrument's plot (W-5) is a well a reading is drawn in.
const PartRole well_roles[] = {PartRole::well, PartRole::track, PartRole::plot};

enum WellParameter : std::size_t {
    well_shape,
    well_corner_radius,
    well_face_top,
    well_face_bottom,
    well_border,
    well_inner_shade,
    well_outer_highlight,
    well_text_padding,
    well_parameter_count,
};

const PrimitiveParameterDeclaration well_parameters[] = {
    {"shape", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, part_shape_names},
    {"corner_radius", PrimitiveParameterKind::number, 0.0, 64.0, 0.0, {}},
    {"face_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {248, 251, 255, 255}},
    {"face_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, white},
    {"border", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {148, 172, 200, 255}},
    {"inner_shade", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {111, 142, 177, 75}},
    {"outer_highlight", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 225}},
    {"text_padding", PrimitiveParameterKind::number, 0.0, 32.0, 2.0, {}},
};
static_assert(std::size(well_parameters) == well_parameter_count, "one declaration per well parameter");

const PrimitiveDescriptor well_descriptor{
    "inset_well", EngineVersion{0, 2}, well_roles, {}, well_parameters, PrimitiveCost{160U, 64U, 0U}, Insets{},
    0U};

// -- the etched divider and the seam ---------------------------------------------

// An instrument's graticule (W-5) is ruled as a divider.
const PartRole divider_roles[] = {PartRole::divider, PartRole::graticule};
const PartRole seam_roles[] = {PartRole::seam};

const PrimitiveParameterDeclaration divider_parameters[] = {
    {"shade_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {143, 170, 201, 125}},
    {"light_line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 235}},
};

const PrimitiveDescriptor divider_descriptor{
    "etched_divider", EngineVersion{0, 2}, divider_roles, {}, divider_parameters, PrimitiveCost{32U, 16U, 0U},
    Insets{}, 0U};

const PrimitiveParameterDeclaration seam_parameters[] = {
    {"line", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {148, 172, 200, 255}},
};

const PrimitiveDescriptor seam_descriptor{
    "seam_line", EngineVersion{0, 2}, seam_roles, {}, seam_parameters, PrimitiveCost{16U, 16U, 0U}, Insets{}, 0U};

// -- the grip --------------------------------------------------------------------

const PartRole grip_roles[] = {PartRole::grip};

const PrimitiveParameterDeclaration grip_parameters[] = {
    {"dot", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {98, 121, 151, 255}},
};

const PrimitiveDescriptor grip_descriptor{
    "grip", EngineVersion{0, 2}, grip_roles, {}, grip_parameters, PrimitiveCost{16U, 16U, 0U}, Insets{}, 0U};

// The fading line pair's weight at position `p` in [0, 1] along it.
double fade_weight(double p) noexcept {
    if (p < 0.16) {
        const double rising = p / 0.16;
        return rising;
    }
    if (p > 0.83) {
        const double falling = (1.0 - p) / 0.17;
        return std::max(0.0, falling);
    }
    return 1.0;
}

} // namespace

// -- PillPlatePrimitive ------------------------------------------------------------

const PrimitiveDescriptor& PillPlatePrimitive::descriptor() const noexcept {
    return pill_descriptor;
}

PartShape PillPlatePrimitive::shape(const PrimitiveParameterBlock& parameters) const noexcept {
    const PartShape logical = shape_from(parameters, pill_shape, pill_corner_radius, 1.0);
    return logical;
}

Insets PillPlatePrimitive::content_insets(const PrimitiveParameterBlock& parameters, Size logical,
                                          DeviceScale scale, double text_scale) const noexcept {
    const Insets insets = plate_content_insets(shape(parameters), logical, scale,
                                               number_at(parameters, pill_text_padding), text_scale);
    return insets;
}

void PillPlatePrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void PillPlatePrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                              std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    const AnatomyStateSet states = target.states;
    const bool disabled = states.contains(AnatomyState::disabled);
    const bool pressed = !disabled && states.contains(AnatomyState::pressed);
    const bool hot = !disabled && !pressed && states.contains(AnatomyState::hot);
    PlateLook look{};
    look.shape = shape_from(parameters, pill_shape, pill_corner_radius, scale);
    look.line = keyline_width(scale);
    look.plate = plate_box(target, look.line);
    // The default action's second perimeter takes the plate's outer line.
    look.perimeter = !disabled && states.contains(AnatomyState::preselect);
    if (look.perimeter) {
        look.ring = look.plate;
        look.perimeter_colour = colour_at(parameters, pill_perimeter);
        look.plate = inset_box(look.plate, look.line);
        look.shape = inset_shape(look.shape, look.line);
    }
    look.face = inset_box(look.plate, look.line);
    look.shadow = !disabled && !pressed;
    look.shadow_colour = colour_at(parameters, pill_shadow);
    look.border = colour_at(parameters, disabled ? pill_disabled_border : hot ? pill_hot_border : pill_border);
    if (disabled) {
        look.face_top = colour_at(parameters, pill_disabled_top);
        look.face_bottom = colour_at(parameters, pill_disabled_bottom);
        look.top_line = true;
        look.top_colour = colour_at(parameters, pill_disabled_upper);
    } else {
        const std::size_t top = pressed ? pill_pressed_top : hot ? pill_hot_top : pill_face_top;
        const std::size_t bottom = pressed ? pill_pressed_bottom : hot ? pill_hot_bottom : pill_face_bottom;
        look.face_top = colour_at(parameters, top);
        look.face_bottom = colour_at(parameters, bottom);
        const Color upper = colour_at(parameters, pressed ? pill_pressed_upper : pill_upper);
        const Color lower = colour_at(parameters, pressed ? pill_pressed_lower : pill_lower);
        look.top_line = true;
        look.top_colour = upper;
        look.left_line = true;
        look.left_colour = upper;
        look.right_line = true;
        look.right_colour = lower;
        look.bottom_line = true;
        look.bottom_colour = lower;
    }
    fill_plate(look, destination);
}

// -- GoldPlatePrimitive ------------------------------------------------------------

const PrimitiveDescriptor& GoldPlatePrimitive::descriptor() const noexcept {
    return gold_descriptor;
}

PartShape GoldPlatePrimitive::shape(const PrimitiveParameterBlock& parameters) const noexcept {
    const PartShape logical = shape_from(parameters, gold_shape, gold_corner_radius, 1.0);
    return logical;
}

Insets GoldPlatePrimitive::content_insets(const PrimitiveParameterBlock& parameters, Size logical,
                                          DeviceScale scale, double text_scale) const noexcept {
    const Insets insets = plate_content_insets(shape(parameters), logical, scale,
                                               number_at(parameters, gold_text_padding), text_scale);
    return insets;
}

void GoldPlatePrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void GoldPlatePrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                              std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    const bool pressed = target.states.contains(AnatomyState::pressed);
    PlateLook look{};
    look.shape = shape_from(parameters, gold_shape, gold_corner_radius, scale);
    look.line = keyline_width(scale);
    look.plate = plate_box(target, look.line);
    look.face = inset_box(look.plate, look.line);
    look.border = colour_at(parameters, gold_border);
    look.face_top = colour_at(parameters, pressed ? gold_pressed_top : gold_face_top);
    look.face_bottom = colour_at(parameters, pressed ? gold_pressed_bottom : gold_face_bottom);
    look.top_line = true;
    look.top_colour = colour_at(parameters, gold_upper);
    look.left_line = true;
    look.left_colour = colour_at(parameters, gold_left);
    look.bottom_line = true;
    look.bottom_colour = colour_at(parameters, gold_lower);
    fill_plate(look, destination);
}

// -- InsetWellPrimitive ------------------------------------------------------------

const PrimitiveDescriptor& InsetWellPrimitive::descriptor() const noexcept {
    return well_descriptor;
}

PartShape InsetWellPrimitive::shape(const PrimitiveParameterBlock& parameters) const noexcept {
    const PartShape logical = shape_from(parameters, well_shape, well_corner_radius, 1.0);
    return logical;
}

Insets InsetWellPrimitive::content_insets(const PrimitiveParameterBlock& parameters, Size logical,
                                          DeviceScale scale, double text_scale) const noexcept {
    const Insets insets = plate_content_insets(shape(parameters), logical, scale,
                                               number_at(parameters, well_text_padding), text_scale);
    return insets;
}

void InsetWellPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void InsetWellPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                              std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    PlateLook look{};
    look.shape = shape_from(parameters, well_shape, well_corner_radius, scale);
    look.line = keyline_width(scale);
    look.plate = plate_box(target, look.line);
    look.face = inset_box(look.plate, look.line);
    look.border = colour_at(parameters, well_border);
    look.face_top = colour_at(parameters, well_face_top);
    look.face_bottom = colour_at(parameters, well_face_bottom);
    look.top_line = true;
    look.top_colour = colour_at(parameters, well_inner_shade);
    const Color highlight = colour_at(parameters, well_outer_highlight);
    // The outer bottom highlight: the line below the well, across it.
    const double below = look.plate.y + look.plate.height;
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        const double band = interval_overlap(static_cast<double>(row), below, below + look.line);
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            Unit pixel{};
            const double across = interval_overlap(static_cast<double>(column), look.plate.x,
                                                   look.plate.x + look.plate.width);
            blend_over(pixel, premultiply(highlight, band * across));
            blend_over(pixel, plate_pixel(look, static_cast<std::int64_t>(column), static_cast<std::int64_t>(row)));
            store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
        }
    }
}

// -- EtchedDividerPrimitive --------------------------------------------------------

const PrimitiveDescriptor& EtchedDividerPrimitive::descriptor() const noexcept {
    return divider_descriptor;
}

void EtchedDividerPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&,
                                    std::span<double>) const noexcept {}

void EtchedDividerPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                                  std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    const double width = target.logical_size.width * scale;
    const double height = target.logical_size.height * scale;
    const double line = keyline_width(scale);
    // Upright when at least as tall as wide; the pair sits across the
    // middle of the short axis, the shade line first.
    const bool upright = height >= width;
    const double across = upright ? width : height;
    const double length = upright ? height : width;
    const double first = std::floor((across - line * 2.0) * 0.5);
    const Color shade = colour_at(parameters, 0U);
    const Color light = colour_at(parameters, 1U);
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            const double along_pixel = upright ? static_cast<double>(row) : static_cast<double>(column);
            const double across_pixel = upright ? static_cast<double>(column) : static_cast<double>(row);
            const double weight = fade_weight((along_pixel + 0.5) / length) *
                                  interval_overlap(along_pixel, 0.0, length);
            Unit pixel{};
            blend_over(pixel, premultiply(shade, weight * interval_overlap(across_pixel, first, first + line)));
            blend_over(pixel,
                       premultiply(light, weight * interval_overlap(across_pixel, first + line, first + line * 2.0)));
            store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
        }
    }
}

// -- SeamPrimitive -------------------------------------------------------------------

const PrimitiveDescriptor& SeamPrimitive::descriptor() const noexcept {
    return seam_descriptor;
}

void SeamPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void SeamPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                         std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    const double width = target.logical_size.width * scale;
    const double height = target.logical_size.height * scale;
    const double line = keyline_width(scale);
    const bool upright = height >= width;
    const double across = upright ? width : height;
    const double length = upright ? height : width;
    const double first = std::floor((across - line) * 0.5);
    const Color colour = colour_at(parameters, 0U);
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            const double along_pixel = upright ? static_cast<double>(row) : static_cast<double>(column);
            const double across_pixel = upright ? static_cast<double>(column) : static_cast<double>(row);
            const double coverage = interval_overlap(across_pixel, first, first + line) *
                                    interval_overlap(along_pixel, 0.0, length);
            Unit pixel{};
            blend_over(pixel, premultiply(colour, coverage));
            store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
        }
    }
}

// -- GripPrimitive -------------------------------------------------------------------

const PrimitiveDescriptor& GripPrimitive::descriptor() const noexcept {
    return grip_descriptor;
}

void GripPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void GripPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                         std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    const double logical_width = target.logical_size.width;
    const double logical_height = target.logical_size.height;
    // Dots at logical 1 + 4n, two units square, whole device pixels: each
    // dot starts at the device pixel nearest its logical corner.
    const double dot = std::max(1.0, std::floor(2.0 * scale + 0.5));
    const Color colour = colour_at(parameters, 0U);
    const Unit ink = premultiply(colour, 1.0);
    const Unit clear{};
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        bool row_ink = false;
        for (double y = 1.0; y + 2.0 <= logical_height; y += 4.0) {
            const double top = std::floor(y * scale + 0.5);
            const double r = static_cast<double>(row);
            if (r >= top && r < top + dot) row_ink = true;
        }
        // A row between dots is clear throughout.
        if (!row_ink) {
            for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
                store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, clear);
            }
            continue;
        }
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            bool inked = false;
            for (double x = 1.0; x + 2.0 <= logical_width; x += 4.0) {
                const double left = std::floor(x * scale + 0.5);
                const double c = static_cast<double>(column);
                if (c >= left && c < left + dot) inked = true;
            }
            store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel,
                  inked ? ink : clear);
        }
    }
}

} // namespace gui_forms::engine_primitives
