// The relief primitives, part shapes and hit regions (T2 plan, phase E8):
// choice parameters register, assign and compile by name; every shape's
// distance, inside test and coverage are as declared; a hit region is the
// drawn shape and never less than the minimum target; each relief primitive
// draws its layers where design language §3.5 and Plan Paint put them; and
// the reference images have the same bytes on every CI platform.
//
// The reference images are FNV-1a hashes of each raster's rows in RGBA
// order, checked by every CI platform. GUI_FORMS_ENGINE_IDENTITY_PRINT=1
// prints the observed table instead of checking it.

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/rgba_order.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Coverage expectations below are computed as the kernels compute them.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_relief_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

const EnginePrimitive& builtin(std::string_view name) {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find(name);
    require(primitive != nullptr, "the built-in registry has the primitive");
    return *primitive;
}

// A raster the tests own, its rows padded so that padding is exercised.
struct TestRaster final {
    std::vector<std::uint8_t> bytes;
    DeviceExtent extent{};
    std::size_t row_bytes{};

    [[nodiscard]] PartRasterView view() {
        const PartRasterView borrowed{std::span<std::uint8_t>(bytes.data(), bytes.size()), extent, row_bytes};
        return borrowed;
    }
    [[nodiscard]] std::array<std::uint8_t, 4> pixel(std::uint32_t column, std::uint32_t row) const {
        const std::size_t offset = static_cast<std::size_t>(row) * row_bytes + static_cast<std::size_t>(column) * 4U;
        // The raster is BGRA8; the value is red, green, blue, alpha.
        const std::array<std::uint8_t, 4> value{bytes[offset + 2U], bytes[offset + 1U], bytes[offset],
                                                bytes[offset + 3U]};
        return value;
    }
};

TestRaster make_raster(Size logical, double scale) {
    const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical.width * scale));
    const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical.height * scale));
    TestRaster raster{};
    raster.extent = DeviceExtent{width, height};
    raster.row_bytes = static_cast<std::size_t>(width) * 4U + 8U;
    raster.bytes.assign(raster.row_bytes * height, 0xA5U);
    return raster;
}

std::uint64_t raster_hash(const TestRaster& raster) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes;
        const std::size_t end = start + static_cast<std::size_t>(raster.extent.width) * 4U;
        const std::span<const std::uint8_t> row_pixels =
            std::span<const std::uint8_t>(raster.bytes).subspan(start, end - start);
        for (const std::uint8_t byte : gui_forms::test_support::rgba_order(row_pixels)) {
            hash = hash ^ byte;
            hash = hash * 1099511628211ULL;
        }
    }
    return hash;
}

// Prepares, then rasterizes, requiring both to succeed.
TestRaster draw(const EnginePrimitive& primitive, const PrimitiveParameterBlock& parameters, Size logical,
                double scale, AnatomyStateSet states) {
    TestRaster raster = make_raster(logical, scale);
    const PrimitiveTarget target{logical, DeviceScale{scale}, states};
    PrimitivePreparation preparation{};
    require(primitive.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "the part prepares");
    require(primitive.rasterize(parameters, target, preparation, raster.view()) == RasterOutcome::painted,
            "the part paints");
    return raster;
}

TestRaster draw_default(std::string_view name, Size logical, double scale, AnatomyStateSet states) {
    const EnginePrimitive& primitive = builtin(name);
    const PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
    TestRaster raster = draw(primitive, parameters, logical, scale, states);
    return raster;
}

AnatomyStateSet one_state(AnatomyState state) {
    AnatomyStateSet states{};
    states.insert(state);
    return states;
}

bool pixel_is(const TestRaster& raster, std::uint32_t column, std::uint32_t row, std::array<std::uint8_t, 4> expected) {
    const bool same = raster.pixel(column, row) == expected;
    return same;
}

// -- the registry and choice parameters ---------------------------------------------

const char* const relief_names[] = {"pill_plate", "gold_plate", "inset_well", "etched_divider", "seam_line", "grip"};

void test_relief_primitives_register() {
    require(!(current_engine_version < EngineVersion{0, 2}), "E8 is engine 0.2, which this engine implements");
    for (const char* const name : relief_names) {
        const PrimitiveDescriptor& descriptor = builtin(name).descriptor();
        require(descriptor.introduced == (EngineVersion{0, 2}), "the relief set is engine 0.2");
        require(validate_parameters(descriptor, default_parameters(descriptor)).defect == ParameterDefect::none,
                "a relief primitive's defaults validate");
        require(descriptor.cost.per_device_pixel > 0U && descriptor.cost.per_device_pixel <= 512U,
                "a relief primitive declares a cost within the budget");
    }
    require(builtin("grip").descriptor().roles[0] == PartRole::grip, "the grip paints the grip role");
}

class FixturePrimitive final : public EnginePrimitive {
public:
    explicit FixturePrimitive(const PrimitiveDescriptor& described) : described_(described) {}
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override { return described_; }

private:
    void derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept override {}
    void fill(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<const double>,
              const PartRasterView&) const noexcept override {}
    PrimitiveDescriptor described_;
};

const PartRole face_role[] = {PartRole::face};
const std::string_view two_choices[] = {"square", "round"};
const std::string_view repeated_choices[] = {"square", "square"};
const std::string_view upper_choices[] = {"Square"};
const PrimitiveParameterDeclaration good_choice[] = {
    {"corner", PrimitiveParameterKind::choice, 0.0, 0.0, 1.0, {}, false, two_choices}};
const PrimitiveParameterDeclaration no_choices[] = {{"corner", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}}};
const PrimitiveParameterDeclaration twice_choices[] = {
    {"corner", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, repeated_choices}};
const PrimitiveParameterDeclaration named_badly[] = {
    {"corner", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, upper_choices}};
const PrimitiveParameterDeclaration default_outside[] = {
    {"corner", PrimitiveParameterKind::choice, 0.0, 0.0, 2.0, {}, false, two_choices}};

PrimitiveDescriptor choice_descriptor(std::string_view name, std::span<const PrimitiveParameterDeclaration> parameters) {
    const PrimitiveDescriptor descriptor{name, EngineVersion{0, 2}, face_role, {}, parameters, PrimitiveCost{1U, 1U},
                                         {}};
    return descriptor;
}

void test_choice_parameters() {
    PrimitiveRegistry registry{};
    const FixturePrimitive good{choice_descriptor("good", good_choice)};
    require(registry.register_primitive(good) == RegistrationOutcome::registered, "a choice parameter registers");
    const FixturePrimitive empty{choice_descriptor("empty", no_choices)};
    require(registry.register_primitive(empty) == RegistrationOutcome::choices_invalid, "a choice needs choices");
    const FixturePrimitive repeated{choice_descriptor("repeated", twice_choices)};
    require(registry.register_primitive(repeated) == RegistrationOutcome::choices_invalid,
            "each choice is named once");
    const FixturePrimitive upper{choice_descriptor("upper", named_badly)};
    require(registry.register_primitive(upper) == RegistrationOutcome::choices_invalid, "choices are snake_case");
    const FixturePrimitive outside{choice_descriptor("outside", default_outside)};
    require(registry.register_primitive(outside) == RegistrationOutcome::default_out_of_range,
            "the default is one of the choices");
    require(registry.primitives().size() == 1U, "refusals leave the registry unchanged");

    const PrimitiveDescriptor& descriptor = good.descriptor();
    PrimitiveParameterBlock block = default_parameters(descriptor);
    require(block.values[0].kind == PrimitiveParameterKind::choice && block.values[0].number == 1.0,
            "the default choice is its position");
    require(set_choice_parameter(block, descriptor, "corner", "square") == ParameterAssignment::assigned &&
                block.values[0].number == 0.0,
            "a choice is assigned by name");
    require(set_choice_parameter(block, descriptor, "corner", "oval") == ParameterAssignment::out_of_range &&
                block.values[0].number == 0.0,
            "an unknown choice is refused and changes nothing");
    require(set_choice_parameter(block, descriptor, "edge", "square") == ParameterAssignment::unknown_name,
            "an unknown parameter is refused");
    block.values[0].number = 0.5;
    require(validate_parameters(descriptor, block).defect == ParameterDefect::out_of_range,
            "a choice is a whole position");
    block.values[0].number = 2.0;
    require(validate_parameters(descriptor, block).defect == ParameterDefect::out_of_range,
            "a choice lies among the choices");

    const PrimitiveDescriptor& pill = builtin("pill_plate").descriptor();
    PrimitiveParameterBlock pill_block = default_parameters(pill);
    require(set_number_parameter(pill_block, pill, "shape", 1.0) == ParameterAssignment::wrong_kind,
            "a shape is not a number");
    require(set_choice_parameter(pill_block, pill, "corner_radius", "pill") == ParameterAssignment::wrong_kind,
            "a radius is not a choice");
}

bool has_kind(const CompiledThemeRules& rules, RuleDiagnosticKind kind, std::string_view property) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind && diagnostic.property == property) return true;
    }
    return false;
}

ThemeRule pill_rule(std::string label, std::vector<RuleDeclaration> declarations) {
    ThemeRule rule{};
    rule.label = label;
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::move(label);
    rule.declarations.push_back(RuleDeclaration::primitive("pill_plate"));
    for (RuleDeclaration& declaration : declarations) rule.declarations.push_back(std::move(declaration));
    return rule;
}

void test_shape_choices_compile() {
    ThemeRuleSheet sheet{};
    std::vector<RuleDeclaration> pill{};
    pill.push_back(RuleDeclaration::choice_value("shape", "pill"));
    sheet.rules.push_back(pill_rule("named", std::move(pill)));
    std::vector<RuleDeclaration> unknown{};
    unknown.push_back(RuleDeclaration::choice_value("shape", "hexagon"));
    sheet.rules.push_back(pill_rule("unknown", std::move(unknown)));
    std::vector<RuleDeclaration> numbered{};
    numbered.push_back(RuleDeclaration::number_value("shape", 1.0));
    sheet.rules.push_back(pill_rule("numbered", std::move(numbered)));
    std::vector<RuleDeclaration> radius{};
    radius.push_back(RuleDeclaration::choice_value("corner_radius", "pill"));
    sheet.rules.push_back(pill_rule("radius", std::move(radius)));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(has_kind(rules, RuleDiagnosticKind::parameter_out_of_range, "shape"), "an unknown shape is refused");
    require(has_kind(rules, RuleDiagnosticKind::parameter_wrong_kind, "shape"), "a numbered shape is refused");
    require(has_kind(rules, RuleDiagnosticKind::parameter_wrong_kind, "corner_radius"),
            "a named radius is refused");
    CompiledThemeRules::PartFacts facts{};
    facts.stable_id = "named";
    facts.role = PartRole::face;
    const CompiledThemeRules::Resolution resolution = rules.resolve(facts);
    require(resolution.outcome == CompiledThemeRules::Outcome::primitive, "the named rule selects the pill plate");
    const PartShape shape = (*resolution.primitive).shape(resolution.parameters);
    require(shape.kind == PartShapeKind::pill, "the rule's shape is the pill");
}

// -- shapes and hit regions -----------------------------------------------------------

void test_shapes() {
    const Rect across{0.0, 0.0, 40.0, 20.0};
    const Rect upright{0.0, 0.0, 20.0, 40.0};
    const PartShape square{PartShapeKind::rounded_rectangle, 0.0};
    require(shape_signed_distance(square, Rect{0.0, 0.0, 10.0, 10.0}, Point{5.0, 5.0}) == -5.0 &&
                shape_signed_distance(square, Rect{0.0, 0.0, 10.0, 10.0}, Point{15.0, 5.0}) == 5.0,
            "a square's distance is to its nearest edge");
    const PartShape rounded{PartShapeKind::rounded_rectangle, 4.0};
    require(shape_contains(rounded, across, Point{20.0, 10.0}) && !shape_contains(rounded, across, Point{0.2, 0.2}),
            "a rounded rectangle's corner is cut");
    require(shape_contains(rounded, across, Point{0.0, 10.0}) && !shape_contains(rounded, across, Point{40.0, 10.0}),
            "the left edge is inside, the right edge outside");
    const PartShape huge{PartShapeKind::rounded_rectangle, 1000.0};
    const PartShape pill{PartShapeKind::pill, 0.0};
    require(shape_signed_distance(huge, across, Point{2.0, 3.0}) == shape_signed_distance(pill, across, Point{2.0, 3.0}),
            "a radius is clamped to half the shorter side");
    require(!shape_contains(pill, across, Point{1.0, 1.0}) && shape_contains(pill, across, Point{1.0, 10.0}) &&
                shape_contains(pill, across, Point{20.0, 0.5}),
            "a pill rounds both ends");
    const PartShape circle{PartShapeKind::circle, 0.0};
    require(!shape_contains(circle, Rect{0.0, 0.0, 20.0, 20.0}, Point{19.0, 19.0}) &&
                shape_contains(circle, Rect{0.0, 0.0, 20.0, 20.0}, Point{10.0, 1.0}),
            "a circle is inscribed");
    require(!shape_contains(circle, across, Point{5.0, 10.0}) && shape_contains(circle, across, Point{20.0, 1.0}),
            "a circle in a long box is centred");
    const PartShape start{PartShapeKind::cap_start, 0.0};
    const PartShape end{PartShapeKind::cap_end, 0.0};
    require(!shape_contains(start, across, Point{1.0, 1.0}) && shape_contains(start, across, Point{39.0, 1.0}),
            "a start cap rounds the left of a box lying across");
    require(shape_contains(end, across, Point{1.0, 1.0}) && !shape_contains(end, across, Point{39.0, 1.0}),
            "an end cap rounds the right");
    require(!shape_contains(start, upright, Point{1.0, 1.0}) && shape_contains(start, upright, Point{1.0, 39.0}),
            "a start cap rounds the top of an upright box");
    require(shape_coverage(pill, across, 1.0, 20, 10) == 1.0 && shape_coverage(pill, across, 1.0, 0, 0) == 0.0,
            "coverage is whole inside and none outside");
    const double edge = shape_coverage(pill, across, 1.0, 0, 9);
    require(edge > 0.0 && edge < 1.0, "an edge pixel is partly covered");
    require(shape_coverage(pill, across, 2.0, 40, 20) == 1.0 && shape_coverage(pill, across, 2.0, 0, 0) == 0.0,
            "coverage is measured in device pixels");
}

void test_hit_regions() {
    const PartHitRegion small{Rect{0.0, 0.0, 40.0, 12.0}, PartShape{PartShapeKind::circle, 0.0}};
    require(part_hit(small, Point{20.0, 6.0}), "a point in the shape hits");
    require(part_hit(small, Point{12.0, 6.0}), "a point in the minimum target hits beyond a small shape");
    require(!part_hit(small, Point{5.0, 6.0}), "a point outside both misses");
    require(!part_hit(small, Point{12.0, 13.0}), "nothing hits outside the bounds");
    const PartHitRegion large{Rect{0.0, 0.0, 60.0, 60.0}, PartShape{PartShapeKind::circle, 0.0}};
    require(!part_hit(large, Point{2.0, 2.0}) && part_hit(large, Point{30.0, 2.0}),
            "a large shape's corners miss and its edge hits");
}

// The hit region the engine derives for a part a pill-plate rule draws.
class Plate final : public Control {
public:
    explicit Plate(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter&, Rect) override {}
};

void test_engine_hit_region() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "engine-relief-test";
    std::vector<RuleDeclaration> pill{};
    pill.push_back(RuleDeclaration::choice_value("shape", "pill"));
    definition.rules.rules.push_back(pill_rule("plate", std::move(pill)));
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Plate> plate = make_control<Plate>(StableId("plate"));
    (*plate).set_requested_bounds(Rect{0.0, 0.0, 100.0, 40.0});
    (*root).add_child(plate);
    std::shared_ptr<Plate> other = make_control<Plate>(StableId("other"));
    (*other).set_requested_bounds(Rect{0.0, 50.0, 100.0, 40.0});
    (*root).add_child(other);
    Window window(root, Size{200.0, 100.0});
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the relief theme compiles cleanly");
    window.perform_layout();
    PartPaint part{};
    part.role = PartRole::face;
    part.family = ControlVisualRole::button;
    part.bounds = Rect{10.0, 10.0, 60.0, 16.0};
    const ThemeEngine& engine = window.theme().engine();
    const PartHitRegion region = engine.hit_region(*plate, part);
    require(region.bounds == part.bounds && region.shape.kind == PartShapeKind::pill,
            "a pill plate's hit region is its pill");
    require(!part_hit(region, Point{10.5, 10.5}) && part_hit(region, Point{40.0, 10.5}),
            "the pill's corners miss");
    const PartHitRegion plain = engine.hit_region(*other, part);
    require(plain.shape.kind == PartShapeKind::rounded_rectangle && plain.shape.corner_radius == 0.0 &&
                part_hit(plain, Point{10.5, 10.5}),
            "a part the built-in recipe draws has a rectangle");
}

// -- what the relief primitives draw ---------------------------------------------

constexpr Size plate_size{40.0, 24.0};

void test_pill_plate_layers() {
    const TestRaster rest = draw_default("pill_plate", plate_size, 1.0, {});
    require(pixel_is(rest, 20U, 0U, {99, 132, 178, 255}), "the plate's top row is its border");
    require(pixel_is(rest, 0U, 0U, {0, 0, 0, 0}), "the rounded corner is clear");
    require(rest.pixel(20U, 23U)[3] == 85U, "the bottom row is the contact shadow");
    const std::array<std::uint8_t, 4> inside = rest.pixel(20U, 12U);
    require(inside[3] == 255U && inside[2] > inside[0], "the face is opaque and blue");
    const std::array<std::uint8_t, 4> upper = rest.pixel(20U, 1U);
    require(upper[0] > 245U && upper[3] == 255U, "the inner top line is light");
    const std::array<std::uint8_t, 4> lower = rest.pixel(20U, 21U);
    require(lower[0] < inside[0], "the inner bottom line is dark");

    const TestRaster hot = draw_default("pill_plate", plate_size, 1.0, one_state(AnatomyState::hot));
    require(pixel_is(hot, 20U, 0U, {53, 94, 150, 255}), "hot darkens the border");
    const TestRaster pressed = draw_default("pill_plate", plate_size, 1.0, one_state(AnatomyState::pressed));
    require(pressed.pixel(20U, 23U)[3] == 0U, "pressed drops the shadow");
    require(pressed.pixel(20U, 1U)[0] < pressed.pixel(20U, 21U)[0], "pressed inverts the inner lines");
    const TestRaster disabled = draw_default("pill_plate", plate_size, 1.0, one_state(AnatomyState::disabled));
    require(disabled.pixel(20U, 0U)[3] < 255U && disabled.pixel(20U, 23U)[3] == 0U,
            "disabled pales the border and drops the shadow");
    const TestRaster preselect = draw_default("pill_plate", plate_size, 1.0, one_state(AnatomyState::preselect));
    require(pixel_is(preselect, 20U, 0U, {33, 75, 129, 255}) && pixel_is(preselect, 20U, 1U, {99, 132, 178, 255}),
            "the default action has a second perimeter outside the border");

    const EnginePrimitive& primitive = builtin("pill_plate");
    PrimitiveParameterBlock pill = default_parameters(primitive.descriptor());
    require(set_choice_parameter(pill, primitive.descriptor(), "shape", "pill") == ParameterAssignment::assigned,
            "the pill shape is chosen");
    const TestRaster round = draw(primitive, pill, plate_size, 1.0, {});
    require(round.pixel(2U, 2U)[3] == 0U && rest.pixel(2U, 2U)[3] == 255U, "the pill's ends are round");

    const TestRaster doubled = draw_default("pill_plate", plate_size, 2.0, {});
    require(pixel_is(doubled, 40U, 0U, {99, 132, 178, 255}) && pixel_is(doubled, 40U, 1U, {99, 132, 178, 255}),
            "at scale 2 the border is two device pixels");
    require(doubled.pixel(40U, 46U)[3] == 85U && doubled.pixel(40U, 47U)[3] == 85U,
            "and so is the contact shadow");
}

void test_gold_and_well() {
    const TestRaster gold = draw_default("gold_plate", plate_size, 1.0, {});
    require(pixel_is(gold, 20U, 0U, {196, 147, 50, 255}), "the gold plate's border is deep gold");
    require(gold.pixel(20U, 23U)[3] == 0U, "the gold plate is flat");
    const std::array<std::uint8_t, 4> face = gold.pixel(20U, 12U);
    require(face[0] == 255U && face[2] < face[1], "the gold face is warm");
    const TestRaster pressed = draw_default("gold_plate", plate_size, 1.0, one_state(AnatomyState::pressed));
    require(pressed.pixel(20U, 6U)[1] < gold.pixel(20U, 6U)[1], "pressed deepens the gold");

    const TestRaster well = draw_default("inset_well", plate_size, 1.0, {});
    require(pixel_is(well, 20U, 0U, {148, 172, 200, 255}), "the well's border");
    require(well.pixel(20U, 23U)[3] == 225U, "the well's outer bottom highlight");
    require(well.pixel(20U, 1U)[0] < well.pixel(20U, 12U)[0], "the well's inner top shade");
}

void test_lines_and_grip() {
    const TestRaster divider = draw_default("etched_divider", Size{2.0, 40.0}, 1.0, {});
    require(pixel_is(divider, 0U, 20U, {70, 83, 99, 125}), "the divider's shade line, premultiplied");
    require(pixel_is(divider, 1U, 20U, {235, 235, 235, 235}), "the divider's light line beside it");
    require(divider.pixel(0U, 0U)[3] < 20U && divider.pixel(0U, 39U)[3] < 20U, "the divider fades at its ends");
    const TestRaster across = draw_default("etched_divider", Size{40.0, 2.0}, 1.0, {});
    require(across.pixel(20U, 0U)[3] == 125U && across.pixel(20U, 1U)[3] == 235U, "a divider lying across");

    const TestRaster seam = draw_default("seam_line", Size{3.0, 30.0}, 1.0, {});
    require(seam.pixel(0U, 10U)[3] == 0U && pixel_is(seam, 1U, 10U, {148, 172, 200, 255}) &&
                seam.pixel(2U, 10U)[3] == 0U,
            "the seam is one centred line");

    const TestRaster grip = draw_default("grip", Size{10.0, 10.0}, 1.0, {});
    require(pixel_is(grip, 1U, 1U, {98, 121, 151, 255}) && pixel_is(grip, 2U, 2U, {98, 121, 151, 255}),
            "a grip dot is two units square, one unit in");
    require(grip.pixel(0U, 0U)[3] == 0U && grip.pixel(3U, 3U)[3] == 0U && grip.pixel(9U, 9U)[3] == 0U,
            "between and beyond the dots is clear");
    require(grip.pixel(5U, 5U)[3] == 255U && grip.pixel(5U, 1U)[3] == 255U, "the dots repeat every four units");
    const TestRaster doubled = draw_default("grip", Size{10.0, 10.0}, 2.0, {});
    require(doubled.pixel(2U, 2U)[3] == 255U && doubled.pixel(5U, 5U)[3] == 255U && doubled.pixel(6U, 6U)[3] == 0U,
            "at scale 2 a dot is four device pixels square");
}

// -- reference images ---------------------------------------------------------------

struct ReferenceCase final {
    const char* primitive;
    const char* variant;
    Size logical;
    AnatomyState state;
    bool stated;
    const char* shape;
};

constexpr Size tall_line{2.0, 40.0};

const std::array<ReferenceCase, 13> reference_cases{{
    {"pill_plate", "rest", plate_size, AnatomyState::normal, false, nullptr},
    {"pill_plate", "hot", plate_size, AnatomyState::hot, true, nullptr},
    {"pill_plate", "pressed", plate_size, AnatomyState::pressed, true, nullptr},
    {"pill_plate", "disabled", plate_size, AnatomyState::disabled, true, nullptr},
    {"pill_plate", "default", plate_size, AnatomyState::preselect, true, nullptr},
    {"pill_plate", "pill", plate_size, AnatomyState::normal, false, "pill"},
    {"pill_plate", "cap_start", plate_size, AnatomyState::normal, false, "cap_start"},
    {"gold_plate", "rest", plate_size, AnatomyState::normal, false, nullptr},
    {"gold_plate", "pressed", plate_size, AnatomyState::pressed, true, nullptr},
    {"inset_well", "rest", plate_size, AnatomyState::normal, false, nullptr},
    {"etched_divider", "upright", tall_line, AnatomyState::normal, false, nullptr},
    {"seam_line", "upright", Size{3.0, 30.0}, AnatomyState::normal, false, nullptr},
    {"grip", "rest", Size{10.0, 22.0}, AnatomyState::normal, false, nullptr},
}};

constexpr std::array<double, 4> reference_scales{1.0, 1.25, 1.5, 2.0};

// Expected hashes, [case][scale].
constexpr std::array<std::array<std::uint64_t, 4>, 13> expected_hashes{{
    {0x258500cfc89da7a2ULL, 0x17d9cc7df0883991ULL, 0x79b691953dfed5ccULL, 0x58bf89cd66628ff1ULL}, // pill_plate rest
    {0x7e3d04ad18b099beULL, 0x249bc460b03df223ULL, 0xbb7fd82ab3efd195ULL, 0xd29c3e459bebf8b3ULL}, // pill_plate hot
    {0x863c3615365a07d1ULL, 0xb577d5b9a8101395ULL, 0xc999b48d8f9ecff1ULL, 0x9ca36f6fa6839441ULL}, // pill_plate pressed
    {0xc5cccde599f725a5ULL, 0xff0fb80ce78c4909ULL, 0x7d69ed5805533dbdULL, 0x9a5fc42435c202a1ULL}, // pill_plate disabled
    {0xac424f12c2d26c44ULL, 0x58d8c0146b1eb943ULL, 0x906cc123b70c9155ULL, 0xcd7912709586b19aULL}, // pill_plate default
    {0x650c827725a3bcf1ULL, 0xc6ea396130a59a93ULL, 0x3ce1745ac757fd72ULL, 0xffecbf3779798151ULL}, // pill_plate pill
    {0x79a3c38a97024ffaULL, 0x79d98aacb1a01b86ULL, 0x276a2bb917f4ccc5ULL, 0x49e9d5e7667d799cULL}, // pill_plate cap_start
    {0xc12eac1b58916074ULL, 0xd3d4d014ff00f12eULL, 0x68352353c13ac0a9ULL, 0xf238123d1546fedcULL}, // gold_plate rest
    {0x6c3c7c3fc4d94761ULL, 0xa75336f05f1cd51aULL, 0x59a3c8c8071dc425ULL, 0x57d2ce1b2f34d6ddULL}, // gold_plate pressed
    {0x9a6615853ead2ba1ULL, 0xb581e2d8370c6eadULL, 0x58ce5a91ca6d7b15ULL, 0x1a9ac0ff1e69b74dULL}, // inset_well rest
    {0xe189d41b5cadee81ULL, 0x402ece9e1d982cfdULL, 0x52342a87b3ba3e56ULL, 0xcd6f587a8671a1b1ULL}, // etched_divider upright
    {0x434c6a1ef2042725ULL, 0xcf11602d22a1b7dcULL, 0x36a123200c60b3b5ULL, 0x5ac2eaf6f900aba5ULL}, // seam_line upright
    {0x497dbcdf94b71735ULL, 0x6be895a1b7da1a21ULL, 0x0f701f3ed2142381ULL, 0xe11213dc61445e65ULL}, // grip rest
}};

// GUI_FORMS_ENGINE_DUMP=<directory> writes each reference raster's pixels
// (rows of RGBA, no padding) to <primitive>_<variant>_<scale x 100>_<w>x<h>.rgba,
// for comparing them by eye with Plan Paint.
void dump_raster(const TestRaster& raster, const char* primitive, const char* variant, double scale) {
    const char* const directory = std::getenv("GUI_FORMS_ENGINE_DUMP");
    if (directory == nullptr) return;
    const int percent = static_cast<int>(scale * 100.0 + 0.5);
    const std::string path = std::string(directory) + "/" + primitive + "_" + variant + "_" + std::to_string(percent) +
                             "_" + std::to_string(raster.extent.width) + "x" + std::to_string(raster.extent.height) +
                             ".rgba";
    std::ofstream file(path, std::ios::binary);
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes;
        const std::size_t width_bytes = static_cast<std::size_t>(raster.extent.width) * 4U;
        const std::span<const std::uint8_t> row_pixels =
            std::span<const std::uint8_t>(raster.bytes).subspan(start, width_bytes);
        const std::vector<std::uint8_t> rgba = gui_forms::test_support::rgba_order(row_pixels);
        file.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }
}

void test_reference_images() {
    const bool printing = print_mode();
    bool all_match = true;
    for (std::size_t case_index = 0U; case_index < reference_cases.size(); ++case_index) {
        const ReferenceCase& reference = reference_cases[case_index];
        const EnginePrimitive& primitive = builtin(reference.primitive);
        PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
        if (reference.shape != nullptr) {
            require(set_choice_parameter(parameters, primitive.descriptor(), "shape", reference.shape) ==
                        ParameterAssignment::assigned,
                    "the case's shape is chosen");
        }
        AnatomyStateSet states{};
        if (reference.stated) states.insert(reference.state);
        if (printing) std::cout << "    {";
        for (std::size_t scale_index = 0U; scale_index < reference_scales.size(); ++scale_index) {
            const double scale = reference_scales[scale_index];
            const TestRaster raster = draw(primitive, parameters, reference.logical, scale, states);
            const std::uint64_t hash = raster_hash(raster);
            dump_raster(raster, reference.primitive, reference.variant, scale);
            if (printing) {
                std::cout << "0x" << std::hex << std::setw(16) << std::setfill('0') << hash << std::dec << "ULL";
                if (scale_index + 1U < reference_scales.size()) std::cout << ", ";
                continue;
            }
            if (hash != expected_hashes[case_index][scale_index]) {
                std::cerr << "reference image differs: " << reference.primitive << ' ' << reference.variant
                          << " at scale " << scale << ": 0x" << std::hex << hash << std::dec << '\n';
                all_match = false;
            }
        }
        if (printing) std::cout << "}, // " << reference.primitive << ' ' << reference.variant << '\n';
    }
    require(all_match, "every reference image has the same bytes on every host");
}

} // namespace

int main() {
    test_relief_primitives_register();
    test_choice_parameters();
    test_shape_choices_compile();
    test_shapes();
    test_hit_regions();
    test_engine_hit_region();
    test_pill_plate_layers();
    test_gold_and_well();
    test_lines_and_grip();
    test_reference_images();
    std::cout << "engine relief tests passed\n";
    return 0;
}
