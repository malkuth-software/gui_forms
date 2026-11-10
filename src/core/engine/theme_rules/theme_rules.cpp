// The cascade (T2 plan, phase E4): compiling a theme's rules against the
// primitive registry with diagnostics, and resolving one part.

#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include "gui_forms/surface_material/painting/surface_material_painting.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>

namespace gui_forms {
namespace {

constexpr std::size_t maximum_style_class_length = 64U;
// Where an inline style's machines are numbered from
// (CompiledTransition::machine), so that none is a theme's.
constexpr std::size_t inline_machine_base = std::size_t{1} << 30U;
// Where a user theme's machines are numbered from, likewise.
constexpr std::size_t user_machine_base = std::size_t{1} << 29U;

// A scoped custom property's name (P12): `--` and at least one character,
// at most 256 bytes in all.
[[nodiscard]] bool scoped_name_valid(std::string_view name) noexcept {
    return name.size() > 2U && name.size() <= 256U && name.substr(0U, 2U) == "--";
}

bool lowercase_letter(char character) noexcept {
    const bool letter = character >= 'a' && character <= 'z';
    return letter;
}

bool class_character(char character) noexcept {
    const bool digit = character >= '0' && character <= '9';
    const bool joiner = character == '-' || character == '_';
    return lowercase_letter(character) || digit || joiner;
}

bool authored(CascadeLevel level) noexcept {
    // The base levels hold the GUI.Forms base sheet under a program's
    // packages (selector step 1); they are a program theme's too.
    const bool base = level == CascadeLevel::base_sheet || level == CascadeLevel::important_base_sheet;
    const bool authored_level = base || level == CascadeLevel::theme_sheet || level == CascadeLevel::class_rule ||
                                level == CascadeLevel::stable_id_rule || level == CascadeLevel::inline_style ||
                                level == CascadeLevel::important_theme_sheet ||
                                level == CascadeLevel::important_inline_style;
    return authored_level;
}

// Whether `level` is one a user theme authors.
bool user_level(CascadeLevel level) noexcept {
    const bool user = level == CascadeLevel::user_theme || level == CascadeLevel::important_user_theme;
    return user;
}

bool paints_role(const PrimitiveDescriptor& descriptor, PartRole role) noexcept {
    for (const PartRole painted : descriptor.roles) {
        if (painted == role) return true;
    }
    return false;
}

RuleDiagnostic diagnostic(RuleDiagnosticKind kind, std::size_t rule, const ThemeRule& source,
                          std::string_view property) {
    RuleDiagnostic result{};
    result.kind = kind;
    result.rule = rule;
    result.label = source.label;
    result.property = std::string(property);
    return result;
}

// The diagnostic for a selector that cannot be used at its level, or none.
// A control's own inline style (`owned`) needs no stable ID.
std::optional<RuleDiagnosticKind> selector_defect(const ThemeRule& rule, bool owned) noexcept {
    const RuleSelector& selector = rule.selector;
    for (const std::string& name : selector.classes) {
        if (!valid_style_class_name(name)) return RuleDiagnosticKind::selector_invalid;
    }
    for (const AnatomyState state : selector.states) {
        if (static_cast<std::size_t>(state) >= anatomy_state_count) return RuleDiagnosticKind::selector_invalid;
    }
    if (selector.ancestors.size() > maximum_rule_ancestors) return RuleDiagnosticKind::selector_invalid;
    for (const RuleAncestor& ancestor : selector.ancestors) {
        for (const std::string& name : ancestor.classes) {
            if (!valid_style_class_name(name)) return RuleDiagnosticKind::selector_invalid;
        }
    }
    for (const RuleExclusion& exclusion : selector.excluded) {
        for (const std::string& name : exclusion.classes) {
            if (!valid_style_class_name(name)) return RuleDiagnosticKind::selector_invalid;
        }
        for (const AnatomyState state : exclusion.states) {
            if (static_cast<std::size_t>(state) >= anatomy_state_count) return RuleDiagnosticKind::selector_invalid;
        }
    }
    if (selector.role && static_cast<std::size_t>(*selector.role) >= part_role_count) {
        return RuleDiagnosticKind::selector_invalid;
    }
    // A given specificity (EC2) stands in for the selector's class and ID.
    const RuleSpecificity specificity = rule.specificity ? *rule.specificity : rule_specificity(selector);
    if (rule.level == CascadeLevel::class_rule && specificity.classes == 0U) {
        return RuleDiagnosticKind::selector_missing_level_field;
    }
    if (rule.level == CascadeLevel::stable_id_rule && specificity.ids == 0U) {
        return RuleDiagnosticKind::selector_missing_level_field;
    }
    const bool inline_level =
        rule.level == CascadeLevel::inline_style || rule.level == CascadeLevel::important_inline_style;
    if (inline_level && !owned && selector.stable_id.empty()) {
        return RuleDiagnosticKind::selector_missing_level_field;
    }
    return std::nullopt;
}

// Why a primitive cannot be selected by this rule, or none.
std::optional<RuleDiagnosticKind> primitive_defect(const EnginePrimitive& primitive,
                                                   const RuleSelector& selector) noexcept {
    const PrimitiveDescriptor& descriptor = primitive.descriptor();
    if (descriptor.cost.per_device_pixel == 0U) return RuleDiagnosticKind::primitive_cost_unbounded;
    if (descriptor.cost.per_device_pixel > maximum_operations_per_device_pixel) {
        return RuleDiagnosticKind::primitive_over_budget;
    }
    if (selector.role && !paints_role(descriptor, *selector.role)) return RuleDiagnosticKind::primitive_role_mismatch;
    return std::nullopt;
}

// Why a parameter declaration cannot be used, or none; `index` is set to the
// parameter's position when it can.
std::optional<RuleDiagnosticKind> parameter_defect(const PrimitiveDescriptor& descriptor,
                                                   const RuleDeclaration& declaration, std::size_t& index) noexcept {
    const std::optional<std::size_t> found = find_parameter(descriptor, declaration.property);
    if (!found) return RuleDiagnosticKind::unknown_parameter;
    const PrimitiveParameterDeclaration& parameter = descriptor.parameters[*found];
    // A choice is named, and only a choice is.
    const bool choice = parameter.kind == PrimitiveParameterKind::choice;
    if ((declaration.kind == RuleValueKind::name) != choice) return RuleDiagnosticKind::parameter_wrong_kind;
    if (choice) {
        if (!find_choice(parameter, declaration.name)) return RuleDiagnosticKind::parameter_out_of_range;
        index = *found;
        return std::nullopt;
    }
    // An expression gives a number; its fallback, in `number`, is checked
    // as a number value is.
    const bool number = declaration.kind == RuleValueKind::number || declaration.kind == RuleValueKind::expression;
    if (number != (parameter.kind == PrimitiveParameterKind::number)) return RuleDiagnosticKind::parameter_wrong_kind;
    if (number) {
        if (!std::isfinite(declaration.number) || !std::isfinite(declaration.number_em)) {
            return RuleDiagnosticKind::parameter_not_finite;
        }
        // A value in em is held within the range when the part is painted.
        const bool em = declaration.number_em != 0.0;
        if (em && declaration.kind != RuleValueKind::number) return RuleDiagnosticKind::parameter_wrong_kind;
        if (!em && (declaration.number < parameter.minimum || declaration.number > parameter.maximum)) {
            return RuleDiagnosticKind::parameter_out_of_range;
        }
    }
    index = *found;
    return std::nullopt;
}

// Whether an expression reads a parameter that is a colour.
bool reads_colour_parameter(const PrimitiveDescriptor& descriptor, const EngineExpression& expression) noexcept {
    for (std::size_t index = 0; index < descriptor.parameters.size(); ++index) {
        const ExpressionParameterIndex position{static_cast<std::uint8_t>(index)};
        if (!expression.dependencies().reads_parameter(position)) continue;
        if (descriptor.parameters[index].kind == PrimitiveParameterKind::colour) return true;
    }
    return false;
}

// The primitive's declared inputs as expression input bits.
std::uint64_t declared_input_bits(const PrimitiveDescriptor& descriptor) noexcept {
    std::uint64_t bits = 0U;
    for (const EngineInput input : descriptor.inputs_read) {
        bits = bits | expression_input_bit(engine_input_position(input));
    }
    return bits;
}

} // namespace

std::string_view cascade_level_name(CascadeLevel level) noexcept {
    switch (level) {
    case CascadeLevel::built_in: return "built_in";
    case CascadeLevel::base_sheet: return "base_sheet";
    case CascadeLevel::user_theme: return "user_theme";
    case CascadeLevel::theme_sheet: return "theme_sheet";
    case CascadeLevel::class_rule: return "class_rule";
    case CascadeLevel::stable_id_rule: return "stable_id_rule";
    case CascadeLevel::inline_style: return "inline_style";
    case CascadeLevel::important_base_sheet: return "important_base_sheet";
    case CascadeLevel::important_user_theme: return "important_user_theme";
    case CascadeLevel::important_theme_sheet: return "important_theme_sheet";
    case CascadeLevel::forced_high_contrast: return "forced_high_contrast";
    case CascadeLevel::important_inline_style: return "important_inline_style";
    case CascadeLevel::count: return {};
    }
    return {};
}

std::string_view theme_layer_name(ThemeLayer layer) noexcept {
    switch (layer) {
    case ThemeLayer::built_in: return "built_in";
    case ThemeLayer::library_pack: return "library_pack";
    case ThemeLayer::project_theme: return "project_theme";
    case ThemeLayer::user_theme: return "user_theme";
    case ThemeLayer::inline_code: return "inline_code";
    case ThemeLayer::forced_high_contrast: return "forced_high_contrast";
    case ThemeLayer::fallback_theme: return "fallback_theme";
    case ThemeLayer::count:
    // A value outside the layers names none: the one fallback.
    default: break;
    }
    return {};
}

bool valid_style_class_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_style_class_length) return false;
    if (!lowercase_letter(name.front())) return false;
    for (const char character : name) {
        if (!class_character(character)) return false;
    }
    return true;
}

namespace {

// What one `:not()` argument counts, as a compound selector would.
RuleSpecificity exclusion_specificity(const RuleExclusion& exclusion) noexcept {
    RuleSpecificity result{};
    result.ids = exclusion.stable_id.empty() ? 0U : 1U;
    const std::size_t family = exclusion.family.empty() ? 0U : 1U;
    result.classes = static_cast<std::uint32_t>(exclusion.classes.size() + exclusion.states.size() + family);
    result.types = exclusion.control_type.empty() ? 0U : 1U;
    return result;
}

} // namespace

RuleSpecificity rule_specificity(const RuleSelector& selector) noexcept {
    RuleSpecificity result{};
    result.ids = selector.stable_id.empty() ? 0U : 1U;
    // Each class, each state and the family, as CSS counts classes and
    // pseudo-classes.
    const std::size_t family = selector.family.empty() ? 0U : 1U;
    result.classes = static_cast<std::uint32_t>(selector.classes.size() + selector.states.size() + family);
    // A surface selector counts as a type selector, as CSS's `window` would;
    // the part's name and role each as a pseudo-element.
    const bool typed = !selector.control_type.empty() || selector.surface != SurfaceKind::none;
    const std::uint32_t type = typed ? 1U : 0U;
    const std::uint32_t named = selector.part.empty() ? 0U : 1U;
    const std::uint32_t role = selector.role ? 1U : 0U;
    result.types = type + named + role;
    // CSS: a `:not()` counts as its most specific argument. The selector's
    // exclusions are one `:not()`'s arguments (the lowering merges them).
    RuleSpecificity most{};
    for (const RuleExclusion& exclusion : selector.excluded) {
        const RuleSpecificity counted = exclusion_specificity(exclusion);
        if (most < counted) most = counted;
    }
    result.ids += most.ids;
    result.classes += most.classes;
    result.types += most.types;
    // Each ancestor compound counts as CSS counts every compound.
    for (const RuleAncestor& ancestor : selector.ancestors) {
        result.ids += ancestor.stable_id.empty() ? 0U : 1U;
        result.classes += static_cast<std::uint32_t>(ancestor.classes.size());
        result.types += ancestor.control_type.empty() ? 0U : 1U;
    }
    return result;
}

namespace {

// FNV-1a over a key's prefix and text, for the ancestor Bloom filter.
std::uint64_t ancestor_key_hash(std::string_view prefix, std::string_view key) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : prefix) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ULL;
    }
    for (const char character : key) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

void AncestorBloom::add(std::string_view prefix, std::string_view key) noexcept {
    // Two bits a key, from the hash's two halves: 256 bits in 4 words.
    const std::uint64_t hash = ancestor_key_hash(prefix, key);
    const std::uint64_t first = hash & 255U;
    const std::uint64_t second = (hash >> 32U) & 255U;
    bits[first >> 6U] |= std::uint64_t{1} << (first & 63U);
    bits[second >> 6U] |= std::uint64_t{1} << (second & 63U);
}

bool AncestorBloom::holds(const AncestorBloom& needed) const noexcept {
    for (std::size_t word = 0U; word < bits.size(); ++word) {
        if ((bits[word] & needed.bits[word]) != needed.bits[word]) return false;
    }
    return true;
}

namespace {

bool resolution_bound_valid(const std::optional<double>& bound) noexcept {
    if (!bound) return true;
    const bool valid = std::isfinite(*bound) && *bound > 0.0;
    return valid;
}

bool feature_holds(const std::optional<bool>& wanted, bool fact) noexcept {
    if (!wanted) return true;
    const bool holds = *wanted == fact;
    return holds;
}

} // namespace

bool media_condition_unconditional(const MediaCondition& condition) noexcept {
    const bool unconditional = !condition.dark_scheme && !condition.forced_colours && !condition.reduced_motion &&
                               !condition.minimum_resolution && !condition.maximum_resolution;
    return unconditional;
}

bool media_condition_valid(const MediaCondition& condition) noexcept {
    const bool valid =
        resolution_bound_valid(condition.minimum_resolution) && resolution_bound_valid(condition.maximum_resolution);
    return valid;
}

bool media_condition_holds(const MediaCondition& condition, const MediaFacts& facts) noexcept {
    if (!feature_holds(condition.dark_scheme, facts.dark_scheme)) return false;
    if (!feature_holds(condition.forced_colours, facts.forced_colours)) return false;
    if (!feature_holds(condition.reduced_motion, facts.reduced_motion)) return false;
    if (condition.minimum_resolution && facts.resolution < *condition.minimum_resolution) return false;
    if (condition.maximum_resolution && facts.resolution > *condition.maximum_resolution) return false;
    return true;
}

std::string_view rule_diagnostic_name(RuleDiagnosticKind kind) noexcept {
    switch (kind) {
    case RuleDiagnosticKind::needs_newer_engine: return "needs_newer_engine";
    case RuleDiagnosticKind::level_not_authored: return "level_not_authored";
    case RuleDiagnosticKind::selector_missing_level_field: return "selector_missing_level_field";
    case RuleDiagnosticKind::selector_invalid: return "selector_invalid";
    case RuleDiagnosticKind::unknown_primitive: return "unknown_primitive";
    case RuleDiagnosticKind::primitive_role_mismatch: return "primitive_role_mismatch";
    case RuleDiagnosticKind::primitive_over_budget: return "primitive_over_budget";
    case RuleDiagnosticKind::primitive_cost_unbounded: return "primitive_cost_unbounded";
    case RuleDiagnosticKind::primitive_value_not_a_name: return "primitive_value_not_a_name";
    case RuleDiagnosticKind::parameter_without_primitive: return "parameter_without_primitive";
    case RuleDiagnosticKind::unknown_parameter: return "unknown_parameter";
    case RuleDiagnosticKind::parameter_wrong_kind: return "parameter_wrong_kind";
    case RuleDiagnosticKind::parameter_not_finite: return "parameter_not_finite";
    case RuleDiagnosticKind::parameter_out_of_range: return "parameter_out_of_range";
    case RuleDiagnosticKind::duplicate_declaration: return "duplicate_declaration";
    case RuleDiagnosticKind::expression_invalid: return "expression_invalid";
    case RuleDiagnosticKind::expression_reads_colour: return "expression_reads_colour";
    case RuleDiagnosticKind::rule_empty: return "rule_empty";
    case RuleDiagnosticKind::backdrop_invalid: return "backdrop_invalid";
    case RuleDiagnosticKind::band_invalid: return "band_invalid";
    case RuleDiagnosticKind::transition_property_unknown: return "transition_property_unknown";
    case RuleDiagnosticKind::transition_property_not_animated: return "transition_property_not_animated";
    case RuleDiagnosticKind::transition_invalid: return "transition_invalid";
    case RuleDiagnosticKind::ambient_over_frame_cap: return "ambient_over_frame_cap";
    case RuleDiagnosticKind::ambient_invalid: return "ambient_invalid";
    case RuleDiagnosticKind::blend_invalid: return "blend_invalid";
    case RuleDiagnosticKind::font_invalid: return "font_invalid";
    case RuleDiagnosticKind::effect_invalid: return "effect_invalid";
    case RuleDiagnosticKind::effect_colour_invalid: return "effect_colour_invalid";
    case RuleDiagnosticKind::published_value_invalid: return "published_value_invalid";
    case RuleDiagnosticKind::material_field_invalid: return "material_field_invalid";
    case RuleDiagnosticKind::recipe_field_invalid: return "recipe_field_invalid";
    case RuleDiagnosticKind::recipe_field_on_part: return "recipe_field_on_part";
    case RuleDiagnosticKind::media_invalid: return "media_invalid";
    case RuleDiagnosticKind::scoped_property_invalid: return "scoped_property_invalid";
    case RuleDiagnosticKind::user_opacity_raised: return "user_opacity_raised";
    }
    return {};
}

// -- material fields (EC1) ----------------------------------------------------------

namespace {

constexpr std::array<std::string_view, material_field_count> material_field_names{
    "background_colour",
    "fills",
    "fill_blend_modes",
    "fill_positions",
    "fill_sizes",
    "fill_repeats",
    "shadows",
    "glows",
    "glow_placements",
    "border_top_style",
    "border_top_width",
    "border_top_colour",
    "border_right_style",
    "border_right_width",
    "border_right_colour",
    "border_bottom_style",
    "border_bottom_width",
    "border_bottom_colour",
    "border_left_style",
    "border_left_width",
    "border_left_colour",
    "border_image",
    "keylines",
    "corner_top_left",
    "corner_top_right",
    "corner_bottom_right",
    "corner_bottom_left",
    "colour_filter",
    "drop_shadow",
    "filter_blur",
    "finish",
    "mask",
    "reflection"};

// The edge fields: four edges of three fields each, from border_top_style.
constexpr std::size_t first_edge_field = static_cast<std::size_t>(MaterialField::border_top_style);
constexpr std::size_t edge_field_count = 12U;
static_assert(static_cast<std::size_t>(MaterialField::border_left_colour) == first_edge_field + edge_field_count - 1U,
              "the edge fields are contiguous: style, width, colour per edge");

// The corner fields, in CSS's order: top-left, top-right, bottom-right,
// bottom-left.
constexpr std::size_t first_corner_field = static_cast<std::size_t>(MaterialField::corner_top_left);
constexpr std::size_t corner_field_count = 4U;
static_assert(static_cast<std::size_t>(MaterialField::corner_bottom_left) ==
                  first_corner_field + corner_field_count - 1U,
              "the corner fields are contiguous, in CSS's order");

// Draws the serial number of a new compilation; the first is 1.
std::uint64_t next_rules_serial() noexcept {
    static std::atomic<std::uint64_t> drawn{0U};
    const std::uint64_t serial = drawn.fetch_add(1U, std::memory_order_relaxed) + 1U;
    return serial;
}

} // namespace

ColourPosition material_field_colour_position(MaterialField field) noexcept {
    switch (field) {
    case MaterialField::background_colour:
    case MaterialField::fills: return ColourPosition::fill;
    case MaterialField::border_top_colour:
    case MaterialField::border_right_colour:
    case MaterialField::border_bottom_colour:
    case MaterialField::border_left_colour:
    case MaterialField::keylines: return ColourPosition::edge;
    case MaterialField::fill_blend_modes:
    case MaterialField::fill_positions:
    case MaterialField::fill_sizes:
    case MaterialField::fill_repeats:
    case MaterialField::shadows:
    case MaterialField::glows:
    case MaterialField::glow_placements:
    case MaterialField::border_top_style:
    case MaterialField::border_top_width:
    case MaterialField::border_right_style:
    case MaterialField::border_right_width:
    case MaterialField::border_bottom_style:
    case MaterialField::border_bottom_width:
    case MaterialField::border_left_style:
    case MaterialField::border_left_width:
    case MaterialField::border_image:
    case MaterialField::corner_top_left:
    case MaterialField::corner_top_right:
    case MaterialField::corner_bottom_right:
    case MaterialField::corner_bottom_left:
    case MaterialField::colour_filter:
    case MaterialField::drop_shadow:
    case MaterialField::filter_blur:
    case MaterialField::finish:
    case MaterialField::mask:
    case MaterialField::reflection:
    case MaterialField::count:
    default: return ColourPosition::none;
    }
}

std::string_view material_field_name(MaterialField field) noexcept {
    const std::size_t position = static_cast<std::size_t>(field);
    if (position >= material_field_names.size()) return {};
    return material_field_names[position];
}

namespace {

double resolved_corner(double value, bool relative, double shorter_side) noexcept {
    const double resolved = relative ? value * shorter_side : value;
    return resolved;
}

// Writes `declaration`'s field into `material`, replacing what was there.
// The fill, edge and corner fields are composed together, not written
// here (compose_fills, assemble_edges, compose_corners).
void apply_material_declaration(const MaterialDeclaration& declaration, SurfaceMaterial& material) {
    switch (declaration.field) {
    case MaterialField::shadows: material.shadows = declaration.shadows; return;
    case MaterialField::glows: material.glows = declaration.glows; return;
    case MaterialField::glow_placements: {
        // Over the glows already in place (assemble_material writes the
        // glows first).
        const std::vector<MaterialGlowPlacement>& placements = declaration.glow_placements;
        if (placements.empty()) return;
        for (std::size_t glow = 0U; glow < material.glows.size(); ++glow) {
            material.glows[glow].placement = placements[glow % placements.size()];
        }
        return;
    }
    case MaterialField::keylines: material.keylines = declaration.keylines; return;
    case MaterialField::colour_filter: material.colour_filter = declaration.colour_filter; return;
    case MaterialField::drop_shadow: material.drop_shadow = declaration.drop_shadow; return;
    case MaterialField::filter_blur: material.filter_blur = declaration.number; return;
    case MaterialField::finish: material.finish = declaration.number; return;
    case MaterialField::mask: material.mask = declaration.mask; return;
    case MaterialField::reflection: material.reflection = declaration.reflection; return;
    case MaterialField::border_top_style:
    case MaterialField::border_top_width:
    case MaterialField::border_top_colour:
    case MaterialField::border_right_style:
    case MaterialField::border_right_width:
    case MaterialField::border_right_colour:
    case MaterialField::border_bottom_style:
    case MaterialField::border_bottom_width:
    case MaterialField::border_bottom_colour:
    case MaterialField::border_left_style:
    case MaterialField::border_left_width:
    case MaterialField::border_left_colour:
    case MaterialField::background_colour:
    case MaterialField::fills:
    case MaterialField::fill_blend_modes:
    case MaterialField::fill_positions:
    case MaterialField::fill_sizes:
    case MaterialField::fill_repeats:
    case MaterialField::border_image:
    case MaterialField::corner_top_left:
    case MaterialField::corner_top_right:
    case MaterialField::corner_bottom_right:
    case MaterialField::corner_bottom_left:
    case MaterialField::count: return;
    // A value outside the enumeration names no field: nothing is written,
    // as for the composed fields above.
    default: return;
    }
}

bool fill_field(MaterialField field) noexcept {
    const bool colour_or_layers = field == MaterialField::background_colour || field == MaterialField::fills ||
                                  field == MaterialField::fill_blend_modes;
    const bool placement = field == MaterialField::fill_positions || field == MaterialField::fill_sizes ||
                           field == MaterialField::fill_repeats;
    return colour_or_layers || placement;
}

bool corner_field(MaterialField field) noexcept {
    const std::size_t position = static_cast<std::size_t>(field);
    const bool corner = position >= first_corner_field && position < first_corner_field + corner_field_count;
    return corner;
}

// The declaration of `field` among `fields` that sets a value, or null
// (none, or an `initial` one: the floor's value).
const MaterialDeclaration* set_field(const std::array<const MaterialDeclaration*, material_field_count>& fields,
                                     MaterialField field) noexcept {
    const MaterialDeclaration* const declaration = fields[static_cast<std::size_t>(field)];
    if (declaration == nullptr || (*declaration).initial) return nullptr;
    return declaration;
}
const MaterialDeclaration* set_field(std::array<const MaterialDeclaration*, material_field_count>&& fields,
                                     MaterialField field) = delete;

// The fills composed from the colour layer and the image layers, each the
// declared one, else the floor's: its bottom layer when solid is its
// colour, its others its image layers.
// An image size's length along a side of `side` logical units.
double sized_length(BoxLength length, double side) noexcept {
    const double resolved = length.fraction * side + length.units;
    return resolved;
}

// Sizes image fill `layer` by `size` for `box`, keeping its proportions:
// its image_scale becomes the source pixels per logical unit of the size.
void size_image(const MaterialImageSize& size, Size box, MaterialFillLayer& layer) noexcept {
    const Size pixels = layer.image_pixel_size;
    switch (size.sizing) {
    case MaterialImageSizing::natural: return; // The image's own size: nothing to change.
    case MaterialImageSizing::stretch: layer.image_mode = MaterialImageMode::stretch; return;
    case MaterialImageSizing::width:
        layer.image_scale = pixels.width / sized_length(size.length, box.width);
        break;
    case MaterialImageSizing::height:
        layer.image_scale = pixels.height / sized_length(size.length, box.height);
        break;
    case MaterialImageSizing::cover:
    case MaterialImageSizing::contain: {
        // The factor from the image's own size, its layer's scale as
        // written, to the box's along each side.
        const double across = box.width * layer.image_scale / pixels.width;
        const double down = box.height * layer.image_scale / pixels.height;
        const bool cover = size.sizing == MaterialImageSizing::cover;
        const double factor = cover ? std::max(across, down) : std::min(across, down);
        layer.image_scale = layer.image_scale / factor;
        break;
    }
    // A value outside the enumeration sizes nothing: the scale as written.
    default: break;
    }
    // A sized image is tiled, unless repetition then makes it single.
    if (layer.image_mode == MaterialImageMode::stretch) layer.image_mode = MaterialImageMode::tile;
}

// Places the image fills among `material`'s image layers, from
// `images_first` up, by the declared sizes, repetitions and positions,
// each list in CSS's order (the top layer's first) and repeated over the
// image layers.
void place_images(const MaterialDeclaration* sizes, const MaterialDeclaration* repeats,
                  const MaterialDeclaration* positions, std::size_t images_first, Size box,
                  SurfaceMaterial& material) noexcept {
    const std::size_t image_count = material.fills.size() - images_first;
    for (std::size_t from_top = 0U; from_top < image_count; ++from_top) {
        MaterialFillLayer& layer = material.fills[material.fills.size() - 1U - from_top];
        if (layer.kind != MaterialFillKind::image || layer.image_mode == MaterialImageMode::nine_patch) continue;
        if (sizes != nullptr && !(*sizes).image_sizes.empty()) {
            const std::vector<MaterialImageSize>& written = (*sizes).image_sizes;
            size_image(written[from_top % written.size()], box, layer);
        }
        const bool tiled = layer.image_mode == MaterialImageMode::tile || layer.image_mode == MaterialImageMode::single;
        if (repeats != nullptr && !(*repeats).image_repeats.empty() && tiled) {
            const std::vector<MaterialImageRepeat>& written = (*repeats).image_repeats;
            const bool alone = written[from_top % written.size()] == MaterialImageRepeat::no_repeat;
            layer.image_mode = alone ? MaterialImageMode::single : MaterialImageMode::tile;
        }
        if (positions != nullptr && !(*positions).image_positions.empty()) {
            const std::vector<MaterialImagePosition>& written = (*positions).image_positions;
            const MaterialImagePosition& position = written[from_top % written.size()];
            layer.image_anchor = Point{position.across.fraction, position.down.fraction};
            layer.image_offset = Point{position.across.units, position.down.units};
        }
    }
}

void compose_fills(const SurfaceMaterial& floor,
                   const std::array<const MaterialDeclaration*, material_field_count>& fields, Size box,
                   SurfaceMaterial& material) {
    const MaterialDeclaration* const colour = set_field(fields, MaterialField::background_colour);
    const MaterialDeclaration* const images = set_field(fields, MaterialField::fills);
    const MaterialDeclaration* const blends = set_field(fields, MaterialField::fill_blend_modes);
    const bool floor_colour = !floor.fills.empty() && floor.fills.front().kind == MaterialFillKind::solid;
    const std::size_t floor_images_first = floor_colour ? 1U : 0U;
    material.fills.clear();
    if (colour != nullptr) {
        if ((*colour).colour.alpha != 0U) material.fills.push_back(MaterialFillLayer::solid((*colour).colour));
    } else if (floor_colour) {
        material.fills.push_back(floor.fills.front());
    }
    const std::size_t images_first = material.fills.size();
    if (images != nullptr) {
        material.fills.insert(material.fills.end(), (*images).fills.begin(), (*images).fills.end());
        const std::vector<AuthoredGradient>& gradients = (*images).gradients;
        for (std::size_t layer = 0U; layer < gradients.size(); ++layer) {
            if (gradients[layer].kind == MaterialFillKind::solid) continue;
            resolve_authored_gradient(gradients[layer], box, material.fills[images_first + layer]);
        }
    } else {
        const std::vector<MaterialFillLayer>::const_iterator first =
            floor.fills.begin() + static_cast<std::ptrdiff_t>(floor_images_first);
        material.fills.insert(material.fills.end(), first, floor.fills.end());
    }
    if (blends != nullptr) {
        // The modes are in CSS's order, the top layer's first; the layers
        // bottom first.
        const std::vector<MaterialBlendMode>& modes = (*blends).blend_modes;
        const std::size_t image_count = material.fills.size() - images_first;
        for (std::size_t from_top = 0U; from_top < image_count; ++from_top) {
            MaterialFillLayer& layer = material.fills[material.fills.size() - 1U - from_top];
            layer.blend = modes.empty() ? MaterialBlendMode::normal : modes[from_top % modes.size()];
        }
    }
    const MaterialDeclaration* const sizes = set_field(fields, MaterialField::fill_sizes);
    const MaterialDeclaration* const repeats = set_field(fields, MaterialField::fill_repeats);
    const MaterialDeclaration* const positions = set_field(fields, MaterialField::fill_positions);
    if (sizes != nullptr || repeats != nullptr || positions != nullptr) {
        place_images(sizes, repeats, positions, images_first, box, material);
    }
    if (material.fills.empty()) material.fills.push_back(MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
}

// One corner of the floor: its corner_radii's, else its corner_radius.
double floor_corner(const SurfaceMaterial& floor, std::size_t corner) noexcept {
    if (!floor.corner_radii) return floor.corner_radius;
    const MaterialCornerRadii& radii = *floor.corner_radii;
    const std::array<double, corner_field_count> corners{radii.top_left, radii.top_right, radii.bottom_right,
                                                         radii.bottom_left};
    return corners[corner];
}

// The corners, each the declared one (a relative one resolved against
// `shorter_side`), else the floor's; one corner_radius when the four are
// equal.
void compose_corners(const SurfaceMaterial& floor,
                     const std::array<const MaterialDeclaration*, material_field_count>& fields,
                     double shorter_side, SurfaceMaterial& material) {
    std::array<double, corner_field_count> corners{};
    for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
        const MaterialDeclaration* const declaration = fields[first_corner_field + corner];
        if (declaration == nullptr || (*declaration).initial) {
            corners[corner] = floor_corner(floor, corner);
            continue;
        }
        corners[corner] = resolved_corner((*declaration).number, (*declaration).relative, shorter_side);
    }
    const double smallest = *std::min_element(corners.begin(), corners.end());
    material.corner_radius = smallest;
    const bool uniform = corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3];
    if (uniform) {
        material.corner_radii.reset();
        return;
    }
    material.corner_radii = MaterialCornerRadii{corners[0], corners[1], corners[2], corners[3]};
}

bool edge_field(MaterialField field) noexcept {
    const std::size_t position = static_cast<std::size_t>(field);
    const bool edge = position >= first_edge_field && position < first_edge_field + edge_field_count;
    return edge;
}

// An edge as it is assembled: whether it is drawn, and its line.
struct AssembledEdge final {
    bool drawn{};
    MaterialBorder border{};
    MaterialEdgeLines lines{MaterialEdgeLines::single};
};

// Writes an edge field's value into `edge`: a style (none stops the edge
// being drawn), a width or a colour.
void apply_edge_declaration(const MaterialDeclaration& declaration, AssembledEdge& edge) {
    const std::size_t part = (static_cast<std::size_t>(declaration.field) - first_edge_field) % 3U;
    if (part == 0U) {
        edge.drawn = declaration.border_style.has_value();
        if (declaration.border_style) edge.border.style = *declaration.border_style;
        edge.lines = declaration.edge_lines;
        return;
    }
    if (part == 1U) {
        edge.border.width = declaration.number;
        return;
    }
    edge.border.color = declaration.colour;
}

// The edge as a material draws it: none when it is not drawn or has no
// width.
std::optional<MaterialBorder> drawn_edge(const AssembledEdge& edge) {
    if (!edge.drawn || edge.border.width == 0.0) return std::nullopt;
    return edge.border;
}

// A colour with each channel halved, rounded down: a groove's or ridge's
// shaded half.
Color edge_shade(Color colour) noexcept {
    const Color shade{static_cast<std::uint8_t>(colour.red / 2U), static_cast<std::uint8_t>(colour.green / 2U),
                      static_cast<std::uint8_t>(colour.blue / 2U), colour.alpha};
    return shade;
}

// Appends edge `edge`'s two lines (MaterialEdgeLines) for `border` to
// `keylines`, the outer first.
void append_edge_lines(MaterialEdge edge, const MaterialBorder& border, MaterialEdgeLines lines,
                       std::vector<MaterialKeyline>& keylines) {
    const double width = border.width;
    MaterialKeyline outer{};
    outer.edge = edge;
    outer.color = border.color;
    MaterialKeyline inner = outer;
    if (lines == MaterialEdgeLines::double_line) {
        outer.width = width / 3.0;
        inner.width = width / 3.0;
        inner.inset = width * 2.0 / 3.0;
    } else {
        outer.width = width / 2.0;
        inner.width = width / 2.0;
        inner.inset = width / 2.0;
        const bool lit_side = edge == MaterialEdge::bottom || edge == MaterialEdge::right;
        // A groove's outer half is shaded on the top and left; a ridge's on
        // the bottom and right.
        const bool outer_shaded = (lines == MaterialEdgeLines::groove) != lit_side;
        if (outer_shaded) {
            outer.color = edge_shade(border.color);
        } else {
            inner.color = edge_shade(border.color);
        }
    }
    keylines.push_back(outer);
    keylines.push_back(inner);
}

// Each edge from the floor's, then its declared fields; into `material`,
// as one uniform border when the four are equal, and an edge of two lines
// as its keylines.
void assemble_edges(const SurfaceMaterial& floor,
                    const std::array<const MaterialDeclaration*, material_field_count>& fields,
                    SurfaceMaterial& material) {
    const bool single = !floor.border_edges.empty();
    const std::array<const std::optional<MaterialBorder>*, 4> floor_edges{
        single ? &floor.border_edges.top : &floor.border, single ? &floor.border_edges.right : &floor.border,
        single ? &floor.border_edges.bottom : &floor.border, single ? &floor.border_edges.left : &floor.border};
    std::array<std::optional<MaterialBorder>, 4> drawn{};
    for (std::size_t edge_index = 0U; edge_index < drawn.size(); ++edge_index) {
        const std::optional<MaterialBorder>& base = *floor_edges[edge_index];
        AssembledEdge edge{};
        edge.drawn = base.has_value();
        if (base) edge.border = *base;
        for (std::size_t part = 0U; part < 3U; ++part) {
            const MaterialDeclaration* const declaration = fields[first_edge_field + 3U * edge_index + part];
            if (declaration != nullptr && !(*declaration).initial) apply_edge_declaration(*declaration, edge);
        }
        drawn[edge_index] = drawn_edge(edge);
        if (!drawn[edge_index] || edge.lines == MaterialEdgeLines::single) continue;
        append_edge_lines(static_cast<MaterialEdge>(edge_index), *drawn[edge_index], edge.lines, material.keylines);
        drawn[edge_index].reset();
    }
    const bool uniform = drawn[0] == drawn[1] && drawn[0] == drawn[2] && drawn[0] == drawn[3];
    if (uniform) {
        material.border = drawn[0];
        material.border_edges = MaterialBorderEdges{};
        return;
    }
    material.border.reset();
    material.border_edges.top = drawn[0];
    material.border_edges.right = drawn[1];
    material.border_edges.bottom = drawn[2];
    material.border_edges.left = drawn[3];
}

// The box an authored gradient is resolved for when it is checked: a part's
// own box may still refuse its assembly.
constexpr Size sample_box{100.0, 100.0};

// A position the image fills admit: anchors within [0, 1], offsets of at
// most 4096 logical units (valid_surface_material's bounds).
bool image_position_admitted(const MaterialImagePosition& position) noexcept {
    const std::array<BoxLength, 2> axes{position.across, position.down};
    for (const BoxLength axis : axes) {
        const bool anchored = std::isfinite(axis.fraction) && axis.fraction >= 0.0 && axis.fraction <= 1.0;
        const bool offset = std::isfinite(axis.units) && std::abs(axis.units) <= 4096.0;
        if (!anchored || !offset) return false;
    }
    return true;
}

// A size of finite parts, a side's not negative and not zero for every box.
// The image scale it makes is checked with the assembled material.
bool image_size_admitted(const MaterialImageSize& size) noexcept {
    const bool sided = size.sizing == MaterialImageSizing::width || size.sizing == MaterialImageSizing::height;
    const BoxLength length = size.length;
    if (!sided) {
        const bool known = size.sizing == MaterialImageSizing::natural || size.sizing == MaterialImageSizing::cover ||
                           size.sizing == MaterialImageSizing::contain || size.sizing == MaterialImageSizing::stretch;
        return known && length == BoxLength{};
    }
    const bool finite_parts = std::isfinite(length.fraction) && std::isfinite(length.units);
    const bool positive = length.fraction >= 0.0 && length.units >= 0.0 && (length.fraction > 0.0 || length.units > 0.0);
    return finite_parts && positive;
}

// Whether `declaration`, alone over the default material, is a material:
// an edge field over a drawn default edge, a relative corner as a fraction
// of a unit side, an authored gradient resolved for sample_box.
bool material_declaration_admitted(const MaterialDeclaration& declaration) {
    SurfaceMaterial checked{};
    if (declaration.initial || declaration.field == MaterialField::background_colour) return true;
    if (declaration.field == MaterialField::border_image) {
        if (!declaration.border_image) return true;
        if ((*declaration.border_image).image_mode != MaterialImageMode::nine_patch) return false;
        checked.fills = {*declaration.border_image};
        const bool admitted = valid_surface_material(checked);
        return admitted;
    }
    if (declaration.field == MaterialField::fill_positions) {
        for (const MaterialImagePosition& position : declaration.image_positions) {
            if (!image_position_admitted(position)) return false;
        }
        return true;
    }
    if (declaration.field == MaterialField::fill_sizes) {
        for (const MaterialImageSize& size : declaration.image_sizes) {
            if (!image_size_admitted(size)) return false;
        }
        return true;
    }
    if (declaration.field == MaterialField::fill_repeats) {
        for (const MaterialImageRepeat repeat : declaration.image_repeats) {
            if (repeat != MaterialImageRepeat::repeat && repeat != MaterialImageRepeat::no_repeat) return false;
        }
        return true;
    }
    if (declaration.field == MaterialField::fill_blend_modes) {
        for (const MaterialBlendMode mode : declaration.blend_modes) {
            if (static_cast<std::uint8_t>(mode) > static_cast<std::uint8_t>(MaterialBlendMode::screen)) return false;
        }
        return true;
    }
    if (corner_field(declaration.field)) {
        checked.corner_radius = declaration.number;
        const bool admitted = valid_surface_material(checked);
        return admitted;
    }
    if (declaration.field == MaterialField::fills) {
        if (declaration.fills.empty()) return true;
        const std::vector<AuthoredGradient>& gradients = declaration.gradients;
        if (!gradients.empty() && gradients.size() != declaration.fills.size()) return false;
        checked.fills = declaration.fills;
        for (std::size_t layer = 0U; layer < gradients.size(); ++layer) {
            if (gradients[layer].kind == MaterialFillKind::solid) continue;
            if (!valid_authored_gradient(gradients[layer])) return false;
            resolve_authored_gradient(gradients[layer], sample_box, checked.fills[layer]);
        }
        const bool admitted = valid_surface_material(checked);
        return admitted;
    }
    if (declaration.field == MaterialField::mask && !declaration.gradients.empty()) {
        const AuthoredGradient& gradient = declaration.gradients.front();
        if (declaration.gradients.size() != 1U || !declaration.mask) return false;
        checked.mask = declaration.mask;
        if (gradient.kind != MaterialFillKind::solid) {
            if (!valid_authored_gradient(gradient)) return false;
            resolve_authored_gradient(gradient, sample_box, *checked.mask);
        }
        const bool admitted = valid_surface_material(checked);
        return admitted;
    }
    if (!edge_field(declaration.field)) {
        apply_material_declaration(declaration, checked);
        const bool admitted = valid_surface_material(checked);
        return admitted;
    }
    const bool lines_known =
        static_cast<std::uint8_t>(declaration.edge_lines) <= static_cast<std::uint8_t>(MaterialEdgeLines::ridge);
    if (!lines_known) return false;
    AssembledEdge edge{};
    edge.drawn = true;
    apply_edge_declaration(declaration, edge);
    const bool width_ok = std::isfinite(edge.border.width) && edge.border.width >= 0.0;
    checked.border_edges.top = drawn_edge(edge);
    const bool admitted = width_ok && valid_surface_material(checked);
    return admitted;
}

} // namespace

bool material_fields_read_box(
    const std::array<const MaterialDeclaration*, material_field_count>& fields) noexcept {
    for (const MaterialDeclaration* const declaration : fields) {
        if (declaration == nullptr || (*declaration).initial) continue;
        if (corner_field((*declaration).field) && (*declaration).relative) return true;
        if ((*declaration).field == MaterialField::fill_sizes) {
            for (const MaterialImageSize& size : (*declaration).image_sizes) {
                const bool proportional = size.sizing == MaterialImageSizing::cover ||
                                          size.sizing == MaterialImageSizing::contain;
                const bool sided = size.sizing == MaterialImageSizing::width ||
                                   size.sizing == MaterialImageSizing::height;
                if (proportional || (sided && size.length.fraction != 0.0)) return true;
            }
            continue;
        }
        const bool layers = (*declaration).field == MaterialField::fills || (*declaration).field == MaterialField::mask;
        if (!layers) continue;
        for (const AuthoredGradient& gradient : (*declaration).gradients) {
            if (gradient.kind != MaterialFillKind::solid && !authored_gradient_fixed(gradient)) return true;
        }
    }
    return false;
}

// -- em lengths (T3 P8c) ----------------------------------------------------------

namespace {

enum class LengthWalkMode : std::uint8_t { read, write, add_em };

// One walk over a declaration's lengths in their fixed order: reading them
// into `read`, writing them from `values`, or adding `em` times each of
// `values` to them.
struct LengthWalk final {
    LengthWalkMode mode{LengthWalkMode::read};
    std::vector<double>& read;
    std::span<const double> values{};
    double em{};
    std::size_t index{};

    void visit(double& length) {
        const bool given = index < values.size();
        switch (mode) {
        case LengthWalkMode::read: read.push_back(length); break;
        case LengthWalkMode::write:
            if (given) length = values[index];
            break;
        case LengthWalkMode::add_em:
            if (given) length = length + em * values[index];
            break;
        default: break; // Not a listed mode: the length is kept.
        }
        index = index + 1U;
    }
};

bool width_field(MaterialField field) noexcept {
    const bool width = field == MaterialField::border_top_width || field == MaterialField::border_right_width ||
                       field == MaterialField::border_bottom_width || field == MaterialField::border_left_width;
    return width;
}

void walk_material_lengths(MaterialDeclaration& declaration, LengthWalk& walk) {
    const MaterialField field = declaration.field;
    const bool corner = corner_field(field) && !declaration.relative;
    if (width_field(field) || corner || field == MaterialField::filter_blur) walk.visit(declaration.number);
    for (AuthoredGradient& gradient : declaration.gradients) {
        walk.visit(gradient.centre_x.units);
        walk.visit(gradient.centre_y.units);
        walk.visit(gradient.radius_x.units);
        walk.visit(gradient.radius_y.units);
        for (AuthoredGradientStop& stop : gradient.stops) walk.visit(stop.position.units);
    }
    for (MaterialImagePosition& position : declaration.image_positions) {
        walk.visit(position.across.units);
        walk.visit(position.down.units);
    }
    for (MaterialImageSize& size : declaration.image_sizes) walk.visit(size.length.units);
    for (MaterialShadow& shadow : declaration.shadows) {
        walk.visit(shadow.offset.x);
        walk.visit(shadow.offset.y);
        walk.visit(shadow.blur_radius);
        walk.visit(shadow.spread);
    }
    for (MaterialGlow& glow : declaration.glows) {
        walk.visit(glow.blur_radius);
        walk.visit(glow.spread);
    }
    for (MaterialKeyline& keyline : declaration.keylines) {
        walk.visit(keyline.width);
        walk.visit(keyline.inset);
    }
    if (declaration.drop_shadow) {
        MaterialDropShadow& shadow = *declaration.drop_shadow;
        walk.visit(shadow.offset.x);
        walk.visit(shadow.offset.y);
        walk.visit(shadow.blur_radius);
    }
    if (declaration.reflection) {
        MaterialReflection& reflection = *declaration.reflection;
        walk.visit(reflection.gap);
        walk.visit(reflection.reach);
    }
}

// The part metrics and shape lengths (RecipeField::block_size on).
bool part_length_field(RecipeField field) noexcept {
    const bool length = field == RecipeField::block_size || field == RecipeField::minimum_inline_size ||
                        field == RecipeField::padding_inline || field == RecipeField::gap ||
                        field == RecipeField::margin_block_start || field == RecipeField::margin_block_end ||
                        field == RecipeField::taper || field == RecipeField::flare ||
                        field == RecipeField::edge_shadow_width;
    return length;
}

void walk_recipe_lengths(RecipeDeclaration& declaration, LengthWalk& walk) {
    const RecipeField field = declaration.field;
    if (field == RecipeField::outline_width || field == RecipeField::outline_offset) walk.visit(declaration.number);
    if (field == RecipeField::default_outline_width || field == RecipeField::stroke_width) {
        walk.visit(declaration.number);
    }
    if (part_length_field(field)) walk.visit(declaration.number);
    if (field == RecipeField::font_size || field == RecipeField::letter_spacing) walk.visit(declaration.number);
    if (field != RecipeField::text_shadow) return;
    walk.visit(declaration.shadow_offset.x);
    walk.visit(declaration.shadow_offset.y);
    walk.visit(declaration.shadow_blur);
}

} // namespace

void material_declaration_lengths(const MaterialDeclaration& declaration, std::vector<double>& lengths) {
    lengths.clear();
    MaterialDeclaration walked = declaration;
    LengthWalk walk{.mode = LengthWalkMode::read, .read = lengths};
    walk_material_lengths(walked, walk);
}

void set_material_declaration_lengths(MaterialDeclaration& declaration, std::span<const double> lengths) noexcept {
    std::vector<double> unread{};
    LengthWalk walk{.mode = LengthWalkMode::write, .read = unread, .values = lengths};
    walk_material_lengths(declaration, walk);
}

void add_material_em(MaterialDeclaration& declaration, double em) noexcept {
    if (declaration.em_lengths.empty()) return;
    std::vector<double> unread{};
    LengthWalk walk{.mode = LengthWalkMode::add_em, .read = unread, .values = declaration.em_lengths, .em = em};
    walk_material_lengths(declaration, walk);
}

void recipe_declaration_lengths(const RecipeDeclaration& declaration, std::vector<double>& lengths) {
    lengths.clear();
    RecipeDeclaration walked = declaration;
    LengthWalk walk{.mode = LengthWalkMode::read, .read = lengths};
    walk_recipe_lengths(walked, walk);
}

void set_recipe_declaration_lengths(RecipeDeclaration& declaration, std::span<const double> lengths) noexcept {
    std::vector<double> unread{};
    LengthWalk walk{.mode = LengthWalkMode::write, .read = unread, .values = lengths};
    walk_recipe_lengths(declaration, walk);
}

void add_recipe_em(RecipeDeclaration& declaration, double em) noexcept {
    if (declaration.em_lengths.empty()) return;
    std::vector<double> unread{};
    LengthWalk walk{.mode = LengthWalkMode::add_em, .read = unread, .values = declaration.em_lengths, .em = em};
    walk_recipe_lengths(declaration, walk);
}

// -- current colours (T3 P8c) ----------------------------------------------------

namespace {

enum class ColourWalkMode : std::uint8_t { read, write, set_current };

// One walk over a declaration's colours in their fixed order: reading them
// into `read`, writing them from `values`, or setting those `slots` names
// to `colour`.
struct ColourWalk final {
    ColourWalkMode mode{ColourWalkMode::read};
    std::vector<Color>& read;
    std::span<const Color> values{};
    std::span<const std::uint16_t> slots{};
    Color colour{};
    std::size_t index{};
    std::size_t slot{};

    void visit(Color& visited) {
        switch (mode) {
        case ColourWalkMode::read: read.push_back(visited); break;
        case ColourWalkMode::write:
            if (index < values.size()) visited = values[index];
            break;
        case ColourWalkMode::set_current:
            if (slot < slots.size() && slots[slot] == index) {
                visited = colour;
                slot = slot + 1U;
            }
            break;
        default: break; // Not a listed mode: the colour is kept.
        }
        index = index + 1U;
    }
};

void walk_material_colours(MaterialDeclaration& declaration, ColourWalk& walk) {
    walk.visit(declaration.colour);
    for (AuthoredGradient& gradient : declaration.gradients) {
        for (AuthoredGradientStop& stop : gradient.stops) walk.visit(stop.colour);
    }
    for (MaterialShadow& shadow : declaration.shadows) walk.visit(shadow.color);
    for (MaterialGlow& glow : declaration.glows) walk.visit(glow.color);
    for (MaterialKeyline& keyline : declaration.keylines) walk.visit(keyline.color);
    if (declaration.drop_shadow) walk.visit((*declaration.drop_shadow).color);
}

} // namespace

void material_declaration_colours(const MaterialDeclaration& declaration, std::vector<Color>& colours) {
    colours.clear();
    MaterialDeclaration walked = declaration;
    ColourWalk walk{.mode = ColourWalkMode::read, .read = colours};
    walk_material_colours(walked, walk);
}

void set_material_declaration_colours(MaterialDeclaration& declaration, std::span<const Color> colours) noexcept {
    std::vector<Color> unread{};
    ColourWalk walk{.mode = ColourWalkMode::write, .read = unread, .values = colours};
    walk_material_colours(declaration, walk);
}

void set_material_current_colour(MaterialDeclaration& declaration, Color colour) noexcept {
    if (declaration.current_colours.empty()) return;
    std::vector<Color> unread{};
    ColourWalk walk{
        .mode = ColourWalkMode::set_current, .read = unread, .slots = declaration.current_colours, .colour = colour};
    walk_material_colours(declaration, walk);
}

bool current_colours_fit(const MaterialDeclaration& declaration) {
    if (declaration.current_colours.empty()) return true;
    std::vector<Color> colours{};
    material_declaration_colours(declaration, colours);
    for (std::size_t index = 0U; index < declaration.current_colours.size(); ++index) {
        const std::size_t slot = declaration.current_colours[index];
        if (slot >= colours.size()) return false;
        if (index > 0U && slot <= declaration.current_colours[index - 1U]) return false;
    }
    return true;
}

bool material_fields_read_current_colour(
    const std::array<const MaterialDeclaration*, material_field_count>& fields) noexcept {
    for (const MaterialDeclaration* const declaration : fields) {
        if (declaration != nullptr && !(*declaration).initial && !(*declaration).current_colours.empty()) return true;
    }
    return false;
}

bool material_fields_read_em(const std::array<const MaterialDeclaration*, material_field_count>& fields) noexcept {
    for (const MaterialDeclaration* const declaration : fields) {
        if (declaration != nullptr && !(*declaration).initial && !(*declaration).em_lengths.empty()) return true;
    }
    return false;
}

void assemble_material(const SurfaceMaterial& floor,
                       const std::array<const MaterialDeclaration*, material_field_count>& fields, Size box,
                       MaterialAssemblyContext context, SurfaceMaterial& material) {
    if (!material_fields_read_em(fields) && !material_fields_read_current_colour(fields)) {
        assemble_material(floor, fields, box, material);
        return;
    }
    // The declarations with em lengths or current colours, copied and
    // taken at the part's em and text colour.
    std::array<const MaterialDeclaration*, material_field_count> sized = fields;
    std::vector<MaterialDeclaration> scaled{};
    scaled.reserve(material_field_count);
    for (std::size_t position = 0U; position < material_field_count; ++position) {
        const MaterialDeclaration* const declaration = fields[position];
        if (declaration == nullptr) continue;
        if ((*declaration).em_lengths.empty() && (*declaration).current_colours.empty()) continue;
        scaled.push_back(*declaration);
        add_material_em(scaled.back(), context.em);
        set_material_current_colour(scaled.back(), context.current_colour);
        sized[position] = &scaled.back();
    }
    assemble_material(floor, sized, box, material);
}

void assemble_material(const SurfaceMaterial& floor,
                       const std::array<const MaterialDeclaration*, material_field_count>& fields, Size box,
                       SurfaceMaterial& material) {
    material = floor;
    bool edges = false;
    bool fills = false;
    bool corners = false;
    for (const MaterialDeclaration* const declaration : fields) {
        if (declaration == nullptr || (*declaration).initial) continue;
        if (edge_field((*declaration).field)) {
            edges = true;
            continue;
        }
        if (fill_field((*declaration).field)) {
            fills = true;
            continue;
        }
        if (corner_field((*declaration).field)) {
            corners = true;
            continue;
        }
        // The placements apply to the glows, assembled first.
        if ((*declaration).field == MaterialField::glow_placements) continue;
        apply_material_declaration(*declaration, material);
    }
    const MaterialDeclaration* const placements = set_field(fields, MaterialField::glow_placements);
    if (placements != nullptr) apply_material_declaration(*placements, material);
    const MaterialDeclaration* const mask = set_field(fields, MaterialField::mask);
    if (mask != nullptr && material.mask && !(*mask).gradients.empty() &&
        (*mask).gradients.front().kind != MaterialFillKind::solid) {
        resolve_authored_gradient((*mask).gradients.front(), box, *material.mask);
    }
    if (fills) compose_fills(floor, fields, box, material);
    if (corners) compose_corners(floor, fields, std::min(box.width, box.height), material);
    const MaterialDeclaration* const border_image = set_field(fields, MaterialField::border_image);
    const bool framed = border_image != nullptr && (*border_image).border_image.has_value();
    if (edges && !framed) assemble_edges(floor, fields, material);
    if (!framed) return;
    // The border image replaces the border's drawing.
    material.fills.push_back(*(*border_image).border_image);
    material.border.reset();
    material.border_edges = MaterialBorderEdges{};
}

RuleDeclaration RuleDeclaration::primitive(std::string_view primitive_name) {
    RuleDeclaration result{};
    result.property = std::string(primitive_property);
    result.kind = RuleValueKind::name;
    result.name = std::string(primitive_name);
    return result;
}

RuleDeclaration RuleDeclaration::number_value(std::string_view parameter, double value) {
    RuleDeclaration result{};
    result.property = std::string(parameter);
    result.kind = RuleValueKind::number;
    result.number = value;
    return result;
}

RuleDeclaration RuleDeclaration::colour_value(std::string_view parameter, Color value) {
    RuleDeclaration result{};
    result.property = std::string(parameter);
    result.kind = RuleValueKind::colour;
    result.colour = value;
    return result;
}

RuleDeclaration RuleDeclaration::choice_value(std::string_view parameter, std::string_view choice) {
    RuleDeclaration result{};
    result.property = std::string(parameter);
    result.kind = RuleValueKind::name;
    result.name = std::string(choice);
    return result;
}

RuleDeclaration RuleDeclaration::expression_value(std::string_view parameter,
                                                  std::span<const ExpressionInstruction> program, double fallback) {
    RuleDeclaration result{};
    result.property = std::string(parameter);
    result.kind = RuleValueKind::expression;
    result.number = fallback;
    result.program.assign(program.begin(), program.end());
    return result;
}

std::optional<ExpressionInputIndex> CompiledThemeRules::published_value_input(std::string_view name) const noexcept {
    if (name.empty()) return std::nullopt;
    for (std::size_t index = 0; index < published_values_.size(); ++index) {
        if (published_values_[index] == name) return published_value_position(index);
    }
    return std::nullopt;
}

// -- compiling -----------------------------------------------------------------

namespace {

// Precedence, lowest first, with authored order breaking ties so that the
// later rule stays later. The order is total: the rules of one level come
// from one sheet (the engine's own table is all built_in), whose positions
// are distinct, so std::sort gives what a stable sort would, without the
// temporary buffer a stable sort allocates.
struct LowerPrecedence final {
    template <typename Rule>
    bool operator()(const Rule& left, const Rule& right) const noexcept {
        if (left.level != right.level) return left.level < right.level;
        if (!(left.specificity == right.specificity)) return left.specificity < right.specificity;
        return left.order < right.order;
    }
};

} // namespace

bool CompiledThemeRules::add_scoped(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                    const ThemeRule& rule, CompiledThemeRules& result, CompiledRule& compiled) {
    // scoped_names_ holds the names the sheet's conditions read
    // (add_rules collects them first): a condition's name is among them
    // unless it was past the limit, and a definition of another name is
    // read by nothing, so it is not kept.
    for (const ScopedCondition& condition : rule.scoped_conditions) {
        const std::optional<std::size_t> position = result.scoped_name_position(condition.name);
        if (!position) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::scoped_property_invalid, index, rule, condition.name));
            return false;
        }
        compiled.scoped_conditions.push_back(
            CompiledScoped{.name = static_cast<std::uint32_t>(*position), .definition = condition.definition});
    }
    for (const ScopedDefinition& definition : rule.scoped_definitions) {
        if (!scoped_name_valid(definition.name) || definition.definition == custom_property_undefined) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::scoped_property_invalid, index, rule, definition.name));
            return false;
        }
        const std::optional<std::size_t> position = result.scoped_name_position(definition.name);
        if (!position) continue;
        compiled.scoped_definitions.push_back(
            CompiledScoped{.name = static_cast<std::uint32_t>(*position), .definition = definition.definition});
    }
    return true;
}

std::optional<std::size_t> CompiledThemeRules::scoped_name_position(std::string_view name) const noexcept {
    for (std::size_t position = 0U; position < scoped_names_.size(); ++position) {
        if (scoped_names_[position] == name) return position;
    }
    return std::nullopt;
}

void CompiledThemeRules::collect_scoped_names(const ThemeRuleSheet& sheet) {
    // The names the conditions read, in sheet order, at most
    // maximum_scoped_custom_properties; a rule reading one past them is
    // dropped by add_scoped.
    for (const ThemeRule& rule : sheet.rules) {
        for (const ScopedCondition& condition : rule.scoped_conditions) {
            if (!scoped_name_valid(condition.name) || scoped_name_position(condition.name)) continue;
            if (scoped_names_.size() >= maximum_scoped_custom_properties) continue;
            scoped_names_.push_back(condition.name);
        }
    }
}

bool CompiledThemeRules::add_reads(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                   const ThemeRule& rule, const CompiledThemeRules& result, CompiledRule& compiled) {
    // read_names_ holds every name a rule reads unless it was past the
    // limit; a rule reading one past it is dropped.
    for (const std::string& name : rule.reads) {
        const std::optional<std::size_t> position = result.read_name_position(name);
        if (!position) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::scoped_property_invalid, index, rule, name));
            return false;
        }
        compiled.reads.push_back(static_cast<std::uint32_t>(*position));
    }
    return true;
}

std::optional<std::size_t> CompiledThemeRules::read_name_position(std::string_view name) const noexcept {
    for (std::size_t position = 0U; position < read_names_.size(); ++position) {
        if (read_names_[position] == name) return position;
    }
    return std::nullopt;
}

void CompiledThemeRules::add_rules(CompiledThemeRules& result, const ThemeRuleSheet& sheet,
                                   const PrimitiveRegistry& registry, bool engine_table) {
    const std::size_t input_count = result.input_count();
    result.collect_scoped_names(sheet);
    // The names the rules read (P12b), in sheet order, at most
    // maximum_read_custom_properties.
    for (const ThemeRule& rule : sheet.rules) {
        for (const std::string& name : rule.reads) {
            if (!scoped_name_valid(name) || result.read_name_position(name)) continue;
            if (result.read_names_.size() >= maximum_read_custom_properties) continue;
            result.read_names_.push_back(name);
        }
    }
    result.rules_.reserve(result.rules_.size() + sheet.rules.size());
    for (std::size_t index = 0; index < sheet.rules.size(); ++index) {
        const ThemeRule& rule = sheet.rules[index];
        const EngineVersion needed = rule.requires_engine.value_or(sheet.requires_engine);
        if (current_engine_version < needed) {
            result.diagnostics_.push_back(diagnostic(RuleDiagnosticKind::needs_newer_engine, index, rule, {}));
            continue;
        }
        // The engine's own table is all built_in; a theme authors only the
        // authored levels, and an inline style only the inline ones.
        const bool inline_level =
            rule.level == CascadeLevel::inline_style || rule.level == CascadeLevel::important_inline_style;
        bool level_allowed = authored(rule.level);
        if (engine_table) level_allowed = rule.level == CascadeLevel::built_in;
        // A theme rule's copy in an inline style compiled against the
        // theme (P12b) keeps the theme rule's authored level.
        if (result.inline_style_) level_allowed = inline_level || (rule.source_position != 0U && authored(rule.level));
        // A user theme authors only the user levels.
        if (result.user_theme_) level_allowed = user_level(rule.level);
        if (!level_allowed) {
            result.diagnostics_.push_back(diagnostic(RuleDiagnosticKind::level_not_authored, index, rule, {}));
            continue;
        }
        const std::optional<RuleDiagnosticKind> selector = selector_defect(rule, result.inline_style_);
        if (selector) {
            result.diagnostics_.push_back(diagnostic(*selector, index, rule, {}));
            continue;
        }
        if (!media_condition_valid(rule.media)) {
            result.diagnostics_.push_back(diagnostic(RuleDiagnosticKind::media_invalid, index, rule, {}));
            continue;
        }
        CompiledRule compiled{};
        compiled.level = rule.level;
        compiled.media = rule.media;
        compiled.specificity = rule.specificity ? *rule.specificity : rule_specificity(rule.selector);
        compiled.order = index;
        compiled.source_position = rule.source_position;
        compiled.provenance = rule.provenance;
        if (engine_table) compiled.provenance.layer = ThemeLayer::built_in;
        compiled.selector = rule.selector;
        for (const AnatomyState state : rule.selector.states) compiled.states.insert(state);
        for (const RuleAncestor& ancestor : rule.selector.ancestors) {
            if (!ancestor.control_type.empty()) compiled.ancestor_keys.add({}, ancestor.control_type);
            if (!ancestor.stable_id.empty()) compiled.ancestor_keys.add("#", ancestor.stable_id);
            for (const std::string& name : ancestor.classes) compiled.ancestor_keys.add(".", name);
        }
        if (!rule.selector.ancestors.empty()) result.uses_ancestors_ = true;
        // The primitive first, so that the parameters can be checked against
        // its declarations.
        for (const RuleDeclaration& declaration : rule.declarations) {
            if (declaration.property != primitive_property) continue;
            if (compiled.selects) {
                result.diagnostics_.push_back(
                    diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, declaration.property));
                continue;
            }
            if (declaration.kind != RuleValueKind::name) {
                result.diagnostics_.push_back(
                    diagnostic(RuleDiagnosticKind::primitive_value_not_a_name, index, rule, declaration.property));
                continue;
            }
            if (declaration.name == built_in_primitive_name) {
                compiled.selects = true;
                compiled.primitive = nullptr;
                continue;
            }
            const EnginePrimitive* const primitive = registry.find(declaration.name);
            if (primitive == nullptr) {
                result.diagnostics_.push_back(
                    diagnostic(RuleDiagnosticKind::unknown_primitive, index, rule, declaration.property));
                continue;
            }
            const std::optional<RuleDiagnosticKind> defect = primitive_defect(*primitive, rule.selector);
            if (defect) {
                result.diagnostics_.push_back(diagnostic(*defect, index, rule, declaration.property));
                continue;
            }
            compiled.selects = true;
            compiled.primitive = primitive;
        }
        for (const RuleDeclaration& declaration : rule.declarations) {
            if (declaration.property == primitive_property) continue;
            if (declaration.property == blend_property) {
                add_blend(result.diagnostics_, index, rule, declaration, compiled);
                continue;
            }
            if (declaration.property == font_property) {
                add_font(result.diagnostics_, index, rule, declaration, compiled);
                continue;
            }
            if (declaration.property == effect_property) {
                add_effect(result.diagnostics_, index, rule, declaration, compiled);
                continue;
            }
            if (declaration.property == effect_primary_property ||
                declaration.property == effect_secondary_property) {
                add_effect_colour(result.diagnostics_, index, rule, declaration, compiled);
                continue;
            }
            if (compiled.primitive == nullptr) {
                result.diagnostics_.push_back(
                    diagnostic(RuleDiagnosticKind::parameter_without_primitive, index, rule, declaration.property));
                continue;
            }
            std::size_t parameter = 0U;
            const std::optional<RuleDiagnosticKind> defect =
                parameter_defect((*compiled.primitive).descriptor(), declaration, parameter);
            if (defect) {
                result.diagnostics_.push_back(diagnostic(*defect, index, rule, declaration.property));
                continue;
            }
            bool repeated = false;
            for (const CompiledDeclaration& earlier : compiled.parameters) {
                if (earlier.parameter == parameter) repeated = true;
            }
            if (repeated) {
                result.diagnostics_.push_back(
                    diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, declaration.property));
                continue;
            }
            CompiledDeclaration value{};
            value.parameter = parameter;
            if (declaration.kind == RuleValueKind::expression) {
                const PrimitiveDescriptor& descriptor = (*compiled.primitive).descriptor();
                ExpressionLimits limits{};
                limits.inputs = input_count;
                limits.parameters = descriptor.parameters.size();
                const ExpressionCompilation built =
                    value.program.compile(declaration.program, declaration.number, limits);
                if (built.defect != ExpressionDefect::none) {
                    RuleDiagnostic invalid =
                        diagnostic(RuleDiagnosticKind::expression_invalid, index, rule, declaration.property);
                    invalid.expression = built.defect;
                    result.diagnostics_.push_back(std::move(invalid));
                    continue;
                }
                if (reads_colour_parameter(descriptor, value.program)) {
                    result.diagnostics_.push_back(
                        diagnostic(RuleDiagnosticKind::expression_reads_colour, index, rule, declaration.property));
                    continue;
                }
                value.expression = true;
                value.value.kind = PrimitiveParameterKind::number;
                value.value.number = declaration.number;
            } else if (declaration.kind == RuleValueKind::number) {
                value.value.kind = PrimitiveParameterKind::number;
                value.value.number = declaration.number;
                value.em = declaration.number_em;
            } else if (declaration.kind == RuleValueKind::name) {
                const PrimitiveParameterDeclaration& declared =
                    (*compiled.primitive).descriptor().parameters[parameter];
                value.value.kind = PrimitiveParameterKind::choice;
                value.value.number = static_cast<double>(*find_choice(declared, declaration.name));
            } else {
                value.value.kind = PrimitiveParameterKind::colour;
                value.value.colour = declaration.colour;
                value.origin = declaration.origin;
            }
            compiled.parameters.push_back(value);
        }
        add_transitions(result.diagnostics_, index, rule, compiled);
        if (result.user_theme_) raise_user_opacity(result.diagnostics_, index, rule, compiled);
        add_ambient(result.diagnostics_, index, rule, compiled);
        add_materials(result.diagnostics_, index, rule, compiled);
        add_recipe_fields(result.diagnostics_, index, rule, compiled);
        add_backdrop(result.diagnostics_, index, rule, compiled);
        add_band(result.diagnostics_, index, rule, compiled);
        if (!add_scoped(result.diagnostics_, index, rule, result, compiled)) continue;
        if (!add_reads(result.diagnostics_, index, rule, result, compiled)) continue;
        const bool declares = compiled.selects || !compiled.parameters.empty() || !compiled.transitions.empty() ||
                              compiled.ambient.has_value() || compiled.blend.has_value() || compiled.font.has_value() ||
                              compiled.effect.has_value() || compiled.effect_primary.has_value() ||
                              compiled.effect_secondary.has_value() ||
                              !compiled.materials.empty() || !compiled.recipe_fields.empty() ||
                              !compiled.scoped_definitions.empty() || compiled.backdrop.has_value() ||
                              compiled.band.has_value();
        // A rule of definitions no condition reads declares nothing the
        // cascade uses, and is dropped without a diagnostic.
        if (!declares && !rule.scoped_definitions.empty()) continue;
        if (!declares) {
            result.diagnostics_.push_back(diagnostic(RuleDiagnosticKind::rule_empty, index, rule, {}));
            continue;
        }
        if (!compiled.recipe_fields.empty()) result.recipe_fields_set_ = true;
        result.rules_.push_back(std::move(compiled));
    }
}

// -- the blend property (E9.6) ----------------------------------------------------

namespace {

constexpr std::array<std::string_view, 3> part_blend_names{"normal", "multiply", "screen"};

} // namespace

void CompiledThemeRules::add_blend(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                                   const RuleDeclaration& declaration, CompiledRule& compiled) {
    if (compiled.blend) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, declaration.property));
        return;
    }
    if (declaration.kind != RuleValueKind::name) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::blend_invalid, index, rule, declaration.property));
        return;
    }
    for (std::size_t position = 0U; position < part_blend_names.size(); ++position) {
        if (part_blend_names[position] != declaration.name) continue;
        compiled.blend = static_cast<PartBlend>(static_cast<std::uint8_t>(position));
        return;
    }
    diagnostics.push_back(diagnostic(RuleDiagnosticKind::blend_invalid, index, rule, declaration.property));
}

void CompiledThemeRules::add_effect_colour(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                           const ThemeRule& rule, const RuleDeclaration& declaration,
                                           CompiledRule& compiled) {
    const bool primary = declaration.property == effect_primary_property;
    std::optional<EffectColourDeclaration>& slot = primary ? compiled.effect_primary : compiled.effect_secondary;
    if (slot) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, declaration.property));
        return;
    }
    if (declaration.kind != RuleValueKind::colour || declaration.origin.source != ColourSource::role) {
        diagnostics.push_back(
            diagnostic(RuleDiagnosticKind::effect_colour_invalid, index, rule, declaration.property));
        return;
    }
    EffectColourDeclaration colour{};
    colour.colour = declaration.colour;
    colour.origin = declaration.origin;
    slot = colour;
}

void CompiledThemeRules::add_effect(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                    const ThemeRule& rule, const RuleDeclaration& declaration,
                                    CompiledRule& compiled) {
    if (compiled.effect) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, declaration.property));
        return;
    }
    if (declaration.kind != RuleValueKind::name) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::effect_invalid, index, rule, declaration.property));
        return;
    }
    if (declaration.name == effect_none_name) {
        compiled.effect = std::string{};
        return;
    }
    if (!effect_name_valid(declaration.name)) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::effect_invalid, index, rule, declaration.property));
        return;
    }
    compiled.effect = declaration.name;
}

void CompiledThemeRules::add_font(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                                  const RuleDeclaration& declaration, CompiledRule& compiled) {
    if (compiled.font) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, declaration.property));
        return;
    }
    if (declaration.kind != RuleValueKind::name) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::font_invalid, index, rule, declaration.property));
        return;
    }
    FontDeclaration font{};
    if (declaration.name == font_inherit_name) {
        font.inherit = true;
        compiled.font = font;
        return;
    }
    const std::optional<TextRole> role = text_role_from_name(declaration.name);
    if (!role) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::font_invalid, index, rule, declaration.property));
        return;
    }
    font.role = *role;
    compiled.font = font;
}

// A field is checked alone, written into the default material: what it
// sets must be what some material admits. The default material has one
// solid fill, so a finish is checked against a fill that takes one; the
// assembled material is checked again when a part draws it
// (MaterialPrimitive::accepts), and one no material admits is drawn by the
// built-in recipe instead.
namespace {

bool is_finite_number(double value) noexcept {
    const bool finite = std::isfinite(value);
    return finite;
}

// Whether `em_lengths` is empty, or one finite coefficient for each of the
// `lengths`.
bool em_coefficients_fit(std::span<const double> em_lengths, std::size_t lengths) noexcept {
    if (em_lengths.empty()) return true;
    if (em_lengths.size() != lengths) return false;
    const bool finite = std::all_of(em_lengths.begin(), em_lengths.end(), &is_finite_number);
    return finite;
}

} // namespace

bool em_lengths_fit(const MaterialDeclaration& declaration) {
    std::vector<double> lengths{};
    material_declaration_lengths(declaration, lengths);
    return em_coefficients_fit(declaration.em_lengths, lengths.size());
}

bool em_lengths_fit(const RecipeDeclaration& declaration) {
    std::vector<double> lengths{};
    recipe_declaration_lengths(declaration, lengths);
    return em_coefficients_fit(declaration.em_lengths, lengths.size());
}

void CompiledThemeRules::add_materials(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                       const ThemeRule& rule, CompiledRule& compiled) {
    std::array<bool, material_field_count> declared{};
    for (const MaterialDeclaration& declaration : rule.materials) {
        const std::size_t position = static_cast<std::size_t>(declaration.field);
        const std::string_view name = material_field_name(declaration.field);
        if (position >= material_field_count) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::material_field_invalid, index, rule, {}));
            continue;
        }
        if (declared[position]) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, name));
            continue;
        }
        const bool relative_fit = em_lengths_fit(declaration) && current_colours_fit(declaration);
        if (!material_declaration_admitted(declaration) || !relative_fit) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::material_field_invalid, index, rule, name));
            continue;
        }
        declared[position] = true;
        compiled.materials.push_back(declaration);
    }
}

// -- recipe fields (T3 P6f) ---------------------------------------------------------

namespace {

bool in_range(double value, double minimum, double maximum) noexcept {
    const bool inside = std::isfinite(value) && value >= minimum && value <= maximum;
    return inside;
}

constexpr std::array<std::string_view, recipe_field_count> recipe_field_names{
    "text",        "outline_style", "outline_colour", "outline_width",  "outline_offset", "text_shadow",
    "font_family", "font_size",     "font_weight",    "font_style",     "letter_spacing", "muted_text",
    "glyph",       "default_outline_width", "default_outline_colour", "fill_text", "stroke_width",
    "needle_shape", "scrim_colour", "block_size", "minimum_inline_size",
    "padding_inline", "gap", "margin_block_start", "margin_block_end", "taper", "taper_colour", "flare",
    "flare_colour", "edge_shadow_width", "edge_shadow_colour",
};

} // namespace

ColourPosition recipe_field_colour_position(RecipeField field) noexcept {
    switch (field) {
    case RecipeField::text:
    case RecipeField::muted_text:
    case RecipeField::glyph:
    case RecipeField::fill_text: return ColourPosition::text;
    // A scrim is a fill over what lies behind.
    case RecipeField::scrim_colour: return ColourPosition::fill;
    case RecipeField::outline_colour:
    case RecipeField::default_outline_colour:
    case RecipeField::taper_colour:
    case RecipeField::flare_colour:
    case RecipeField::edge_shadow_colour: return ColourPosition::edge;
    case RecipeField::outline_style:
    case RecipeField::outline_width:
    case RecipeField::outline_offset:
    case RecipeField::text_shadow:
    case RecipeField::font_family:
    case RecipeField::font_size:
    case RecipeField::font_weight:
    case RecipeField::font_style:
    case RecipeField::letter_spacing:
    case RecipeField::default_outline_width:
    case RecipeField::block_size:
    case RecipeField::minimum_inline_size:
    case RecipeField::padding_inline:
    case RecipeField::gap:
    case RecipeField::margin_block_start:
    case RecipeField::margin_block_end:
    case RecipeField::taper:
    case RecipeField::flare:
    case RecipeField::edge_shadow_width:
    case RecipeField::stroke_width:
    case RecipeField::needle_shape:
    case RecipeField::count:
    default: return ColourPosition::none;
    }
}

std::string_view recipe_field_name(RecipeField field) noexcept {
    const std::size_t position = static_cast<std::size_t>(field);
    if (position >= recipe_field_count) return {};
    return recipe_field_names[position];
}

bool recipe_declaration_admitted(const RecipeDeclaration& declaration) noexcept {
    switch (declaration.field) {
    case RecipeField::text:
    case RecipeField::outline_colour:
    case RecipeField::muted_text:
    case RecipeField::glyph:
    case RecipeField::fill_text:
    case RecipeField::default_outline_colour:
    case RecipeField::scrim_colour:
    case RecipeField::taper_colour:
    case RecipeField::flare_colour:
    case RecipeField::edge_shadow_colour: return true;
    case RecipeField::stroke_width: return in_range(declaration.number, 0.0, maximum_recipe_stroke_width);
    case RecipeField::needle_shape:
        return declaration.needle_shape == NeedleShape::line || declaration.needle_shape == NeedleShape::tapered ||
               declaration.needle_shape == NeedleShape::dot;
    case RecipeField::block_size:
    case RecipeField::minimum_inline_size:
    case RecipeField::padding_inline: return in_range(declaration.number, 0.0, maximum_part_metric);
    case RecipeField::gap: return in_range(declaration.number, -maximum_part_margin, maximum_part_metric);
    case RecipeField::margin_block_start:
    case RecipeField::margin_block_end:
        return in_range(declaration.number, -maximum_part_margin, maximum_part_margin);
    case RecipeField::taper:
    case RecipeField::flare:
    case RecipeField::edge_shadow_width: return in_range(declaration.number, 0.0, maximum_part_shape);
    case RecipeField::outline_style:
        return declaration.outline_style == RecipeOutlineStyle::ring ||
               declaration.outline_style == RecipeOutlineStyle::solid ||
               declaration.outline_style == RecipeOutlineStyle::dotted ||
               declaration.outline_style == RecipeOutlineStyle::none;
    case RecipeField::outline_width:
    case RecipeField::default_outline_width: return in_range(declaration.number, 0.0, maximum_recipe_outline_width);
    case RecipeField::outline_offset:
        return in_range(declaration.number, -maximum_recipe_outline_offset, maximum_recipe_outline_offset);
    case RecipeField::text_shadow: {
        if (!declaration.shadowed) return true;
        const Point offset = declaration.shadow_offset;
        const bool placed = in_range(offset.x, -maximum_text_shadow_offset, maximum_text_shadow_offset) &&
                            in_range(offset.y, -maximum_text_shadow_offset, maximum_text_shadow_offset);
        const bool blurred = in_range(declaration.shadow_blur, 0.0, maximum_text_shadow_blur);
        return placed && blurred;
    }
    case RecipeField::font_family:
        return declaration.font_role == FontRole::control || declaration.font_role == FontRole::content ||
               declaration.font_role == FontRole::monospace;
    // A size's pixels are 0 where it is in em; the em part is checked when
    // the control's font resolves.
    case RecipeField::font_size: return in_range(declaration.number, 0.0, maximum_rule_font_size);
    case RecipeField::font_weight:
        return in_range(declaration.number, minimum_rule_font_weight, maximum_rule_font_weight) &&
               std::floor(declaration.number) == declaration.number;
    case RecipeField::font_style: return true;
    case RecipeField::letter_spacing:
        return in_range(declaration.number, -maximum_rule_font_size * 0.25, maximum_rule_font_size);
    case RecipeField::count: break;
    // A value outside the enumeration names no field, so it is invalid.
    default: break;
    }
    return false;
}

bool backdrop_declaration_valid(const BackdropDeclaration& backdrop) noexcept {
    if (backdrop.length > BackdropDeclaration::maximum_steps) return false;
    std::size_t blurs = 0U;
    for (std::size_t index = 0U; index < backdrop.length; ++index) {
        const BackdropStep& step = backdrop.steps[index];
        if (!std::isfinite(step.amount)) return false;
        switch (step.kind) {
        case BackdropStepKind::blur:
            blurs = blurs + 1U;
            if (step.amount < 0.0 || step.amount > maximum_backdrop_blur) return false;
            break;
        case BackdropStepKind::saturate:
            if (step.amount < 0.0 || step.amount > maximum_backdrop_saturation) return false;
            break;
        case BackdropStepKind::tint: break; // Any colour tints.
        default: return false;
        }
    }
    const bool one_blur = blurs <= 1U;
    return one_blur;
}

void CompiledThemeRules::add_backdrop(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                      const ThemeRule& rule, CompiledRule& compiled) {
    if (!rule.backdrop) return;
    const bool on_surface = rule.selector.surface != SurfaceKind::none;
    if (!on_surface || !backdrop_declaration_valid(*rule.backdrop)) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::backdrop_invalid, index, rule, "backdrop"));
        return;
    }
    compiled.backdrop = rule.backdrop;
}

bool band_declaration_valid(const BandDeclaration& band) noexcept {
    if (band.width) {
        const double width = *band.width;
        if (!std::isfinite(width) || width < 0.0 || width > maximum_theme_band_width) return false;
    }
    if (band.effect && *band.effect != BandEffect::none && *band.effect != BandEffect::flash) return false;
    return true;
}

void CompiledThemeRules::add_band(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                  const ThemeRule& rule, CompiledRule& compiled) {
    if (!rule.band) return;
    // Only a window has a band: a popup is not a window's frame.
    const bool on_window = rule.selector.surface == SurfaceKind::window;
    if (!on_window || (*rule.band).empty() || !band_declaration_valid(*rule.band)) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::band_invalid, index, rule, "band"));
        return;
    }
    compiled.band = rule.band;
}

void CompiledThemeRules::add_recipe_fields(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                           const ThemeRule& rule, CompiledRule& compiled) {
    // The recipe's fields are the control's: a selector naming a part or a
    // role sets none, except on a named part the text colours (its text,
    // muted text and glyphs: Theme::resolve with the part's name), its font
    // (Theme::resolve_font with the part's name), the dial and scrim fields
    // and its metrics and shape (RecipeField::block_size on), which are a
    // named part's own geometry.
    const bool on_role = rule.selector.role.has_value();
    const bool on_named_part = !rule.selector.part.empty() && !on_role;
    std::array<bool, recipe_field_count> declared{};
    for (const RecipeDeclaration& declaration : rule.recipe_fields) {
        const std::size_t position = static_cast<std::size_t>(declaration.field);
        const std::string_view name = recipe_field_name(declaration.field);
        if (position >= recipe_field_count) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::recipe_field_invalid, index, rule, {}));
            continue;
        }
        const bool text_colour = declaration.field == RecipeField::text ||
                                 declaration.field == RecipeField::muted_text || declaration.field == RecipeField::glyph ||
                                 declaration.field == RecipeField::fill_text;
        const bool font = declaration.field >= RecipeField::font_family &&
                          declaration.field <= RecipeField::letter_spacing;
        const bool part_field = declaration.field >= RecipeField::block_size;
        const bool part_geometry = declaration.field == RecipeField::stroke_width ||
                                   declaration.field == RecipeField::needle_shape ||
                                   declaration.field == RecipeField::scrim_colour;
        if (on_role || (on_named_part && !text_colour && !font && !part_field && !part_geometry)) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::recipe_field_on_part, index, rule, name));
            continue;
        }
        if (declared[position]) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, name));
            continue;
        }
        if (!declaration.initial && (!recipe_declaration_admitted(declaration) || !em_lengths_fit(declaration))) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::recipe_field_invalid, index, rule, name));
            continue;
        }
        declared[position] = true;
        compiled.recipe_fields.push_back(declaration);
    }
}

// -- transitions and ambient effects (E7) ------------------------------------------

namespace {

// Whether a state's stops (E9.6) are usable: none, or at most
// maximum_motion_stops on a timed curve, offsets strictly increasing
// within (0, 1) and values within [minimum, maximum].
bool usable_stops(const PresentationState& state, const MotionCurve& curve, double minimum, double maximum) noexcept {
    if (state.stops.empty()) return true;
    if (curve.kind != MotionKind::timed || state.stops.size() > maximum_motion_stops) return false;
    double previous = 0.0;
    for (const MotionStop& stop : state.stops) {
        const bool ordered = std::isfinite(stop.offset) && stop.offset > previous && stop.offset < 1.0;
        if (!ordered || !in_range(stop.value, minimum, maximum)) return false;
        previous = stop.offset;
    }
    return true;
}

// Whether a machine's states are usable for a property ranging over
// [minimum, maximum].
bool usable_states(const PropertyTransition& transition, double minimum, double maximum) noexcept {
    if (transition.states.empty() || transition.states.size() > maximum_presentation_states) return false;
    for (const PresentationState& state : transition.states) {
        for (const AnatomyState when : state.when) {
            if (static_cast<std::size_t>(when) >= anatomy_state_count) return false;
        }
        if (!in_range(state.target, minimum, maximum)) return false;
        if (state.enter_from && !in_range(*state.enter_from, minimum, maximum)) return false;
        if (!usable_stops(state, transition.curve, minimum, maximum)) return false;
    }
    return true;
}

} // namespace

namespace {

// Whether a state's value, entry or a stop is in em (P8c).
bool state_reads_em(const PresentationState& state) noexcept {
    bool reads = state.target_em != 0.0 || state.enter_from_em != 0.0;
    for (const MotionStop& stop : state.stops) {
        if (stop.value_em != 0.0) reads = true;
    }
    return reads;
}

bool em_finite(const PresentationState& state) noexcept {
    bool finite = std::isfinite(state.target_em) && std::isfinite(state.enter_from_em);
    for (const MotionStop& stop : state.stops) {
        if (!std::isfinite(stop.value_em)) finite = false;
    }
    return finite;
}

} // namespace

void CompiledThemeRules::add_transitions(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                         const ThemeRule& rule, CompiledRule& compiled) {
    for (const PropertyTransition& transition : rule.transitions) {
        CompiledTransition made{};
        double minimum = 0.0;
        double maximum = 1.0;
        if (transition.property == opacity_property) {
            made.property = PresentationProperty::opacity;
        } else if (transition.property == scale_property) {
            made.property = PresentationProperty::scale;
            minimum = minimum_presentational_scale;
            maximum = maximum_presentational_scale;
        } else if (transition.property == face_property) {
            made.property = PresentationProperty::face;
        } else if (transition.property == translate_x_property || transition.property == translate_y_property) {
            const bool across = transition.property == translate_x_property;
            made.property = across ? PresentationProperty::translate_x : PresentationProperty::translate_y;
            minimum = -maximum_presentational_translate;
            maximum = maximum_presentational_translate;
        } else {
            if (compiled.primitive == nullptr) {
                diagnostics.push_back(
                    diagnostic(RuleDiagnosticKind::parameter_without_primitive, index, rule, transition.property));
                continue;
            }
            const PrimitiveDescriptor& descriptor = (*compiled.primitive).descriptor();
            const std::optional<std::size_t> found = find_parameter(descriptor, transition.property);
            if (!found) {
                diagnostics.push_back(
                    diagnostic(RuleDiagnosticKind::transition_property_unknown, index, rule, transition.property));
                continue;
            }
            const PrimitiveParameterDeclaration& declared = descriptor.parameters[*found];
            if (declared.kind != PrimitiveParameterKind::number || !declared.animated) {
                diagnostics.push_back(diagnostic(RuleDiagnosticKind::transition_property_not_animated, index, rule,
                                                 transition.property));
                continue;
            }
            made.parameter = *found;
            minimum = declared.minimum;
            maximum = declared.maximum;
        }
        bool repeated = false;
        for (const CompiledTransition& earlier : compiled.transitions) {
            if (earlier.property == made.property && earlier.parameter == made.parameter) repeated = true;
        }
        if (repeated) {
            diagnostics.push_back(
                diagnostic(RuleDiagnosticKind::duplicate_declaration, index, rule, transition.property));
            continue;
        }
        // A face cross-fade names no states; every other machine needs some.
        // Only a translation and a number parameter are lengths (P8c).
        const bool lengths = made.property == PresentationProperty::translate_x ||
                             made.property == PresentationProperty::translate_y ||
                             made.property == PresentationProperty::parameter;
        bool em_fits = true;
        for (const PresentationState& state : transition.states) {
            if (state_reads_em(state) && (!lengths || !em_finite(state))) em_fits = false;
        }
        const bool states_fit = made.property == PresentationProperty::face
                                    ? transition.states.empty()
                                    : usable_states(transition, minimum, maximum) && em_fits;
        if (compiled.transitions.size() >= maximum_part_transitions || !states_fit) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::transition_invalid, index, rule, transition.property));
            continue;
        }
        made.states.reserve(transition.states.size());
        for (const PresentationState& state : transition.states) {
            CompiledPresentationState compiled_state{};
            for (const AnatomyState when : state.when) compiled_state.when.insert(when);
            compiled_state.target = state.target;
            compiled_state.enter_from = state.enter_from;
            compiled_state.stops = state.stops;
            compiled_state.target_em = state.target_em;
            compiled_state.enter_from_em = state.enter_from_em;
            if (state_reads_em(state)) made.reads_em = true;
            made.states.push_back(std::move(compiled_state));
        }
        made.curve = transition.curve;
        compiled.transitions.push_back(std::move(made));
    }
}

void CompiledThemeRules::add_ambient(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                     const ThemeRule& rule, CompiledRule& compiled) {
    if (!rule.ambient) return;
    const AmbientEffect& ambient = *rule.ambient;
    const double rate = ambient.frames_per_second;
    // Refused at load (design language §9.4): no frame cap, or one above 30.
    if (!(rate > 0.0) || !(rate <= maximum_ambient_frames_per_second)) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::ambient_over_frame_cap, index, rule, "ambient"));
        return;
    }
    const bool timed = ambient.period > FrameInterval::zero() && ambient.pose >= FrameInterval::zero() &&
                       ambient.pose < ambient.period;
    if (!timed) {
        diagnostics.push_back(diagnostic(RuleDiagnosticKind::ambient_invalid, index, rule, "ambient"));
        return;
    }
    compiled.ambient = ambient;
}

CompiledThemeRules::CompiledThemeRules() : serial_(next_rules_serial()) {}

CompiledThemeRules::CompiledThemeRules(const CompiledThemeRules& other)
    : rules_(other.rules_),
      diagnostics_(other.diagnostics_),
      published_values_(other.published_values_),
      built_in_rules_(other.built_in_rules_),
      serial_(next_rules_serial()),
      recipe_fields_set_(other.recipe_fields_set_),
      inline_style_(other.inline_style_),
      user_theme_(other.user_theme_),
      uses_ancestors_(other.uses_ancestors_),
      scoped_names_(other.scoped_names_),
      read_names_(other.read_names_),
      theme_serial_(other.theme_serial_),
      theme_read_positions_(other.theme_read_positions_),
      theme_scoped_positions_(other.theme_scoped_positions_) {}

CompiledThemeRules& CompiledThemeRules::operator=(const CompiledThemeRules& other) {
    if (this == &other) return *this;
    rules_ = other.rules_;
    diagnostics_ = other.diagnostics_;
    published_values_ = other.published_values_;
    built_in_rules_ = other.built_in_rules_;
    serial_ = next_rules_serial();
    recipe_fields_set_ = other.recipe_fields_set_;
    inline_style_ = other.inline_style_;
    user_theme_ = other.user_theme_;
    uses_ancestors_ = other.uses_ancestors_;
    scoped_names_ = other.scoped_names_;
    read_names_ = other.read_names_;
    theme_serial_ = other.theme_serial_;
    theme_read_positions_ = other.theme_read_positions_;
    theme_scoped_positions_ = other.theme_scoped_positions_;
    return *this;
}

CompiledThemeRules CompiledThemeRules::compile(const ThemeRuleSheet& sheet, const PrimitiveRegistry& registry) {
    CompiledThemeRules result{};
    // The published values first: expressions are checked against their
    // count. An unusable name keeps its position, empty, so that the
    // positions after it stay where the sheet's expressions expect them.
    const std::size_t published = std::min(sheet.published_values.size(), maximum_published_values);
    result.published_values_.reserve(published);
    for (std::size_t index = 0; index < sheet.published_values.size(); ++index) {
        const std::string& name = sheet.published_values[index];
        bool usable = index < maximum_published_values && valid_style_class_name(name);
        for (std::size_t earlier = 0; usable && earlier < index; ++earlier) {
            if (sheet.published_values[earlier] == name) usable = false;
        }
        if (!usable) {
            RuleDiagnostic invalid{};
            invalid.kind = RuleDiagnosticKind::published_value_invalid;
            invalid.rule = sheet_diagnostic;
            invalid.property = name;
            result.diagnostics_.push_back(std::move(invalid));
        }
        if (index >= maximum_published_values) continue;
        result.published_values_.push_back(usable ? name : std::string{});
    }
    add_rules(result, sheet, registry, false);
    std::sort(result.rules_.begin(), result.rules_.end(), LowerPrecedence{});
    result.number_machines();
    return result;
}

CompiledThemeRules CompiledThemeRules::compile_with_built_in(const ThemeRuleSheet& sheet,
                                                            const PrimitiveRegistry& registry) {
    CompiledThemeRules result = compile(sheet, registry);
    // The engine's rules after the sheet's, so that the sheet's diagnostics
    // keep their positions; precedence puts them first.
    const std::size_t authored_rules = result.rules_.size();
    add_rules(result, built_in_theme_rules(), registry, true);
    result.built_in_rules_ = result.rules_.size() - authored_rules;
    std::sort(result.rules_.begin(), result.rules_.end(), LowerPrecedence{});
    result.number_machines();
    return result;
}

CompiledThemeRules CompiledThemeRules::compile_inline(const ThemeRuleSheet& sheet, const PrimitiveRegistry& registry,
                                                      const CompiledThemeRules* theme) {
    CompiledThemeRules result{};
    result.inline_style_ = true;
    // An inline style's expressions read the engine's inputs and the
    // published values of the theme it is compiled against (P12b: the
    // theme's rules lowered again with the style's definitions read them
    // at the theme's positions); the style names none of its own.
    if (theme != nullptr) result.published_values_ = (*theme).published_values_;
    for (const std::string& name : sheet.published_values) {
        RuleDiagnostic invalid{};
        invalid.kind = RuleDiagnosticKind::published_value_invalid;
        invalid.rule = sheet_diagnostic;
        invalid.property = name;
        result.diagnostics_.push_back(std::move(invalid));
    }
    add_rules(result, sheet, registry, false);
    std::sort(result.rules_.begin(), result.rules_.end(), LowerPrecedence{});
    result.number_machines();
    // Each scoped name's place among the theme's read names: where the
    // style's definition of it wins, the theme's rules reading it give way.
    if (theme != nullptr) {
        result.theme_serial_ = (*theme).serial_;
        result.theme_read_positions_.assign(result.scoped_names_.size(), no_read_name);
        result.theme_scoped_positions_.assign(result.scoped_names_.size(), no_read_name);
        for (std::size_t position = 0U; position < result.scoped_names_.size(); ++position) {
            const std::string& name = result.scoped_names_[position];
            const std::optional<std::size_t> read = (*theme).read_name_position(name);
            if (read) result.theme_read_positions_[position] = static_cast<std::uint32_t>(*read);
            const std::vector<std::string>& theme_names = (*theme).scoped_names_;
            for (std::size_t scoped = 0U; scoped < theme_names.size(); ++scoped) {
                if (theme_names[scoped] == name) result.theme_scoped_positions_[position] = static_cast<std::uint32_t>(scoped);
            }
        }
    }
    return result;
}

CompiledThemeRules CompiledThemeRules::compile_user(const ThemeRuleSheet& sheet, const PrimitiveRegistry& registry) {
    CompiledThemeRules result{};
    result.user_theme_ = true;
    for (const std::string& name : sheet.published_values) {
        RuleDiagnostic invalid{};
        invalid.kind = RuleDiagnosticKind::published_value_invalid;
        invalid.rule = sheet_diagnostic;
        invalid.property = name;
        result.diagnostics_.push_back(std::move(invalid));
    }
    add_rules(result, sheet, registry, false);
    std::sort(result.rules_.begin(), result.rules_.end(), LowerPrecedence{});
    result.number_machines();
    return result;
}

void CompiledThemeRules::raise_user_opacity(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                            const ThemeRule& rule, CompiledRule& compiled) {
    for (CompiledTransition& transition : compiled.transitions) {
        if (transition.property != PresentationProperty::opacity) continue;
        bool raised = false;
        for (CompiledPresentationState& state : transition.states) {
            if (state.target < user_theme_minimum_opacity) {
                state.target = user_theme_minimum_opacity;
                raised = true;
            }
            if (state.enter_from && *state.enter_from < user_theme_minimum_opacity) {
                state.enter_from = user_theme_minimum_opacity;
                raised = true;
            }
            for (MotionStop& stop : state.stops) {
                if (stop.value >= user_theme_minimum_opacity) continue;
                stop.value = user_theme_minimum_opacity;
                raised = true;
            }
        }
        if (raised) {
            diagnostics.push_back(diagnostic(RuleDiagnosticKind::user_opacity_raised, index, rule, opacity_property));
        }
    }
}

void CompiledThemeRules::number_machines() noexcept {
    std::size_t next = 0U;
    if (inline_style_) next = inline_machine_base;
    if (user_theme_) next = user_machine_base;
    for (CompiledRule& rule : rules_) {
        for (CompiledTransition& transition : rule.transitions) {
            transition.machine = next;
            next = next + 1U;
        }
    }
}

// -- resolving -----------------------------------------------------------------

bool CompiledThemeRules::matches(const CompiledRule& rule, const PartFacts& facts) noexcept {
    const RuleSelector& selector = rule.selector;
    if (!media_condition_holds(rule.media, facts.media)) return false;
    if (selector.surface != facts.surface) return false;
    if (!selector.control_type.empty() && selector.control_type != facts.control_type) return false;
    if (!selector.stable_id.empty() && selector.stable_id != facts.stable_id) return false;
    if (!selector.part.empty() && selector.part != facts.part) return false;
    if (selector.role && *selector.role != facts.role) return false;
    const std::uint64_t needed = rule.states.standard_bits();
    if ((facts.states.standard_bits() & needed) != needed) return false;
    for (const std::string& name : selector.classes) {
        bool present = false;
        for (const std::string& held : facts.classes) {
            if (held == name) present = true;
        }
        if (!present) return false;
    }
    if (!selector.family.empty() && selector.family != facts.family) return false;
    for (const RuleExclusion& exclusion : selector.excluded) {
        if (exclusion_matches(exclusion, facts)) return false;
    }
    if (!selector.ancestors.empty() && !ancestors_match(rule, facts)) return false;
    return true;
}

namespace {

// Whether one ancestor compound matches one ancestor control.
bool ancestor_matches(const RuleAncestor& ancestor, const AncestorFacts& facts) noexcept {
    if (!ancestor.control_type.empty() && ancestor.control_type != facts.control_type) return false;
    if (!ancestor.stable_id.empty() && ancestor.stable_id != facts.stable_id) return false;
    for (const std::string& name : ancestor.classes) {
        bool present = false;
        for (const std::string& held : facts.classes) {
            if (held == name) present = true;
        }
        if (!present) return false;
    }
    return true;
}

// A set of ancestor positions, one bit each, below maximum_ancestor_depth.
using AncestorPositions = std::array<std::uint64_t, maximum_ancestor_depth / 64U>;

bool position_held(const AncestorPositions& positions, std::size_t position) noexcept {
    return (positions[position >> 6U] >> (position & 63U) & 1U) != 0U;
}

void hold_position(AncestorPositions& positions, std::size_t position) noexcept {
    positions[position >> 6U] |= std::uint64_t{1} << (position & 63U);
}

} // namespace

// Matched from the farthest compound to the nearest, one set of positions
// a compound: `reach` holds the positions where a compound matches with
// every farther one matched beyond it. A child combinator needs the
// farther compound at the next position; a descendant one at any farther
// position, so the farthest held position decides it. The work is the
// compounds times the depth, with no backtracking.
bool CompiledThemeRules::ancestors_match(const CompiledRule& rule, const PartFacts& facts) noexcept {
    if (!facts.ancestor_bloom.holds(rule.ancestor_keys)) return false;
    const std::vector<RuleAncestor>& ancestors = rule.selector.ancestors;
    const std::size_t depth = std::min(facts.ancestors.size(), maximum_ancestor_depth);
    if (depth == 0U) return false;
    AncestorPositions reach{};
    // The farthest compound alone.
    const std::size_t farthest = ancestors.size() - 1U;
    for (std::size_t position = 0U; position < depth; ++position) {
        if (ancestor_matches(ancestors[farthest], facts.ancestors[position])) hold_position(reach, position);
    }
    for (std::size_t index = farthest; index > 0U; --index) {
        // The compound nearer than `index`, related to it by its combinator.
        const RuleAncestor& nearer = ancestors[index - 1U];
        const bool child = ancestors[index].combinator == AncestorCombinator::child;
        AncestorPositions next{};
        // Whether `reach` holds a position beyond `position`, walked from
        // the farthest position down.
        bool beyond = false;
        for (std::size_t step = depth; step > 0U; --step) {
            const std::size_t position = step - 1U;
            const bool linked = child ? position + 1U < depth && position_held(reach, position + 1U) : beyond;
            if (linked && ancestor_matches(nearer, facts.ancestors[position])) hold_position(next, position);
            if (position_held(reach, position)) beyond = true;
        }
        reach = next;
    }
    if (ancestors.front().combinator == AncestorCombinator::child) return position_held(reach, 0U);
    for (const std::uint64_t word : reach) {
        if (word != 0U) return true;
    }
    return false;
}

bool CompiledThemeRules::exclusion_matches(const RuleExclusion& exclusion, const PartFacts& facts) noexcept {
    if (!exclusion.control_type.empty() && exclusion.control_type != facts.control_type) return false;
    if (!exclusion.stable_id.empty() && exclusion.stable_id != facts.stable_id) return false;
    if (!exclusion.family.empty() && exclusion.family != facts.family) return false;
    for (const AnatomyState state : exclusion.states) {
        if (!facts.states.contains(state)) return false;
    }
    for (const std::string& name : exclusion.classes) {
        bool present = false;
        for (const std::string& held : facts.classes) {
            if (held == name) present = true;
        }
        if (!present) return false;
    }
    return true;
}

// The rules of a compilation, of a part's user theme and of its inline style, merged in
// ascending precedence: by level, then specificity; at equal level and
// specificity the compilation's come first, each set in its own order.
// A walk for the part's drawing under forced high contrast
// (PartFacts::forced_high_contrast) passes over every rule below
// important_inline_style; one given the part's scoped winners (P12) passes
// over every copy whose conditions they do not meet.
class CompiledThemeRules::RuleWalk final {
public:
    // Which set a rule of the walk is from.
    enum class Set : std::uint8_t { theme, user, inline_style };

    RuleWalk(const CompiledThemeRules& rules, const PartFacts& facts, bool drawing,
             const ScopedWinners* winners) noexcept
        : theme_(rules.rules_),
          user_(facts.user_rules == nullptr ? std::span<const CompiledRule>{}
                                            : std::span<const CompiledRule>((*facts.user_rules).rules_)),
          inline_(facts.inline_rules == nullptr ? std::span<const CompiledRule>{}
                                                : std::span<const CompiledRule>((*facts.inline_rules).rules_)),
          winners_(winners),
          forced_(drawing && facts.forced_high_contrast) {}

    // The next rule, or null after the last.
    [[nodiscard]] const CompiledRule* next() noexcept {
        const CompiledRule* taken = merged();
        while (taken != nullptr && !admitted(*taken)) taken = merged();
        return taken;
    }

    // Only the rules of `set`: each set's scoped winners are found among
    // its own definitions (P12b).
    void restrict_to(Set set) noexcept {
        if (set != Set::theme) theme_next_ = theme_.size();
        if (set != Set::user) user_next_ = user_.size();
        if (set != Set::inline_style) inline_next_ = inline_.size();
    }

private:
    // The next rule of the merge, or null after the last; taken_ tells
    // which set it is from. A user theme's levels are its own, so its
    // rules are ordered against the others' by level alone.
    [[nodiscard]] const CompiledRule* merged() noexcept {
        const bool theme_left = theme_next_ < theme_.size();
        const bool inline_left = inline_next_ < inline_.size();
        const bool user_left = user_next_ < user_.size();
        const CompiledRule* best = nullptr;
        Set best_set = Set::theme;
        if (theme_left && (!inline_left || !precedes(inline_[inline_next_], theme_[theme_next_]))) {
            best = &theme_[theme_next_];
        } else if (inline_left) {
            best = &inline_[inline_next_];
            best_set = Set::inline_style;
        }
        if (user_left && (best == nullptr || user_[user_next_].level < (*best).level)) {
            best = &user_[user_next_];
            best_set = Set::user;
        }
        if (best == nullptr) return nullptr;
        if (best_set == Set::theme) theme_next_ = theme_next_ + 1U;
        if (best_set == Set::user) user_next_ = user_next_ + 1U;
        if (best_set == Set::inline_style) inline_next_ = inline_next_ + 1U;
        taken_ = best_set;
        return best;
    }

    // Whether the inline style's rule `left` comes before the theme's
    // `right`: at equal level and specificity a copy of a theme rule
    // (P12b) comes after the theme's rules from earlier in the theme's
    // source, and its own rules after all of them.
    [[nodiscard]] static bool precedes(const CompiledRule& left, const CompiledRule& right) noexcept {
        if (left.level != right.level) return left.level < right.level;
        if (!(left.specificity == right.specificity)) return left.specificity < right.specificity;
        return left.source_position != 0U && left.source_position < right.source_position;
    }

    [[nodiscard]] bool admitted(const CompiledRule& rule) const noexcept {
        if (forced_ && rule.level < CascadeLevel::important_inline_style) return false;
        if (winners_ == nullptr) return true;
        const std::array<std::uint32_t, maximum_scoped_custom_properties>* winning = &(*winners_).definitions;
        if (taken_ == Set::inline_style) winning = &(*winners_).inline_definitions;
        if (taken_ == Set::user) winning = &(*winners_).user_definitions;
        for (const CompiledScoped& condition : rule.scoped_conditions) {
            if ((*winning)[condition.name] != condition.definition) return false;
        }
        // A theme rule reading a name the inline style's definition wins
        // for gives way to the style's copy of it.
        if (taken_ != Set::theme) return true;
        for (const std::uint32_t read : rule.reads) {
            const std::uint64_t bit = std::uint64_t{1} << (read % 64U);
            if (((*winners_).inline_won[read / 64U] & bit) != 0U) return false;
        }
        return true;
    }

    std::span<const CompiledRule> theme_;
    std::span<const CompiledRule> user_;
    std::span<const CompiledRule> inline_;
    std::size_t theme_next_{};
    std::size_t user_next_{};
    std::size_t inline_next_{};
    const ScopedWinners* winners_{};
    bool forced_{};
    Set taken_{Set::theme};
};

CompiledThemeRules::Resolution CompiledThemeRules::resolve(const PartFacts& facts) const noexcept {
    Resolution result{};
    const bool inline_rules = facts.inline_rules != nullptr && !(*facts.inline_rules).rules_.empty();
    if (inline_rules) result.inline_serial = (*facts.inline_rules).serial_;
    const bool user_rules = facts.user_rules != nullptr && !(*facts.user_rules).rules_.empty();
    if (user_rules) result.user_serial = (*facts.user_rules).serial_;
    if (rules_.empty() && !inline_rules && !user_rules) return result;
    // The winner is the last matching rule that selects, rules being in
    // ascending precedence. A built-in rule's primitive (the ribbon's rise,
    // flat at rest) is the stock default only: a theme rule that authors
    // the part's material without naming a primitive stands over it, and
    // the part is drawn from that material (Plan Paint's raised faces at
    // rest).
    const CompiledRule* winner = nullptr;
    bool authored_material = false;
    ScopedWinners winners{};
    resolve_scoped(facts, winners);
    RuleWalk walk(*this, facts, true, &winners);
    for (const CompiledRule* rule = walk.next(); rule != nullptr; rule = walk.next()) {
        if (!matches(*rule, facts)) continue;
        if ((*rule).selects) {
            winner = rule;
            authored_material = false;
        } else if ((*rule).level > CascadeLevel::built_in && !(*rule).materials.empty()) {
            authored_material = true;
        }
    }
    if (winner != nullptr && (*winner).level == CascadeLevel::built_in && authored_material) winner = nullptr;
    if (winner != nullptr) resolve_primitive(facts, winners, *winner, result);
    resolve_transitions(facts, winners, result);
    resolve_materials(facts, winners, result);
    return result;
}

void CompiledThemeRules::resolve_scoped(const PartFacts& facts, ScopedWinners& winners) const noexcept {
    winners.definitions.fill(custom_property_undefined);
    winners.inline_definitions.fill(custom_property_undefined);
    winners.inline_won.fill(0U);
    winners.user_definitions.fill(custom_property_undefined);
    // The last matching definition of each name wins, rules being in
    // ascending precedence. Definitions hold no conditions.
    if (!scoped_names_.empty()) {
        RuleWalk walk(*this, facts, false, nullptr);
        walk.restrict_to(RuleWalk::Set::theme);
        for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
            const CompiledRule& rule = *next;
            if (rule.scoped_definitions.empty() || !matches(rule, facts)) continue;
            for (const CompiledScoped& definition : rule.scoped_definitions) {
                winners.definitions[definition.name] = definition.definition;
            }
        }
    }
    // The inline style's own names among its own definitions, which hold
    // the theme's of the same names where it was compiled against this
    // theme; a style compiled against another theme (stale until it is
    // compiled again) has no definitions the theme's rules give way to.
    // The user theme's names among its own definitions.
    const CompiledThemeRules* const user = facts.user_rules;
    if (user != nullptr && !(*user).scoped_names_.empty()) {
        RuleWalk walk(*this, facts, false, nullptr);
        walk.restrict_to(RuleWalk::Set::user);
        for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
            const CompiledRule& rule = *next;
            if (rule.scoped_definitions.empty() || !matches(rule, facts)) continue;
            for (const CompiledScoped& definition : rule.scoped_definitions) {
                winners.user_definitions[definition.name] = definition.definition;
            }
        }
    }
    const CompiledThemeRules* const style = facts.inline_rules;
    if (style == nullptr || (*style).scoped_names_.empty()) return;
    RuleWalk walk(*this, facts, false, nullptr);
    walk.restrict_to(RuleWalk::Set::inline_style);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        if (rule.scoped_definitions.empty() || !matches(rule, facts)) continue;
        for (const CompiledScoped& definition : rule.scoped_definitions) {
            winners.inline_definitions[definition.name] = definition.definition;
        }
    }
    if ((*style).theme_serial_ != serial_) return;
    // Where none of the style's definitions of a name holds, the theme's
    // winner does: the style's own rules reading a name the theme defines
    // (P12b) hold copies for the theme's definitions of it.
    for (std::size_t position = 0U; position < (*style).theme_scoped_positions_.size(); ++position) {
        const std::uint32_t scoped = (*style).theme_scoped_positions_[position];
        if (scoped == no_read_name || winners.inline_definitions[position] != custom_property_undefined) continue;
        winners.inline_definitions[position] = winners.definitions[scoped];
    }
    for (std::size_t position = 0U; position < (*style).theme_read_positions_.size(); ++position) {
        const std::uint32_t read = (*style).theme_read_positions_[position];
        const std::uint32_t winner = winners.inline_definitions[position];
        if (read == no_read_name || winner == custom_property_undefined || winner < inline_definition_base) continue;
        winners.inline_won[read / 64U] = winners.inline_won[read / 64U] | (std::uint64_t{1} << (read % 64U));
    }
}

void CompiledThemeRules::resolve_materials(const PartFacts& facts, const ScopedWinners& winners,
                                           Resolution& result) const noexcept {
    // The last matching declaration of each field wins, rules being in
    // ascending precedence; a rule declares each field at most once.
    RuleWalk walk(*this, facts, true, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        if (rule.materials.empty() || !matches(rule, facts)) continue;
        for (const MaterialDeclaration& declaration : rule.materials) {
            result.materials[static_cast<std::size_t>(declaration.field)] = &declaration;
        }
        result.materials_authored = true;
    }
}

CompiledThemeRules::RecipeResolution CompiledThemeRules::resolve_recipe_fields(const PartFacts& facts) const noexcept {
    RecipeResolution result{};
    const bool inline_fields = facts.inline_rules != nullptr && (*facts.inline_rules).recipe_fields_set_;
    const bool user_fields = facts.user_rules != nullptr && (*facts.user_rules).recipe_fields_set_;
    if (!recipe_fields_set_ && !inline_fields && !user_fields) return result;
    // As for the material fields: the last matching declaration of each
    // field wins, and a rule declares each field at most once.
    ScopedWinners winners{};
    resolve_scoped(facts, winners);
    RuleWalk walk(*this, facts, true, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        if (rule.recipe_fields.empty() || !matches(rule, facts)) continue;
        for (const RecipeDeclaration& declaration : rule.recipe_fields) {
            const std::size_t position = static_cast<std::size_t>(declaration.field);
            result.fields[position] = &declaration;
            result.levels[position] = rule.level;
        }
        result.authored = true;
    }
    return result;
}

CompiledThemeRules::FieldProvenance CompiledThemeRules::field_provenance(const CompiledRule& rule,
                                                                        ColourOrigin origin) noexcept {
    FieldProvenance result{};
    result.authored = true;
    result.level = rule.level;
    result.source = &rule.provenance;
    result.selector = &rule.selector;
    result.specificity = rule.specificity;
    result.origin = origin;
    // The level names the layer where it is one of its own: the built-in
    // table, a user theme, inline code and forced high contrast; the sheet
    // levels take the layer the compiler recorded.
    switch (rule.level) {
    case CascadeLevel::built_in: result.layer = ThemeLayer::built_in; break;
    case CascadeLevel::user_theme:
    case CascadeLevel::important_user_theme: result.layer = ThemeLayer::user_theme; break;
    case CascadeLevel::inline_style:
    case CascadeLevel::important_inline_style: result.layer = ThemeLayer::inline_code; break;
    case CascadeLevel::forced_high_contrast: result.layer = ThemeLayer::forced_high_contrast; break;
    case CascadeLevel::base_sheet:
    case CascadeLevel::theme_sheet:
    case CascadeLevel::class_rule:
    case CascadeLevel::stable_id_rule:
    case CascadeLevel::important_base_sheet:
    case CascadeLevel::important_theme_sheet:
    case CascadeLevel::count:
    default: result.layer = rule.provenance.layer; break;
    }
    return result;
}

void CompiledThemeRules::resolve_provenance(const PartFacts& facts, PartProvenance& result) const noexcept {
    result = PartProvenance{};
    ScopedWinners winners{};
    resolve_scoped(facts, winners);
    // The primitive's winner first, as resolve() finds it: the last
    // matching rule that selects, rules being in ascending precedence.
    const CompiledRule* winner = nullptr;
    bool authored_material = false;
    RuleWalk choosing(*this, facts, true, &winners);
    for (const CompiledRule* next = choosing.next(); next != nullptr; next = choosing.next()) {
        if (!matches(*next, facts)) continue;
        if ((*next).selects) {
            winner = next;
            authored_material = false;
        } else if ((*next).level > CascadeLevel::built_in && !(*next).materials.empty()) {
            authored_material = true;
        }
    }
    if (winner != nullptr && (*winner).level == CascadeLevel::built_in && authored_material) winner = nullptr;
    const EnginePrimitive* primitive = nullptr;
    if (winner != nullptr && (*winner).primitive != nullptr &&
        paints_role((*(*winner).primitive).descriptor(), facts.role)) {
        primitive = (*winner).primitive;
        result.parameter_count = default_parameters((*primitive).descriptor()).count;
    }
    // Then every field: the last matching declaration of each wins, a
    // parameter only from the rules selecting the chosen primitive.
    RuleWalk walk(*this, facts, true, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        const bool parameters = primitive != nullptr && rule.primitive == primitive;
        const bool declares = parameters || !rule.materials.empty() || !rule.recipe_fields.empty();
        if (!declares || !matches(rule, facts)) continue;
        for (const MaterialDeclaration& declaration : rule.materials) {
            result.materials[static_cast<std::size_t>(declaration.field)] = field_provenance(rule, declaration.origin);
        }
        for (const RecipeDeclaration& declaration : rule.recipe_fields) {
            const std::size_t position = static_cast<std::size_t>(declaration.field);
            result.recipe_fields[position] = field_provenance(rule, declaration.origin);
        }
        if (!parameters) continue;
        for (const CompiledDeclaration& declaration : rule.parameters) {
            result.parameters[declaration.parameter] = field_provenance(rule, declaration.origin);
        }
    }
}

const BackdropDeclaration* CompiledThemeRules::resolve_backdrop(const PartFacts& facts) const noexcept {
    // Only a surface rule holds a backdrop (add_backdrop). Rules are walked
    // in ascending precedence, so the last match wins.
    if (facts.surface == SurfaceKind::none) return nullptr;
    ScopedWinners winners{};
    resolve_scoped(facts, winners);
    const BackdropDeclaration* winner = nullptr;
    RuleWalk walk(*this, facts, false, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        if (!rule.backdrop || !matches(rule, facts)) continue;
        winner = &*rule.backdrop;
    }
    return winner;
}

BandDeclaration CompiledThemeRules::resolve_band(const PartFacts& facts) const noexcept {
    // Only a window rule holds a band (add_band). Rules are walked in
    // ascending precedence, so each field's last setter wins.
    BandDeclaration merged{};
    if (facts.surface != SurfaceKind::window) return merged;
    ScopedWinners winners{};
    resolve_scoped(facts, winners);
    RuleWalk walk(*this, facts, false, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        if (!rule.band || !matches(rule, facts)) continue;
        const BandDeclaration& band = *rule.band;
        if (band.width) merged.width = band.width;
        if (band.rest_colour) merged.rest_colour = band.rest_colour;
        if (band.lit_colour) merged.lit_colour = band.lit_colour;
        if (band.effect) merged.effect = band.effect;
    }
    return merged;
}

void CompiledThemeRules::resolve_primitive(const PartFacts& facts, const ScopedWinners& winners,
                                           const CompiledRule& chosen, Resolution& result) const noexcept {
    result.level = chosen.level;
    if (chosen.primitive == nullptr) {
        result.outcome = Outcome::built_in_selected;
        return;
    }
    const PrimitiveDescriptor& descriptor = (*chosen.primitive).descriptor();
    if (!paints_role(descriptor, facts.role)) {
        result.outcome = Outcome::role_mismatch;
        return;
    }
    result.parameters = default_parameters(descriptor);
    // Every matching rule for the same primitive contributes, in ascending
    // precedence, so the last write of each parameter is its winner. A
    // winning expression keeps its fallback as the parameter's value until
    // the engine evaluates it.
    RuleWalk walk(*this, facts, true, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        if (rule.primitive != chosen.primitive || !matches(rule, facts)) continue;
        for (const CompiledDeclaration& declaration : rule.parameters) {
            result.parameters.values[declaration.parameter] = declaration.value;
            result.expressions[declaration.parameter] = declaration.expression ? &declaration.program : nullptr;
            result.parameter_em[declaration.parameter] = declaration.em;
            result.parameter_origins[declaration.parameter] = declaration.origin;
        }
    }
    result.inputs_read = declared_input_bits(descriptor);
    for (std::size_t index = 0; index < result.parameters.count; ++index) {
        if (result.parameter_em[index] != 0.0) result.parameters_read_em = true;
        const EngineExpression* const expression = result.expressions[index];
        if (expression != nullptr) result.inputs_read = result.inputs_read | (*expression).dependencies().inputs;
    }
    result.outcome = Outcome::primitive;
    result.primitive = chosen.primitive;
}

void CompiledThemeRules::resolve_transitions(const PartFacts& facts, const ScopedWinners& winners,
                                             Resolution& result) const noexcept {
    // The last matching declaration of each property wins: opacity, scale
    // and face from any rule, a parameter only from the rules selecting the
    // part's primitive.
    const CompiledTransition* opacity = nullptr;
    const CompiledTransition* scale = nullptr;
    const CompiledTransition* face = nullptr;
    const CompiledTransition* across = nullptr;
    const CompiledTransition* down = nullptr;
    std::array<const CompiledTransition*, maximum_primitive_parameters> parameters{};
    const bool primitive = result.outcome == Outcome::primitive;
    RuleWalk walk(*this, facts, false, &winners);
    for (const CompiledRule* next = walk.next(); next != nullptr; next = walk.next()) {
        const CompiledRule& rule = *next;
        const bool presents =
            !rule.transitions.empty() || rule.ambient || rule.blend || rule.font || rule.effect.has_value() ||
            rule.effect_primary.has_value() || rule.effect_secondary.has_value();
        if (!presents || !matches(rule, facts)) continue;
        if (rule.ambient) result.ambient = &*rule.ambient;
        if (rule.blend) result.blend = *rule.blend;
        if (rule.font) result.font = *rule.font;
        if (rule.effect) {
            result.effect = *rule.effect;
            result.effect_declared = true;
        }
        if (rule.effect_primary) result.effect_primary = rule.effect_primary;
        if (rule.effect_secondary) result.effect_secondary = rule.effect_secondary;
        const bool same_primitive = primitive && rule.primitive == result.primitive;
        for (const CompiledTransition& transition : rule.transitions) {
            if (transition.property == PresentationProperty::opacity) {
                opacity = &transition;
            } else if (transition.property == PresentationProperty::scale) {
                scale = &transition;
            } else if (transition.property == PresentationProperty::face) {
                face = &transition;
            } else if (transition.property == PresentationProperty::translate_x) {
                across = &transition;
            } else if (transition.property == PresentationProperty::translate_y) {
                down = &transition;
            } else if (same_primitive) {
                parameters[transition.parameter] = &transition;
            }
        }
    }
    result.transition_count = 0U;
    if (opacity != nullptr) {
        result.transitions[result.transition_count] = opacity;
        result.transition_count = result.transition_count + 1U;
    }
    if (scale != nullptr) {
        result.transitions[result.transition_count] = scale;
        result.transition_count = result.transition_count + 1U;
    }
    if (face != nullptr) {
        result.transitions[result.transition_count] = face;
        result.transition_count = result.transition_count + 1U;
    }
    if (across != nullptr) {
        result.transitions[result.transition_count] = across;
        result.transition_count = result.transition_count + 1U;
    }
    if (down != nullptr) {
        result.transitions[result.transition_count] = down;
        result.transition_count = result.transition_count + 1U;
    }
    for (std::size_t index = 0; index < result.parameters.count; ++index) {
        if (parameters[index] == nullptr || result.transition_count >= maximum_part_transitions) continue;
        result.transitions[result.transition_count] = parameters[index];
        result.transition_count = result.transition_count + 1U;
    }
}

} // namespace gui_forms
