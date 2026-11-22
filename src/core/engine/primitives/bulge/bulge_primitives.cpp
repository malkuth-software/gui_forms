#include "bulge_primitives.hpp"

#include "../kernel/pixel_kernel.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/types/linear_light/linear_light.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// -- shared geometry -----------------------------------------------------------

// One field's ellipse, as fractions of the part's width and height measured
// from its top-left corner (reference §2).
struct FieldShape final {
    double centre_x{};
    double centre_y{};
    double radius_x{};
    double radius_y{};
};

constexpr FieldShape bulge_light{0.42, 0.38, 0.48, 0.46};
constexpr FieldShape bulge_shade{0.60, 0.64, 0.50, 0.48};
constexpr FieldShape crown_shape{0.50, 0.50, 0.50, 0.50};
// The dent swaps where the slopes sit; each keeps its own radii.
constexpr FieldShape dent_light{0.60, 0.64, 0.48, 0.46};
constexpr FieldShape dent_shade{0.40, 0.36, 0.50, 0.48};

// A field's ellipse in the part's logical units.
struct FieldBox final {
    double centre_x{};
    double centre_y{};
    double radius_x{};
    double radius_y{};
};

FieldBox field_box(FieldShape shape, Size size) noexcept {
    const FieldBox box{shape.centre_x * size.width, shape.centre_y * size.height, shape.radius_x * size.width,
                       shape.radius_y * size.height};
    return box;
}

// 1 − t^q inside the ellipse, where t is the normalized distance from its
// centre, and 0 outside it.
double field_value(const FieldBox& box, double x, double y, double exponent) noexcept {
    const double dx = (x - box.centre_x) / box.radius_x;
    const double dy = (y - box.centre_y) / box.radius_y;
    const double t = std::sqrt(dx * dx + dy * dy);
    if (t >= 1.0) return 0.0;
    const double falloff = engine_power(t, exponent);
    const double value = 1.0 - falloff;
    return value;
}

// The three fields of one variant.
struct BulgeFields final {
    FieldBox light{};
    FieldBox shade{};
    FieldBox crown{};
};

BulgeFields bulge_fields(Size size, bool dent) noexcept {
    const FieldShape light = dent ? dent_light : bulge_light;
    const FieldShape shade = dent ? dent_shade : bulge_shade;
    const BulgeFields fields{field_box(light, size), field_box(shade, size), field_box(crown_shape, size)};
    return fields;
}

// A value in [0, 1] as a byte: floor(v · 255 + 0.5), the engine's one
// quantization rule.
std::uint8_t quantize_unit(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 1.0);
    const double scaled = std::floor(clamped * 255.0 + 0.5);
    const std::uint8_t byte = static_cast<std::uint8_t>(scaled);
    return byte;
}

// A colour as premultiplied bytes.
struct PremultipliedPixel final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{};
};

PremultipliedPixel premultiplied(Color colour) noexcept {
    const double alpha = static_cast<double>(colour.alpha) / 255.0;
    const PremultipliedPixel pixel{quantize_unit(static_cast<double>(colour.red) / 255.0 * alpha),
                                   quantize_unit(static_cast<double>(colour.green) / 255.0 * alpha),
                                   quantize_unit(static_cast<double>(colour.blue) / 255.0 * alpha), colour.alpha};
    return pixel;
}

void write_pixel(const PartRasterView& destination, std::size_t offset, PremultipliedPixel pixel) noexcept {
    destination.pixels[offset] = pixel.blue;
    destination.pixels[offset + 1U] = pixel.green;
    destination.pixels[offset + 2U] = pixel.red;
    destination.pixels[offset + 3U] = pixel.alpha;
}

// Every pixel of every row transparent; padding is left alone.
void clear_destination(const PartRasterView& destination) noexcept {
    const std::size_t row_pixel_bytes = static_cast<std::size_t>(destination.extent.width) * part_raster_bytes_per_pixel;
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        std::fill_n(destination.pixels.begin() + static_cast<std::ptrdiff_t>(start), row_pixel_bytes,
                    std::uint8_t{0});
    }
}

const PartRole painted_roles[] = {PartRole::face};
// The dithered form also paints an actuator: a ribbon split's arrow half,
// which hovers as the soft dither as its face does.
const PartRole dithered_roles[] = {PartRole::face, PartRole::actuator};
const AnatomyState read_states[] = {AnatomyState::pressed, AnatomyState::active_tool};

// Whether the bulge is drawn in gold (reference §2, "Active tool"): the
// part is the active tool, or a rule sets the `gold` parameter, which the
// built-in rules do for a checked ribbon command and a selected gallery
// item (the selected look; ledger R12). `gold` is a number so that a rule
// sets it with an ordinary value; half or more is gold.
bool gold_variant(const PrimitiveTarget& target, double gold) noexcept {
    const bool result = target.states.contains(AnatomyState::active_tool) || gold >= 0.5;
    return result;
}

// -- the smooth form -----------------------------------------------------------

enum SmoothParameter : std::size_t {
    smooth_amplitude,
    smooth_exponent,
    smooth_light_colour,
    smooth_light_peak,
    smooth_shade_colour,
    smooth_shade_peak,
    smooth_crown_colour,
    smooth_crown_peak,
    smooth_gold_light_colour,
    smooth_gold_light_peak,
    smooth_gold_shade_colour,
    smooth_gold_shade_peak,
    smooth_gold_crown_colour,
    smooth_gold_crown_peak,
    smooth_gold,
    smooth_parameter_count,
};

constexpr Color white{255, 255, 255, 255};

const PrimitiveParameterDeclaration smooth_parameters[] = {
    {"amplitude", PrimitiveParameterKind::number, 0.0, 1.0, 1.0, {}, true},
    {"exponent", PrimitiveParameterKind::number, 1.0, 8.0, 2.2, {}},
    {"light_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, white},
    {"light_peak", PrimitiveParameterKind::number, 0.0, 1.0, 0.55, {}},
    {"shade_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {55, 90, 150, 255}},
    {"shade_peak", PrimitiveParameterKind::number, 0.0, 1.0, 0.22, {}},
    {"crown_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, white},
    {"crown_peak", PrimitiveParameterKind::number, 0.0, 1.0, 0.12, {}},
    {"gold_light_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 250, 225, 255}},
    {"gold_light_peak", PrimitiveParameterKind::number, 0.0, 1.0, 0.70, {}},
    {"gold_shade_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {190, 130, 25, 255}},
    {"gold_shade_peak", PrimitiveParameterKind::number, 0.0, 1.0, 0.30, {}},
    {"gold_crown_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 214, 110, 255}},
    {"gold_crown_peak", PrimitiveParameterKind::number, 0.0, 1.0, 0.45, {}},
    {"gold", PrimitiveParameterKind::number, 0.0, 1.0, 0.0, {}},
};
static_assert(sizeof(smooth_parameters) / sizeof(smooth_parameters[0]) == smooth_parameter_count);

// Three fields, each about 80 operations (a square root and a power), and
// three source-over composites. The smooth form derives nothing.
const PrimitiveDescriptor smooth_descriptor{
    "bulge", EngineVersion{0, 1}, painted_roles, read_states, smooth_parameters, PrimitiveCost{320U, 64U, 0U},
    Insets{}, 0U,
};

// One layer of the smooth form: a straight colour and the coverage its
// field reaches at its peak.
struct SmoothLayer final {
    double red{};
    double green{};
    double blue{};
    double strength{};
};

SmoothLayer smooth_layer(const PrimitiveParameterBlock& parameters, std::size_t colour, std::size_t peak,
                         double amplitude) noexcept {
    const Color value = parameters.values[colour].colour;
    const double colour_alpha = static_cast<double>(value.alpha) / 255.0;
    const double strength = amplitude * parameters.values[peak].number * colour_alpha;
    const SmoothLayer layer{static_cast<double>(value.red) / 255.0, static_cast<double>(value.green) / 255.0,
                            static_cast<double>(value.blue) / 255.0, strength};
    return layer;
}

// A premultiplied colour accumulated in doubles.
struct PremultipliedSample final {
    double red{};
    double green{};
    double blue{};
    double alpha{};
};

// `layer` over `below`, by premultiplied source-over, at `coverage`.
PremultipliedSample composite_over(PremultipliedSample below, const SmoothLayer& layer, double coverage) noexcept {
    const double remaining = 1.0 - coverage;
    const PremultipliedSample result{layer.red * coverage + below.red * remaining,
                                     layer.green * coverage + below.green * remaining,
                                     layer.blue * coverage + below.blue * remaining,
                                     coverage + below.alpha * remaining};
    return result;
}

// -- the dithered form ---------------------------------------------------------

enum DitheredParameter : std::size_t {
    dithered_amplitude,
    dithered_exponent,
    dithered_lit_1,
    dithered_lit_2,
    dithered_lit_3,
    dithered_shaded_1,
    dithered_shaded_2,
    dithered_shaded_3,
    dithered_lit_gain,
    dithered_shade_gain,
    dithered_crown_gain,
    dithered_gold_lit_1,
    dithered_gold_lit_2,
    dithered_gold_lit_3,
    dithered_gold_body_1,
    dithered_gold_body_2,
    dithered_gold_body_3,
    dithered_gold_shaded_1,
    dithered_gold_shaded_2,
    dithered_gold_shaded_3,
    dithered_gold,
    dithered_plate_top,
    dithered_plate_bottom,
    dithered_plate_border,
    dithered_parameter_count,
};
// The registry refuses a descriptor with more parameters than its block
// holds, and the built-in registry aborts on a refusal: every program would
// stop at startup.
static_assert(dithered_parameter_count <= maximum_primitive_parameters,
              "the dithered bulge's parameters fit a parameter block");

const PrimitiveParameterDeclaration dithered_parameters[] = {
    {"amplitude", PrimitiveParameterKind::number, 0.0, 1.0, 1.0, {}, true},
    {"exponent", PrimitiveParameterKind::number, 1.0, 8.0, 2.2, {}},
    {"lit_1", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {236, 244, 254, 255}},
    {"lit_2", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {246, 250, 255, 255}},
    {"lit_3", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, white},
    {"shaded_1", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {176, 198, 232, 255}},
    {"shaded_2", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {132, 162, 212, 255}},
    {"shaded_3", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {88, 122, 186, 255}},
    {"lit_gain", PrimitiveParameterKind::number, 0.0, 4.0, 1.0, {}},
    {"shade_gain", PrimitiveParameterKind::number, 0.0, 4.0, 0.85, {}},
    {"crown_gain", PrimitiveParameterKind::number, 0.0, 4.0, 0.25, {}},
    {"gold_lit_1", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 234, 176, 255}},
    {"gold_lit_2", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 244, 212, 255}},
    {"gold_lit_3", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 252, 240, 255}},
    {"gold_body_1", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {250, 205, 104, 255}},
    {"gold_body_2", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 220, 140, 255}},
    {"gold_body_3", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 232, 178, 255}},
    {"gold_shaded_1", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {232, 172, 62, 255}},
    {"gold_shaded_2", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {206, 142, 40, 255}},
    {"gold_shaded_3", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {170, 112, 24, 255}},
    {"gold", PrimitiveParameterKind::number, 0.0, 1.0, 0.0, {}},
    // The gold form's plate (reference §1, "active tool"): the gold plate
    // primitive's face and border. Its pressed face and radius are the gold
    // plate primitive's defaults, constants below: the parameter block
    // holds at most maximum_primitive_parameters (24), which these three
    // reach.
    {"gold_plate_top", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 237, 186, 255}},
    {"gold_plate_bottom", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 207, 102, 255}},
    {"gold_plate_border", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {196, 147, 50, 255}},
};
static_assert(sizeof(dithered_parameters) / sizeof(dithered_parameters[0]) == dithered_parameter_count);

// Per device pixel: the clear and one write (about 10 operations), and a
// quarter of one dot's work (its three fields, normalization and
// quantization, about 270): the lattice has one k x k dot in every
// 2k x 2k cell (dither_lattice), so at most one dot per four device pixels
// at any scale, and fewer above scale 1. The declaration was 280, one
// whole dot per pixel, which put a 142-unit ribbon tab's rise over the
// animated frame budget at scale 2 (14 904 pixels x 280 > 4 million), so
// the plan dropped it to the built-in recipe and the menu-item plate
// covered the selected tab (Plan Paint, owner 2026-11-17). The gold form
// adds its plate: one more write per pixel (about 10), and the shape's
// coverage only in the four corner squares, so 90. The preparation is
// bounded per device pixel as before; it runs once per geometry.
const PrimitiveDescriptor dithered_descriptor{
    "dithered_bulge", EngineVersion{0, 1}, dithered_roles, read_states, dithered_parameters,
    PrimitiveCost{90U, 64U, 250U}, Insets{}, 3U,
};

// What the dithered form's preparation derives (reference §3, "Rescaled
// quantization"): the largest tone of each side and the largest crown, over
// every dot of the part's lattice, at amplitude 1.
enum DitheredPrepared : std::size_t {
    prepared_positive_extreme,
    prepared_negative_extreme,
    prepared_crown_extreme,
};

// The ordered 4×4 threshold (reference §3), indexed by row then column.
constexpr std::array<double, 16> bayer{0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                       3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0};

// Three steps per side: s = clamp(v, 0, 0.999) · 3, step = floor(s), and one
// step more when the remainder passes the threshold. 0 means no dot.
std::size_t quantize_tone(double value, double threshold) noexcept {
    const double scaled = std::clamp(value, 0.0, 0.999) * 3.0;
    const double floor_step = std::floor(scaled);
    std::size_t step = static_cast<std::size_t>(floor_step);
    if (scaled - floor_step > threshold) step = step + 1U;
    return step;
}

// One side's step under the rescaled rule: quantize(a · magnitude / extreme),
// where `magnitude` is the dot's full-amplitude value on that side and
// `extreme` the largest such value over the part. A side whose extreme is 0
// draws no dots.
std::size_t rescaled_step(double magnitude, double extreme, double amplitude, double threshold) noexcept {
    if (extreme <= 0.0) return 0U;
    const double raised = amplitude * magnitude;
    const double normalized = raised / extreme;
    const std::size_t step = quantize_tone(normalized, threshold);
    return step;
}

// The dithered lattice at display scale s (reference §3, "Fractional display
// scales"). Every dot is k × k device px, k = max(1, floor(s + 0.5)), on a
// period of 2k device px from the part's device origin: dot n covers device
// [(2n + 1)·k, (2n + 2)·k) on each axis, and its fields are sampled at
// logical (2n + 1)·k / s. A dot lies wholly inside the part's device box,
// (2n + 2)·k ≤ w·s, so there are floor(w·s / 2k) columns and as many rows
// for the height.
//
// The counts are computed as floor((w / 2) · (s / k)) and the sample step
// as k / s. At an integer scale s = k both ratios are exactly 1, so the
// lattice is the studies' rule bit for bit: floor(w / 2) dots at odd logical
// positions 2n + 1, each s × s device px at device (2n + 1)·s.
struct DitherLattice final {
    std::uint32_t dot_side{};
    double logical_step{};
    std::size_t columns{};
    std::size_t rows{};
};

DitherLattice dither_lattice(const PrimitiveTarget& target) noexcept {
    const double scale = target.scale.value;
    const double rounded_scale = std::floor(scale + 0.5);
    const double side = std::max(1.0, rounded_scale);
    const double logical_step = side / scale;
    const double dots_per_two_logical = scale / side;
    const double columns = std::floor(target.logical_size.width / 2.0 * dots_per_two_logical);
    const double rows = std::floor(target.logical_size.height / 2.0 * dots_per_two_logical);
    const DitherLattice lattice{static_cast<std::uint32_t>(side), logical_step, static_cast<std::size_t>(columns),
                                static_cast<std::size_t>(rows)};
    return lattice;
}

// The logical coordinate at which lattice index n samples the fields.
double lattice_sample(const DitherLattice& lattice, std::size_t index) noexcept {
    const double odd = 2.0 * static_cast<double>(index) + 1.0;
    const double logical = odd * lattice.logical_step;
    return logical;
}

// The device pixels lattice index n covers on one axis, clipped to the
// destination's `limit`. Empty when the dot starts at or past the limit.
struct DotSpan final {
    std::uint32_t first{};
    std::uint32_t end{};
};

DotSpan dot_span(const DitherLattice& lattice, std::size_t index, std::uint32_t limit) noexcept {
    // index < columns or rows, both at most w·s / 2 ≤ 16384 · 8 / 2, so the
    // device position fits 32 bits.
    const std::uint32_t odd = 2U * static_cast<std::uint32_t>(index) + 1U;
    const std::uint32_t origin = odd * lattice.dot_side;
    const std::uint32_t first = std::min(origin, limit);
    const std::uint32_t end = std::min(limit, first + lattice.dot_side);
    const DotSpan span{first, end};
    return span;
}

// The fixed inputs of the dithered tone, read once per call.
struct ToneGains final {
    double exponent{};
    double lit{};
    double shade{};
    double crown{};
};

ToneGains tone_gains(const PrimitiveParameterBlock& parameters) noexcept {
    const ToneGains gains{parameters.values[dithered_exponent].number, parameters.values[dithered_lit_gain].number,
                          parameters.values[dithered_shade_gain].number,
                          parameters.values[dithered_crown_gain].number};
    return gains;
}

// One dot's values at amplitude 1: the signed tone
// light · lit_gain + crown · crown_gain − shade · shade_gain, and the crown
// term alone, which the gold form's body reads. The preparation and the
// fill both call this, so the dot that sets an extreme normalizes to
// exactly 1 at full amplitude.
struct DotTone final {
    double relief{};
    double crown{};
};

DotTone dot_tone(const BulgeFields& fields, const ToneGains& gains, double x, double y) noexcept {
    const double light_field = field_value(fields.light, x, y, gains.exponent);
    const double shade_field = field_value(fields.shade, x, y, gains.exponent);
    const double crown_field = field_value(fields.crown, x, y, gains.exponent);
    const double crown = crown_field * gains.crown;
    const double relief = light_field * gains.lit + crown - shade_field * gains.shade;
    const DotTone tone{relief, crown};
    return tone;
}

// The full-amplitude extremes the rescaled rule divides by.
struct ToneExtremes final {
    double positive{};
    double negative{};
    double crown{};
};

// The dithered form's colours as premultiplied pixels, in step order.
struct DitherPalette final {
    std::array<PremultipliedPixel, 3> lit{};
    std::array<PremultipliedPixel, 3> shaded{};
    std::array<PremultipliedPixel, 3> gold_lit{};
    std::array<PremultipliedPixel, 3> gold_body{};
    std::array<PremultipliedPixel, 3> gold_shaded{};
};

std::array<PremultipliedPixel, 3> palette_steps(const PrimitiveParameterBlock& parameters,
                                                std::size_t first) noexcept {
    const std::array<PremultipliedPixel, 3> steps{premultiplied(parameters.values[first].colour),
                                                  premultiplied(parameters.values[first + 1U].colour),
                                                  premultiplied(parameters.values[first + 2U].colour)};
    return steps;
}

// The dot a lattice point shows, if any.
struct DotChoice final {
    bool shown{};
    PremultipliedPixel pixel{};
};

// Reference §3: a non-negative tone picks a lit step, a negative one a
// shaded step, each rescaled by its side's extreme. The sign of the tone
// a · relief is the sign of relief, since the caller has already drawn
// nothing at a = 0.
DotChoice plain_dot(const DitherPalette& palette, const ToneExtremes& extremes, double amplitude, DotTone tone,
                    double threshold) noexcept {
    DotChoice choice{};
    if (tone.relief >= 0.0) {
        const std::size_t step = rescaled_step(tone.relief, extremes.positive, amplitude, threshold);
        if (step > 0U) choice = DotChoice{true, palette.lit[step - 1U]};
        return choice;
    }
    const std::size_t step = rescaled_step(-tone.relief, extremes.negative, amplitude, threshold);
    if (step > 0U) choice = DotChoice{true, palette.shaded[step - 1U]};
    return choice;
}

// Gold (reference §3): a lit step uses gold lit, else the crown quantized
// alone picks the gold body (no dot when it rounds to zero); a shaded step
// uses gold shaded, else the first gold body colour. The reference does not
// say how the crown is quantized under the rescaled rule; it is rescaled by
// its own full-amplitude extreme like each tone side, so that all three body
// steps are reachable whatever the crown gain (the intent of G1-3).
DotChoice gold_dot(const DitherPalette& palette, const ToneExtremes& extremes, double amplitude, DotTone tone,
                   double threshold) noexcept {
    DotChoice choice{};
    if (tone.relief >= 0.0) {
        const std::size_t step = rescaled_step(tone.relief, extremes.positive, amplitude, threshold);
        if (step > 0U) {
            choice = DotChoice{true, palette.gold_lit[step - 1U]};
            return choice;
        }
        const std::size_t body = rescaled_step(tone.crown, extremes.crown, amplitude, threshold);
        if (body > 0U) choice = DotChoice{true, palette.gold_body[body - 1U]};
        return choice;
    }
    const std::size_t step = rescaled_step(-tone.relief, extremes.negative, amplitude, threshold);
    if (step > 0U) {
        choice = DotChoice{true, palette.gold_shaded[step - 1U]};
        return choice;
    }
    choice = DotChoice{true, palette.gold_body[0]};
    return choice;
}

// -- the gold form's plate -----------------------------------------------------

// The selected look's body (owner, 2026-11-22; reference §1, "active tool":
// gold #FFEDBA to #FFCF66, border #C49332, radius 2). The dots alone read as
// noise on the plane, so the gold form first draws the gold plate, a
// vertical gradient inside a one-keyline border, and its dots shade the
// plate. Pressed, the plate takes the gold plate primitive's pressed face,
// as the dots take the dent.
//
// All in device pixels. The part's device box has integer sides and the
// keyline is a whole number of pixels, so every pixel outside the four
// corner squares is wholly border or wholly face; only the corner squares,
// `corner` pixels on a side, take the shapes' coverage.
// The gold plate primitive's pressed face and corner radius (logical
// units), as relief_primitives.cpp declares their defaults.
constexpr Color gold_plate_pressed_top{246, 193, 91, 255};
constexpr Color gold_plate_pressed_bottom{255, 226, 151, 255};
constexpr double gold_plate_corner_radius = 2.0;

struct GoldPlate final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t line{};
    std::uint32_t corner{};
    PartShape outer{};
    PartShape inner{};
    Rect outer_box{};
    Rect inner_box{};
    Color border{};
    Color face_top{};
    Color face_bottom{};
};

GoldPlate gold_plate(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target, bool pressed,
                     DeviceExtent extent) noexcept {
    GoldPlate plate{};
    plate.width = extent.width;
    plate.height = extent.height;
    const std::uint32_t half = std::min(extent.width, extent.height) / 2U;
    // keyline_width is 1 or 2.
    plate.line = std::min(static_cast<std::uint32_t>(keyline_width(target.scale.value)), half);
    const double line = static_cast<double>(plate.line);
    const double radius = gold_plate_corner_radius * target.scale.value;
    plate.outer.kind = PartShapeKind::rounded_rectangle;
    plate.outer.corner_radius = radius;
    plate.inner.kind = PartShapeKind::rounded_rectangle;
    plate.inner.corner_radius = std::max(0.0, radius - line);
    const double width = static_cast<double>(extent.width);
    const double height = static_cast<double>(extent.height);
    plate.outer_box = Rect{0.0, 0.0, width, height};
    plate.inner_box = Rect{line, line, std::max(0.0, width - line * 2.0), std::max(0.0, height - line * 2.0)};
    // The radius is at most 64 · 8 device pixels, so its ceiling fits.
    const std::uint32_t reach = static_cast<std::uint32_t>(std::ceil(std::max(0.0, radius)));
    plate.corner = std::min(reach, half);
    plate.border = parameters.values[dithered_plate_border].colour;
    plate.face_top = pressed ? gold_plate_pressed_top : parameters.values[dithered_plate_top].colour;
    plate.face_bottom = pressed ? gold_plate_pressed_bottom : parameters.values[dithered_plate_bottom].colour;
    return plate;
}

// The face's colour on device row `row`: the vertical gradient from top to
// bottom of the face, interpolated in linear light as every gradient is.
Color plate_face_colour(const GoldPlate& plate, std::uint32_t row) noexcept {
    const double height = plate.inner_box.height;
    const double t = height > 0.0 ? (static_cast<double>(row) + 0.5 - plate.inner_box.y) / height : 0.0;
    const Color colour = interpolate_linear_light(plate.face_top, plate.face_bottom, linear_light_weight(t));
    return colour;
}

// One pixel of the plate by its shapes' coverage: the border's shape, and
// the face over it.
Unit plate_pixel(const GoldPlate& plate, Color face, std::uint32_t x, std::uint32_t y) noexcept {
    const std::int64_t column = static_cast<std::int64_t>(x);
    const std::int64_t row = static_cast<std::int64_t>(y);
    Unit pixel = premultiply(plate.border, shape_coverage(plate.outer, plate.outer_box, 1.0, column, row));
    blend_over(pixel, premultiply(face, shape_coverage(plate.inner, plate.inner_box, 1.0, column, row)));
    return pixel;
}

// Pixels [first, end) of device row `row`, all `unit`.
void store_span(const PartRasterView& destination, std::uint32_t row, std::uint32_t first, std::uint32_t end,
                const Unit& unit) noexcept {
    const std::size_t row_start = static_cast<std::size_t>(row) * destination.row_bytes;
    for (std::uint32_t column = first; column < end; ++column) {
        store(destination, row_start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, unit);
    }
}

// Pixels [first, end) of device row `row` by the shapes' coverage.
void store_covered_span(const PartRasterView& destination, const GoldPlate& plate, Color face, std::uint32_t row,
                        std::uint32_t first, std::uint32_t end) noexcept {
    const std::size_t row_start = static_cast<std::size_t>(row) * destination.row_bytes;
    for (std::uint32_t column = first; column < end; ++column) {
        const Unit pixel = plate_pixel(plate, face, column, row);
        store(destination, row_start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
    }
}

// Writes every pixel of the destination, row by row. A row in the corner
// squares' height takes the coverage at both ends and is border or face
// between them; any other row is border for a keyline at each end and face
// between.
void fill_gold_plate(const GoldPlate& plate, const PartRasterView& destination) noexcept {
    const Unit border = premultiply(plate.border, 1.0);
    const std::uint32_t corner_end = plate.width - plate.corner;
    const std::uint32_t line_end = plate.width - plate.line;
    for (std::uint32_t row = 0U; row < plate.height; ++row) {
        const Color face_colour = plate_face_colour(plate, row);
        const Unit face = premultiply(face_colour, 1.0);
        const bool corner_row = row < plate.corner || row >= plate.height - plate.corner;
        const bool border_row = row < plate.line || row >= plate.height - plate.line;
        if (corner_row) {
            store_covered_span(destination, plate, face_colour, row, 0U, plate.corner);
            store_span(destination, row, plate.corner, corner_end, border_row ? border : face);
            store_covered_span(destination, plate, face_colour, row, corner_end, plate.width);
            continue;
        }
        store_span(destination, row, 0U, plate.line, border);
        store_span(destination, row, plate.line, line_end, face);
        store_span(destination, row, line_end, plate.width, border);
    }
}

// A dot's premultiplied colour as a unit, at `coverage`.
Unit dot_unit(PremultipliedPixel pixel, double coverage) noexcept {
    const Unit unit{static_cast<double>(pixel.red) / 255.0 * coverage,
                    static_cast<double>(pixel.green) / 255.0 * coverage,
                    static_cast<double>(pixel.blue) / 255.0 * coverage,
                    static_cast<double>(pixel.alpha) / 255.0 * coverage};
    return unit;
}

// A gold dot that reaches into a corner square: each of its pixels is the
// plate with the dot over it at the face's coverage, so that no dot shows
// outside the rounded face.
void store_cornered_dot(const PartRasterView& destination, const GoldPlate& plate, DotSpan rows, DotSpan columns,
                        PremultipliedPixel dot) noexcept {
    for (std::uint32_t row = rows.first; row < rows.end; ++row) {
        const Color face = plate_face_colour(plate, row);
        const std::size_t row_start = static_cast<std::size_t>(row) * destination.row_bytes;
        for (std::uint32_t column = columns.first; column < columns.end; ++column) {
            Unit pixel = plate_pixel(plate, face, column, row);
            const double coverage = shape_coverage(plate.inner, plate.inner_box, 1.0, static_cast<std::int64_t>(column),
                                                   static_cast<std::int64_t>(row));
            blend_over(pixel, dot_unit(dot, coverage));
            store(destination, row_start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
        }
    }
}

// Whether a dot's pixels reach into one of the plate's corner squares.
bool dot_in_corner(const GoldPlate& plate, DotSpan rows, DotSpan columns) noexcept {
    const bool across = columns.first < plate.corner || columns.end > plate.width - plate.corner;
    const bool down = rows.first < plate.corner || rows.end > plate.height - plate.corner;
    const bool inside = across && down;
    return inside;
}

} // namespace

// -- BulgePrimitive ------------------------------------------------------------

const PrimitiveDescriptor& BulgePrimitive::descriptor() const noexcept {
    return smooth_descriptor;
}

// The amplitude is the first parameter of both bulges.
bool BulgePrimitive::blank(const PrimitiveParameterBlock& parameters) const noexcept {
    const bool flat = parameters.count > 0U && parameters.values[0].number == 0.0;
    return flat;
}

void BulgePrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {
    // The smooth form has no value worth keeping between frames.
}

void BulgePrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                          std::span<const double>, const PartRasterView& destination) const noexcept {
    // Choose the variant and read every parameter before the pixel loop.
    const bool dent = target.states.contains(AnatomyState::pressed);
    const bool gold = gold_variant(target, parameters.values[smooth_gold].number);
    const double amplitude = parameters.values[smooth_amplitude].number;
    const double exponent = parameters.values[smooth_exponent].number;
    const BulgeFields fields = bulge_fields(target.logical_size, dent);
    const std::size_t light_colour = gold ? smooth_gold_light_colour : smooth_light_colour;
    const std::size_t light_peak = gold ? smooth_gold_light_peak : smooth_light_peak;
    const std::size_t shade_colour = gold ? smooth_gold_shade_colour : smooth_shade_colour;
    const std::size_t shade_peak = gold ? smooth_gold_shade_peak : smooth_shade_peak;
    const std::size_t crown_colour = gold ? smooth_gold_crown_colour : smooth_crown_colour;
    const std::size_t crown_peak = gold ? smooth_gold_crown_peak : smooth_crown_peak;
    const SmoothLayer light = smooth_layer(parameters, light_colour, light_peak, amplitude);
    const SmoothLayer shade = smooth_layer(parameters, shade_colour, shade_peak, amplitude);
    const SmoothLayer crown = smooth_layer(parameters, crown_colour, crown_peak, amplitude);
    const double scale = target.scale.value;

    // Each device pixel samples the fields at its centre, in logical units.
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const double y = (static_cast<double>(row) + 0.5) / scale;
        const std::size_t row_start = static_cast<std::size_t>(row) * destination.row_bytes;
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            const double x = (static_cast<double>(column) + 0.5) / scale;
            const double light_field = field_value(fields.light, x, y, exponent);
            const double shade_field = field_value(fields.shade, x, y, exponent);
            const double crown_field = field_value(fields.crown, x, y, exponent);
            PremultipliedSample sample{};
            sample = composite_over(sample, light, light.strength * light_field);
            sample = composite_over(sample, shade, shade.strength * shade_field);
            sample = composite_over(sample, crown, crown.strength * crown_field);
            const PremultipliedPixel pixel{quantize_unit(sample.red), quantize_unit(sample.green),
                                           quantize_unit(sample.blue), quantize_unit(sample.alpha)};
            const std::size_t offset = row_start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel;
            write_pixel(destination, offset, pixel);
        }
    }
}

// -- DitheredBulgePrimitive ----------------------------------------------------

const PrimitiveDescriptor& DitheredBulgePrimitive::descriptor() const noexcept {
    return dithered_descriptor;
}

bool DitheredBulgePrimitive::blank(const PrimitiveParameterBlock& parameters) const noexcept {
    const bool flat = parameters.count > 0U && parameters.values[0].number == 0.0;
    return flat;
}

// The preparation visits every dot of the lattice once, at amplitude 1, and
// keeps the largest tone on each side and the largest crown. It reads the
// part's size and scale (the lattice), the pressed state (bulge or dent),
// the exponent and the gains; it never reads the amplitude, so a rise
// reuses one preparation from 0 to 1.
void DitheredBulgePrimitive::derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                                    std::span<double> values) const noexcept {
    const bool dent = target.states.contains(AnatomyState::pressed);
    const ToneGains gains = tone_gains(parameters);
    const BulgeFields fields = bulge_fields(target.logical_size, dent);
    const DitherLattice lattice = dither_lattice(target);
    ToneExtremes extremes{};
    for (std::size_t j = 0U; j < lattice.rows; ++j) {
        const double y = lattice_sample(lattice, j);
        for (std::size_t i = 0U; i < lattice.columns; ++i) {
            const double x = lattice_sample(lattice, i);
            const DotTone tone = dot_tone(fields, gains, x, y);
            extremes.positive = std::max(extremes.positive, tone.relief);
            extremes.negative = std::max(extremes.negative, -tone.relief);
            extremes.crown = std::max(extremes.crown, tone.crown);
        }
    }
    values[prepared_positive_extreme] = extremes.positive;
    values[prepared_negative_extreme] = extremes.negative;
    values[prepared_crown_extreme] = extremes.crown;
}

void DitheredBulgePrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                                  std::span<const double> prepared, const PartRasterView& destination) const noexcept {
    // Amplitude 0 is the flat plane in every variant (reference §2: a ribbon
    // button is flat at rest). Without this exit the gold form would still
    // show its plate and its body colour.
    const double amplitude = parameters.values[dithered_amplitude].number;
    if (amplitude == 0.0) {
        clear_destination(destination);
        return;
    }

    // Choose the variant and read every parameter before the lattice loop.
    const bool dent = target.states.contains(AnatomyState::pressed);
    const bool gold = gold_variant(target, parameters.values[dithered_gold].number);
    // The gold form's plate writes every pixel; the plain form's dots lie on
    // the clear plane. The gold dots stay inside the plate's face: the first
    // dot starts at k ≥ the keyline, so only the far ends need the limit.
    const GoldPlate plate = gold_plate(parameters, target, dent, destination.extent);
    std::uint32_t column_limit = destination.extent.width;
    std::uint32_t row_limit = destination.extent.height;
    if (gold) {
        fill_gold_plate(plate, destination);
        column_limit = plate.width - plate.line;
        row_limit = plate.height - plate.line;
    } else {
        clear_destination(destination);
    }
    const ToneGains gains = tone_gains(parameters);
    const BulgeFields fields = bulge_fields(target.logical_size, dent);
    const DitherPalette palette{palette_steps(parameters, dithered_lit_1), palette_steps(parameters, dithered_shaded_1),
                                palette_steps(parameters, dithered_gold_lit_1),
                                palette_steps(parameters, dithered_gold_body_1),
                                palette_steps(parameters, dithered_gold_shaded_1)};
    const ToneExtremes extremes{prepared[prepared_positive_extreme], prepared[prepared_negative_extreme],
                                prepared[prepared_crown_extreme]};
    const DitherLattice lattice = dither_lattice(target);

    for (std::size_t j = 0U; j < lattice.rows; ++j) {
        const DotSpan rows = dot_span(lattice, j, row_limit);
        // Rows of dots below the destination's last row draw nothing.
        if (rows.first >= rows.end) break;
        const double y = lattice_sample(lattice, j);
        for (std::size_t i = 0U; i < lattice.columns; ++i) {
            const DotSpan columns = dot_span(lattice, i, column_limit);
            if (columns.first >= columns.end) break;
            const double x = lattice_sample(lattice, i);
            const DotTone tone = dot_tone(fields, gains, x, y);
            // The threshold is indexed by the lattice index (x >> 1 and
            // y >> 1 of the studies' odd logical positions).
            const std::size_t threshold_index = (j & 3U) * 4U + (i & 3U);
            const double threshold = (bayer[threshold_index] + 0.5) / 16.0;
            const DotChoice choice = gold ? gold_dot(palette, extremes, amplitude, tone, threshold)
                                          : plain_dot(palette, extremes, amplitude, tone, threshold);
            if (!choice.shown) continue;
            if (gold && dot_in_corner(plate, rows, columns)) {
                store_cornered_dot(destination, plate, rows, columns, choice.pixel);
                continue;
            }
            for (std::uint32_t row = rows.first; row < rows.end; ++row) {
                const std::size_t row_start = static_cast<std::size_t>(row) * destination.row_bytes;
                for (std::uint32_t column = columns.first; column < columns.end; ++column) {
                    const std::size_t offset =
                        row_start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel;
                    write_pixel(destination, offset, choice.pixel);
                }
            }
        }
    }
}

} // namespace gui_forms::engine_primitives
