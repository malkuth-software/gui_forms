// E9.6, fills and shape (notes:planning/stages/T2-engine-core/CSS_COVERAGE.md,
// rows 1 to 4): the material primitive's repeating radial and conic
// gradients, per-corner radii, its `shape` parameter, and dashed and dotted
// borders. Each is checked by what it draws at chosen pixels, and by
// reference images: FNV-1a hashes of each raster's rows in RGBA order,
// checked on every CI platform. GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints
// the observed table instead of checking it.
//
// A material with none of these draws the bytes it drew before E9.6: the
// material tests' reference images are unchanged.

#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "support/rgba_order.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_fill_shape_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

const EnginePrimitive& material_primitive() {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find("material");
    require(primitive != nullptr, "the built-in registry has the material primitive");
    return *primitive;
}

// The button recipe drawn with `shape`, one of part_shape_names.
PrimitiveParameterBlock shaped(std::string_view shape) {
    const PrimitiveDescriptor& descriptor = material_primitive().descriptor();
    PrimitiveParameterBlock parameters = default_parameters(descriptor);
    require(set_choice_parameter(parameters, descriptor, "shape", shape) == ParameterAssignment::assigned,
            "a shape is a choice");
    return parameters;
}

// A raster the tests own: tightly packed rows of BGRA8 bytes.
struct TestRaster final {
    std::vector<std::uint8_t> bytes;
    DeviceExtent extent{};
    std::size_t row_bytes{};

    // Channel `index` of a pixel: 0 red, 1 green, 2 blue, 3 alpha.
    [[nodiscard]] std::uint8_t channel(std::uint32_t column, std::uint32_t row, std::size_t index) const {
        constexpr std::array<std::size_t, 4> byte_of{2U, 1U, 0U, 3U};
        const std::size_t offset =
            static_cast<std::size_t>(row) * row_bytes + static_cast<std::size_t>(column) * 4U + byte_of[index];
        return bytes[offset];
    }
};

std::uint64_t raster_hash(const TestRaster& raster) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : gui_forms::test_support::rgba_order(raster.bytes)) {
        hash = hash ^ byte;
        hash = hash * 1099511628211ULL;
    }
    return hash;
}

// Draws `material` as a part of `logical` size at `scale`, with an own
// corner radius when `own_radius` is non-negative, into a destination grown
// by the material's outsets of 8 on every side.
TestRaster draw(const SurfaceMaterial& material, Size logical, double scale,
                std::string_view shape = "rounded_rectangle", double own_radius = -1.0) {
    TestRaster raster{};
    const double width = std::ceil((logical.width + 16.0) * scale);
    const double height = std::ceil((logical.height + 16.0) * scale);
    raster.extent = DeviceExtent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    raster.row_bytes = static_cast<std::size_t>(raster.extent.width) * 4U;
    raster.bytes.assign(raster.row_bytes * raster.extent.height, 0xA5U);
    PrimitiveTarget target{};
    target.logical_size = logical;
    target.scale = DeviceScale{scale};
    target.material = &material;
    if (own_radius >= 0.0) target.corner_radius.set(own_radius);
    const EnginePrimitive& primitive = material_primitive();
    const PrimitiveParameterBlock parameters = shaped(shape);
    PrimitivePreparation preparation{};
    require(primitive.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "the part prepares");
    const PartRasterView view{std::span<std::uint8_t>(raster.bytes.data(), raster.bytes.size()), raster.extent,
                              raster.row_bytes};
    require(primitive.rasterize(parameters, target, preparation, view) == RasterOutcome::painted,
            "the material paints");
    return raster;
}

// Checks `raster`'s hash against `expected`, or prints it in print mode.
bool reference_matches(const TestRaster& raster, const char* name, std::uint64_t expected) {
    const std::uint64_t hash = raster_hash(raster);
    if (print_mode()) {
        std::cout << "    " << name << " 0x" << std::hex << hash << std::dec << "ULL\n";
        return true;
    }
    if (hash == expected) return true;
    std::cerr << name << " hash 0x" << std::hex << hash << std::dec << '\n';
    return false;
}

constexpr Color red{255, 0, 0, 255};
constexpr Color blue{0, 0, 255, 255};
constexpr Color ink{20, 30, 40, 255};
constexpr Color shade{0, 0, 0, 96};

SurfaceMaterial filled_with(MaterialFillLayer fill) {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(std::move(fill));
    return material;
}

const std::vector<GradientStop> red_to_blue{GradientStop{0.0, red}, GradientStop{1.0, blue}};

// The reference hashes, in the order the tests check them.
constexpr std::uint64_t expected_hashes[] = {
    0x1b0f72f013af4685ULL, 0xa01bf0370ea92165ULL, 0xfa172c454612b0d5ULL, 0x33592d40fdca4005ULL,
    0xa6b3b5e2870c76c0ULL, 0x2a4b7ac3d222df1fULL, 0x50838a8da95444b5ULL, 0xc9f335d9b9942970ULL,
    0x9d9e8d053bf20df9ULL, 0xa3df483b164b7900ULL, 0xd10bcec38a004b2fULL,
};
std::size_t next_reference = 0U;

bool next_matches(const TestRaster& raster, const char* name) {
    require(next_reference < std::size(expected_hashes), "a reference hash for each image");
    const bool matches = reference_matches(raster, name, expected_hashes[next_reference]);
    next_reference = next_reference + 1U;
    return matches;
}

// -- the shape parameter ------------------------------------------------------------

void shape_parameter() {
    const PrimitiveDescriptor& descriptor = material_primitive().descriptor();
    require(descriptor.parameters.size() == 2U && descriptor.parameters[1].name == "shape",
            "the material's second parameter is its shape");
    const std::span<const std::string_view> choices = descriptor.parameters[1].choices;
    require(choices.size() == part_shape_kind_count, "one choice per part shape");
    for (std::size_t index = 0U; index < choices.size(); ++index) {
        require(choices[index] == part_shape_names[index], "the choices are the shapes' names in enum order");
    }
    const PartShape plain = material_primitive().shape(default_parameters(descriptor));
    require(plain.kind == PartShapeKind::rounded_rectangle && plain.corner_radius == 0.0 && !plain.per_corner,
            "by default a material's hit region is its box");
    require(material_primitive().shape(shaped("pill")).kind == PartShapeKind::pill,
            "a pill's hit region is the pill");
    require(material_primitive().shape(shaped("circle")).kind == PartShapeKind::circle,
            "a circle's hit region is the circle");
}

// -- fills --------------------------------------------------------------------------

void repeating_radial() {
    // Radii of a quarter of the box: past them a padded gradient holds its
    // last stop, a repeating one starts again and a reflected one turns.
    const Size logical{40.0, 40.0};
    const MaterialFillLayer padded = MaterialFillLayer::radial(Point{0.5, 0.5}, Size{0.25, 0.25}, red_to_blue);
    const MaterialFillLayer repeated =
        MaterialFillLayer::repeating_radial(Point{0.5, 0.5}, Size{0.25, 0.25}, red_to_blue);
    MaterialFillLayer reflected = padded;
    reflected.spread = GradientSpreadMode::reflect;
    require(repeated.spread == GradientSpreadMode::repeat, "a repeating radial gradient repeats");
    const TestRaster pad = draw(filled_with(padded), logical, 1.0);
    const TestRaster repeat = draw(filled_with(repeated), logical, 1.0);
    const TestRaster reflect = draw(filled_with(reflected), logical, 1.0);
    // The pixel at (43, 28) is 15.5 to the right of the centre (28, 28): t
    // is about 1.55.
    require(pad.channel(43U, 28U, 0U) == 0U, "padded, the last stop holds");
    // Red in linear light (the gradient interpolates there): 165 to 190 is
    // the old sRGB window of 100 to 125 decoded, as 187 to 205 is 130 to 150.
    require(repeat.channel(43U, 28U, 0U) > 165U && repeat.channel(43U, 28U, 0U) < 190U,
            "repeated, the stops begin again");
    require(reflect.channel(43U, 28U, 0U) > 187U && reflect.channel(43U, 28U, 0U) < 205U,
            "reflected, the stops run back");
    require(repeat.channel(28U, 28U, 0U) > 230U && pad.channel(28U, 28U, 0U) == repeat.channel(28U, 28U, 0U),
            "within the radii every spread is the same");
    const bool first = next_matches(repeat, "repeating_radial_1");
    const bool second = next_matches(draw(filled_with(reflected), logical, 2.0), "reflected_radial_2");
    require(first && second, "the repeating radial gradients match their references");
}

void conic() {
    const Size logical{40.0, 40.0};
    const MaterialFillLayer turn = MaterialFillLayer::conic(Point{0.5, 0.5}, 0.0, red_to_blue);
    require(turn.kind == MaterialFillKind::conic_gradient && turn.period_degrees == 360.0 &&
                turn.spread == GradientSpreadMode::pad,
            "a conic gradient sweeps one turn");
    const TestRaster swept = draw(filled_with(turn), logical, 1.0);
    // Just right of up from the centre (28, 28) the sweep begins; just left
    // of it, it ends. The pixel centres lie 1.6 degrees from up, t = 0.0045
    // from either end, which linear light encodes to about 14.
    require(swept.channel(28U, 10U, 0U) > 250U && swept.channel(28U, 10U, 2U) < 25U, "the sweep begins at up");
    require(swept.channel(27U, 10U, 0U) < 25U && swept.channel(27U, 10U, 2U) > 250U, "and ends there");
    // Right of the centre, a quarter of the way round: t = 0.255, red about
    // 224 in linear light.
    require(swept.channel(45U, 28U, 0U) > 215U && swept.channel(45U, 28U, 0U) < 235U,
            "a quarter turn is a quarter of the way");
    // From 90 degrees, right of the centre is where it begins.
    const TestRaster turned = draw(filled_with(MaterialFillLayer::conic(Point{0.5, 0.5}, 90.0, red_to_blue)),
                                   logical, 1.0);
    require(turned.channel(45U, 27U, 0U) < 25U && turned.channel(45U, 28U, 0U) > 245U,
            "the from-angle turns the sweep");
    // Repeating every 90 degrees: a quarter turn begins again.
    const MaterialFillLayer quarters =
        MaterialFillLayer::repeating_conic(Point{0.5, 0.5}, 0.0, 90.0, red_to_blue);
    require(quarters.spread == GradientSpreadMode::repeat && quarters.period_degrees == 90.0,
            "a repeating conic gradient repeats its period");
    const TestRaster repeated = draw(filled_with(quarters), logical, 1.0);
    require(repeated.channel(45U, 28U, 0U) > 240U, "each quarter begins again");
    // t = 0.981 of its quarter: red about 38 in linear light.
    require(repeated.channel(44U, 27U, 0U) < 50U, "and ends before the next");
    const bool first = next_matches(swept, "conic_1");
    const bool second = next_matches(draw(filled_with(quarters), logical, 2.0), "repeating_conic_2");
    require(first && second, "the conic gradients match their references");
}

// -- corners and shapes -------------------------------------------------------------

// A material that exercises every layer the outline shapes: a fill, an
// outer and an inset shadow, a glow and a border.
SurfaceMaterial layered(double corner_radius) {
    SurfaceMaterial material = filled_with(MaterialFillLayer::solid(red));
    material.corner_radius = corner_radius;
    material.shadows.push_back(MaterialShadow{Point{0.0, 1.0}, 3.0, 1.0, shade, false});
    material.shadows.push_back(MaterialShadow{Point{0.0, 1.0}, 2.0, 0.0, shade, true});
    MaterialGlow glow{};
    glow.color = Color{255, 255, 255, 96};
    glow.blur_radius = 2.0;
    glow.spread = 1.0;
    material.glows.push_back(glow);
    material.border.set(MaterialBorder{ink, 1.0});
    return material;
}

void corner_radii() {
    const Size logical{40.0, 20.0};
    SurfaceMaterial corners = layered(0.0);
    corners.corner_radii.set(MaterialCornerRadii{10.0, 0.0, 4.0, 0.0});
    require(valid_surface_material(corners), "four corners are a valid material");
    const TestRaster drawn = draw(corners, logical, 1.0);
    // The box spans pixels 8 to 47 across and 8 to 27 down.
    require(drawn.channel(8U, 8U, 3U) < 60U, "the top-left corner is rounded by 10");
    require(drawn.channel(47U, 8U, 3U) == 255U, "the top-right corner is square");
    require(drawn.channel(8U, 27U, 3U) == 255U, "the bottom-left corner is square");
    require(drawn.channel(47U, 27U, 3U) < 255U, "the bottom-right corner is rounded by 4");

    // Four equal corners draw as one radius, every layer to the same bytes.
    SurfaceMaterial equal = layered(0.0);
    equal.corner_radii.set(MaterialCornerRadii{5.0, 5.0, 5.0, 5.0});
    require(raster_hash(draw(equal, logical, 1.0)) == raster_hash(draw(layered(5.0), logical, 1.0)),
            "four equal corners are one radius");
    // A part's own radius overrides the material's corners.
    require(raster_hash(draw(corners, logical, 1.0, "rounded_rectangle", 3.0)) ==
                raster_hash(draw(layered(0.0), logical, 1.0, "rounded_rectangle", 3.0)),
            "a part's own radius overrides the corners");
    const bool first = next_matches(drawn, "corner_radii_1");
    const bool second = next_matches(draw(corners, logical, 2.0), "corner_radii_2");
    require(first && second, "the corners match their references");
}

void shapes() {
    const Size logical{40.0, 20.0};
    // A pill is the box rounded by half its height.
    require(raster_hash(draw(layered(0.0), logical, 1.0, "pill")) == raster_hash(draw(layered(10.0), logical, 1.0)),
            "a pill is rounded by half the shorter side");
    // A circle is inscribed: the box's ends, beside it, are clear.
    const TestRaster round = draw(layered(0.0), logical, 1.0, "circle");
    require(round.channel(27U, 18U, 3U) == 255U, "the circle's centre is drawn");
    require(round.channel(9U, 18U, 3U) < 30U, "the box's end beside the circle is clear");
    // A cap rounds one end.
    const TestRaster cap = draw(layered(0.0), logical, 1.0, "cap_start");
    require(cap.channel(8U, 8U, 3U) < 60U && cap.channel(47U, 8U, 3U) == 255U, "a start cap rounds the start");
    const bool first = next_matches(round, "circle_1");
    const bool second = next_matches(draw(layered(0.0), logical, 2.0, "cap_end"), "cap_end_2");
    require(first && second, "the shapes match their references");
}

// -- borders ------------------------------------------------------------------------

// Across the top edge's border, away from the corners: how many pixels the
// border's colour covers and how many it leaves clear.
struct EdgeCount final {
    std::size_t drawn{};
    std::size_t clear{};
};

EdgeCount count_top_edge(const TestRaster& raster, std::uint32_t row, std::uint32_t first, std::uint32_t last) {
    EdgeCount count{};
    for (std::uint32_t column = first; column <= last; ++column) {
        const std::uint8_t alpha = raster.channel(column, row, 3U);
        if (alpha > 200U) count.drawn = count.drawn + 1U;
        if (alpha < 40U) count.clear = count.clear + 1U;
    }
    return count;
}

void patterned_borders() {
    const Size logical{60.0, 20.0};
    // A clear face, so that only the border draws.
    SurfaceMaterial dashed = filled_with(MaterialFillLayer::solid(Color{0, 0, 0, 0}));
    dashed.corner_radius = 4.0;
    dashed.border.set(MaterialBorder{ink, 2.0, MaterialBorderStyle::dashed});
    require(valid_surface_material(dashed), "a dashed border is valid");
    const TestRaster dashes = draw(dashed, logical, 1.0);
    const EdgeCount dash_count = count_top_edge(dashes, 8U, 16U, 59U);
    require(dash_count.drawn > 12U && dash_count.clear > 8U, "dashes and gaps run along the edge");

    SurfaceMaterial dotted = dashed;
    (*dotted.border.value()).style = MaterialBorderStyle::dotted;
    const TestRaster dots = draw(dotted, logical, 1.0);
    const EdgeCount dot_count = count_top_edge(dots, 9U, 16U, 59U);
    require(dot_count.clear > 8U, "dots leave gaps");
    require(dots.channel(28U, 18U, 3U) == 0U, "inside the border nothing draws");

    // The pattern never draws outside the solid border's ring.
    SurfaceMaterial solid = dashed;
    (*solid.border.value()).style = MaterialBorderStyle::solid;
    const TestRaster ring = draw(solid, logical, 1.0);
    bool within = true;
    for (std::size_t index = 3U; index < ring.bytes.size(); index = index + 4U) {
        if (dashes.bytes[index] > ring.bytes[index] || dots.bytes[index] > ring.bytes[index]) within = false;
    }
    require(within, "dashes and dots lie within the ring");

    // A dashed circle.
    const TestRaster circle = draw(dashed, Size{30.0, 30.0}, 1.0, "circle");
    const bool first = next_matches(dashes, "dashed_1");
    const bool second = next_matches(draw(dotted, logical, 2.0), "dotted_2");
    const bool third = next_matches(circle, "dashed_circle_1");
    require(first && second && third, "the patterned borders match their references");
}

// -- validation ---------------------------------------------------------------------

void validation() {
    SurfaceMaterial material = filled_with(MaterialFillLayer::conic(Point{0.5, 0.5}, 45.0, red_to_blue));
    require(valid_surface_material(material), "a conic fill is valid");
    material.fills[0].period_degrees = 0.0;
    require(!valid_surface_material(material), "a conic period of zero is refused");
    material.fills[0].period_degrees = 361.0;
    require(!valid_surface_material(material), "a conic period past a turn is refused");
    material.fills[0].period_degrees = 30.0;
    material.fills[0].stops.clear();
    require(!valid_surface_material(material), "a conic fill needs stops");

    SurfaceMaterial cornered = filled_with(MaterialFillLayer::solid(red));
    cornered.corner_radii.set(MaterialCornerRadii{1.0, -1.0, 0.0, 0.0});
    require(!valid_surface_material(cornered), "a negative corner is refused");
    cornered.corner_radii.set(MaterialCornerRadii{1.0, 2.0, 3.0, 4.0});
    cornered.border_edges.top.set(MaterialBorder{ink, 1.0});
    require(!valid_surface_material(cornered), "single edges are square-cornered");

    SurfaceMaterial bordered = filled_with(MaterialFillLayer::solid(red));
    bordered.border.set(MaterialBorder{ink, 1.0, static_cast<MaterialBorderStyle>(7U)});
    require(!valid_surface_material(bordered), "a border style outside the enumeration is refused");
}

} // namespace

int main() {
    shape_parameter();
    repeating_radial();
    conic();
    corner_radii();
    shapes();
    patterned_borders();
    validation();
    require(next_reference == std::size(expected_hashes), "every reference hash is checked");
    std::cout << "gui_forms_engine_fill_shape_tests passed\n";
    return 0;
}
