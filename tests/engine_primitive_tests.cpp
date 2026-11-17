// The engine's primitive registry and its first primitives (T2 plan, phase
// E1): registration, parameter and preparation checks refuse without
// changing anything, a destination is validated before any byte is written,
// and the ribbon's bulge and dithered bulge (notes:planning/stages/T4-default-theme/
// RENDERING_REFERENCE.md §2, §3, with the T2 round 001 decisions G1-3 and
// G3-3) draw what the reference describes, with the same bytes on every CI
// platform.
//
// The reference images are FNV-1a hashes of each raster's rows in RGBA
// order. The expected values were produced on Windows x64 and are checked
// by every CI platform. GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the
// observed table instead of checking it.

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "support/rgba_order.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
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

// A raster the tests own, with a row stride wider than its pixels so that
// padding is exercised.
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

constexpr std::uint8_t padding_byte = 0xA5;

TestRaster make_raster(Size logical, double scale) {
    const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical.width * scale));
    const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical.height * scale));
    TestRaster raster{};
    raster.extent = DeviceExtent{width, height};
    raster.row_bytes = static_cast<std::size_t>(width) * 4U + 8U;
    raster.bytes.assign(raster.row_bytes * height, padding_byte);
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

bool padding_untouched(const TestRaster& raster) {
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes;
        const std::size_t padding = start + static_cast<std::size_t>(raster.extent.width) * 4U;
        for (std::size_t index = padding; index < start + raster.row_bytes; ++index) {
            if (raster.bytes[index] != padding_byte) return false;
        }
    }
    return true;
}

AnatomyStateSet states_of(bool pressed, bool active) {
    AnatomyStateSet states{};
    if (pressed) states.insert(AnatomyState::pressed);
    if (active) states.insert(AnatomyState::active_tool);
    return states;
}

// Prepares, then rasterizes: what a part drawn once does.
RasterOutcome draw(const EnginePrimitive& primitive, const PrimitiveParameterBlock& parameters, TestRaster& raster,
                   Size logical, double scale, AnatomyStateSet states) {
    const PrimitiveTarget target{logical, DeviceScale{scale}, states};
    PrimitivePreparation preparation{};
    require(primitive.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "the part prepares");
    const RasterOutcome outcome = primitive.rasterize(parameters, target, preparation, raster.view());
    return outcome;
}

PrimitiveParameterBlock with_amplitude(const EnginePrimitive& primitive, double amplitude) {
    PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
    require(set_number_parameter(parameters, primitive.descriptor(), "amplitude", amplitude) ==
                ParameterAssignment::assigned,
            "the amplitude is assigned");
    return parameters;
}

std::uint32_t rgb_of(const std::array<std::uint8_t, 4>& pixel) {
    const std::uint32_t rgb = (static_cast<std::uint32_t>(pixel[0]) << 16U) |
                              (static_cast<std::uint32_t>(pixel[1]) << 8U) | pixel[2];
    return rgb;
}

// The distinct colours of a raster's visible pixels.
std::set<std::uint32_t> dot_colours(const TestRaster& raster) {
    std::set<std::uint32_t> colours{};
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
            const std::array<std::uint8_t, 4> pixel = raster.pixel(column, row);
            if (pixel[3] == 0U) continue;
            colours.insert(rgb_of(pixel));
        }
    }
    return colours;
}

constexpr Size large_button{40.0, 56.0};

// -- registry ------------------------------------------------------------------

// A primitive whose descriptor the tests choose.
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
const PrimitiveParameterDeclaration one_number[] = {{"strength", PrimitiveParameterKind::number, 0.0, 1.0, 0.5, {}}};
const PrimitiveParameterDeclaration bad_default[] = {{"strength", PrimitiveParameterKind::number, 0.0, 1.0, 2.0, {}}};
const PrimitiveParameterDeclaration bad_name[] = {{"Strength", PrimitiveParameterKind::number, 0.0, 1.0, 0.5, {}}};
const PrimitiveParameterDeclaration twice[] = {{"strength", PrimitiveParameterKind::number, 0.0, 1.0, 0.5, {}},
                                               {"strength", PrimitiveParameterKind::number, 0.0, 1.0, 0.5, {}}};

void test_builtin_registry() {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    require(registry.primitives().size() == 12U,
            "the built-in registry holds the bulge pair, the relief set, the material, the overlay shadow, the sheen and the glass");
    require(registry.find("not_a_primitive") == nullptr, "an unknown name finds nothing");
    const char* const names[] = {"bulge", "dithered_bulge"};
    for (const char* const name : names) {
        const PrimitiveDescriptor& descriptor = builtin(name).descriptor();
        require(descriptor.name == name, "a primitive is found under its own name");
        require(descriptor.roles.size() == 1U && descriptor.roles[0] == PartRole::face, "the bulge paints faces");
        std::set<AnatomyState> read(descriptor.states_read.begin(), descriptor.states_read.end());
        require(read.size() == 2U && read.count(AnatomyState::pressed) == 1U &&
                    read.count(AnatomyState::active_tool) == 1U,
                "the bulge reads pressed and active_tool only");
        require(descriptor.introduced == EngineVersion{0, 1}, "the bulge pair is engine 0.1");
        require(descriptor.cost.per_device_pixel > 0U, "the bulge declares its cost");
        const PrimitiveParameterBlock defaults = default_parameters(descriptor);
        require(validate_parameters(descriptor, defaults).defect == ParameterDefect::none, "defaults validate");
    }
}

void test_registration_refusals() {
    PrimitiveRegistry registry{};
    const PrimitiveDescriptor good{"fixture", EngineVersion{0, 1}, face_role, {}, one_number, PrimitiveCost{1U, 1U}, {}};
    const FixturePrimitive first{good};
    require(registry.register_primitive(first) == RegistrationOutcome::registered, "a valid primitive registers");
    const FixturePrimitive again{good};
    require(registry.register_primitive(again) == RegistrationOutcome::duplicate_name, "a duplicate name is refused");
    require(registry.primitives().size() == 1U, "a refusal leaves the registry unchanged");

    PrimitiveDescriptor descriptor = good;
    descriptor.name = "Fixture";
    const FixturePrimitive upper{descriptor};
    require(registry.register_primitive(upper) == RegistrationOutcome::name_not_snake_case, "names are snake_case");

    descriptor = good;
    descriptor.name = "no_roles";
    descriptor.roles = {};
    const FixturePrimitive roleless{descriptor};
    require(registry.register_primitive(roleless) == RegistrationOutcome::no_roles, "a primitive paints a role");

    descriptor = good;
    descriptor.name = "late";
    descriptor.introduced = EngineVersion{current_engine_version.major, static_cast<std::uint16_t>(current_engine_version.minor + 1U)};
    const FixturePrimitive late{descriptor};
    require(registry.register_primitive(late) == RegistrationOutcome::introduced_after_engine,
            "a primitive from a later engine is refused");

    descriptor = good;
    descriptor.name = "bad_default";
    descriptor.parameters = bad_default;
    const FixturePrimitive out_of_range{descriptor};
    require(registry.register_primitive(out_of_range) == RegistrationOutcome::default_out_of_range,
            "a default outside its range is refused");

    descriptor = good;
    descriptor.name = "bad_parameter";
    descriptor.parameters = bad_name;
    const FixturePrimitive misnamed{descriptor};
    require(registry.register_primitive(misnamed) == RegistrationOutcome::parameter_name_not_snake_case,
            "parameter names are snake_case");

    descriptor = good;
    descriptor.name = "twice";
    descriptor.parameters = twice;
    const FixturePrimitive duplicated{descriptor};
    require(registry.register_primitive(duplicated) == RegistrationOutcome::duplicate_parameter_name,
            "a parameter name is declared once");

    descriptor = good;
    descriptor.name = "greedy";
    descriptor.prepared_values = maximum_prepared_values + 1U;
    const FixturePrimitive greedy{descriptor};
    require(registry.register_primitive(greedy) == RegistrationOutcome::too_many_prepared_values,
            "a preparation is bounded");
    require(registry.primitives().size() == 1U, "every refusal left the registry unchanged");
}

void test_parameter_refusals() {
    const PrimitiveDescriptor& descriptor = builtin("bulge").descriptor();
    PrimitiveParameterBlock block = default_parameters(descriptor);
    const PrimitiveParameterBlock before = block;
    require(set_number_parameter(block, descriptor, "amplitude", 1.5) == ParameterAssignment::out_of_range,
            "an out-of-range number is refused");
    require(set_number_parameter(block, descriptor, "amplitude", std::nan("")) == ParameterAssignment::not_finite,
            "NaN is refused");
    require(set_number_parameter(block, descriptor, "light_colour", 0.5) == ParameterAssignment::wrong_kind,
            "a number is not a colour");
    require(set_colour_parameter(block, descriptor, "amplitude", Color{}) == ParameterAssignment::wrong_kind,
            "a colour is not a number");
    require(set_number_parameter(block, descriptor, "glow", 0.5) == ParameterAssignment::unknown_name,
            "an undeclared name is refused");
    bool unchanged = true;
    for (std::size_t index = 0U; index < block.count; ++index) {
        if (!(block.values[index] == before.values[index])) unchanged = false;
    }
    require(unchanged, "refused assignments leave the block unchanged");
    require(set_number_parameter(block, descriptor, "amplitude", 0.25) == ParameterAssignment::assigned,
            "an in-range number is assigned");

    PrimitiveParameterBlock short_block = block;
    short_block.count = block.count - 1U;
    require(validate_parameters(descriptor, short_block).defect == ParameterDefect::wrong_count,
            "a block for another primitive is refused");
    PrimitiveParameterBlock forged = block;
    forged.values[0].number = 3.0;
    const ParameterValidation validation = validate_parameters(descriptor, forged);
    require(validation.defect == ParameterDefect::out_of_range && validation.parameter == 0U,
            "validation names the parameter out of range");
}

// -- destination ---------------------------------------------------------------

void test_destination_refusals() {
    const EnginePrimitive& bulge = builtin("bulge");
    const PrimitiveParameterBlock defaults = default_parameters(bulge.descriptor());
    const Size logical{20.0, 10.0};

    TestRaster raster = make_raster(logical, 1.0);
    const std::vector<std::uint8_t> before = raster.bytes;
    const PrimitiveTarget target{logical, DeviceScale{1.0}, {}};
    PrimitivePreparation preparation{};
    require(bulge.prepare(defaults, target, preparation) == PreparationOutcome::prepared, "the bulge prepares");
    PartRasterView view = raster.view();
    view.pixels = view.pixels.first(view.pixels.size() - 9U);
    require(bulge.rasterize(defaults, target, preparation, view) == RasterOutcome::invalid_view,
            "a short buffer is refused");
    view = raster.view();
    view.row_bytes = 20U * 4U - 4U;
    require(bulge.rasterize(defaults, target, preparation, view) == RasterOutcome::invalid_view,
            "a short stride is refused");
    view.row_bytes = 20U * 4U + 2U;
    require(bulge.rasterize(defaults, target, preparation, view) == RasterOutcome::invalid_view,
            "a split-pixel stride is refused");
    view = raster.view();
    view.extent = DeviceExtent{maximum_part_raster_side + 1U, 1U};
    require(bulge.rasterize(defaults, target, preparation, view) == RasterOutcome::invalid_view,
            "an oversized extent is refused");

    const PrimitiveTarget no_scale{logical, DeviceScale{0.25}, {}};
    require(bulge.rasterize(defaults, no_scale, preparation, raster.view()) == RasterOutcome::invalid_target,
            "a scale below 0.5 is refused");
    const PrimitiveTarget no_size{Size{0.0, 10.0}, DeviceScale{1.0}, {}};
    require(bulge.rasterize(defaults, no_size, preparation, raster.view()) == RasterOutcome::invalid_target,
            "an empty part is refused");
    PrimitiveParameterBlock forged = defaults;
    forged.values[1].number = 0.0;
    require(bulge.rasterize(forged, target, preparation, raster.view()) == RasterOutcome::invalid_parameters,
            "invalid parameters are refused");
    require(raster.bytes == before, "every refusal left the destination's bytes unchanged");

    view = raster.view();
    view.extent = DeviceExtent{0U, 0U};
    require(bulge.rasterize(defaults, target, preparation, view) == RasterOutcome::empty,
            "an empty destination paints nothing");
    require(raster.bytes == before, "an empty destination is not written");
}

// -- preparations --------------------------------------------------------------

bool same_preparation(const PrimitivePreparation& left, const PrimitivePreparation& right) {
    if (left.primitive != right.primitive || left.count != right.count) return false;
    if (!(left.logical_size == right.logical_size) || !(left.scale == right.scale)) return false;
    if (!(left.states_read == right.states_read) || left.parameters.count != right.parameters.count) return false;
    for (std::size_t index = 0U; index < left.parameters.count; ++index) {
        if (!(left.parameters.values[index] == right.parameters.values[index])) return false;
    }
    for (std::size_t index = 0U; index < left.count; ++index) {
        if (left.values[index] != right.values[index]) return false;
    }
    return true;
}

void test_preparation_refusals() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PrimitiveDescriptor& descriptor = dithered.descriptor();
    require(descriptor.prepared_values == 3U, "the dither derives its three extremes");
    require(builtin("bulge").descriptor().prepared_values == 0U, "the smooth bulge derives nothing");
    const stxlib::Optional<std::size_t> amplitude = find_parameter(descriptor, "amplitude");
    require(amplitude.present() && descriptor.parameters[(*amplitude.value())].animated, "the amplitude is animated");
    const PrimitiveParameterBlock defaults = default_parameters(descriptor);
    const PrimitiveTarget target{large_button, DeviceScale{1.0}, {}};

    PrimitivePreparation preparation{};
    require(dithered.prepare(defaults, target, preparation) == PreparationOutcome::prepared, "the dither prepares");
    require(preparation.primitive == &dithered && preparation.count == 3U, "the preparation names its primitive");
    require(preparation.values[0] > 0.0 && preparation.values[1] > 0.0 && preparation.values[2] > 0.0,
            "the light default has a lit side, a shaded side and a crown");

    // The extremes are taken at amplitude 1 whatever the amplitude is.
    PrimitivePreparation at_half{};
    require(dithered.prepare(with_amplitude(dithered, 0.5), target, at_half) == PreparationOutcome::prepared,
            "the dither prepares at half amplitude");
    require(at_half.values == preparation.values, "the extremes do not depend on the amplitude");

    const PrimitivePreparation before = preparation;
    const PrimitiveTarget no_scale{large_button, DeviceScale{9.0}, {}};
    require(dithered.prepare(defaults, no_scale, preparation) == PreparationOutcome::invalid_target,
            "a scale above 8 is refused");
    PrimitiveParameterBlock forged = defaults;
    forged.values[0].number = 2.0;
    require(dithered.prepare(forged, target, preparation) == PreparationOutcome::invalid_parameters,
            "invalid parameters are refused");
    require(same_preparation(preparation, before), "a refused preparation is left unchanged");

    TestRaster raster = make_raster(large_button, 1.0);
    const std::vector<std::uint8_t> blank = raster.bytes;
    const PrimitivePreparation unprepared{};
    require(dithered.rasterize(defaults, target, unprepared, raster.view()) == RasterOutcome::stale_preparation,
            "an unprepared part is refused");
    const EnginePrimitive& bulge = builtin("bulge");
    PrimitivePreparation smooth{};
    require(bulge.prepare(default_parameters(bulge.descriptor()), target, smooth) == PreparationOutcome::prepared,
            "the smooth bulge prepares");
    require(dithered.rasterize(defaults, target, smooth, raster.view()) == RasterOutcome::stale_preparation,
            "another primitive's preparation is refused");
    const PrimitiveTarget wider{Size{42.0, 56.0}, DeviceScale{1.0}, {}};
    require(dithered.rasterize(defaults, wider, preparation, raster.view()) == RasterOutcome::stale_preparation,
            "a new size makes the preparation stale");
    const PrimitiveTarget rescaled{large_button, DeviceScale{1.25}, {}};
    require(dithered.rasterize(defaults, rescaled, preparation, raster.view()) == RasterOutcome::stale_preparation,
            "a new scale makes the preparation stale");
    const PrimitiveTarget pressed{large_button, DeviceScale{1.0}, states_of(true, false)};
    require(dithered.rasterize(defaults, pressed, preparation, raster.view()) == RasterOutcome::stale_preparation,
            "a new read state makes the preparation stale");
    PrimitiveParameterBlock regained = defaults;
    require(set_number_parameter(regained, descriptor, "shade_gain", 1.0) == ParameterAssignment::assigned,
            "a gain is changed");
    require(dithered.rasterize(regained, target, preparation, raster.view()) == RasterOutcome::stale_preparation,
            "a new gain makes the preparation stale");
    require(raster.bytes == blank, "a stale preparation writes nothing");

    // Neither the amplitude, which the rise animates, nor a state the
    // primitive does not read, makes the preparation stale.
    AnatomyStateSet hot{};
    hot.insert(AnatomyState::hot);
    const PrimitiveTarget hot_target{large_button, DeviceScale{1.0}, hot};
    require(dithered.rasterize(with_amplitude(dithered, 0.5), hot_target, preparation, raster.view()) ==
                RasterOutcome::painted,
            "the rise reuses one preparation");
    require(padding_untouched(raster), "row padding is never written");
}

// -- what the bulge draws ------------------------------------------------------

void test_amplitude_zero_is_the_plane() {
    const char* const names[] = {"bulge", "dithered_bulge"};
    for (const char* const name : names) {
        const EnginePrimitive& primitive = builtin(name);
        PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
        require(set_number_parameter(parameters, primitive.descriptor(), "amplitude", 0.0) ==
                    ParameterAssignment::assigned,
                "amplitude set to zero");
        const bool variants[] = {false, true};
        for (const bool pressed : variants) {
            for (const bool active : variants) {
                TestRaster raster = make_raster(large_button, 1.5);
                require(draw(primitive, parameters, raster, large_button, 1.5, states_of(pressed, active)) ==
                            RasterOutcome::painted,
                        "the flat bulge paints");
                bool transparent = true;
                for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
                    for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
                        if (raster.pixel(column, row)[3] != 0U) transparent = false;
                    }
                }
                require(transparent, "at amplitude 0 the plane shows everywhere");
                require(padding_untouched(raster), "row padding is never written");
            }
        }
    }
}

void test_smooth_bulge_is_premultiplied_and_edgeless() {
    const EnginePrimitive& bulge = builtin("bulge");
    const PrimitiveParameterBlock parameters = default_parameters(bulge.descriptor());
    TestRaster raster = make_raster(large_button, 2.0);
    require(draw(bulge, parameters, raster, large_button, 2.0, {}) == RasterOutcome::painted, "the bulge paints");
    bool premultiplied = true;
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
            const std::array<std::uint8_t, 4> pixel = raster.pixel(column, row);
            if (pixel[0] > pixel[3] || pixel[1] > pixel[3] || pixel[2] > pixel[3]) premultiplied = false;
        }
    }
    require(premultiplied, "no channel exceeds alpha");
    // Every field is zero at the top-left corner: the plane shows there.
    require(raster.pixel(0U, 0U)[3] == 0U, "the top-left corner is the plane");
    // The lit slope is white and the shaded one blue-grey: unpremultiplied,
    // the pixel at the light's centre is redder (whiter) than the one at the
    // shade's centre.
    const std::array<std::uint8_t, 4> lit = raster.pixel(static_cast<std::uint32_t>(0.42 * 80.0),
                                                         static_cast<std::uint32_t>(0.38 * 112.0));
    const std::array<std::uint8_t, 4> shaded = raster.pixel(static_cast<std::uint32_t>(0.60 * 80.0),
                                                            static_cast<std::uint32_t>(0.64 * 112.0));
    require(lit[3] > 0U && shaded[3] > 0U, "both slopes cover the plane");
    const double lit_red = static_cast<double>(lit[0]) / static_cast<double>(lit[3]);
    const double shaded_red = static_cast<double>(shaded[0]) / static_cast<double>(shaded[3]);
    require(lit_red > shaded_red, "the lit slope is whiter than the shaded slope");
    require(padding_untouched(raster), "row padding is never written");
}

struct DotCounts final {
    std::size_t lit_upper_left{};
    std::size_t lit_lower_right{};
};

bool is_lit_dot(const std::array<std::uint8_t, 4>& pixel) {
    // The default lit steps are near white; the shaded steps are blue.
    const bool lit = pixel[3] == 255U && pixel[0] >= 236U;
    return lit;
}

DotCounts count_lit_dots(const TestRaster& raster) {
    DotCounts counts{};
    const std::uint32_t half_width = raster.extent.width / 2U;
    const std::uint32_t half_height = raster.extent.height / 2U;
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
            if (!is_lit_dot(raster.pixel(column, row))) continue;
            if (column < half_width && row < half_height) counts.lit_upper_left = counts.lit_upper_left + 1U;
            if (column >= half_width && row >= half_height) counts.lit_lower_right = counts.lit_lower_right + 1U;
        }
    }
    return counts;
}

void test_dithered_lattice_and_tones() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PrimitiveParameterBlock parameters = default_parameters(dithered.descriptor());
    TestRaster raster = make_raster(large_button, 1.0);
    require(draw(dithered, parameters, raster, large_button, 1.0, {}) == RasterOutcome::painted, "the dither paints");
    bool on_lattice = true;
    bool opaque_or_clear = true;
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
            const std::array<std::uint8_t, 4> pixel = raster.pixel(column, row);
            if (pixel[3] == 0U) continue;
            if (pixel[3] != 255U) opaque_or_clear = false;
            if (column % 2U != 1U || row % 2U != 1U) on_lattice = false;
        }
    }
    require(on_lattice, "at scale 1 dots fall only on odd positions");
    require(opaque_or_clear, "a dot is opaque; between dots the plane shows");

    const DotCounts bulge = count_lit_dots(raster);
    require(bulge.lit_upper_left > bulge.lit_lower_right, "the bulge is lit toward the upper-left light");
    TestRaster dent_raster = make_raster(large_button, 1.0);
    require(draw(dithered, parameters, dent_raster, large_button, 1.0, states_of(true, false)) == RasterOutcome::painted,
            "the dent paints");
    const DotCounts dent = count_lit_dots(dent_raster);
    require(dent.lit_lower_right > dent.lit_upper_left, "the dent is lit on the far slope");
}

// GIVEN G1-3: each side of the tone is rescaled to the part's own extreme.
// Before it, the light default's tone never fell below about −0.63, short
// of the −0.68 the third shaded step needed; now the darkest dots of each
// side reach the third step, at every reference scale, bulge or dent.
void test_rescaled_light_default_uses_every_step() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PrimitiveParameterBlock parameters = default_parameters(dithered.descriptor());
    const std::uint32_t light_default[] = {0xECF4FEU, 0xF6FAFFU, 0xFFFFFFU, 0xB0C6E8U, 0x84A2D4U, 0x587ABAU};
    const double scales[] = {1.0, 1.25, 1.5, 2.0};
    const bool variants[] = {false, true};
    for (const double scale : scales) {
        for (const bool pressed : variants) {
            TestRaster raster = make_raster(large_button, scale);
            require(draw(dithered, parameters, raster, large_button, scale, states_of(pressed, false)) ==
                        RasterOutcome::painted,
                    "the dither paints");
            const std::set<std::uint32_t> colours = dot_colours(raster);
            for (const std::uint32_t colour : light_default) {
                require(colours.count(colour) == 1U, "every lit and shaded step of the light default appears");
            }
            require(colours.size() == 6U, "only the palette's colours appear");
        }
    }

    // Gold reaches all three steps of its lit and shaded colours, and shows
    // its first body colour.
    TestRaster gold = make_raster(large_button, 1.0);
    require(draw(dithered, parameters, gold, large_button, 1.0, states_of(false, true)) == RasterOutcome::painted,
            "the gold dither paints");
    const std::set<std::uint32_t> gold_colours = dot_colours(gold);
    const std::uint32_t gold_steps[] = {0xFFEAB0U, 0xFFF4D4U, 0xFFFCF0U, 0xE8AC3EU, 0xCE8E28U, 0xAA7018U, 0xFACD68U};
    for (const std::uint32_t colour : gold_steps) {
        require(gold_colours.count(colour) == 1U, "every gold lit and shaded step appears, and the gold body");
    }
}

// The dark variant's gains reach all three steps of both sides (they did
// before the rescaling too).
void apply_dark_palette(PrimitiveParameterBlock& parameters, const PrimitiveDescriptor& descriptor);

void test_dark_variant_reaches_every_step() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    PrimitiveParameterBlock parameters = default_parameters(dithered.descriptor());
    apply_dark_palette(parameters, dithered.descriptor());
    TestRaster raster = make_raster(large_button, 1.0);
    require(draw(dithered, parameters, raster, large_button, 1.0, {}) == RasterOutcome::painted, "the dark dither paints");
    const std::set<std::uint32_t> colours = dot_colours(raster);
    require(colours.count(0x0E120FU) == 1U && colours.count(0x080A09U) == 1U && colours.count(0x000000U) == 1U,
            "all three dark shaded steps appear");
    require(colours.count(0x344238U) == 1U && colours.count(0x4E6252U) == 1U && colours.count(0x76966CU) == 1U,
            "all three dark lit steps appear");
}

// Which pixels of a raster are visible, row by row.
std::vector<bool> visible_pixels(const TestRaster& raster) {
    std::vector<bool> visible{};
    visible.reserve(static_cast<std::size_t>(raster.extent.width) * raster.extent.height);
    for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
        for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
            visible.push_back(raster.pixel(column, row)[3] != 0U);
        }
    }
    return visible;
}

std::size_t count_visible(const std::vector<bool>& visible) {
    std::size_t count = 0U;
    for (const bool shown : visible) {
        if (shown) count = count + 1U;
    }
    return count;
}

// Amplitude 0 draws no dots, and as the amplitude rises every dot already
// shown stays shown, so the dot count only grows: the rise grows the dither
// rather than popping it, because the extremes are taken at amplitude 1.
void test_dot_count_grows_with_amplitude() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    struct Variant final {
        bool pressed;
        bool active;
        bool dark;
    };
    const Variant variants[] = {{false, false, false}, {true, false, false}, {false, false, true}, {false, true, false}};
    for (const Variant& variant : variants) {
        const AnatomyStateSet states = states_of(variant.pressed, variant.active);
        std::vector<bool> previous{};
        std::size_t previous_count = 0U;
        std::size_t half_count = 0U;
        // Eighths are exact in binary, so the steps are the amplitudes named.
        for (std::size_t eighth = 0U; eighth <= 8U; ++eighth) {
            const double amplitude = static_cast<double>(eighth) / 8.0;
            PrimitiveParameterBlock parameters = with_amplitude(dithered, amplitude);
            if (variant.dark) apply_dark_palette(parameters, dithered.descriptor());
            TestRaster raster = make_raster(large_button, 1.0);
            require(draw(dithered, parameters, raster, large_button, 1.0, states) == RasterOutcome::painted,
                    "the dither paints");
            const std::vector<bool> visible = visible_pixels(raster);
            const std::size_t count = count_visible(visible);
            if (eighth == 0U) require(count == 0U, "amplitude 0 draws no dots");
            if (eighth == 4U) half_count = count;
            if (eighth > 0U) {
                bool kept = true;
                for (std::size_t index = 0U; index < visible.size(); ++index) {
                    if (previous[index] && !visible[index]) kept = false;
                }
                require(kept, "a dot shown at a lower amplitude stays shown at a higher one");
                require(count >= previous_count, "the dot count never falls as the amplitude rises");
            }
            previous = visible;
            previous_count = count;
        }
        require(half_count > 0U, "half amplitude draws dots");
        // Gold shows its first body colour wherever the tone is negative at
        // any amplitude above 0 (reference §3), so only the plain variants
        // must grow strictly between half and full amplitude.
        if (!variant.active) require(previous_count > half_count, "full amplitude draws more dots than half");
    }
}

// The magnification of `small` by an integer `factor`: device pixel (c, r)
// of the larger raster shows pixel (c / factor, r / factor) of the smaller.
bool magnifies(const TestRaster& large, const TestRaster& small, std::uint32_t factor) {
    for (std::uint32_t row = 0U; row < large.extent.height; ++row) {
        for (std::uint32_t column = 0U; column < large.extent.width; ++column) {
            const std::uint32_t small_row = row / factor;
            const std::uint32_t small_column = column / factor;
            if (small_row >= small.extent.height || small_column >= small.extent.width) return false;
            if (large.pixel(column, row) != small.pixel(small_column, small_row)) return false;
        }
    }
    return true;
}

// GIVEN G3-3 at integer scales is the studies' rule exactly: 1-logical-px
// dots at odd logical positions every 2 logical px, floor(w / 2) of them,
// each covering s × s device px at device (2n + 1)·s, sampled at the logical
// positions themselves. Under that rule the raster at an integer scale s is
// the scale-1 raster magnified by s, and the scale-1 raster has dots only at
// odd positions inside 2·floor(w / 2) (checked here and in
// test_dithered_lattice_and_tones).
void test_integer_scales_reproduce_the_studies_lattice() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const Size sizes[] = {large_button, Size{41.0, 57.0}, Size{40.5, 56.25}, Size{13.0, 9.0}};
    struct Variant final {
        bool pressed;
        bool active;
        bool dark;
        double amplitude;
    };
    const Variant variants[] = {{false, false, false, 1.0}, {true, false, false, 1.0}, {false, true, false, 1.0},
                                {false, false, true, 1.0},  {false, false, false, 0.5}, {false, true, false, 0.5}};
    const std::uint32_t factors[] = {2U, 3U, 4U};
    for (const Size size : sizes) {
        for (const Variant& variant : variants) {
            PrimitiveParameterBlock parameters = with_amplitude(dithered, variant.amplitude);
            if (variant.dark) apply_dark_palette(parameters, dithered.descriptor());
            const AnatomyStateSet states = states_of(variant.pressed, variant.active);
            TestRaster unit = make_raster(size, 1.0);
            require(draw(dithered, parameters, unit, size, 1.0, states) == RasterOutcome::painted, "scale 1 paints");
            const std::uint32_t lattice_end = 2U * static_cast<std::uint32_t>(std::floor(size.width / 2.0));
            const std::uint32_t lattice_bottom = 2U * static_cast<std::uint32_t>(std::floor(size.height / 2.0));
            bool inside = true;
            for (std::uint32_t row = 0U; row < unit.extent.height; ++row) {
                for (std::uint32_t column = 0U; column < unit.extent.width; ++column) {
                    if (unit.pixel(column, row)[3] == 0U) continue;
                    if (column % 2U != 1U || row % 2U != 1U) inside = false;
                    if (column >= lattice_end || row >= lattice_bottom) inside = false;
                }
            }
            require(inside, "at scale 1 every dot is at an odd position of the studies' lattice");
            for (const std::uint32_t factor : factors) {
                const double scale = static_cast<double>(factor);
                TestRaster magnified = make_raster(size, scale);
                require(draw(dithered, parameters, magnified, size, scale, states) == RasterOutcome::painted,
                        "an integer scale paints");
                require(magnifies(magnified, unit, factor), "an integer scale is the scale-1 lattice magnified");
            }
        }
    }
}

// GIVEN G3-3 at every scale: k = max(1, floor(s + 0.5)); every dot is a
// k × k block of one colour whose corner is at device (2n + 1)·k on each
// axis, and there are floor(w·s / 2k) dot columns and rows for the height.
void test_lattice_follows_the_nearest_integer_scale() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PrimitiveParameterBlock parameters = default_parameters(dithered.descriptor());
    const double scales[] = {0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0};
    for (const double scale : scales) {
        const std::uint32_t side = static_cast<std::uint32_t>(std::max(1.0, std::floor(scale + 0.5)));
        const std::uint32_t period = 2U * side;
        const std::uint32_t columns = static_cast<std::uint32_t>(std::floor(large_button.width * scale / period));
        const std::uint32_t rows = static_cast<std::uint32_t>(std::floor(large_button.height * scale / period));
        TestRaster raster = make_raster(large_button, scale);
        require(draw(dithered, parameters, raster, large_button, scale, {}) == RasterOutcome::painted,
                "the dither paints");
        bool on_lattice = true;
        bool whole_dots = true;
        std::uint32_t last_column = 0U;
        std::uint32_t last_row = 0U;
        for (std::uint32_t row = 0U; row < raster.extent.height; ++row) {
            for (std::uint32_t column = 0U; column < raster.extent.width; ++column) {
                const std::array<std::uint8_t, 4> pixel = raster.pixel(column, row);
                if (pixel[3] == 0U) continue;
                const std::uint32_t lattice_column = column / side;
                const std::uint32_t lattice_row = row / side;
                if (lattice_column % 2U != 1U || lattice_row % 2U != 1U) on_lattice = false;
                const std::uint32_t dot_column = lattice_column / 2U;
                const std::uint32_t dot_row = lattice_row / 2U;
                if (dot_column >= columns || dot_row >= rows) on_lattice = false;
                last_column = std::max(last_column, dot_column);
                last_row = std::max(last_row, dot_row);
                const std::uint32_t corner_column = lattice_column * side;
                const std::uint32_t corner_row = lattice_row * side;
                if (pixel != raster.pixel(corner_column, corner_row)) whole_dots = false;
            }
        }
        require(on_lattice, "every dot is on the period-2k lattice inside floor(w·s / 2k) dots");
        require(whole_dots, "every dot is one k × k block of one colour");
        // The bulge's light field is zero along the box's edges, but its
        // shaded slope reaches the last column and row of dots.
        require(last_column + 1U == columns && last_row + 1U == rows, "the lattice spans the part");
    }
}

// The integer-scale lattice against the code it replaced. Before GIVEN G1-3
// and G3-3, #161 quantized the tone unscaled and placed dots at odd logical
// positions; its reference hashes at scales 1 and 2 are kept below. Forcing
// the preparation's three extremes to 1 turns the rescaled rule back into
// the unscaled one (a·t / 1 is a·t), so the new lattice must then give the
// old bytes exactly at integer scales. A caller never writes a preparation's
// values; this test does so only to switch the rescaling off.
struct OldReferenceCase final {
    const char* variant;
    bool pressed;
    bool active;
    double amplitude;
    bool dark;
    std::uint64_t scale_1;
    std::uint64_t scale_2;
};

constexpr std::array<OldReferenceCase, 5> old_reference_cases{{
    {"rest", false, false, 1.0, false, 0x53cca0febc6ccef7ULL, 0xe7555604ef50bcf5ULL},
    {"pressed", true, false, 1.0, false, 0x5ecafcd3609907c7ULL, 0x64067dc3a4ee299dULL},
    {"active", false, true, 1.0, false, 0xad170a954e2e3b4cULL, 0xd840f9e4182893d5ULL},
    {"half", false, false, 0.5, false, 0x2c3fc566a9e7e9d2ULL, 0x82b6a73be196ccadULL},
    {"dark", false, false, 1.0, true, 0x5208613e5af59721ULL, 0x30da795d91c6d9c5ULL},
}};

void test_integer_scales_reproduce_the_old_reference_images() {
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    for (const OldReferenceCase& reference : old_reference_cases) {
        PrimitiveParameterBlock parameters = with_amplitude(dithered, reference.amplitude);
        if (reference.dark) apply_dark_palette(parameters, dithered.descriptor());
        const AnatomyStateSet states = states_of(reference.pressed, reference.active);
        const double scales[] = {1.0, 2.0};
        for (const double scale : scales) {
            const PrimitiveTarget target{large_button, DeviceScale{scale}, states};
            PrimitivePreparation unscaled{};
            require(dithered.prepare(parameters, target, unscaled) == PreparationOutcome::prepared,
                    "the dither prepares");
            for (std::size_t index = 0U; index < unscaled.count; ++index) unscaled.values[index] = 1.0;
            TestRaster raster = make_raster(large_button, scale);
            require(dithered.rasterize(parameters, target, unscaled, raster.view()) == RasterOutcome::painted,
                    "the unscaled dither paints");
            const std::uint64_t expected = scale == 1.0 ? reference.scale_1 : reference.scale_2;
            if (raster_hash(raster) != expected) {
                std::cerr << "the integer-scale lattice differs from the old rule: " << reference.variant
                          << " at scale " << scale << '\n';
                std::exit(1);
            }
        }
    }
}

// -- reference images ----------------------------------------------------------

struct ReferenceCase final {
    const char* primitive;
    const char* variant;
    bool pressed;
    bool active;
    double amplitude;
    bool dark;
};

// Amplitudes 0, 0.5 and 1 for each form; the rise passes through all of
// them (reference §2, "Hover is the rise").
constexpr std::array<ReferenceCase, 13> reference_cases{{
    {"bulge", "rest", false, false, 1.0, false},
    {"bulge", "pressed", true, false, 1.0, false},
    {"bulge", "active", false, true, 1.0, false},
    {"bulge", "half", false, false, 0.5, false},
    {"bulge", "flat", false, false, 0.0, false},
    {"dithered_bulge", "rest", false, false, 1.0, false},
    {"dithered_bulge", "pressed", true, false, 1.0, false},
    {"dithered_bulge", "active", false, true, 1.0, false},
    {"dithered_bulge", "half", false, false, 0.5, false},
    {"dithered_bulge", "flat", false, false, 0.0, false},
    {"dithered_bulge", "active_half", false, true, 0.5, false},
    {"dithered_bulge", "pressed_half", true, false, 0.5, false},
    {"dithered_bulge", "dark", false, false, 1.0, true},
}};

constexpr std::array<double, 4> reference_scales{1.0, 1.25, 1.5, 2.0};

// Expected hashes, [case][scale].
constexpr std::array<std::array<std::uint64_t, 4>, 13> expected_hashes{{
    {0x11f9d12a3f56dc56ULL, 0x61b646439800fd9bULL, 0x31ea1814e17e8f60ULL, 0x96a785b1da65cb4aULL}, // bulge rest
    {0x86797a9c95bfa496ULL, 0xee41b09f75d6affaULL, 0x6c935b6cc7110de0ULL, 0x23a0399065abafdbULL}, // bulge pressed
    {0xae9d9cfdef9517baULL, 0x4ed51eb848557c67ULL, 0x4f5b4e8d0dc6a8bbULL, 0x0ff0e6fe00b6e4d4ULL}, // bulge active
    {0xc3ae7106580c8b8eULL, 0x8b55306473494b7aULL, 0xe1fd3dae3506bc64ULL, 0x1b214abcbfdca34aULL}, // bulge half
    {0xe7f1527fa3215f25ULL, 0xcaa4c737b94a58e5ULL, 0x951952b219c4ea25ULL, 0xa55c6ca074ff1325ULL}, // bulge flat
    {0x01ace47b72818b79ULL, 0x7923187f7bacb6c0ULL, 0xd39e2ebbddf9d7adULL, 0xcd12d7dbcb60d2bdULL}, // dithered rest
    {0x389bbb1a25ae59e7ULL, 0x601e47fe5310c5ceULL, 0x67eca5628bc8bfddULL, 0x73a3879ddef6a265ULL}, // dithered pressed
    {0xfb90047b1f41a576ULL, 0x1f023cd6ab13b4dfULL, 0x98ed4bbdbe78df2dULL, 0xc05abbffd608633dULL}, // dithered active
    {0x41690111d07d89a8ULL, 0x9b2eea19b6280a95ULL, 0x78295cc94e201585ULL, 0x3153bbec176cb8d5ULL}, // dithered half
    {0xe7f1527fa3215f25ULL, 0xcaa4c737b94a58e5ULL, 0x951952b219c4ea25ULL, 0xa55c6ca074ff1325ULL}, // dithered flat
    {0x72394a8b750b0a11ULL, 0xa35d9db2d55576c6ULL, 0x366008ddfa3cb6c5ULL, 0x4b1b8e6ca32a0de5ULL}, // dithered active_half
    {0x0ba65549256e6ecdULL, 0x70d8937f3e3bd454ULL, 0xb01ddb96616b42ddULL, 0xc7c0b455bfcbcd5dULL}, // dithered pressed_half
    {0xec5afbf735a59962ULL, 0xc619ade9e1b1f527ULL, 0x258bf254bf93a38dULL, 0x074d84c288fbaafdULL}, // dithered dark
}};

// GUI_FORMS_ENGINE_DUMP=<directory> writes each reference raster's pixels
// (rows of RGBA, no padding) to <primitive>_<variant>_<scale x 100>_<w>x<h>.rgba,
// for comparing them by eye with the rendering reference.
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

// The dark variant's dithered palette (reference §3, "Dark variant").
void apply_dark_palette(PrimitiveParameterBlock& parameters, const PrimitiveDescriptor& descriptor) {
    struct NamedColour final {
        const char* name;
        Color colour;
    };
    const NamedColour dark[] = {
        {"lit_1", {52, 66, 56, 255}},      {"lit_2", {78, 98, 82, 255}},        {"lit_3", {118, 150, 108, 255}},
        {"shaded_1", {14, 18, 15, 255}},   {"shaded_2", {8, 10, 9, 255}},       {"shaded_3", {0, 0, 0, 255}},
        {"gold_lit_1", {150, 90, 120, 255}}, {"gold_lit_2", {190, 120, 150, 255}}, {"gold_lit_3", {225, 160, 190, 255}},
        {"gold_body_1", {110, 58, 84, 255}}, {"gold_body_2", {140, 72, 104, 255}}, {"gold_body_3", {170, 92, 128, 255}},
        {"gold_shaded_1", {70, 30, 50, 255}}, {"gold_shaded_2", {52, 20, 36, 255}}, {"gold_shaded_3", {34, 10, 22, 255}},
    };
    for (const NamedColour& entry : dark) {
        require(set_colour_parameter(parameters, descriptor, entry.name, entry.colour) == ParameterAssignment::assigned,
                "a dark palette colour is assigned");
    }
    require(set_number_parameter(parameters, descriptor, "lit_gain", 0.9) == ParameterAssignment::assigned, "lit gain");
    require(set_number_parameter(parameters, descriptor, "shade_gain", 1.0) == ParameterAssignment::assigned,
            "shade gain");
    require(set_number_parameter(parameters, descriptor, "crown_gain", 0.2) == ParameterAssignment::assigned,
            "crown gain");
}

void test_reference_images() {
    const bool printing = print_mode();
    bool all_match = true;
    for (std::size_t case_index = 0U; case_index < reference_cases.size(); ++case_index) {
        const ReferenceCase& reference = reference_cases[case_index];
        const EnginePrimitive& primitive = builtin(reference.primitive);
        PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
        require(set_number_parameter(parameters, primitive.descriptor(), "amplitude", reference.amplitude) ==
                    ParameterAssignment::assigned,
                "the case's amplitude is assigned");
        if (reference.dark) apply_dark_palette(parameters, primitive.descriptor());
        if (printing) std::cout << "    {";
        for (std::size_t scale_index = 0U; scale_index < reference_scales.size(); ++scale_index) {
            const double scale = reference_scales[scale_index];
            TestRaster raster = make_raster(large_button, scale);
            const AnatomyStateSet states = states_of(reference.pressed, reference.active);
            require(draw(primitive, parameters, raster, large_button, scale, states) == RasterOutcome::painted,
                    "a reference case paints");
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
    test_builtin_registry();
    test_registration_refusals();
    test_parameter_refusals();
    test_destination_refusals();
    test_preparation_refusals();
    test_amplitude_zero_is_the_plane();
    test_smooth_bulge_is_premultiplied_and_edgeless();
    test_dithered_lattice_and_tones();
    test_rescaled_light_default_uses_every_step();
    test_dark_variant_reaches_every_step();
    test_dot_count_grows_with_amplitude();
    test_integer_scales_reproduce_the_studies_lattice();
    test_lattice_follows_the_nearest_integer_scale();
    test_integer_scales_reproduce_the_old_reference_images();
    test_reference_images();
    std::cout << "engine primitive tests passed\n";
    return 0;
}
