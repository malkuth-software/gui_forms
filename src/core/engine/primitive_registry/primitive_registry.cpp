#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

#include "gui_forms/schema/types/schema_types.hpp"
#include "../primitives/bulge/bulge_primitives.hpp"
#include "../primitives/material/material_primitive.hpp"
#include "../primitives/light/glass_primitive.hpp"
#include "../primitives/light/sheen_primitive.hpp"
#include "../primitives/material/overlay_shadow_primitive.hpp"
#include "../primitives/relief/relief_primitives.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gui_forms {
namespace {

bool number_in_range(const PrimitiveParameterDeclaration& declaration, double value) noexcept {
    const bool inside = value >= declaration.minimum && value <= declaration.maximum;
    return inside;
}

// Whether `value` is the position of one of the declaration's choices.
bool choice_in_range(const PrimitiveParameterDeclaration& declaration, double value) noexcept {
    if (!std::isfinite(value) || value < 0.0 || std::floor(value) != value) return false;
    const bool inside = value < static_cast<double>(declaration.choices.size());
    return inside;
}

PrimitiveParameterValue default_value(const PrimitiveParameterDeclaration& declaration) noexcept {
    const PrimitiveParameterValue value{declaration.kind, declaration.default_number, declaration.default_colour};
    return value;
}

bool target_in_range(const PrimitiveTarget& target) noexcept {
    const Size size = target.logical_size;
    const double scale = target.scale.value;
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || !std::isfinite(scale)) return false;
    if (size.width <= 0.0 || size.height <= 0.0) return false;
    // A part's box is bounded like its raster, so no kernel loop runs away.
    const double largest = static_cast<double>(maximum_part_raster_side);
    if (size.width > largest || size.height > largest) return false;
    const bool scale_in_range = scale >= minimum_device_scale && scale <= maximum_device_scale;
    return scale_in_range;
}

RegistrationOutcome check_parameters(const PrimitiveDescriptor& descriptor) noexcept {
    const std::span<const PrimitiveParameterDeclaration> parameters = descriptor.parameters;
    if (parameters.size() > maximum_primitive_parameters) return RegistrationOutcome::too_many_parameters;
    for (std::size_t index = 0U; index < parameters.size(); ++index) {
        const PrimitiveParameterDeclaration& declaration = parameters[index];
        if (!is_registered_name(declaration.name)) return RegistrationOutcome::parameter_name_not_snake_case;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (parameters[earlier].name == declaration.name) return RegistrationOutcome::duplicate_parameter_name;
        }
        if (declaration.kind == PrimitiveParameterKind::choice) {
            if (declaration.choices.empty()) return RegistrationOutcome::choices_invalid;
            for (std::size_t choice = 0U; choice < declaration.choices.size(); ++choice) {
                if (!is_registered_name(declaration.choices[choice])) return RegistrationOutcome::choices_invalid;
                for (std::size_t earlier = 0U; earlier < choice; ++earlier) {
                    if (declaration.choices[earlier] == declaration.choices[choice]) {
                        return RegistrationOutcome::choices_invalid;
                    }
                }
            }
            if (!choice_in_range(declaration, declaration.default_number)) return RegistrationOutcome::default_out_of_range;
            continue;
        }
        if (declaration.kind != PrimitiveParameterKind::number) continue;
        const bool finite = std::isfinite(declaration.minimum) && std::isfinite(declaration.maximum);
        if (!finite || declaration.minimum > declaration.maximum) return RegistrationOutcome::parameter_range_invalid;
        if (!number_in_range(declaration, declaration.default_number)) return RegistrationOutcome::default_out_of_range;
    }
    return RegistrationOutcome::registered;
}

RegistrationOutcome check_descriptor(const PrimitiveDescriptor& descriptor) noexcept {
    if (!is_registered_name(descriptor.name)) return RegistrationOutcome::name_not_snake_case;
    if (descriptor.roles.empty()) return RegistrationOutcome::no_roles;
    for (const PartRole role : descriptor.roles) {
        if (static_cast<std::size_t>(role) >= part_role_count) return RegistrationOutcome::role_out_of_range;
    }
    for (const AnatomyState state : descriptor.states_read) {
        if (static_cast<std::size_t>(state) >= anatomy_state_count) return RegistrationOutcome::state_out_of_range;
    }
    for (const EngineInput input : descriptor.inputs_read) {
        if (static_cast<std::size_t>(input) >= engine_input_count) return RegistrationOutcome::input_out_of_range;
    }
    if (current_engine_version < descriptor.introduced) return RegistrationOutcome::introduced_after_engine;
    if (descriptor.prepared_values > maximum_prepared_values) return RegistrationOutcome::too_many_prepared_values;
    const RegistrationOutcome parameters = check_parameters(descriptor);
    return parameters;
}

// The first-party primitives. Static storage: they outlive every registry
// that borrows them.
const engine_primitives::BulgePrimitive bulge_primitive{};
const engine_primitives::DitheredBulgePrimitive dithered_bulge_primitive{};
const engine_primitives::PillPlatePrimitive pill_plate_primitive{};
const engine_primitives::GoldPlatePrimitive gold_plate_primitive{};
const engine_primitives::InsetWellPrimitive inset_well_primitive{};
const engine_primitives::EtchedDividerPrimitive etched_divider_primitive{};
const engine_primitives::SeamPrimitive seam_primitive{};
const engine_primitives::GripPrimitive grip_primitive{};
const engine_primitives::MaterialPrimitive material_primitive{};
const engine_primitives::OverlayShadowPrimitive overlay_shadow_primitive{};
const engine_primitives::SheenPrimitive sheen_primitive{};
const engine_primitives::GlassPrimitive glass_primitive{};

PrimitiveRegistry make_builtin_registry() {
    const EnginePrimitive* const first_party[] = {
        &bulge_primitive,       &dithered_bulge_primitive, &pill_plate_primitive, &gold_plate_primitive,
        &inset_well_primitive,  &etched_divider_primitive, &seam_primitive,       &grip_primitive,
        &material_primitive,    &overlay_shadow_primitive, &sheen_primitive,        &glass_primitive,
    };
    PrimitiveRegistry registry{};
    for (const EnginePrimitive* const primitive : first_party) {
        const RegistrationOutcome outcome = registry.register_primitive(*primitive);
        // A first-party descriptor that fails its own checks is a defect of
        // this build, not a runtime condition.
        if (outcome != RegistrationOutcome::registered) std::abort();
    }
    return registry;
}

// The target's states that the descriptor declares it reads; a change to any
// other state never makes a preparation stale.
AnatomyStateSet read_states_of(const PrimitiveDescriptor& descriptor, const PrimitiveTarget& target) noexcept {
    AnatomyStateSet read{};
    for (const AnatomyState state : descriptor.states_read) {
        if (target.states.contains(state)) read.insert(state);
    }
    return read;
}

// Whether `preparation` was derived by `primitive` for this target and these
// parameters. Animated parameters are not compared. `parameters` has
// already been validated against the descriptor.
bool preparation_matches(const EnginePrimitive& primitive, const PrimitiveParameterBlock& parameters,
                         const PrimitiveTarget& target, const PrimitivePreparation& preparation) noexcept {
    const PrimitiveDescriptor& descriptor = primitive.descriptor();
    if (preparation.primitive != &primitive) return false;
    if (preparation.count != descriptor.prepared_values) return false;
    if (!(preparation.logical_size == target.logical_size)) return false;
    if (!(preparation.scale == target.scale)) return false;
    if (!(preparation.states_read == read_states_of(descriptor, target))) return false;
    if (preparation.parameters.count != parameters.count) return false;
    for (std::size_t index = 0U; index < parameters.count; ++index) {
        if (descriptor.parameters[index].animated) continue;
        if (!(preparation.parameters.values[index] == parameters.values[index])) return false;
    }
    return true;
}

} // namespace

PrimitiveParameterBlock default_parameters(const PrimitiveDescriptor& descriptor) noexcept {
    PrimitiveParameterBlock block{};
    const std::size_t count = std::min(descriptor.parameters.size(), maximum_primitive_parameters);
    for (std::size_t index = 0U; index < count; ++index) {
        block.values[index] = default_value(descriptor.parameters[index]);
    }
    block.count = count;
    return block;
}

ParameterValidation validate_parameters(const PrimitiveDescriptor& descriptor,
                                        const PrimitiveParameterBlock& block) noexcept {
    if (block.count != descriptor.parameters.size() || block.count > maximum_primitive_parameters) {
        return ParameterValidation{ParameterDefect::wrong_count, 0U};
    }
    for (std::size_t index = 0U; index < block.count; ++index) {
        const PrimitiveParameterDeclaration& declaration = descriptor.parameters[index];
        const PrimitiveParameterValue& value = block.values[index];
        if (value.kind != declaration.kind) return ParameterValidation{ParameterDefect::wrong_kind, index};
        if (declaration.kind == PrimitiveParameterKind::choice) {
            if (!choice_in_range(declaration, value.number)) {
                return ParameterValidation{ParameterDefect::out_of_range, index};
            }
            continue;
        }
        if (declaration.kind != PrimitiveParameterKind::number) continue;
        if (!std::isfinite(value.number)) return ParameterValidation{ParameterDefect::not_finite, index};
        if (!number_in_range(declaration, value.number)) return ParameterValidation{ParameterDefect::out_of_range, index};
    }
    return ParameterValidation{};
}

std::optional<std::size_t> find_parameter(const PrimitiveDescriptor& descriptor, std::string_view name) noexcept {
    for (std::size_t index = 0U; index < descriptor.parameters.size(); ++index) {
        if (descriptor.parameters[index].name == name) return index;
    }
    return std::nullopt;
}

ParameterAssignment set_number_parameter(PrimitiveParameterBlock& block, const PrimitiveDescriptor& descriptor,
                                         std::string_view name, double value) noexcept {
    const std::optional<std::size_t> index = find_parameter(descriptor, name);
    if (!index || *index >= block.count) return ParameterAssignment::unknown_name;
    const PrimitiveParameterDeclaration& declaration = descriptor.parameters[*index];
    if (declaration.kind != PrimitiveParameterKind::number) return ParameterAssignment::wrong_kind;
    if (!std::isfinite(value)) return ParameterAssignment::not_finite;
    if (!number_in_range(declaration, value)) return ParameterAssignment::out_of_range;
    block.values[*index].number = value;
    return ParameterAssignment::assigned;
}

ParameterAssignment set_colour_parameter(PrimitiveParameterBlock& block, const PrimitiveDescriptor& descriptor,
                                         std::string_view name, Color value) noexcept {
    const std::optional<std::size_t> index = find_parameter(descriptor, name);
    if (!index || *index >= block.count) return ParameterAssignment::unknown_name;
    const PrimitiveParameterDeclaration& declaration = descriptor.parameters[*index];
    if (declaration.kind != PrimitiveParameterKind::colour) return ParameterAssignment::wrong_kind;
    block.values[*index].colour = value;
    return ParameterAssignment::assigned;
}

std::optional<std::size_t> find_choice(const PrimitiveParameterDeclaration& declaration,
                                       std::string_view choice) noexcept {
    for (std::size_t index = 0U; index < declaration.choices.size(); ++index) {
        if (declaration.choices[index] == choice) return index;
    }
    return std::nullopt;
}

ParameterAssignment set_choice_parameter(PrimitiveParameterBlock& block, const PrimitiveDescriptor& descriptor,
                                         std::string_view name, std::string_view choice) noexcept {
    const std::optional<std::size_t> index = find_parameter(descriptor, name);
    if (!index || *index >= block.count) return ParameterAssignment::unknown_name;
    const PrimitiveParameterDeclaration& declaration = descriptor.parameters[*index];
    if (declaration.kind != PrimitiveParameterKind::choice) return ParameterAssignment::wrong_kind;
    const std::optional<std::size_t> position = find_choice(declaration, choice);
    if (!position) return ParameterAssignment::out_of_range;
    block.values[*index].number = static_cast<double>(*position);
    return ParameterAssignment::assigned;
}

PartShape EnginePrimitive::shape(const PrimitiveParameterBlock&) const noexcept {
    const PartShape rectangle{};
    return rectangle;
}

Insets EnginePrimitive::content_insets(const PrimitiveParameterBlock&, Size, DeviceScale, double) const noexcept {
    const Insets none{};
    return none;
}

bool EnginePrimitive::blank(const PrimitiveParameterBlock&) const noexcept {
    return false;
}

std::optional<ControlVisualRole> EnginePrimitive::theme_material(const PrimitiveParameterBlock&,
                                                                 ControlVisualRole) const noexcept {
    return std::nullopt;
}

bool EnginePrimitive::lends_overlay_shadow() const noexcept {
    return false;
}

std::optional<PointerOverlay> EnginePrimitive::pointer_overlay(const PrimitiveParameterBlock&, Size) const noexcept {
    return std::nullopt;
}

std::optional<std::uint32_t> EnginePrimitive::backplane_margin(const PrimitiveParameterBlock&,
                                                               DeviceScale) const noexcept {
    return std::nullopt;
}

std::size_t EnginePrimitive::image_variants(const PrimitiveParameterBlock&, const PrimitiveTarget&,
                                            std::span<ImageVariantRequest>) const noexcept {
    return 0U;
}

bool EnginePrimitive::accepts(const PrimitiveParameterBlock&, const PrimitiveTarget&) const noexcept {
    return true;
}

MaterialDither EnginePrimitive::affordable_dither(const PrimitiveParameterBlock&,
                                                  const PrimitiveTarget& target) const noexcept {
    if (target.material == nullptr) return MaterialDither::none;
    return (*target.material).dither;
}

PrimitiveSlicePlan EnginePrimitive::slice_plan(const PrimitiveParameterBlock&, const PrimitiveTarget&,
                                               DeviceExtent) const noexcept {
    const PrimitiveSlicePlan whole{};
    return whole;
}

PreparationOutcome EnginePrimitive::prepare(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                                            PrimitivePreparation& preparation) const noexcept {
    const PrimitiveDescriptor& described = descriptor();
    if (!target_in_range(target)) return PreparationOutcome::invalid_target;
    const ParameterValidation validation = validate_parameters(described, parameters);
    if (validation.defect != ParameterDefect::none) return PreparationOutcome::invalid_parameters;
    // Derive into a local record and publish it whole, so that the caller's
    // preparation is never seen half written.
    PrimitivePreparation prepared{};
    prepared.primitive = this;
    prepared.logical_size = target.logical_size;
    prepared.scale = target.scale;
    prepared.states_read = read_states_of(described, target);
    prepared.parameters = parameters;
    prepared.count = described.prepared_values;
    const std::span<double> values(prepared.values.data(), prepared.count);
    derive(parameters, target, values);
    preparation = prepared;
    return PreparationOutcome::prepared;
}

RasterOutcome EnginePrimitive::rasterize(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                                         const PrimitivePreparation& preparation,
                                         const PartRasterView& destination) const noexcept {
    if (validate_raster_view(destination) != RasterViewDefect::none) return RasterOutcome::invalid_view;
    if (!target_in_range(target)) return RasterOutcome::invalid_target;
    const ParameterValidation validation = validate_parameters(descriptor(), parameters);
    if (validation.defect != ParameterDefect::none) return RasterOutcome::invalid_parameters;
    if (!preparation_matches(*this, parameters, target, preparation)) return RasterOutcome::stale_preparation;
    if (!accepts(parameters, target)) return RasterOutcome::invalid_target;
    if (destination.extent.width == 0U || destination.extent.height == 0U) return RasterOutcome::empty;
    const std::span<const double> prepared(preparation.values.data(), preparation.count);
    fill(parameters, target, prepared, destination);
    return RasterOutcome::painted;
}

const char* registration_outcome_name(RegistrationOutcome outcome) noexcept {
    switch (outcome) {
    case RegistrationOutcome::registered: return "registered";
    case RegistrationOutcome::registry_full: return "registry_full";
    case RegistrationOutcome::name_not_snake_case: return "name_not_snake_case";
    case RegistrationOutcome::duplicate_name: return "duplicate_name";
    case RegistrationOutcome::no_roles: return "no_roles";
    case RegistrationOutcome::role_out_of_range: return "role_out_of_range";
    case RegistrationOutcome::state_out_of_range: return "state_out_of_range";
    case RegistrationOutcome::too_many_parameters: return "too_many_parameters";
    case RegistrationOutcome::parameter_name_not_snake_case: return "parameter_name_not_snake_case";
    case RegistrationOutcome::duplicate_parameter_name: return "duplicate_parameter_name";
    case RegistrationOutcome::parameter_range_invalid: return "parameter_range_invalid";
    case RegistrationOutcome::default_out_of_range: return "default_out_of_range";
    case RegistrationOutcome::introduced_after_engine: return "introduced_after_engine";
    case RegistrationOutcome::too_many_prepared_values: return "too_many_prepared_values";
    case RegistrationOutcome::input_out_of_range: return "input_out_of_range";
    case RegistrationOutcome::choices_invalid: return "choices_invalid";
    }
    return "unknown";
}

RegistrationOutcome PrimitiveRegistry::register_primitive(const EnginePrimitive& primitive) noexcept {
    const PrimitiveDescriptor& descriptor = primitive.descriptor();
    const RegistrationOutcome checked = check_descriptor(descriptor);
    if (checked != RegistrationOutcome::registered) return checked;
    if (find(descriptor.name) != nullptr) return RegistrationOutcome::duplicate_name;
    if (count_ == maximum_registered_primitives) return RegistrationOutcome::registry_full;
    entries_[count_] = &primitive;
    count_ = count_ + 1U;
    return RegistrationOutcome::registered;
}

const EnginePrimitive* PrimitiveRegistry::find(std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < count_; ++index) {
        const EnginePrimitive* const entry = entries_[index];
        if ((*entry).descriptor().name == name) return entry;
    }
    return nullptr;
}

std::span<const EnginePrimitive* const> PrimitiveRegistry::primitives() const noexcept {
    const std::span<const EnginePrimitive* const> live(entries_.data(), count_);
    return live;
}

const PrimitiveRegistry& builtin_primitive_registry() {
    static const PrimitiveRegistry registry = make_builtin_registry();
    return registry;
}

} // namespace gui_forms
