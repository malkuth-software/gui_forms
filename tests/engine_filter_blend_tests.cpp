// E9.6, filters and blend (notes:planning/stages/T2-engine-core/CSS_COVERAGE.md,
// rows 6 to 8): the colour matrix and its CSS factories, the material's
// part filters (the colour filter, the drop shadow and the blur), glass's
// recipe colour filter on its backdrop, fill blend modes, and the rule
// `blend` property. Each is checked by what it draws at chosen pixels, and
// by reference images: FNV-1a hashes of each raster's rows in RGBA order,
// checked on every CI platform. GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints
// the observed table instead of checking it.
//
// A material with none of these draws the bytes it drew before E9.6: the
// material tests' reference images are unchanged, and so is the glass
// test's.

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
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_filter_blend_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

const EnginePrimitive& primitive_named(std::string_view name) {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find(name);
    require(primitive != nullptr, "the built-in registry has the primitive");
    return *primitive;
}

// A raster the tests own: tightly packed rows of BGRA8 bytes.
struct TestRaster final {
    std::vector<std::uint8_t> bytes;
    DeviceExtent extent{};
    std::size_t row_bytes{};

    [[nodiscard]] std::array<std::uint8_t, 4> pixel(std::uint32_t column, std::uint32_t row) const {
        const std::size_t offset = static_cast<std::size_t>(row) * row_bytes + static_cast<std::size_t>(column) * 4U;
        // The raster is BGRA8; the value is red, green, blue, alpha.
        const std::array<std::uint8_t, 4> value{bytes[offset + 2U], bytes[offset + 1U], bytes[offset],
                                                bytes[offset + 3U]};
        return value;
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

// Draws `material` with the material primitive as a part of `logical` size
// at `scale`, into a destination grown by the material's outsets of 8 on
// every side.
TestRaster draw(const SurfaceMaterial& material, Size logical, double scale) {
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
    const EnginePrimitive& primitive = primitive_named("material");
    const PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
    PrimitivePreparation preparation{};
    require(primitive.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "the part prepares");
    const PartRasterView view{std::span<std::uint8_t>(raster.bytes.data(), raster.bytes.size()), raster.extent,
                              raster.row_bytes};
    require(primitive.rasterize(parameters, target, preparation, view) == RasterOutcome::painted,
            "the material paints");
    return raster;
}

// The reference hashes, in the order the tests check them.
constexpr std::uint64_t expected_hashes[] = {
    0xeaaa242b8d197d19ULL, 0x04494afdcf90dfd9ULL, 0x58d7ff8c3abe8c94ULL,
    0xac5625a01e6884d6ULL, 0x07b04cccd96b761dULL, 0x6c94f6ee6f8e60f1ULL,
};
std::size_t next_reference = 0U;

bool next_matches(const TestRaster& raster, const char* name) {
    require(next_reference < std::size(expected_hashes), "a reference hash for each image");
    const std::uint64_t hash = raster_hash(raster);
    const std::uint64_t expected = expected_hashes[next_reference];
    next_reference = next_reference + 1U;
    if (print_mode()) {
        std::cout << "    " << name << " 0x" << std::hex << hash << std::dec << "ULL\n";
        return true;
    }
    if (hash == expected) return true;
    std::cerr << name << " hash 0x" << std::hex << hash << std::dec << '\n';
    return false;
}

constexpr Color red{255, 0, 0, 255};
constexpr Color brick{200, 100, 50, 255};
constexpr Color grey{128, 128, 128, 255};

SurfaceMaterial filled_with(Color colour) {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(colour));
    return material;
}

bool near_matrix(const MaterialColourMatrix& left, const MaterialColourMatrix& right) {
    for (std::size_t index = 0U; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > 1e-12) return false;
    }
    return true;
}

bool near_byte(std::uint8_t observed, double expected) {
    const bool near = std::abs(static_cast<double>(observed) - expected) <= 1.0;
    return near;
}

// -- the colour matrix --------------------------------------------------------------

void colour_matrices() {
    const MaterialColourMatrix identity{};
    require(MaterialColourMatrix::brightness(1.0) == identity && MaterialColourMatrix::contrast(1.0) == identity &&
                MaterialColourMatrix::invert(0.0) == identity && MaterialColourMatrix::opacity(1.0) == identity,
            "the linear functions at their neutral amounts are the identity");
    require(near_matrix(MaterialColourMatrix::saturate(1.0), identity) &&
                near_matrix(MaterialColourMatrix::grayscale(0.0), identity) &&
                near_matrix(MaterialColourMatrix::sepia(0.0), identity) &&
                near_matrix(MaterialColourMatrix::hue_rotate(0.0), identity) &&
                near_matrix(MaterialColourMatrix::hue_rotate(360.0), identity),
            "the colour functions at their neutral amounts are the identity");
    require(MaterialColourMatrix::grayscale(2.0) == MaterialColourMatrix::grayscale(1.0) &&
                MaterialColourMatrix::sepia(-1.0) == MaterialColourMatrix::sepia(0.0) &&
                MaterialColourMatrix::brightness(-1.0) == MaterialColourMatrix::brightness(0.0),
            "amounts are clamped as CSS clamps them");
    require(MaterialColourMatrix::brightness(0.5).then(MaterialColourMatrix::brightness(0.5)) ==
                MaterialColourMatrix::brightness(0.25),
            "two brightnesses compose into their product");
    require(MaterialColourMatrix::invert(1.0).then(MaterialColourMatrix::invert(1.0)) == identity,
            "inverting twice is the identity");
    // Source order: brightness then invert gives 1 − c / 2; invert then
    // brightness gives (1 − c) / 2.
    const MaterialColourMatrix dim_then_invert =
        MaterialColourMatrix::brightness(0.5).then(MaterialColourMatrix::invert(1.0));
    const MaterialColourMatrix invert_then_dim =
        MaterialColourMatrix::invert(1.0).then(MaterialColourMatrix::brightness(0.5));
    require(dim_then_invert.values[0] == -0.5 && dim_then_invert.values[4] == 1.0, "dim, then invert");
    require(invert_then_dim.values[0] == -0.5 && invert_then_dim.values[4] == 0.5, "invert, then dim");
    bool threw = false;
    try {
        static_cast<void>(MaterialColourMatrix::brightness(std::numeric_limits<double>::quiet_NaN()));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a non-finite amount throws");
    threw = false;
    try {
        static_cast<void>(MaterialColourMatrix::hue_rotate(std::numeric_limits<double>::infinity()));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a non-finite angle throws");
    MaterialColourMatrix large{};
    require(valid_colour_matrix(large), "the identity is valid");
    large.values[7] = 300.0;
    require(!valid_colour_matrix(large), "an entry past 256 is refused");
    large.values[7] = std::numeric_limits<double>::quiet_NaN();
    require(!valid_colour_matrix(large), "a non-finite entry is refused");

    // The holder: empty by default, deep copies, equal by its matrix.
    MaterialColourFilter empty{};
    require(!empty && !empty.has_value() && empty == MaterialColourFilter{}, "a filter is empty by default");
    MaterialColourFilter sepia = MaterialColourMatrix::sepia(1.0);
    const MaterialColourFilter copy = sepia;
    require(copy.has_value() && copy == sepia && !(copy == empty), "a copy holds an equal matrix");
    require(&*copy != &*sepia, "a copy holds its own matrix");
    MaterialColourFilter moved = std::move(sepia);
    require(moved == copy && !sepia.has_value(), "a move leaves the source empty");
    moved.reset();
    require(!moved.has_value(), "reset empties the filter");
}

// -- the part filters ---------------------------------------------------------------

void colour_filters() {
    const Size logical{24.0, 16.0};
    SurfaceMaterial inverted = filled_with(brick);
    inverted.colour_filter = MaterialColourMatrix::invert(1.0);
    const TestRaster drawn = draw(inverted, logical, 1.0);
    require(drawn.pixel(20U, 16U) == (std::array<std::uint8_t, 4>{55U, 155U, 205U, 255U}),
            "an inverted fill is its complement");
    require(drawn.pixel(2U, 2U)[3] == 0U, "the filter leaves transparent pixels transparent");
    SurfaceMaterial grey_scaled = filled_with(red);
    grey_scaled.colour_filter = MaterialColourMatrix::grayscale(1.0);
    const TestRaster grey_drawn = draw(grey_scaled, logical, 1.0);
    const std::array<std::uint8_t, 4> centre = grey_drawn.pixel(20U, 16U);
    require(centre[0] == centre[1] && centre[1] == centre[2] && near_byte(centre[0], 0.2126 * 255.0),
            "grayscale gives red its luminance");
    SurfaceMaterial composed = filled_with(brick);
    composed.corner_radius = 4.0;
    composed.shadows.push_back(MaterialShadow{Point{0.0, 1.0}, 3.0, 0.0, Color{0, 0, 0, 80}, false});
    composed.colour_filter = MaterialColourMatrix::sepia(0.8).then(MaterialColourMatrix::hue_rotate(90.0));
    const bool first = next_matches(draw(composed, logical, 1.0), "sepia_hue_1");
    const bool second = next_matches(draw(composed, logical, 1.5), "sepia_hue_1_5");
    require(first && second, "the colour filters match their references");
}

// The colour stages (owner 2026-11-16): fill and duotone as matrices,
// tritone as the luminance matrix and a table, and the table's transfer
// functions, each checked to the byte.
void colour_stages() {
    const Size logical{24.0, 16.0};
    SurfaceMaterial filled = filled_with(brick);
    filled.colour_filter = MaterialColourMatrix::fill(Color{10, 20, 30, 255});
    require(draw(filled, logical, 1.0).pixel(20U, 16U) == (std::array<std::uint8_t, 4>{10U, 20U, 30U, 255U}),
            "fill recolours the face and keeps its alpha");
    // L = 128 / 255 on grey: 20 + 200 L = 120.39, 40 + 200 L = 140.39,
    // 60 + 195 L = 157.88.
    SurfaceMaterial toned = filled_with(grey);
    toned.colour_filter = MaterialColourMatrix::duotone(Color{20, 40, 60, 255}, Color{220, 240, 255, 255});
    require(draw(toned, logical, 1.0).pixel(20U, 16U) == (std::array<std::uint8_t, 4>{120U, 140U, 158U, 255U}),
            "duotone maps luminance between its colours");
    // Grey's luminance is level 128, 0.00392 past the midtone: red rises
    // to 1, green falls to 254, blue stays 0.
    SurfaceMaterial tritoned = filled_with(grey);
    tritoned.colour_filter =
        MaterialColourFilter(MaterialColourMatrix::luminance(),
                             MaterialColourTable::tritone(Color{0, 0, 255, 255}, Color{0, 255, 0, 255},
                                                          Color{255, 0, 0, 255}));
    require(draw(tritoned, logical, 1.0).pixel(20U, 16U) == (std::array<std::uint8_t, 4>{1U, 254U, 0U, 255U}),
            "tritone maps luminance through its three colours");

    MaterialTransferFunction peak{};
    peak.kind = MaterialTransferKind::table;
    peak.values = {0.0, 1.0, 0.0};
    MaterialTransferFunction steps{};
    steps.kind = MaterialTransferKind::discrete;
    steps.values = {0.2, 0.8};
    MaterialTransferFunction line{};
    line.kind = MaterialTransferKind::linear;
    line.slope = 0.5;
    line.intercept = 0.25;
    MaterialTransferFunction square{};
    square.kind = MaterialTransferKind::gamma;
    square.exponent = 2.0;
    const MaterialColourTable table = MaterialColourTable::transfer(peak, steps, line, square);
    const std::size_t levels = MaterialColourTable::levels;
    require(table.entries[0] == 0U && table.entries[64] == 128U && table.entries[255] == 0U,
            "a table interpolates between its values");
    require(table.entries[levels + 127U] == 51U && table.entries[levels + 128U] == 204U,
            "discrete steps at its intervals");
    require(table.entries[2U * levels] == 64U && table.entries[2U * levels + 255U] == 191U,
            "linear is slope C + intercept");
    require(table.entries[3U * levels + 128U] == 64U, "gamma is amplitude C^exponent + offset");
    require(MaterialColourTable{}.then(table) == table && table.then(MaterialColourTable{}) == table,
            "the identity table composes to the other");
    require(MaterialColourTable{}.then_matrix(MaterialColourMatrix::brightness(0.5)).entries[255] == 128U,
            "a per-channel matrix folds into a table");
    require(!MaterialColourMatrix::saturate(2.0).per_channel() && MaterialColourMatrix::opacity(0.5).per_channel(),
            "only a matrix reading each channel alone folds");
    MaterialTransferFunction flat{};
    flat.kind = MaterialTransferKind::table;
    flat.values = {0.5};
    bool refused = false;
    try {
        static_cast<void>(MaterialColourTable::transfer(flat, flat, flat, flat));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !valid_transfer_function(flat), "a table of one value is refused");
}

void drop_shadows() {
    const Size logical{24.0, 16.0};
    SurfaceMaterial lifted = filled_with(brick);
    lifted.corner_radius = 3.0;
    lifted.drop_shadow = MaterialDropShadow{Point{3.0, 3.0}, 2.0, Color{0, 0, 0, 128}};
    const TestRaster drawn = draw(lifted, logical, 1.0);
    // The part's box is (8, 8) to (32, 24); its shadow lies 3 right and 3
    // down, under it.
    require(drawn.pixel(20U, 16U) == (std::array<std::uint8_t, 4>{200U, 100U, 50U, 255U}),
            "the shadow lies beneath the part");
    const std::array<std::uint8_t, 4> below = drawn.pixel(20U, 26U);
    require(below[3] > 0U && below[0] == 0U, "the shadow shows below the part, in its colour");
    require(drawn.pixel(4U, 4U)[3] == 0U, "the shadow does not reach above and left of the part");
    // A negative offset moves the shadow above and left of the part, and none
    // of it is left below and right.
    SurfaceMaterial raised = lifted;
    raised.drop_shadow = MaterialDropShadow{Point{-3.0, -3.0}, 2.0, Color{0, 0, 0, 128}};
    const TestRaster up = draw(raised, logical, 1.0);
    require(up.pixel(6U, 6U)[3] > 0U && up.pixel(6U, 6U)[0] == 0U,
            "a negative offset shows the shadow above and left of the part");
    require(up.pixel(36U, 28U)[3] == 0U, "a negative offset leaves no shadow below and right of the part");
    const bool first = next_matches(drawn, "drop_shadow_1");
    const bool second = next_matches(draw(lifted, logical, 2.0), "drop_shadow_2");
    require(first && second, "the drop shadows match their references");
}

void blurs() {
    const Size logical{24.0, 16.0};
    SurfaceMaterial soft = filled_with(brick);
    soft.filter_blur = 4.0;
    const TestRaster drawn = draw(soft, logical, 1.0);
    require(drawn.pixel(20U, 16U) == (std::array<std::uint8_t, 4>{200U, 100U, 50U, 255U}),
            "the blur keeps a uniform middle");
    const std::array<std::uint8_t, 4> edge = drawn.pixel(8U, 16U);
    require(edge[3] > 0U && edge[3] < 255U, "the blur softens the part's edge");
    require(drawn.pixel(6U, 16U)[3] > 0U, "the blur spreads past the part's edge");
    require(next_matches(drawn, "blur_1"), "the blur matches its reference");
}

// -- fill blend modes ---------------------------------------------------------------

void fill_blends() {
    const Size logical{24.0, 16.0};
    SurfaceMaterial multiplied = filled_with(brick);
    multiplied.fills.push_back(MaterialFillLayer::solid(grey));
    multiplied.fills[1].blend = MaterialBlendMode::multiply;
    const std::array<std::uint8_t, 4> product = draw(multiplied, logical, 1.0).pixel(20U, 16U);
    require(near_byte(product[0], 200.0 * 128.0 / 255.0) && near_byte(product[1], 100.0 * 128.0 / 255.0) &&
                near_byte(product[2], 50.0 * 128.0 / 255.0) && product[3] == 255U,
            "multiply gives the product");
    SurfaceMaterial screened = filled_with(brick);
    screened.fills.push_back(MaterialFillLayer::solid(grey));
    screened.fills[1].blend = MaterialBlendMode::screen;
    const std::array<std::uint8_t, 4> lifted = draw(screened, logical, 1.0).pixel(20U, 16U);
    const double screen_red = 200.0 + 128.0 - 200.0 * 128.0 / 255.0;
    require(near_byte(lifted[0], screen_red) && lifted[3] == 255U, "screen gives s + d − s × d");
    // The fills form one face before the shadow meets them.
    screened.shadows.push_back(MaterialShadow{Point{0.0, 2.0}, 4.0, 0.0, Color{0, 0, 0, 96}, false});
    screened.corner_radius = 5.0;
    require(next_matches(draw(screened, logical, 1.25), "screen_face_1_25"), "the blended face matches");
}

// -- glass's recipe colour filter ---------------------------------------------------

void glass_recipe() {
    const EnginePrimitive& glass = primitive_named("glass");
    const PrimitiveDescriptor& descriptor = glass.descriptor();
    PrimitiveParameterBlock parameters = default_parameters(descriptor);
    require(!glass.theme_material(parameters, ControlVisualRole::button).has_value(), "by default glass reads no recipe");
    require(set_choice_parameter(parameters, descriptor, "recipe", "button") == ParameterAssignment::assigned,
            "a recipe is a choice");
    const std::optional<ControlVisualRole> role = glass.theme_material(parameters, ControlVisualRole::button);
    require(role.has_value() && *role == ControlVisualRole::button, "the choice names the recipe's role");
    parameters.values[1].colour = Color::rgba(0, 0, 0, 0);

    // A uniform backplane, inverted: the blur keeps it uniform, the filter
    // inverts it, and with no tint that is the glass.
    const Size logical{20.0, 10.0};
    const std::optional<std::uint32_t> margin = glass.backplane_margin(parameters, DeviceScale{1.0});
    require(margin.has_value(), "glass samples the backplane");
    const DeviceExtent grown{20U + *margin * 2U, 10U + *margin * 2U};
    std::shared_ptr<PartRaster> backplane = PartRaster::create(grown, DeviceScale{1.0});
    require(backplane != nullptr, "the backplane is made");
    const PartRasterView plane = (*backplane).view();
    for (std::size_t at = 0U; at < plane.pixels.size(); at = at + 4U) {
        // BGRA8: red 200, green 100, blue 50.
        plane.pixels[at] = 50U;
        plane.pixels[at + 1U] = 100U;
        plane.pixels[at + 2U] = 200U;
        plane.pixels[at + 3U] = 255U;
    }
    SurfaceMaterial recipe = filled_with(brick);
    recipe.colour_filter = MaterialColourMatrix::invert(1.0);
    PrimitiveTarget target{};
    target.logical_size = logical;
    target.scale = DeviceScale{1.0};
    target.backplane = backplane.get();
    target.backplane_margin = *margin;
    target.material = &recipe;
    TestRaster drawn{};
    drawn.extent = DeviceExtent{20U, 10U};
    drawn.row_bytes = 80U;
    drawn.bytes.assign(800U, 0U);
    PrimitivePreparation preparation{};
    require(glass.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "glass prepares");
    const PartRasterView view{std::span<std::uint8_t>(drawn.bytes.data(), drawn.bytes.size()), drawn.extent,
                              drawn.row_bytes};
    require(glass.rasterize(parameters, target, preparation, view) == RasterOutcome::painted, "glass paints");
    for (std::uint32_t row = 0U; row < 10U; ++row) {
        for (std::uint32_t column = 0U; column < 20U; ++column) {
            require(drawn.pixel(column, row) == (std::array<std::uint8_t, 4>{55U, 155U, 205U, 255U}),
                    "glass inverts its backdrop by its recipe's colour filter");
        }
    }
}

// -- the rule blend property --------------------------------------------------------

CompiledThemeRules::PartFacts face_facts() {
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "pad";
    facts.stable_id = "subject";
    facts.part = "face";
    facts.role = PartRole::face;
    return facts;
}

ThemeRule blend_rule(std::string label, RuleDeclaration declaration) {
    ThemeRule rule{};
    rule.label = std::move(label);
    rule.selector.part = "face";
    rule.declarations.push_back(std::move(declaration));
    return rule;
}

bool has_diagnostic(const CompiledThemeRules& rules, RuleDiagnosticKind kind) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind && diagnostic.property == blend_property) return true;
    }
    return false;
}

void rule_blend() {
    ThemeRuleSheet sheet{};
    sheet.rules.push_back(blend_rule("multiplied", RuleDeclaration::choice_value(blend_property, "multiply")));
    const CompiledThemeRules multiplied = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(multiplied.diagnostics().empty() && multiplied.rule_count() == 1U,
            "a rule declaring only a blend stands, without a primitive");
    const CompiledThemeRules::Resolution resolved = multiplied.resolve(face_facts());
    require(resolved.blend == PartBlend::multiply && resolved.outcome == CompiledThemeRules::Outcome::no_rule,
            "the part multiplies, whatever draws it");

    // The last matching rule wins.
    sheet.rules.push_back(blend_rule("screened", RuleDeclaration::choice_value(blend_property, "screen")));
    const CompiledThemeRules screened = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(screened.resolve(face_facts()).blend == PartBlend::screen, "the later rule's blend wins");
    CompiledThemeRules::PartFacts other = face_facts();
    other.part = "glyph";
    require(screened.resolve(other).blend == PartBlend::normal, "a part no rule matches is normal");

    ThemeRuleSheet faulty{};
    faulty.rules.push_back(blend_rule("unknown", RuleDeclaration::choice_value(blend_property, "overlay")));
    faulty.rules.push_back(blend_rule("number", RuleDeclaration::number_value(blend_property, 1.0)));
    ThemeRule twice = blend_rule("twice", RuleDeclaration::choice_value(blend_property, "screen"));
    twice.declarations.push_back(RuleDeclaration::choice_value(blend_property, "multiply"));
    faulty.rules.push_back(twice);
    const CompiledThemeRules checked = CompiledThemeRules::compile(faulty, builtin_primitive_registry());
    require(has_diagnostic(checked, RuleDiagnosticKind::blend_invalid) &&
                has_diagnostic(checked, RuleDiagnosticKind::duplicate_declaration),
            "a blend outside its names, or declared twice, is dropped with a diagnostic");
    require(checked.rule_count() == 1U && checked.resolve(face_facts()).blend == PartBlend::screen,
            "the rules left with nothing are dropped; the first of two blends stands");
    require(rule_diagnostic_name(RuleDiagnosticKind::blend_invalid) == "blend_invalid", "the diagnostic is named");
}

// -- validation ---------------------------------------------------------------------

void validation() {
    SurfaceMaterial material = filled_with(red);
    material.colour_filter = MaterialColourMatrix::sepia(1.0);
    material.drop_shadow = MaterialDropShadow{Point{2.0, -2.0}, 2.0, Color{0, 0, 0, 96}};
    material.filter_blur = 2.0;
    require(valid_surface_material(material), "filters within the reach are valid");
    MaterialColourMatrix wild{};
    wild.values[0] = 1000.0;
    material.colour_filter = wild;
    require(!valid_surface_material(material), "a colour matrix past its bound is refused");
    material.colour_filter.reset();
    material.filter_blur = 6.0;
    require(!valid_surface_material(material), "filters reaching past the outsets are refused");
    material.filter_blur = -1.0;
    require(!valid_surface_material(material), "a negative blur is refused");
    material.filter_blur = 0.0;
    material.drop_shadow = MaterialDropShadow{Point{0.0, 0.0}, std::numeric_limits<double>::infinity(), red};
    require(!valid_surface_material(material), "a non-finite shadow is refused");
    material.drop_shadow.reset();
    material.fills[0].blend = static_cast<MaterialBlendMode>(9U);
    require(!valid_surface_material(material), "a blend outside the enumeration is refused");
}

} // namespace

int main() {
    colour_matrices();
    colour_filters();
    colour_stages();
    drop_shadows();
    blurs();
    fill_blends();
    glass_recipe();
    rule_blend();
    validation();
    require(next_reference == std::size(expected_hashes), "every reference hash is checked");
    std::cout << "gui_forms_engine_filter_blend_tests passed\n";
    return 0;
}
