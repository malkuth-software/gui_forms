// The theme compiler (PLAN.md P5). One pass over the tree's nodes in
// document order: each style rule's selectors are lowered, then its
// `primitive` declarations (the last usable one selects the rule's
// primitive, which its parameters need), then its other declarations, each
// replacing an earlier one of the same engine property, as in CSS. Each
// expanded selector then gets one copy of the rule's records, counted
// against the lowered-size budget, so expansion builds a bounded amount.

#include "theme_compiler.hpp"

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

// -- the property tables (PLAN.md section 1.7) ------------------------------------

// Known CSS properties the language leaves out (W107): layout belongs to
// forms and stage L, the rest of typography to the text stage, and
// geometry to the engine. The font longhands T4 admits (font-family,
// font-size, font-style, font-weight, letter-spacing) are material
// properties, found before these. Sorted, for binary_search. A property in one of the families
// below is left out too.
constexpr std::array<std::string_view, 142> not_admitted_properties{
    "accent-color", "align-content", "align-items", "align-self", "appearance", "aspect-ratio",
    "backface-visibility", "block-size", "bottom", "box-sizing", "caret-color", "clear", "clip", "clip-path",
    "column-count", "column-fill", "column-gap", "column-rule", "column-span", "column-width", "columns", "contain",
    "content", "content-visibility", "counter-increment", "counter-reset", "cursor", "direction", "display", "flex",
    "flex-basis", "flex-direction", "flex-flow", "flex-grow", "flex-shrink", "flex-wrap", "float",
    "font-feature-settings", "font-kerning", "font-stretch", "font-variant",
    "gap", "grid", "grid-area", "grid-auto-columns", "grid-auto-flow", "grid-auto-rows", "grid-column",
    "grid-column-end", "grid-column-start", "grid-row", "grid-row-end", "grid-row-start", "grid-template",
    "grid-template-areas", "grid-template-columns", "grid-template-rows", "height", "hyphens", "inline-size", "inset",
    "inset-block", "inset-inline", "justify-content", "justify-items", "justify-self", "left",
    "line-height", "list-style", "margin", "margin-block", "margin-bottom", "margin-inline", "margin-left",
    "margin-right", "margin-top", "max-block-size", "max-height", "max-inline-size", "max-width", "min-block-size",
    "min-height", "min-inline-size", "min-width", "object-fit", "object-position", "order", "overflow",
    "overflow-wrap", "overflow-x", "overflow-y", "padding", "padding-block", "padding-bottom", "padding-inline",
    "padding-left", "padding-right", "padding-top", "perspective", "perspective-origin", "place-content",
    "place-items", "place-self", "pointer-events", "position", "quotes", "resize", "right", "rotate", "row-gap",
    "scroll-behavior", "tab-size", "table-layout", "text-align", "text-decoration", "text-decoration-color",
    "text-decoration-line", "text-decoration-style", "text-decoration-thickness", "text-indent", "text-overflow",
    "text-transform", "text-underline-offset", "text-wrap", "top", "transform", "transform-box", "transform-origin",
    "transform-style", "unicode-bidi", "user-select", "vertical-align", "visibility", "white-space", "width",
    "will-change", "word-break", "word-spacing", "word-wrap", "writing-mode", "z-index", "zoom",
};
static_assert(std::is_sorted(not_admitted_properties.begin(), not_admitted_properties.end()),
              "binary_search needs the table sorted");

// Families of left-out properties: the name, or the name and a '-'. The
// `animation` longhands the motion lowering does not list (such as
// `animation-composition` and `animation-timeline`) are left out.
constexpr std::array<std::string_view, 13> not_admitted_families{
    "animation", "column",     "flex",    "font",   "grid",            "inset",     "margin",
    "overflow",  "overscroll", "padding", "scroll", "text-decoration", "transform",
};

// The properties of P6d to P8 (the rest of the authored materials, motion
// and the colour and font roles): refused with not_yet_built until their
// phases, so that no interim meaning ships. Sorted. A family's members the
// material lowering lists (P6b, P6c) are lowered before these are consulted.
constexpr std::array<std::string_view, 1> later_properties{
    "backdrop-filter",
};
static_assert(std::is_sorted(later_properties.begin(), later_properties.end()), "binary_search needs the table sorted");

constexpr std::array<std::string_view, 5> later_families{
    "background", "border", "mask", "outline", "transition",
};

constexpr std::array<std::string_view, 3> blend_modes{"normal", "multiply", "screen"};

// The primitive a rule of material fields selects when it names none
// (owner C7).
constexpr std::string_view material_primitive_name = "material";

[[nodiscard]] bool listed(std::span<const std::string_view> sorted, std::string_view name) noexcept {
    const bool found = std::binary_search(sorted.begin(), sorted.end(), name);
    return found;
}

// Whether `name` is `family` or starts with `family` and a '-'.
[[nodiscard]] bool in_family(std::span<const std::string_view> families, std::string_view name) noexcept {
    for (const std::string_view family : families) {
        if (name == family) return true;
        const bool prefixed =
            name.size() > family.size() && name.substr(0U, family.size()) == family && name[family.size()] == '-';
        if (prefixed) return true;
    }
    return false;
}

enum class PropertyClass : std::uint8_t {
    custom,
    primitive,
    opacity,
    scale,
    translate,
    blend,
    isolation,
    // `font` (P8c): the part's text role.
    font,
    // `effect`: the effect program the part draws (effect programs, design
    // note section 7).
    effect,
    // `effect-primary` and `effect-secondary`: the effect's colour roles.
    effect_colour,
    // `backdrop`: a window's or popup's backdrop chain (owner decision
    // 2026-11-05).
    backdrop,
    // A material property (P6b): its fields are lowered by the material
    // lowering.
    material,
    // `transition` and its longhands (P7a).
    transition,
    // `animation` and its longhands (P7b).
    animation,
    later,
    not_admitted,
    // A primitive parameter, or an unknown name.
    other,
};

// `name` is the property in lower case.
[[nodiscard]] PropertyClass classify(std::string_view name) noexcept {
    if (name.substr(0U, 2U) == "--") return PropertyClass::custom;
    if (name == primitive_property) return PropertyClass::primitive;
    if (name == "opacity") return PropertyClass::opacity;
    if (name == "scale") return PropertyClass::scale;
    if (name == "translate") return PropertyClass::translate;
    if (name == "mix-blend-mode") return PropertyClass::blend;
    if (name == "isolation") return PropertyClass::isolation;
    if (name == font_property) return PropertyClass::font;
    if (name == effect_property) return PropertyClass::effect;
    if (name == "effect-primary" || name == "effect-secondary") return PropertyClass::effect_colour;
    if (name == "backdrop") return PropertyClass::backdrop;
    if (material_property(name)) return PropertyClass::material;
    if (transition_property(name)) return PropertyClass::transition;
    if (animation_property(name)) return PropertyClass::animation;
    if (listed(later_properties, name) || in_family(later_families, name)) return PropertyClass::later;
    if (listed(not_admitted_properties, name) || in_family(not_admitted_families, name)) {
        return PropertyClass::not_admitted;
    }
    return PropertyClass::other;
}

// Whether `property` is one of the font longhands (T4 fonts).
[[nodiscard]] bool font_property_of(MaterialProperty property) noexcept {
    const bool font = property == MaterialProperty::font_family || property == MaterialProperty::font_size ||
                      property == MaterialProperty::font_style || property == MaterialProperty::font_weight ||
                      property == MaterialProperty::letter_spacing;
    return font;
}

// The CSS-wide keywords: `initial` is the built-in recipe's value (the
// primitive's default); `inherit` is admitted only on colour and font (P8);
// `unset`, `revert` and `revert-layer` are outside the language.
enum class WideKeyword : std::uint8_t { none, initial, inherit, outside };

void fold_ascii(std::string_view text, std::string& output) {
    output.assign(text);
    for (char& character : output) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
}

// A machine holding `target` in every state, reached at once: a static
// presentational value (CSS_COVERAGE section 4).
[[nodiscard]] PropertyTransition still_machine(std::string_view property, double target, double target_em = 0.0) {
    PropertyTransition machine{};
    machine.property = std::string(property);
    PresentationState state{};
    state.target = target;
    state.target_em = target_em;
    machine.states.push_back(std::move(state));
    machine.curve = MotionCurve::timed(FrameInterval{}, MotionEasing::linear);
    return machine;
}

[[nodiscard]] bool paints_role(const PrimitiveDescriptor& descriptor, PartRole role) noexcept {
    for (const PartRole painted : descriptor.roles) {
        if (painted == role) return true;
    }
    return false;
}

[[nodiscard]] std::size_t declaration_bytes(const RuleDeclaration& declaration) noexcept {
    const std::size_t bytes = lowered_record_bytes + declaration.property.size() + declaration.name.size() +
                              declaration.program.size() * sizeof(ExpressionInstruction);
    return bytes;
}

// A material declaration's lowered bytes: a record for it and for each of
// its layers and gradients, and each stop's bytes.
[[nodiscard]] std::size_t material_bytes(const MaterialDeclaration& declaration) noexcept {
    std::size_t records = 1U + declaration.fills.size() + declaration.gradients.size();
    std::size_t stops = 0U;
    for (const MaterialFillLayer& layer : declaration.fills) stops += layer.stops.size();
    for (const AuthoredGradient& gradient : declaration.gradients) stops += gradient.stops.size();
    records += declaration.blend_modes.size();
    const std::size_t bytes = records * lowered_record_bytes + stops * sizeof(AuthoredGradientStop);
    return bytes;
}

[[nodiscard]] std::size_t transition_bytes(const PropertyTransition& transition) noexcept {
    std::size_t bytes =
        lowered_record_bytes + transition.property.size() + transition.states.size() * lowered_record_bytes;
    for (const PresentationState& state : transition.states) bytes += state.stops.size() * sizeof(MotionStop);
    return bytes;
}

// The value a presentational property rests at: 1 for opacity and scale,
// 0 for a translation.
[[nodiscard]] double engine_rest_value(std::string_view property) noexcept {
    const bool translation = property == translate_x_property || property == translate_y_property;
    return translation ? 0.0 : 1.0;
}

// The number a rule's own declaration gives `parameter`, if it declares
// one.
[[nodiscard]] std::optional<double> own_number(const ThemeRule& rule, std::string_view parameter) noexcept {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == parameter && declaration.kind == RuleValueKind::number) return declaration.number;
    }
    return std::nullopt;
}

// That declaration's em coefficient (P8c), 0 where it has none.
[[nodiscard]] double own_number_em(const ThemeRule& rule, std::string_view parameter) noexcept {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == parameter && declaration.kind == RuleValueKind::number) {
            return declaration.number_em;
        }
    }
    return 0.0;
}

// The rule's primitive, as its `primitive` declarations select it.
struct PrimitiveSelection final {
    // Whether a usable declaration selected one: `primitive` is null for
    // the built-in recipe.
    bool selects{};
    const EnginePrimitive* primitive{};
    // Whether a declaration naming one was dropped (an unknown or unusable
    // primitive): parameters no selected primitive declares are then
    // dropped with it, without a warning each.
    bool dropped{};
};

// One rule a style rule lowers to (P9): at its selector's level, or, for
// its `!important` declarations, at the important level. It selects
// `selection`'s primitive where that selects one, else the material
// primitive where `implicit_material`. A `borrowed` selection is the
// important sibling's, carried for the ordinary rule's parameters: alone,
// it is not emitted.
struct EmittedRule final {
    PrimitiveSelection selection{};
    bool implicit_material{};
    bool important{};
    bool borrowed{};
};

// Whether the property `folded` (folded to lowercase) is a parameter of
// the primitive `descriptor` describes.
[[nodiscard]] bool names_parameter(const PrimitiveDescriptor& descriptor, std::string_view folded) {
    const std::string registered = registered_name_from_kebab(folded);
    const bool named = !registered.empty() && find_parameter(descriptor, registered).has_value();
    return named;
}

// The join of one optional flag of two media conditions into `joined`:
// false where they name opposite values.
[[nodiscard]] bool join_flag(std::optional<bool> left, std::optional<bool> right,
                             std::optional<bool>& joined) noexcept {
    if (left && right && *left != *right) return false;
    joined = left ? left : right;
    return true;
}

// The condition that holds where both `left` and `right` hold, into
// `joined`; false where no window can meet both.
[[nodiscard]] bool join_media(const MediaCondition& left, const MediaCondition& right,
                              MediaCondition& joined) noexcept {
    MediaCondition both{};
    if (!join_flag(left.dark_scheme, right.dark_scheme, both.dark_scheme)) return false;
    if (!join_flag(left.forced_colours, right.forced_colours, both.forced_colours)) return false;
    if (!join_flag(left.reduced_motion, right.reduced_motion, both.reduced_motion)) return false;
    both.minimum_resolution = left.minimum_resolution ? left.minimum_resolution : right.minimum_resolution;
    if (left.minimum_resolution && right.minimum_resolution) {
        both.minimum_resolution = std::max(*left.minimum_resolution, *right.minimum_resolution);
    }
    both.maximum_resolution = left.maximum_resolution ? left.maximum_resolution : right.maximum_resolution;
    if (left.maximum_resolution && right.maximum_resolution) {
        both.maximum_resolution = std::min(*left.maximum_resolution, *right.maximum_resolution);
    }
    if (both.minimum_resolution && both.maximum_resolution && *both.minimum_resolution > *both.maximum_resolution) {
        return false;
    }
    joined = both;
    return true;
}

// The token of the value's first em length, if it has one (P8c).
[[nodiscard]] std::optional<std::uint32_t> first_em_length(const ThemeValue& value) noexcept {
    for (const ValueComponent& component : value.components) {
        if (component.kind == ValueComponentKind::number && component.unit == ValueUnit::em) return component.token;
    }
    return std::nullopt;
}

// Whether the value names CSS's currentcolor (P8c).
[[nodiscard]] bool names_current_colour(const ThemeValue& value) noexcept {
    for (const ValueComponent& component : value.components) {
        if (component.kind != ValueComponentKind::keyword) continue;
        if (equals_ascii_folded(component.text, "currentcolor")) return true;
    }
    return false;
}

// A colour a declaration's value writes: a colour component, or a named
// colour's keyword.
[[nodiscard]] bool writes_colour(const ValueComponent& component) noexcept {
    if (component.kind == ValueComponentKind::colour) return true;
    return component.kind == ValueComponentKind::keyword && named_colour(component.text).has_value();
}

// Where one declaration's colours came from (EC4): a literal, where any
// colour of the value is one (its token into `literal`); otherwise its
// first role; none for a value without a colour.
[[nodiscard]] ColourOrigin value_colour_origin(const ThemeValue& value, std::uint32_t& literal) noexcept {
    ColourOrigin origin{};
    for (const ValueComponent& component : value.components) {
        if (!writes_colour(component)) continue;
        if (component.origin.source != ColourSource::role) {
            ColourOrigin written{};
            written.source = ColourSource::literal;
            literal = component.token;
            return written;
        }
        if (origin.source == ColourSource::none) origin = component.origin;
    }
    return origin;
}

// One compilation.
class CompilePass final {
public:
    CompilePass(const ThemeCompileInput& input, ThemeRuleSheet& sheet, ThemeDiagnosticList& diagnostics,
                ThemeTokenBuffer& tokens, LineStartTable& lines, ThemeSyntaxTree& tree,
                CustomPropertyTable& custom_properties, SelectorLowering& selectors,
                VariableSubstitution& substitution, ValueReader& reader,
                std::vector<LoweredSelector>& lowered_selectors, std::vector<ValueToken>& substituted,
                ThemeValue& value, std::vector<std::string_view>& parameter_names,
                std::vector<LoweredDeclaration>& declarations, std::vector<MaterialDeclaration>& materials,
                MaterialLowering& material_lowering, std::vector<MaterialField>& material_fields,
                std::vector<RecipeDeclaration>& recipe_fields, MotionStorage& motion,
                std::vector<MediaQuery>& media_queries, PaletteStorage& palette, VariationStorage& variations,
                std::string& folded, ThemeCompileWork& work) noexcept;

    [[nodiscard]] CompileOutcome run();

private:
    // The `:root` palette (P8b), each block's role declarations under its
    // media condition, into palette_; false after an error.
    [[nodiscard]] bool collect_palette();
    // One `:root` block's declarations, recorded under media_ when
    // `record` (a block no media query lowers is only checked).
    [[nodiscard]] bool collect_palette_block(std::size_t rule_index, bool record);
    // `color-scheme` in the palette's block (T4 theme variants): `light`,
    // `dark` or both, written once each in any order, into
    // sheet_.colour_schemes; false after an error (another word, or the
    // declaration under `@media`).
    [[nodiscard]] bool lower_colour_scheme(const SyntaxNode& node);
    // Whether the style rule's selector is `:root` alone: the palette's
    // block, collected before the rules.
    [[nodiscard]] bool selects_root(std::size_t rule_index) const noexcept;
    // Whether every escaped name is in a style rule's selector (owner
    // 2026-11-04: escapes name stable IDs and classes); reports the first
    // that is not (E105) and gives false then.
    [[nodiscard]] bool escapes_in_selectors_only();
    // Whether a definition is the palette's, or holds everywhere as the
    // palette's do (NameVariation::global_declaration, P12b).
    [[nodiscard]] bool palette_scope(const CustomPropertyDefinition& definition) const noexcept;
    // Whether a node is an inline style's, in a theme compiled for it
    // (ThemeCompileInput::inline_definitions_from).
    [[nodiscard]] bool in_inline_segment(std::size_t node_index) const noexcept;
    // The number a definition declared at `declaration` goes by (P12b):
    // an inline style's from inline_definition_base, a theme's its node.
    [[nodiscard]] std::uint32_t definition_number(std::uint32_t declaration) const noexcept;
    // The next combination of the varying roles' variants in order; false
    // after the last.
    [[nodiscard]] bool next_palette_choice() noexcept;
    [[nodiscard]] bool lower_media(std::size_t media_index);
    // The style rules of an `@media` block under media_, which is then
    // reset.
    [[nodiscard]] bool lower_media_rules(std::size_t media_index);
    // How each custom property varies (P12) into variations_, and the
    // palette's definitions of the scoped names style rules read as the
    // sheet's first rules; false after an error.
    [[nodiscard]] bool collect_variations();
    // The palette definitions of a name (`group`, its definitions) in
    // cascade order, each under each media query it holds under.
    void palette_variants(std::span<const CustomPropertyDefinition> group, std::vector<MediaVariant>& variants);
    // The queries of the `@media` block at `media_index` into
    // media_queries_; false when its prelude is refused.
    [[nodiscard]] bool read_block_queries(std::uint32_t media_index);
    // A scoped name's candidates, from its definitions (`group`) and its
    // palette variants; false after an error.
    [[nodiscard]] bool scoped_candidates(std::span<const CustomPropertyDefinition> group, NameVariation& name);
    // Whether the scoped names' candidates chosen through `depth` (of
    // variations_.scoped_read) can win together in the world `world`,
    // whose conditions joined with the rule's give `joined`.
    [[nodiscard]] bool candidates_meet(std::size_t depth, std::size_t world, const MediaCondition& joined) const;
    // A rule of scoped definitions for each of the rule's selectors (with
    // its media condition), at the level `important` picks.
    [[nodiscard]] bool emit_definitions(std::span<const LoweredSelector> selectors, bool important,
                                        std::uint32_t at);
    // The rule's definitions of the scoped names some rule reads, as rules
    // of their own.
    [[nodiscard]] bool lower_definitions(std::size_t rule_index);
    // The names the rule reads directly, and the media names it reads
    // through any number of definitions with their conditions, into
    // variations_.
    void read_variations(std::size_t rule_index);
    // The scopes of the rule's selectors into `scopes`, and into `covers`
    // the control types it selects whole, when given; nothing for a rule
    // whose selectors are refused (its own lowering reports them).
    void rule_scopes(std::size_t rule_index, std::vector<SelectorScope>& scopes, std::vector<std::string>* covers);
    // The first scoped name, in name order, that the copy being lowered
    // reads and has not decided: through the rule's direct names and the
    // definitions they read, a decided scoped name's only through its
    // chosen candidate. None when every name it reads is decided.
    [[nodiscard]] std::optional<std::uint32_t> undecided_name();
    // A style rule: its scoped definitions, then a copy for each
    // combination of the definitions of the varying names it reads (P12),
    // each lowered by lower_rule_palette.
    [[nodiscard]] bool lower_rule(std::size_t rule_index);
    // A style rule, and its copies for the palette's conditional variants
    // of the roles it reads (P8b).
    [[nodiscard]] bool lower_rule_palette(std::size_t rule_index);
    [[nodiscard]] bool lower_rule_once(std::size_t rule_index);
    // The rule's ordinary or its `!important` declarations (P9) into
    // declarations_, materials_, recipe_fields_ and motion_, parameters
    // being `selection`'s; `other` is the rule's other selection.
    [[nodiscard]] bool lower_declarations(std::size_t rule_index, bool important,
                                          const PrimitiveSelection& selection, const PrimitiveSelection& other);
    [[nodiscard]] bool select_primitive(const SyntaxNode& node, PrimitiveSelection& selection);
    [[nodiscard]] bool lower_declaration(const SyntaxNode& node, PropertyClass kind,
                                         const PrimitiveSelection& selection);
    [[nodiscard]] bool lower_parameter(const SyntaxNode& node, const PrimitiveDescriptor& descriptor,
                                       std::size_t parameter);
    [[nodiscard]] bool lower_opacity_or_scale(const SyntaxNode& node, PropertyClass kind);
    [[nodiscard]] bool lower_translate(const SyntaxNode& node);
    [[nodiscard]] bool lower_keyword_property(const SyntaxNode& node, PropertyClass kind);
    [[nodiscard]] bool lower_material(const SyntaxNode& node);
    // `font: <role>` (P8c, D6): the part's text role, or `inherit`, the
    // parent control's (D7).
    [[nodiscard]] bool lower_font(const SyntaxNode& node);
    // `effect`: a registered effect's name in kebab case, `none`, or
    // `initial` (none).
    [[nodiscard]] bool lower_effect(const SyntaxNode& node);
    // `effect-primary` and `effect-secondary`: one colour role, role(<name>)
    // (a pair's foreground unless its side is named).
    [[nodiscard]] bool lower_effect_colour(const SyntaxNode& node);
    [[nodiscard]] bool lower_backdrop(const SyntaxNode& node);
    // One backdrop function of value_ at `function` into `step`; false
    // after an error.
    [[nodiscard]] bool read_backdrop_step(std::size_t function, BackdropStepKind kind, BackdropStep& step);
    // Reports material_lowering_'s problem; true where the declaration is
    // only dropped.
    [[nodiscard]] bool report_material_problem();
    // The em coefficients (P8c, D4) of the declarations material_lowering_
    // holds, lowered at an em of 0 and the current colour black, where the
    // value has an em length at `em_token`, and their current colours
    // where `current` (the lowering read currentcolor); false after an
    // error.
    [[nodiscard]] bool lower_relative(MaterialProperty property, std::uint32_t start,
                                      std::optional<std::uint32_t> em_token, bool current);
    // Lowers again at an em of 1, and takes the lengths that differ as em
    // coefficients, into em_materials_ and em_recipe_fields_.
    [[nodiscard]] bool lower_em_pass(MaterialProperty property, std::uint32_t start);
    // Lowers again at the current colour white, and takes the colours that
    // differ as current colours, into em_materials_ and em_recipe_fields_.
    [[nodiscard]] bool lower_current_colour_pass(MaterialProperty property, std::uint32_t start);
    // The coefficients em_at_one_ less em_pixels_ into `coefficients`;
    // whether any is not 0.
    [[nodiscard]] bool em_coefficients(std::vector<double>& coefficients) const;
    [[nodiscard]] bool lower_transition(const SyntaxNode& node);
    [[nodiscard]] bool lower_animation(const SyntaxNode& node);
    // The rule's declared curves on its machines (P7a), the machines'
    // origins into motion_.rule_machines; false after an error.
    [[nodiscard]] bool apply_transitions(const PrimitiveSelection& selection, ThemeRule& lowered);
    // The rule's animations (P7b): moves on its machines, loops as its
    // parameters' expressions and its ambient effect; false after an
    // error.
    [[nodiscard]] bool apply_animations(const PrimitiveSelection& selection, ThemeRule& lowered);
    [[nodiscard]] bool apply_loop(const DeclaredLoop& loop, ThemeRule& lowered);
    // A `@keyframes` rule's definition into motion_.keyframes; false after
    // an error.
    [[nodiscard]] bool collect_keyframes(std::size_t index);
    // The offsets a keyframe block's selectors name, into `offsets`.
    [[nodiscard]] bool keyframe_offsets(const SyntaxNode& block, std::vector<double>& offsets);
    // One keyframe declaration's values into `definition` at `offsets`;
    // false after an error (a dropped declaration returns true).
    [[nodiscard]] bool keyframe_declaration(const SyntaxNode& node, std::span<const double> offsets,
                                            KeyframesDefinition& definition);
    void add_material(MaterialDeclaration declaration);
    void add_recipe_field(const RecipeDeclaration& declaration);
    // Whether a selector of the rule names a part or a role: the recipe's
    // fields are the control's (P6f).
    [[nodiscard]] bool rule_selects_part() const noexcept;
    [[nodiscard]] bool rule_has_states() const noexcept;
    // Whether any of the rule's selectors names a window or popup, and
    // whether all of them do.
    [[nodiscard]] bool rule_selects_surface() const noexcept;
    [[nodiscard]] bool rule_selects_only_surfaces() const noexcept;
    // The lowered declarations as a rule for each selector (EmittedRule).
    [[nodiscard]] bool emit_rule(const EmittedRule& emitted, std::uint32_t at);

    // Substitutes and reads the declaration's value into value_, the
    // parameters being parameter_names_; false after an error.
    [[nodiscard]] bool read_value(const SyntaxNode& node);
    [[nodiscard]] WideKeyword wide_keyword() const noexcept;
    // The first generic function in value_, reported as unknown_function
    // (the declaration is dropped); false when there is none.
    [[nodiscard]] bool drop_unknown_function();
    // A plain number or a percentage of value_'s only component, as a
    // fraction; reports and returns false otherwise.
    [[nodiscard]] bool fraction_of(const ValueComponent& component, double& fraction);
    [[nodiscard]] bool keyword_of(const ValueComponent& component, std::string_view expected);
    void add_lowered(LoweredDeclaration lowered);
    [[nodiscard]] std::string_view token_text(std::uint32_t index) const noexcept;
    // The text from the range's first token through its last; empty for an
    // empty range.
    [[nodiscard]] std::string_view range_text(TokenRange range) const noexcept;
    void report(DiagnosticCode code, std::uint32_t token, std::string detail);

    const ThemeCompileInput& input_;
    ThemeRuleSheet& sheet_;
    ThemeDiagnosticList& diagnostics_;
    ThemeTokenBuffer& tokens_;
    LineStartTable& lines_;
    ThemeSyntaxTree& tree_;
    CustomPropertyTable& custom_properties_;
    SelectorLowering& selectors_;
    VariableSubstitution& substitution_;
    ValueReader& reader_;
    std::vector<LoweredSelector>& lowered_selectors_;
    std::vector<ValueToken>& substituted_;
    ThemeValue& value_;
    std::vector<std::string_view>& parameter_names_;
    std::vector<LoweredDeclaration>& declarations_;
    std::vector<MaterialDeclaration>& materials_;
    MaterialLowering& material_lowering_;
    std::vector<MaterialField>& material_fields_;
    std::vector<RecipeDeclaration>& recipe_fields_;
    MotionStorage& motion_;
    std::vector<MediaQuery>& media_queries_;
    PaletteStorage& palette_;
    VariationStorage& variations_;
    std::string& folded_;
    ThemeCompileWork& work_;
    SourcePositionCursor cursor_;
    ReservedInputs reserved_{};
    LoweringBudget budget_{};
    bool newer_engine_{};
    // The media condition of the query being lowered (EC3); outside
    // `@media`, the unconditional one.
    MediaCondition media_{};
    // Whether the value being read is a palette's own: role() is not
    // admitted there.
    bool reading_palette_{};
    // The side role(<pair>) gives without one, as the property being
    // lowered sets it.
    std::optional<ColourRoleSide> bare_side_{};
    // The backdrop the rule being lowered declares, or none.
    std::optional<BackdropDeclaration> backdrop_{};
    // The roles the rule being lowered read, one bit a role.
    std::uint32_t rule_roles_read_{};
    // Whether a declaration whose var() reads a name no definition of which
    // holds is left undeclared (a rule's, P12) rather than refused; and
    // whether the value just read was so left.
    bool undefined_drops_{};
    bool undefined_read_{};
    // The style rule being lowered (ThemeRule::source_position), 0 for
    // none; and the first node of an inline style's rules in a theme
    // compiled for it (P12b), 0 for none.
    std::uint32_t source_position_{};
    std::uint32_t segment_node_{};
    // Whether the sheet names a custom property at all: only then can an
    // inline style's definitions vary its rules, and only then are its
    // rules given their source positions (P12b).
    bool positions_{};
    // The em lowering's storage (P8c).
    std::vector<MaterialDeclaration> em_materials_{};
    std::vector<RecipeDeclaration> em_recipe_fields_{};
    std::vector<double> em_pixels_{};
    std::vector<double> em_at_one_{};
    std::vector<Color> base_colours_{};
    std::vector<Color> other_colours_{};
    // The rule and the property being lowered, as diagnostics name them.
    std::string rule_{};
    std::string property_{};
};

CompilePass::CompilePass(const ThemeCompileInput& input, ThemeRuleSheet& sheet, ThemeDiagnosticList& diagnostics,
                         ThemeTokenBuffer& tokens, LineStartTable& lines, ThemeSyntaxTree& tree,
                         CustomPropertyTable& custom_properties, SelectorLowering& selectors,
                         VariableSubstitution& substitution, ValueReader& reader,
                         std::vector<LoweredSelector>& lowered_selectors, std::vector<ValueToken>& substituted,
                         ThemeValue& value, std::vector<std::string_view>& parameter_names,
                         std::vector<LoweredDeclaration>& declarations, std::vector<MaterialDeclaration>& materials,
                         MaterialLowering& material_lowering, std::vector<MaterialField>& material_fields,
                         std::vector<RecipeDeclaration>& recipe_fields, MotionStorage& motion,
                         std::vector<MediaQuery>& media_queries, PaletteStorage& palette,
                         VariationStorage& variations, std::string& folded, ThemeCompileWork& work) noexcept
    : input_(input), sheet_(sheet), diagnostics_(diagnostics), tokens_(tokens), lines_(lines), tree_(tree),
      custom_properties_(custom_properties), selectors_(selectors), substitution_(substitution), reader_(reader),
      lowered_selectors_(lowered_selectors), substituted_(substituted), value_(value),
      parameter_names_(parameter_names), declarations_(declarations), materials_(materials),
      material_lowering_(material_lowering), material_fields_(material_fields), recipe_fields_(recipe_fields),
      motion_(motion), media_queries_(media_queries), palette_(palette), variations_(variations),
      folded_(folded),
      work_(work),
      cursor_(lines, input.source.text) {}

CompileOutcome CompilePass::run() {
    work_ = ThemeCompileWork{};
    const TokenizeOutcome tokenized =
        tokenize_theme_sheet(input_.source, tokens_, lines_, diagnostics_, NameEscapes::in_names);
    work_.tokens = tokens_.tokens.size();
    if (tokenized != TokenizeOutcome::tokens_cover_sheet) return CompileOutcome::refused;
    const ParseOutcome parsed = parse_theme_sheet(input_.source, tokens_, lines_, tree_, diagnostics_, input_.form);
    work_.nodes = tree_.nodes.size();
    if (parsed != ParseOutcome::tree_complete) return CompileOutcome::refused;
    if (!escapes_in_selectors_only()) return CompileOutcome::refused;
    newer_engine_ = current_engine_version < input_.requires_engine;
    substitution_.clear_theme_read();
    segment_node_ = 0U;
    positions_ = false;
    if (input_.form == ThemeSheetForm::sheet) {
        for (const Token& token : tokens_.tokens) {
            if (token.kind != TokenKind::ident || token.length.value < 2U) continue;
            const std::string_view name = input_.source.text.substr(token.offset.value, 2U);
            if (name == "--") positions_ = true;
        }
    }
    if (input_.inline_definitions_from) {
        const std::vector<SyntaxNode>& all = tree_.nodes;
        for (std::uint32_t index = 1U; index < all.size() && segment_node_ == 0U; ++index) {
            if (all[index].kind != SyntaxNodeKind::style_rule) continue;
            const ByteOffset at = tokens_.tokens[all[index].tokens.first.value].offset;
            if (at.value >= *input_.inline_definitions_from) segment_node_ = index;
        }
    }
    motion_.keyframes.clear();
    if (newer_engine_) {
        const std::string version =
            std::to_string(input_.requires_engine.major) + "." + std::to_string(input_.requires_engine.minor);
        report(DiagnosticCode::needs_newer_engine, 0U, version);
    }
    reserved_.published_values = input_.published_values;
    if (!custom_properties_.collect(input_.source, tokens_, tree_, reserved_, cursor_, diagnostics_)) {
        return CompileOutcome::refused;
    }
    parameter_names_.reserve(maximum_primitive_parameters);
    motion_.origins.clear();
    // How the custom properties vary, which the palette's values read;
    // then the palette: a rule reads it wherever its `:root` block is.
    if (!collect_variations()) return CompileOutcome::refused;
    // The palette is the theme's whole: a name an inline style defines
    // reads there as the theme alone defines it (P12b).
    for (std::uint32_t name_index = 0U; name_index < variations_.names.size(); ++name_index) {
        const NameVariation& name = variations_.names[name_index];
        if (!name.inline_defined || name.global_declaration == 0U) continue;
        const std::span<const CustomPropertyDefinition> named = custom_properties_.definitions_named(name_index);
        for (std::uint32_t ordinal = 0U; ordinal < named.size(); ++ordinal) {
            if (named[ordinal].declaration.value == name.global_declaration) variations_.choices[name_index] = ordinal;
        }
    }
    const bool palette = collect_palette();
    for (std::uint32_t name_index = 0U; name_index < variations_.names.size(); ++name_index) {
        if (variations_.names[name_index].inline_defined) variations_.choices[name_index] = variable_unassigned;
    }
    if (!palette) return CompileOutcome::refused;
    const std::vector<SyntaxNode>& nodes = tree_.nodes;
    // The keyframes first: an animation may name keyframes defined after
    // its rule, as in CSS.
    std::size_t keyframes_rules = 0U;
    for (std::size_t index = 1U; index < nodes.size(); index = nodes[index].subtree_end.value) {
        if (nodes[index].kind != SyntaxNodeKind::keyframes_rule) continue;
        keyframes_rules = keyframes_rules + 1U;
        if (keyframes_rules > maximum_keyframes_rules) {
            report(DiagnosticCode::too_many_keyframes, nodes[index].tokens.first.value,
                   std::to_string(maximum_keyframes_rules));
            return CompileOutcome::refused;
        }
        if (!collect_keyframes(index)) return CompileOutcome::refused;
    }
    std::size_t index = 1U;
    while (index < nodes.size()) {
        const SyntaxNode& node = nodes[index];
        const std::size_t next = node.subtree_end.value;
        if (node.kind == SyntaxNodeKind::media_rule) {
            if (!lower_media(index)) return CompileOutcome::refused;
            index = next;
            continue;
        }
        if (node.kind == SyntaxNodeKind::import_rule) {
            // An @import resolves only among installed packs (P10, D2): a
            // package's closure is compiled with its imports in place
            // (compile_theme_package), so a sheet compiled alone has none.
            const std::uint32_t name = node.content.first.value;
            const std::string_view quoted = token_text(name);
            report(DiagnosticCode::import_not_installed, name,
                   clipped_theme_detail(quoted.substr(1U, quoted.size() - 2U)));
            return CompileOutcome::refused;
        }
        if (node.kind == SyntaxNodeKind::style_rule && !lower_rule(index)) return CompileOutcome::refused;
        // An unknown at-rule was reported by the parser and is skipped.
        index = next;
    }
    // Each transition takes the state values of the rules refining its own
    // (R2-18).
    const MotionLoweringOutcome grouped = group_transitions(sheet_, motion_.origins, motion_.problem);
    if (grouped != MotionLoweringOutcome::lowered) {
        MotionLoweringProblem& problem = motion_.problem;
        report(problem.code, problem.token, std::move(problem.detail));
        return CompileOutcome::refused;
    }
    sheet_.requires_engine = newer_engine_ ? current_engine_version : input_.requires_engine;
    sheet_.published_values.assign(input_.published_values.begin(), input_.published_values.end());
    return CompileOutcome::compiled;
}

// An `@media` block (EC3): its rules, lowered once for each query of its
// list that names only known features and that some window can meet. A
// block no query lowers is still lowered once and its rules discarded, so
// its errors refuse the sheet as any other rule's do (B2).
bool CompilePass::lower_media(std::size_t media_index) {
    const SyntaxNode& media = tree_.nodes[media_index];
    MediaQueryProblem problem{};
    const MediaQueryOutcome read =
        read_media_queries(input_.source.text, tokens_, media.content, media_queries_, problem);
    if (read == MediaQueryOutcome::refused) {
        report(problem.code, problem.token, std::move(problem.detail));
        return false;
    }
    bool lowered = false;
    const bool muted = diagnostics_.warnings_muted();
    for (const MediaQuery& query : media_queries_) {
        if (query.unknown_feature) {
            report(DiagnosticCode::unknown_media_feature, query.unknown_token,
                   clipped_theme_detail(token_text(query.unknown_token)));
            continue;
        }
        if (query.never) continue;
        media_ = query.condition;
        // The block's warnings are reported once, for its first query.
        if (lowered) diagnostics_.mute_warnings(true);
        lowered = true;
        const bool block = lower_media_rules(media_index);
        diagnostics_.mute_warnings(muted);
        if (!block) return false;
    }
    if (lowered) return true;
    const std::size_t rules = sheet_.rules.size();
    const std::size_t origins = motion_.origins.size();
    const LoweringBudget budget = budget_;
    if (!lower_media_rules(media_index)) return false;
    sheet_.rules.resize(rules);
    motion_.origins.resize(origins);
    budget_ = budget;
    return true;
}

bool CompilePass::lower_media_rules(std::size_t media_index) {
    const std::size_t end = tree_.nodes[media_index].subtree_end.value;
    for (std::size_t child = media_index + 1U; child < end; child = tree_.nodes[child].subtree_end.value) {
        if (tree_.nodes[child].kind != SyntaxNodeKind::style_rule) continue;
        if (!lower_rule(child)) return false;
    }
    media_ = MediaCondition{};
    return true;
}

namespace {

// The palette's definition of a scoped name, for collect_variations: the
// definitions of one `:root` block at one importance become one rule.
struct PaletteDefinition final {
    bool important{};
    std::uint32_t rule{};
    std::uint32_t media{};
    std::uint32_t declaration{};
    std::string_view name{};
    // A theme's one definition outside the palette (P12b), which keeps its
    // importance: a palette's `!important` never beats a rule's own.
    bool global{};
};

struct PaletteDefinitionLess final {
    bool operator()(const PaletteDefinition& left, const PaletteDefinition& right) const noexcept {
        if (left.important != right.important) return !left.important;
        if (left.rule != right.rule) return left.rule < right.rule;
        return left.declaration < right.declaration;
    }
};

} // namespace

bool CompilePass::collect_variations() {
    VariationStorage& variations = variations_;
    const std::size_t name_count = custom_properties_.name_count();
    variations.names.assign(name_count, NameVariation{});
    variations.choices.assign(name_count, variable_unassigned);
    variations.seen.assign(name_count, 0U);
    variations.copy_conditions.clear();
    const std::span<const CustomPropertyDefinition> definitions = custom_properties_.definitions();
    for (const CustomPropertyDefinition& definition : definitions) {
        NameVariation& name = variations.names[definition.name_index];
        append_variable_names(input_.source, tokens_.tokens, definition.value, custom_properties_, name.reads);
    }
    // The names some style rule's declaration reads, through any number of
    // definitions.
    const std::vector<SyntaxNode>& nodes = tree_.nodes;
    variations.pending.clear();
    std::uint32_t rule_end = 0U;
    for (std::uint32_t index = 0U; index < nodes.size(); ++index) {
        const SyntaxNode& node = nodes[index];
        if (node.kind == SyntaxNodeKind::style_rule) rule_end = node.subtree_end.value;
        if (node.kind == SyntaxNodeKind::keyframe_block) rule_end = 0U;
        if (node.kind != SyntaxNodeKind::declaration || index >= rule_end) continue;
        fold_ascii(token_text(node.tokens.first.value), folded_);
        if (classify(folded_) == PropertyClass::custom) continue;
        append_variable_names(input_.source, tokens_.tokens, node.content, custom_properties_, variations.pending);
    }
    while (!variations.pending.empty()) {
        const std::uint32_t name_index = variations.pending.back();
        variations.pending.pop_back();
        NameVariation& name = variations.names[name_index];
        if (name.read) continue;
        name.read = true;
        variations.pending.insert(variations.pending.end(), name.reads.begin(), name.reads.end());
    }
    std::vector<PaletteDefinition> palette_definitions{};
    std::size_t scoped_read = 0U;
    std::size_t first = 0U;
    while (first < definitions.size()) {
        const std::uint32_t name_index = definitions[first].name_index;
        std::size_t last = first + 1U;
        while (last < definitions.size() && definitions[last].name_index == name_index) ++last;
        const std::span<const CustomPropertyDefinition> group = definitions.subspan(first, last - first);
        first = last;
        NameVariation& name = variations.names[name_index];
        // In a theme compiled for an inline style (P12b), a name the style
        // defines varies for its readers; the theme's one definition of it
        // holds everywhere, as when the theme stands alone. The style alone
        // defines its names for the theme's readers as well as its own, so
        // each is scoped and lowered whether it reads them or not.
        const bool inline_form = input_.form == ThemeSheetForm::inline_style;
        std::size_t theme_definitions = 0U;
        for (const CustomPropertyDefinition& definition : group) {
            if (inline_form || in_inline_segment(definition.declaration.value)) {
                name.inline_defined = true;
                continue;
            }
            theme_definitions = theme_definitions + 1U;
            name.global_declaration = definition.declaration.value;
        }
        if (theme_definitions != 1U || !name.inline_defined) name.global_declaration = 0U;
        if (group.size() == 1U && !name.inline_defined) {
            // Defined in one place: substituted wherever it is read.
            name.fixed = 0U;
            variations.choices[name_index] = 0U;
            continue;
        }
        bool in_palette = !name.inline_defined;
        for (const CustomPropertyDefinition& definition : group) {
            in_palette = in_palette && selects_root(definition.rule.value);
        }
        palette_variants(group, name.variants);
        const bool everywhere = !name.variants.empty() && media_condition_unconditional(name.variants[0].media);
        if (in_palette) {
            if (name.variants.empty() || (everywhere && name.variants.size() == 1U)) {
                name.fixed = name.variants.empty() ? variable_undefined : name.variants[0].ordinal;
                variations.choices[name_index] = name.fixed;
                continue;
            }
            name.kind = Variation::media;
            continue;
        }
        name.kind = Variation::scoped;
        if (!scoped_candidates(group, name)) return false;
        if (!name.read) continue;
        scoped_read = scoped_read + 1U;
        if (scoped_read > maximum_scoped_custom_properties) {
            const std::uint32_t token = nodes[group[0].declaration.value].tokens.first.value;
            report(DiagnosticCode::too_many_scoped_variables, token, std::to_string(maximum_scoped_custom_properties));
            return false;
        }
        for (const CustomPropertyDefinition& definition : group) {
            if (!palette_scope(definition)) continue;
            palette_definitions.push_back(PaletteDefinition{.important = definition.important,
                                                            .rule = definition.rule.value,
                                                            .media = definition.media.value,
                                                            .declaration = definition.declaration.value,
                                                            .name = definition.name,
                                                            .global = !selects_root(definition.rule.value)});
        }
    }
    // The palette's definitions of the scoped names, first in the sheet:
    // at the sheet's level and no specificity, so that any rule's own
    // definition replaces them, as a CSS declaration replaces an
    // inherited value; the `!important` ones after the ordinary ones.
    std::sort(palette_definitions.begin(), palette_definitions.end(), PaletteDefinitionLess{});
    rule_ = ":root";
    const LoweredSelector everything{};
    const std::span<const LoweredSelector> root(&everything, 1U);
    std::size_t start = 0U;
    while (start < palette_definitions.size()) {
        const PaletteDefinition& head = palette_definitions[start];
        std::size_t end = start;
        const bool important = head.global && head.important;
        std::vector<ScopedDefinition>& into = important ? variations.important : variations.ordinary;
        variations.ordinary.clear();
        variations.important.clear();
        while (end < palette_definitions.size() && palette_definitions[end].rule == head.rule &&
               palette_definitions[end].important == head.important) {
            const PaletteDefinition& definition = palette_definitions[end];
            into.push_back(ScopedDefinition{.name = std::string(definition.name),
                                            .definition = definition_number(definition.declaration)});
            ++end;
        }
        start = end;
        const std::uint32_t at = nodes[head.declaration].tokens.first.value;
        source_position_ = head.rule;
        if (head.media == 0U) {
            media_ = MediaCondition{};
            if (!emit_definitions(root, important, at)) return false;
            continue;
        }
        MediaQueryProblem problem{};
        const MediaQueryOutcome read =
            read_media_queries(input_.source.text, tokens_, nodes[head.media].content, media_queries_, problem);
        if (read == MediaQueryOutcome::refused) continue;
        for (const MediaQuery& query : media_queries_) {
            if (query.unknown_feature || query.never) continue;
            media_ = query.condition;
            if (!emit_definitions(root, important, at)) return false;
        }
    }
    media_ = MediaCondition{};
    variations.ordinary.clear();
    variations.important.clear();
    source_position_ = 0U;
    rule_.clear();
    return true;
}

bool CompilePass::palette_scope(const CustomPropertyDefinition& definition) const noexcept {
    if (selects_root(definition.rule.value)) return true;
    const NameVariation& name = variations_.names[definition.name_index];
    return name.global_declaration != 0U && name.global_declaration == definition.declaration.value;
}

bool CompilePass::in_inline_segment(std::size_t node_index) const noexcept {
    return segment_node_ != 0U && node_index >= segment_node_;
}

std::uint32_t CompilePass::definition_number(std::uint32_t declaration) const noexcept {
    // An inline style numbers its own definitions by their nodes; written
    // after a theme, its first rule's node stands for its own first rule's
    // (node 1), its nodes keeping their order and nesting.
    if (input_.form == ThemeSheetForm::inline_style) return inline_definition_base | declaration;
    if (in_inline_segment(declaration)) return inline_definition_base | (declaration - segment_node_ + 1U);
    return declaration;
}

void CompilePass::palette_variants(std::span<const CustomPropertyDefinition> group,
                                   std::vector<MediaVariant>& variants) {
    // In cascade order, as the palette's roles (collect_palette): the
    // ordinary definitions, then the `!important` ones, each in sheet
    // order; an unconditional one replaces those before it.
    variants.clear();
    for (std::size_t pass = 0U; pass < 2U; ++pass) {
        for (std::size_t ordinal = 0U; ordinal < group.size(); ++ordinal) {
            const CustomPropertyDefinition& definition = group[ordinal];
            if (definition.important != (pass == 1U) || !palette_scope(definition)) continue;
            if (definition.media.value == 0U) {
                variants.clear();
                variants.push_back(MediaVariant{.ordinal = static_cast<std::uint32_t>(ordinal), .media = {}});
                continue;
            }
            // The rules' pass reports a refused prelude's problems.
            if (!read_block_queries(definition.media.value)) continue;
            for (const MediaQuery& query : media_queries_) {
                if (query.unknown_feature || query.never) continue;
                if (media_condition_unconditional(query.condition)) variants.clear();
                variants.push_back(
                    MediaVariant{.ordinal = static_cast<std::uint32_t>(ordinal), .media = query.condition});
            }
        }
    }
}

bool CompilePass::read_block_queries(std::uint32_t media_index) {
    MediaQueryProblem problem{};
    const MediaQueryOutcome read = read_media_queries(input_.source.text, tokens_, tree_.nodes[media_index].content,
                                                      media_queries_, problem);
    return read != MediaQueryOutcome::refused;
}

bool CompilePass::scoped_candidates(std::span<const CustomPropertyDefinition> group, NameVariation& name) {
    for (const CustomPropertyDefinition& definition : group) {
        if (!palette_scope(definition)) name.scoped_rules.push_back(definition.rule.value);
    }
    // The palette's definitions that may win: those left in its cascade.
    const std::vector<MediaVariant>& variants = name.variants;
    for (std::size_t index = 0U; index < variants.size(); ++index) {
        const std::uint32_t ordinal = variants[index].ordinal;
        // One candidate per definition, at its last variant (a block of
        // several queries gives one variant a query).
        bool later_same = false;
        std::size_t queries = 0U;
        for (std::size_t other = 0U; other < variants.size(); ++other) {
            if (variants[other].ordinal != ordinal) continue;
            queries = queries + 1U;
            later_same = later_same || other > index;
        }
        if (later_same) continue;
        VariableCandidate candidate{};
        candidate.ordinal = ordinal;
        candidate.unscoped = true;
        append_variable_names(input_.source, tokens_.tokens, group[ordinal].value, custom_properties_,
                              candidate.reads);
        if (queries == 1U && !media_condition_unconditional(variants[index].media)) {
            candidate.needs = variants[index].media;
        }
        for (std::size_t later = index + 1U; later < variants.size(); ++later) {
            candidate.replaced_under.push_back(variants[later].media);
        }
        name.candidates.push_back(std::move(candidate));
    }
    // The scoped definitions, but for one a later declaration in its rule
    // at its importance always replaces.
    for (std::size_t ordinal = 0U; ordinal < group.size(); ++ordinal) {
        const CustomPropertyDefinition& definition = group[ordinal];
        if (palette_scope(definition)) continue;
        bool replaced = false;
        for (std::size_t later = ordinal + 1U; later < group.size(); ++later) {
            replaced = replaced || (group[later].rule == definition.rule && group[later].important == definition.important);
        }
        if (replaced) continue;
        VariableCandidate candidate{};
        candidate.ordinal = static_cast<std::uint32_t>(ordinal);
        candidate.rule = definition.rule.value;
        append_variable_names(input_.source, tokens_.tokens, definition.value, custom_properties_, candidate.reads);
        rule_scopes(definition.rule.value, candidate.scopes,
                    definition.media.value == 0U ? &candidate.covers : nullptr);
        if (definition.media.value != 0U && read_block_queries(definition.media.value)) {
            std::size_t queries = 0U;
            for (const MediaQuery& query : media_queries_) {
                if (query.unknown_feature || query.never) continue;
                queries = queries + 1U;
                candidate.needs = query.condition;
            }
            if (queries != 1U) candidate.needs.reset();
        }
        name.candidates.push_back(std::move(candidate));
    }
    // None, where no palette definition holds everywhere.
    if (variants.empty() || !media_condition_unconditional(variants[0].media)) {
        VariableCandidate none{};
        none.unscoped = true;
        for (const MediaVariant& variant : variants) none.replaced_under.push_back(variant.media);
        name.candidates.push_back(std::move(none));
    }
    return true;
}

bool CompilePass::emit_definitions(std::span<const LoweredSelector> selectors, bool important, std::uint32_t at) {
    const std::vector<ScopedDefinition>& definitions = important ? variations_.important : variations_.ordinary;
    if (definitions.empty()) return true;
    std::size_t bytes = lowered_record_bytes + rule_.size();
    for (const ScopedDefinition& definition : definitions) bytes += lowered_record_bytes + definition.name.size();
    for (const LoweredSelector& selector : selectors) {
        if (bytes > budget_.bytes) {
            report(DiagnosticCode::lowered_sheet_too_large, at, std::to_string(maximum_lowered_bytes));
            return false;
        }
        budget_.bytes -= bytes;
        ThemeRule rule{};
        rule.label = rule_;
        rule.media = media_;
        rule.selector = selector.selector;
        rule.level = important ? CascadeLevel::important_theme_sheet : selector.level;
        // An inline style's definitions, alone or written after a theme
        // (P12b), are at the inline levels.
        if (input_.form == ThemeSheetForm::inline_style || in_inline_segment(source_position_)) {
            rule.level = important ? CascadeLevel::important_inline_style : CascadeLevel::inline_style;
        }
        if (input_.user_theme) rule.level = important ? CascadeLevel::important_user_theme : CascadeLevel::user_theme;
        if (!(selector.specificity == rule_specificity(selector.selector))) rule.specificity = selector.specificity;
        rule.scoped_definitions = definitions;
        rule.source_position = positions_ ? source_position_ : 0U;
        sheet_.rules.push_back(std::move(rule));
    }
    return true;
}

bool CompilePass::lower_definitions(std::size_t rule_index) {
    VariationStorage& variations = variations_;
    variations.ordinary.clear();
    variations.important.clear();
    const SyntaxNode& rule = tree_.nodes[rule_index];
    const std::size_t end = rule.subtree_end.value;
    for (std::size_t child = rule_index + 1U; child < end; child = tree_.nodes[child].subtree_end.value) {
        const SyntaxNode& node = tree_.nodes[child];
        const std::string_view name = token_text(node.tokens.first.value);
        const std::span<const CustomPropertyDefinition> found = custom_properties_.definitions_of(name);
        // Only a custom property's declaration is found among the
        // definitions (a declaration of another property is not).
        if (found.empty()) continue;
        const NameVariation& variation = variations.names[found[0].name_index];
        if (variation.kind != Variation::scoped || (!variation.read && !variation.inline_defined)) continue;
        // The theme's one definition of a name an inline style defines
        // went first, with the palette's (P12b).
        if (variation.global_declaration == child) continue;
        const bool important = (node.flags & syntax_flag_important) != 0U;
        std::vector<ScopedDefinition>& into = important ? variations.important : variations.ordinary;
        const ScopedDefinition definition{.name = std::string(name),
                                          .definition = definition_number(static_cast<std::uint32_t>(child))};
        // A later declaration of the name in the rule replaces this one.
        bool replaced = false;
        for (ScopedDefinition& earlier : into) {
            if (earlier.name != definition.name) continue;
            earlier.definition = definition.definition;
            replaced = true;
        }
        if (!replaced) into.push_back(definition);
    }
    if (variations.ordinary.empty() && variations.important.empty()) return true;
    const std::string_view written = range_text(rule.content);
    rule_ = clipped_theme_detail(written);
    lowered_selectors_.clear();
    const SelectorInput selector_input{.source = input_.source,
                                       .tokens = tokens_,
                                       .selectors = rule.content,
                                       .rule_label = written,
                                       .cursor = cursor_};
    // The rule's own lowering reports its selectors' warnings, and counts
    // it against the rule limit.
    const std::uint32_t at = rule.content.first.value;
    if (input_.form == ThemeSheetForm::inline_style && rule.content.first.value == rule.content.end.value) {
        // An inline style's own declarations: the control itself.
        lowered_selectors_.push_back(LoweredSelector{.selector = {}, .specificity = {}, .level = {}});
        if (!emit_definitions(lowered_selectors_, false, at)) return false;
        return emit_definitions(lowered_selectors_, true, at);
    }
    const bool muted = diagnostics_.warnings_muted();
    const std::size_t rules = budget_.rules;
    diagnostics_.mute_warnings(true);
    const SelectorOutcome selected =
        selectors_.lower(selector_input, input_.vocabulary, budget_, lowered_selectors_, diagnostics_);
    diagnostics_.mute_warnings(muted);
    budget_.rules = rules;
    if (selected == SelectorOutcome::refused) return false;
    if (selected == SelectorOutcome::dropped) return true;
    if (!emit_definitions(lowered_selectors_, false, at)) return false;
    return emit_definitions(lowered_selectors_, true, at);
}

void CompilePass::read_variations(std::size_t rule_index) {
    VariationStorage& variations = variations_;
    variations.direct.clear();
    variations.pending.clear();
    variations.visited.clear();
    variations.media_read.clear();
    variations.conditions.clear();
    variations.reads_scoped = false;
    const SyntaxNode& rule = tree_.nodes[rule_index];
    const std::size_t end = rule.subtree_end.value;
    for (std::size_t child = rule_index + 1U; child < end; child = tree_.nodes[child].subtree_end.value) {
        const SyntaxNode& node = tree_.nodes[child];
        fold_ascii(token_text(node.tokens.first.value), folded_);
        if (classify(folded_) == PropertyClass::custom) continue;
        append_variable_names(input_.source, tokens_.tokens, node.content, custom_properties_, variations.direct);
    }
    std::sort(variations.direct.begin(), variations.direct.end());
    const std::vector<std::uint32_t>::iterator unique_end =
        std::unique(variations.direct.begin(), variations.direct.end());
    variations.direct.erase(unique_end, variations.direct.end());
    // Every name it may read, through every definition: its media names
    // set the worlds (a world a copy's chosen candidates do not need
    // gives that copy again, unchanged, which costs room but not
    // correctness).
    variations.pending.assign(variations.direct.begin(), variations.direct.end());
    while (!variations.pending.empty()) {
        const std::uint32_t name_index = variations.pending.back();
        variations.pending.pop_back();
        if (variations.seen[name_index] != 0U) continue;
        variations.seen[name_index] = 1U;
        variations.visited.push_back(name_index);
        const NameVariation& name = variations.names[name_index];
        if (name.kind == Variation::media) variations.media_read.push_back(name_index);
        if (name.kind == Variation::scoped) variations.reads_scoped = true;
        variations.pending.insert(variations.pending.end(), name.reads.begin(), name.reads.end());
    }
    for (const std::uint32_t name_index : variations.visited) variations.seen[name_index] = 0U;
    // In name order, so that the conditions' order follows the names'.
    std::sort(variations.media_read.begin(), variations.media_read.end());
    for (const std::uint32_t name_index : variations.media_read) {
        for (const MediaVariant& variant : variations.names[name_index].variants) {
            if (media_condition_unconditional(variant.media)) continue;
            const bool known = std::find(variations.conditions.begin(), variations.conditions.end(),
                                         variant.media) != variations.conditions.end();
            if (!known) variations.conditions.push_back(variant.media);
        }
    }
}

void CompilePass::rule_scopes(std::size_t rule_index, std::vector<SelectorScope>& scopes,
                              std::vector<std::string>* covers) {
    scopes.clear();
    if (covers != nullptr) (*covers).clear();
    const SyntaxNode& rule = tree_.nodes[rule_index];
    const std::string_view written = range_text(rule.content);
    const SelectorInput selector_input{.source = input_.source,
                                       .tokens = tokens_,
                                       .selectors = rule.content,
                                       .rule_label = written,
                                       .cursor = cursor_};
    // The rule's own lowering reports its selectors and counts them
    // against the limits, so this one does neither.
    LoweringBudget budget = budget_;
    ThemeDiagnosticList unreported{};
    std::vector<LoweredSelector>& lowered = variations_.scope_selectors;
    lowered.clear();
    const SelectorOutcome selected = selectors_.lower(selector_input, input_.vocabulary, budget, lowered, unreported);
    if (selected != SelectorOutcome::lowered) return;
    for (const LoweredSelector& selector : lowered) {
        const RuleSelector& named = selector.selector;
        scopes.push_back(SelectorScope{.control_type = named.control_type, .stable_id = named.stable_id});
        const bool whole = !named.control_type.empty() && named.stable_id.empty() && named.classes.empty() &&
                           named.part.empty() && !named.role.has_value() && named.states.empty();
        if (whole && covers != nullptr) (*covers).push_back(named.control_type);
    }
}

std::optional<std::uint32_t> CompilePass::undecided_name() {
    VariationStorage& variations = variations_;
    variations.pending.assign(variations.direct.begin(), variations.direct.end());
    variations.visited.clear();
    std::optional<std::uint32_t> first{};
    while (!variations.pending.empty()) {
        const std::uint32_t name_index = variations.pending.back();
        variations.pending.pop_back();
        if (variations.seen[name_index] != 0U) continue;
        variations.seen[name_index] = 1U;
        variations.visited.push_back(name_index);
        const NameVariation& name = variations.names[name_index];
        if (name.kind != Variation::scoped) {
            variations.pending.insert(variations.pending.end(), name.reads.begin(), name.reads.end());
            continue;
        }
        std::optional<std::size_t> level{};
        for (std::size_t index = 0U; index < variations.scoped_read.size(); ++index) {
            if (variations.scoped_read[index] == name_index) level = index;
        }
        if (!level) {
            if (!first || name_index < *first) first = name_index;
            continue;
        }
        const VariableCandidate& chosen = name.candidates[variations.scoped_choice[*level]];
        variations.pending.insert(variations.pending.end(), chosen.reads.begin(), chosen.reads.end());
    }
    for (const std::uint32_t name_index : variations.visited) variations.seen[name_index] = 0U;
    return first;
}

namespace {

// Whether two selectors can match a part in common (SelectorScope).
[[nodiscard]] bool scopes_meet(const SelectorScope& left, const SelectorScope& right) noexcept {
    const bool types_differ =
        !left.control_type.empty() && !right.control_type.empty() && left.control_type != right.control_type;
    const bool ids_differ = !left.stable_id.empty() && !right.stable_id.empty() && left.stable_id != right.stable_id;
    return !types_differ && !ids_differ;
}

// Whether a candidate's rule can match a part some selector of the reader
// matches; a rule or reader of unknown scopes can.
[[nodiscard]] bool candidate_reaches(const VariableCandidate& candidate,
                                     std::span<const SelectorScope> reader) noexcept {
    if (candidate.rule == 0U || candidate.scopes.empty() || reader.empty()) return true;
    for (const SelectorScope& own : candidate.scopes) {
        for (const SelectorScope& read : reader) {
            if (scopes_meet(own, read)) return true;
        }
    }
    return false;
}

// Whether every selector of the reader is of a type the name's scoped
// definitions select whole: then one of them always matches its parts.
[[nodiscard]] bool reader_covered(const NameVariation& name, std::span<const SelectorScope> reader) noexcept {
    if (reader.empty()) return false;
    for (const VariableCandidate& candidate : name.candidates) {
        for (const std::string& type : candidate.covers) {
            bool all = true;
            for (const SelectorScope& read : reader) all = all && read.control_type == type;
            if (all) return true;
        }
    }
    return false;
}

} // namespace

bool CompilePass::candidates_meet(std::size_t depth, std::size_t world, const MediaCondition& joined) const {
    const VariationStorage& variations = variations_;
    const std::span<const SelectorScope> reader(variations.reader_scopes);
    MediaCondition needed = joined;
    for (std::size_t index = 0U; index <= depth; ++index) {
        const NameVariation& name = variations.names[variations.scoped_read[index]];
        if (variations.scoped_choice[index] >= name.candidates.size()) return false;
        const VariableCandidate& candidate = name.candidates[variations.scoped_choice[index]];
        if (candidate.needs && !join_media(needed, *candidate.needs, needed)) return false;
        // A rule that matches none of the reader's parts never wins for
        // them; and where a definition selecting the reader's control
        // whole always matches, neither the palette's nor none wins.
        if (!candidate_reaches(candidate, reader)) return false;
        if (candidate.unscoped && reader_covered(name, reader)) return false;
    }
    // Where a condition the world leaves out holds, a later world's copy
    // applies; and a candidate's replacing conditions must not hold.
    for (std::size_t index = 0U; index < variations.conditions.size(); ++index) {
        if ((world & (std::size_t{1} << index)) != 0U) continue;
        MediaCondition with{};
        if (join_media(needed, variations.conditions[index], with) && with == needed) return false;
    }
    for (std::size_t index = 0U; index <= depth; ++index) {
        const NameVariation& name = variations.names[variations.scoped_read[index]];
        const VariableCandidate& candidate = name.candidates[variations.scoped_choice[index]];
        for (const MediaCondition& replacing : candidate.replaced_under) {
            MediaCondition with{};
            if (join_media(needed, replacing, with) && with == needed) return false;
        }
        // A scoped definition's rule matches where it wins; where a
        // palette definition or none wins, no scoped definition of that
        // name matches.
        if (candidate.rule == 0U) continue;
        for (std::size_t other = 0U; other <= depth; ++other) {
            const NameVariation& unscoped = variations.names[variations.scoped_read[other]];
            if (!unscoped.candidates[variations.scoped_choice[other]].unscoped) continue;
            const bool excluded = std::find(unscoped.scoped_rules.begin(), unscoped.scoped_rules.end(),
                                            candidate.rule) != unscoped.scoped_rules.end();
            if (excluded) return false;
        }
    }
    return true;
}

// The copies of a rule reading varying names (P12): one for each set of
// the media names' conditions taken as holding (a world), in counting
// order, so that a window's last holding copy is the one of every
// condition it meets, whose media names read their last definitions that
// hold there; and within each, one for each combination of the scoped
// names' candidates that can win together, which the engine tells apart
// by the part's winners. The combinations are a decision tree: a copy
// decides only the scoped names it reads given the candidates chosen
// above them (a candidate's value may read further names), so its
// conditions name those alone, and the copies of a world still cover
// every part once.
bool CompilePass::lower_rule(std::size_t rule_index) {
    // The palette's own block was collected first.
    if (selects_root(rule_index)) return true;
    source_position_ = static_cast<std::uint32_t>(rule_index);
    const bool definitions = lower_definitions(rule_index);
    // An inline style's rules after a theme (P12b) give their definitions
    // only, its declarations being the style's own compilation's, unless
    // the style reads the theme's names (inline_declarations).
    if (!definitions || (in_inline_segment(rule_index) && !input_.inline_declarations)) {
        source_position_ = 0U;
        return definitions;
    }
    read_variations(rule_index);
    VariationStorage& variations = variations_;
    variations.world = 0U;
    // The names it reads, for the inline styles that redefine one (P12b).
    variations.read_names.clear();
    if (input_.form == ThemeSheetForm::sheet && !in_inline_segment(rule_index)) {
        for (const std::uint32_t name_index : variations.visited) {
            const std::span<const CustomPropertyDefinition> named = custom_properties_.definitions_named(name_index);
            variations.read_names.push_back(std::string(named[0].name));
        }
        std::sort(variations.read_names.begin(), variations.read_names.end());
    }
    undefined_drops_ = true;
    if (variations.media_read.empty() && !variations.reads_scoped) {
        variations.conditions.clear();
        const bool lowered = lower_rule_palette(rule_index);
        undefined_drops_ = false;
        source_position_ = 0U;
        return lowered;
    }
    const SyntaxNode& rule = tree_.nodes[rule_index];
    variations.reader_scopes.clear();
    if (variations.reads_scoped) rule_scopes(rule_index, variations.reader_scopes, nullptr);
    // At most maximum_variable_combinations copies, and so at most
    // log2 of it conditions.
    std::size_t worlds = 1U;
    for (std::size_t index = 0U; index < variations.conditions.size() && worlds <= maximum_variable_combinations;
         ++index) {
        worlds = worlds * 2U;
    }
    const MediaCondition rule_media = media_;
    const bool muted = diagnostics_.warnings_muted();
    variations.assigned.clear();
    std::size_t copies = 0U;
    std::size_t steps = 0U;
    bool over = worlds > maximum_variable_combinations;
    bool lowered = true;
    for (std::size_t world = 0U; lowered && !over && world < worlds; ++world) {
        work_.variation_worlds += 1U;
        MediaCondition joined = rule_media;
        bool holds = true;
        for (std::size_t index = 0U; index < variations.conditions.size() && holds; ++index) {
            if ((world & (std::size_t{1} << index)) == 0U) continue;
            holds = join_media(joined, variations.conditions[index], joined);
        }
        // A world no window meets is skipped, and so is one where a
        // condition it leaves out holds wherever it does: the world with
        // that condition too follows it.
        for (std::size_t index = 0U; index < variations.conditions.size() && holds; ++index) {
            if ((world & (std::size_t{1} << index)) != 0U) continue;
            MediaCondition with{};
            holds = !(join_media(joined, variations.conditions[index], with) && with == joined);
        }
        if (!holds) continue;
        variations.world = world;
        for (const std::uint32_t name_index : variations.media_read) {
            std::uint32_t choice = variable_undefined;
            for (const MediaVariant& variant : variations.names[name_index].variants) {
                bool meets = media_condition_unconditional(variant.media);
                for (std::size_t index = 0U; index < variations.conditions.size(); ++index) {
                    const bool taken = (world & (std::size_t{1} << index)) != 0U;
                    meets = meets || (taken && variations.conditions[index] == variant.media);
                }
                if (meets) choice = variant.ordinal;
            }
            variations.choices[name_index] = choice;
            variations.assigned.push_back(name_index);
        }
        // The decision tree, depth first in counting order: each level
        // decides the next scoped name the copy reads; a level whose
        // candidates cannot win together with those above is passed over.
        variations.scoped_read.clear();
        variations.scoped_choice.clear();
        bool fresh = true;
        bool searching = true;
        while (searching && lowered && !over) {
            if (fresh) {
                bool feasible = true;
                if (!variations.scoped_read.empty()) {
                    steps = steps + 1U;
                    over = steps > maximum_variable_steps;
                    if (over) break;
                    feasible = candidates_meet(variations.scoped_read.size() - 1U, world, joined);
                }
                const std::optional<std::uint32_t> next = feasible ? undecided_name() : std::nullopt;
                if (next) {
                    variations.scoped_read.push_back(*next);
                    variations.scoped_choice.push_back(0U);
                    continue;
                }
                if (feasible) {
                    copies = copies + 1U;
                    over = copies > maximum_variable_combinations;
                    if (over) break;
                    variations.copy_conditions.clear();
                    for (std::size_t index = 0U; index < variations.scoped_read.size(); ++index) {
                        const std::uint32_t name_index = variations.scoped_read[index];
                        const VariableCandidate& candidate =
                            variations.names[name_index].candidates[variations.scoped_choice[index]];
                        variations.choices[name_index] = candidate.ordinal;
                        variations.assigned.push_back(name_index);
                        const std::span<const CustomPropertyDefinition> named =
                            custom_properties_.definitions_named(name_index);
                        ScopedCondition condition{};
                        condition.name = std::string(named[0].name);
                        if (candidate.ordinal != variable_undefined) {
                            condition.definition = definition_number(named[candidate.ordinal].declaration.value);
                        }
                        variations.copy_conditions.push_back(std::move(condition));
                    }
                    media_ = joined;
                    // The copies' warnings are the rule's, reported once.
                    if (copies > 1U) diagnostics_.mute_warnings(true);
                    lowered = lower_rule_palette(rule_index);
                    for (const std::uint32_t name_index : variations.scoped_read) {
                        variations.choices[name_index] = variable_unassigned;
                    }
                }
            }
            // The next candidate at the deepest level, or back up.
            fresh = false;
            while (!variations.scoped_read.empty()) {
                const NameVariation& name = variations.names[variations.scoped_read.back()];
                if (variations.scoped_choice.back() + 1U < name.candidates.size()) {
                    variations.scoped_choice.back() = variations.scoped_choice.back() + 1U;
                    fresh = true;
                    break;
                }
                variations.scoped_read.pop_back();
                variations.scoped_choice.pop_back();
            }
            searching = fresh;
        }
    }
    media_ = rule_media;
    diagnostics_.mute_warnings(muted);
    variations.copy_conditions.clear();
    variations.scoped_read.clear();
    variations.scoped_choice.clear();
    variations.world = 0U;
    for (const std::uint32_t name_index : variations.assigned) variations.choices[name_index] = variable_unassigned;
    variations.assigned.clear();
    undefined_drops_ = false;
    source_position_ = 0U;
    if (over && lowered) {
        rule_ = clipped_theme_detail(range_text(rule.content));
        property_.clear();
        report(DiagnosticCode::too_many_variable_combinations, rule.content.first.value,
               std::to_string(maximum_variable_combinations));
        return false;
    }
    return lowered;
}

// A window takes the last copy whose condition holds there: for each role,
// its last variant that holds, as CSS's later `:root` declaration wins.
bool CompilePass::lower_rule_palette(std::size_t rule_index) {
    palette_.in_use = palette_.base;
    rule_roles_read_ = 0U;
    const std::size_t rules = sheet_.rules.size();
    const std::size_t origins = motion_.origins.size();
    const LoweringBudget budget = budget_;
    if (!lower_rule_once(rule_index)) return false;
    palette_.varying.clear();
    for (std::size_t role = 0U; role < colour_role_count; ++role) {
        const bool read = (rule_roles_read_ & (1U << role)) != 0U;
        if (read && palette_.variants[role].size() > 1U) palette_.varying.push_back(role);
    }
    if (palette_.varying.empty()) return true;
    palette_.choice.assign(palette_.varying.size(), 0U);
    const MediaCondition rule_media = media_;
    // The base copy is taken back where a later variant of a role it reads
    // holds wherever the rule does (a world of lower_rule that takes the
    // variant's condition): the copies with that variant follow and cover
    // it. Its lowering still told which roles the rule reads.
    bool dominated = false;
    for (const std::size_t role : palette_.varying) {
        const std::vector<PaletteVariant>& variants = palette_.variants[role];
        if (!media_condition_unconditional(variants[0].media)) continue;
        for (std::size_t later = 1U; later < variants.size(); ++later) {
            MediaCondition with{};
            dominated = dominated || (join_media(rule_media, variants[later].media, with) && with == rule_media);
        }
    }
    if (dominated) {
        sheet_.rules.resize(rules);
        motion_.origins.resize(origins);
        budget_ = budget;
    }
    const bool muted = diagnostics_.warnings_muted();
    // The copies' warnings are the rule's, reported once.
    diagnostics_.mute_warnings(true);
    bool lowered = true;
    while (lowered && next_palette_choice()) {
        MediaCondition joined = rule_media;
        bool holds = true;
        for (std::size_t index = 0U; index < palette_.varying.size(); ++index) {
            const std::size_t role = palette_.varying[index];
            const PaletteVariant& variant = palette_.variants[role][palette_.choice[index]];
            palette_.in_use.roles[role] = variant.colours;
            if (holds) holds = join_media(joined, variant.media, joined);
        }
        // A copy no window can meet is skipped, and so is one a later copy
        // covers: where a later variant of one of its roles holds wherever
        // it does, or a condition the variable world (lower_rule) leaves
        // out does, that world's copies following.
        for (std::size_t index = 0U; index < palette_.varying.size() && holds; ++index) {
            const std::vector<PaletteVariant>& variants = palette_.variants[palette_.varying[index]];
            for (std::size_t later = palette_.choice[index] + 1U; later < variants.size() && holds; ++later) {
                MediaCondition with{};
                holds = !(join_media(joined, variants[later].media, with) && with == joined);
            }
        }
        for (std::size_t index = 0U; index < variations_.conditions.size() && holds; ++index) {
            if ((variations_.world & (std::size_t{1} << index)) != 0U) continue;
            MediaCondition with{};
            holds = !(join_media(joined, variations_.conditions[index], with) && with == joined);
        }
        if (!holds) continue;
        media_ = joined;
        lowered = lower_rule_once(rule_index);
    }
    media_ = rule_media;
    palette_.in_use = palette_.base;
    diagnostics_.mute_warnings(muted);
    return lowered;
}

bool CompilePass::next_palette_choice() noexcept {
    // The last role's variant moves fastest: the combinations come in
    // lexicographic order, the base's first.
    for (std::size_t index = palette_.choice.size(); index > 0U; --index) {
        std::size_t& choice = palette_.choice[index - 1U];
        const std::size_t role = palette_.varying[index - 1U];
        if (choice + 1U < palette_.variants[role].size()) {
            choice = choice + 1U;
            return true;
        }
        choice = 0U;
    }
    return false;
}

bool CompilePass::escapes_in_selectors_only() {
    const std::vector<Token>& tokens = tokens_.tokens;
    std::size_t escaped = tokens.size();
    for (std::size_t index = 0U; index < tokens.size() && escaped == tokens.size(); ++index) {
        if ((tokens[index].flags & token_flag_escaped) != 0U) escaped = index;
    }
    if (escaped == tokens.size()) return true;
    // The style rules' selectors, in token order (the tree is in source
    // order, a rule's prelude before its block and its nested rules).
    std::size_t token = escaped;
    for (const SyntaxNode& node : tree_.nodes) {
        if (node.kind != SyntaxNodeKind::style_rule) continue;
        while (token < tokens.size() && token < node.content.first.value) {
            if ((tokens[token].flags & token_flag_escaped) != 0U) {
                report(DiagnosticCode::escape_not_admitted, static_cast<std::uint32_t>(token),
                       "an escape outside a selector");
                return false;
            }
            ++token;
        }
        if (token < node.content.end.value) token = node.content.end.value;
    }
    while (token < tokens.size()) {
        if ((tokens[token].flags & token_flag_escaped) != 0U) {
            report(DiagnosticCode::escape_not_admitted, static_cast<std::uint32_t>(token),
                   "an escape outside a selector");
            return false;
        }
        ++token;
    }
    return true;
}

bool CompilePass::selects_root(std::size_t rule_index) const noexcept {
    // An inline style has no palette: `:root` there is a selector.
    if (input_.form == ThemeSheetForm::inline_style) return false;
    const TokenRange range = tree_.nodes[rule_index].content;
    std::size_t written = 0U;
    bool root = false;
    for (std::uint32_t index = range.first.value; index < range.end.value; ++index) {
        const Token& token = tokens_.tokens[index];
        if (token.kind == TokenKind::whitespace || token.kind == TokenKind::comment) continue;
        written = written + 1U;
        if (written == 1U && token.kind != TokenKind::colon) return false;
        if (written == 2U) root = token.kind == TokenKind::ident && equals_ascii_folded(token_text(index), "root");
    }
    return written == 2U && root;
}

bool CompilePass::collect_palette() {
    for (std::vector<PaletteVariant>& variants : palette_.variants) variants.clear();
    palette_.base = RolePalette{};
    const std::vector<SyntaxNode>& nodes = tree_.nodes;
    for (std::size_t index = 1U; index < nodes.size(); index = nodes[index].subtree_end.value) {
        const SyntaxNode& node = nodes[index];
        if (node.kind == SyntaxNodeKind::style_rule && selects_root(index)) {
            if (!collect_palette_block(index, true)) return false;
            continue;
        }
        if (node.kind != SyntaxNodeKind::media_rule) continue;
        // The rules' pass reports the prelude's problems.
        MediaQueryProblem problem{};
        const MediaQueryOutcome read =
            read_media_queries(input_.source.text, tokens_, node.content, media_queries_, problem);
        if (read == MediaQueryOutcome::refused) continue;
        const std::size_t end = node.subtree_end.value;
        bool lowered = false;
        for (const MediaQuery& query : media_queries_) {
            if (query.unknown_feature || query.never) continue;
            media_ = query.condition;
            for (std::size_t child = index + 1U; child < end; child = nodes[child].subtree_end.value) {
                if (nodes[child].kind != SyntaxNodeKind::style_rule || !selects_root(child)) continue;
                if (!collect_palette_block(child, true)) return false;
            }
            lowered = true;
        }
        media_ = MediaCondition{};
        for (std::size_t child = index + 1U; !lowered && child < end; child = nodes[child].subtree_end.value) {
            if (nodes[child].kind != SyntaxNodeKind::style_rule || !selects_root(child)) continue;
            if (!collect_palette_block(child, false)) return false;
        }
    }
    for (std::size_t role = 0U; role < colour_role_count; ++role) {
        const std::vector<PaletteVariant>& variants = palette_.variants[role];
        if (variants.empty() || !media_condition_unconditional(variants[0].media)) continue;
        palette_.base.roles[role] = variants[0].colours;
    }
    palette_.in_use = palette_.base;
    rule_.clear();
    property_.clear();
    return true;
}

bool CompilePass::lower_colour_scheme(const SyntaxNode& node) {
    const std::uint32_t property = node.tokens.first.value;
    if (!media_condition_unconditional(media_)) {
        report(DiagnosticCode::value_form_not_admitted, property, "color-scheme under @media");
        return false;
    }
    ColourSchemeSet declared{.light = false, .dark = false};
    std::size_t words = 0U;
    for (std::uint32_t index = node.content.first.value; index < node.content.end.value; ++index) {
        const Token& token = tokens_.tokens[index];
        if (token.kind == TokenKind::whitespace || token.kind == TokenKind::comment) continue;
        const std::string_view word = token_text(index);
        const bool light = token.kind == TokenKind::ident && equals_ascii_folded(word, "light");
        const bool dark = token.kind == TokenKind::ident && equals_ascii_folded(word, "dark");
        const bool repeated = (light && declared.light) || (dark && declared.dark);
        if ((!light && !dark) || repeated) {
            // `normal`, `only` and every other word are outside the language.
            report(DiagnosticCode::keyword_unknown, index, clipped_theme_detail(word));
            return false;
        }
        if (light) declared.light = true;
        if (dark) declared.dark = true;
        words = words + 1U;
    }
    if (words == 0U) {
        report(DiagnosticCode::value_syntax, property, "color-scheme: light | dark | light dark");
        return false;
    }
    sheet_.colour_schemes = declared;
    return true;
}

bool CompilePass::collect_palette_block(std::size_t rule_index, bool record) {
    const SyntaxNode& rule = tree_.nodes[rule_index];
    rule_ = clipped_theme_detail(range_text(rule.content));
    const std::size_t end = rule.subtree_end.value;
    for (std::size_t child = rule_index + 1U; child < end; child = tree_.nodes[child].subtree_end.value) {
        const SyntaxNode& node = tree_.nodes[child];
        const std::uint32_t property = node.tokens.first.value;
        property_ = clipped_theme_detail(token_text(property));
        fold_ascii(token_text(property), folded_);
        if (classify(folded_) == PropertyClass::custom) continue;
        if ((node.flags & syntax_flag_important) != 0U) {
            report(DiagnosticCode::value_form_not_admitted, property, "!important in the palette");
            return false;
        }
        if (folded_ == "color-scheme") {
            if (!record) continue;
            if (!lower_colour_scheme(node)) return false;
            continue;
        }
        const std::optional<ColourRole> role = colour_role_from_name(folded_);
        if (!role) {
            // A palette declares roles and custom properties only.
            report(DiagnosticCode::property_not_admitted, property, property_ + " in the palette");
            continue;
        }
        const bool paired = colour_role_paired(*role);
        parameter_names_.clear();
        reading_palette_ = true;
        const bool read = read_value(node);
        reading_palette_ = false;
        if (!read) return false;
        const std::vector<ValueComponent>& components = value_.components;
        bool shaped = paired ? components.size() == 3U : components.size() == 1U;
        if (shaped) shaped = writes_colour(components[0]) && writes_colour(components.back());
        if (shaped && paired) {
            const ValueComponent& between = components[1];
            shaped = between.kind == ValueComponentKind::keyword && equals_ascii_folded(between.text, "on");
        }
        if (!shaped) {
            report(DiagnosticCode::value_wrong_kind, components[0].token,
                   paired ? "<colour> on <colour>" : "one colour");
            return false;
        }
        const ValueComponent& foreground = components[0];
        const ValueComponent& background = components.back();
        PaletteVariant variant{};
        variant.media = media_;
        variant.colours.foreground =
            foreground.kind == ValueComponentKind::colour ? foreground.colour : *named_colour(foreground.text);
        variant.colours.background =
            background.kind == ValueComponentKind::colour ? background.colour : *named_colour(background.text);
        if (!record) continue;
        std::vector<PaletteVariant>& variants = palette_.variants[static_cast<std::size_t>(*role)];
        // An unconditional declaration replaces the role's earlier ones.
        if (media_condition_unconditional(media_)) variants.clear();
        variants.push_back(variant);
    }
    return true;
}

bool CompilePass::lower_rule_once(std::size_t rule_index) {
    const SyntaxNode& rule = tree_.nodes[rule_index];
    const std::string_view written = range_text(rule.content);
    rule_ = clipped_theme_detail(written);
    property_.clear();
    lowered_selectors_.clear();
    const SelectorInput selector_input{.source = input_.source,
                                       .tokens = tokens_,
                                       .selectors = rule.content,
                                       .rule_label = written,
                                       .cursor = cursor_};
    const bool inline_style = input_.form == ThemeSheetForm::inline_style;
    if (inline_style && rule.content.first.value == rule.content.end.value) {
        // An inline style's own declarations: the control itself.
        lowered_selectors_.push_back(LoweredSelector{.selector = {}, .specificity = {}, .level = {}});
    } else {
        const SelectorOutcome selected =
            selectors_.lower(selector_input, input_.vocabulary, budget_, lowered_selectors_, diagnostics_);
        if (selected == SelectorOutcome::refused) return false;
        // A dropped rule (an unknown name, reported) declares nothing.
        if (selected == SelectorOutcome::dropped) return true;
    }
    for (const LoweredSelector& lowered : lowered_selectors_) {
        const RuleSelector& named = lowered.selector;
        const bool other = !named.control_type.empty() || !named.stable_id.empty() || !named.classes.empty();
        if (inline_style && other) {
            // An inline style's blocks name its control's parts and states.
            report(DiagnosticCode::selector_syntax, rule.content.first.value, "a selector of its own control only");
            return false;
        }
    }

    // The rule's primitive, from its ordinary and its `!important`
    // `primitive` declarations, each the last of its kind (P9, EC6).
    const std::size_t end = rule.subtree_end.value;
    PrimitiveSelection ordinary{};
    PrimitiveSelection important{};
    bool any_important = false;
    for (std::size_t child = rule_index + 1U; child < end; child = tree_.nodes[child].subtree_end.value) {
        const SyntaxNode& node = tree_.nodes[child];
        const std::uint32_t property = node.tokens.first.value;
        property_ = clipped_theme_detail(token_text(property));
        const bool flagged = (node.flags & syntax_flag_important) != 0U;
        any_important = any_important || flagged;
        fold_ascii(token_text(property), folded_);
        const PropertyClass kind = classify(folded_);
        if (kind != PropertyClass::primitive) continue;
        PrimitiveSelection one{};
        if (!select_primitive(node, one)) {
            // A var() no definition of which holds for this copy: the
            // declaration is not declared (P12).
            if (!undefined_read_) return false;
            undefined_read_ = false;
            continue;
        }
        PrimitiveSelection& into = flagged ? important : ordinary;
        if (one.selects) {
            into.selects = true;
            into.primitive = one.primitive;
        }
        into.dropped = into.dropped || one.dropped;
    }
    // The ordinary declarations are a rule at the selector's level, the
    // `!important` ones a sibling rule at the important level, with the
    // same selector, label and media condition. The sibling selects the
    // primitive only where its own `primitive` declaration is important;
    // the ordinary rule's parameters belong to its own selection, else to
    // the important one. The levels differ, so the order of the two in the
    // sheet changes nothing; the sibling goes first, so the ordinary rule
    // knows whether the rule has material fields (C7).
    const bool undeclared = !ordinary.selects && !important.selects && !ordinary.dropped && !important.dropped &&
                            input_.registry.find(material_primitive_name) != nullptr;
    bool important_materials = false;
    if (any_important) {
        PrimitiveSelection sibling = important;
        sibling.dropped = important.dropped || ordinary.dropped;
        if (!lower_declarations(rule_index, true, sibling, ordinary)) return false;
        important_materials = !materials_.empty();
        // A rule of material fields and no `primitive` selects the material
        // primitive (owner C7, 2026-10-26), as if it were declared with
        // them: with the `!important` ones where there are any, so that
        // the selection stands where they do, even under forced high
        // contrast (E1).
        const EmittedRule sibling_rule{.selection = important,
                                       .implicit_material = undeclared && important_materials,
                                       .important = true};
        if (!emit_rule(sibling_rule, rule.content.first.value)) return false;
    }
    PrimitiveSelection own = ordinary.selects ? ordinary : important;
    own.dropped = ordinary.dropped || important.dropped;
    if (!lower_declarations(rule_index, false, own, important)) return false;
    // Without `!important` material fields, the ordinary rule's own select
    // the material primitive at its level; with them, the ordinary rule
    // borrows the sibling's selection.
    const bool implicit_material = undeclared && (!materials_.empty() || important_materials);
    const bool borrowed = (!ordinary.selects && important.selects) || (undeclared && important_materials);
    const EmittedRule emitted{.selection = own, .implicit_material = implicit_material, .borrowed = borrowed};
    return emit_rule(emitted, rule.content.first.value);
}

bool CompilePass::lower_declarations(std::size_t rule_index, bool important, const PrimitiveSelection& selection,
                                     const PrimitiveSelection& other) {
    const std::size_t end = tree_.nodes[rule_index].subtree_end.value;
    declarations_.clear();
    materials_.clear();
    recipe_fields_.clear();
    backdrop_.reset();
    clear_written_transitions(motion_.written);
    clear_written_animations(motion_.animations_written);
    for (std::size_t child = rule_index + 1U; child < end; child = tree_.nodes[child].subtree_end.value) {
        const SyntaxNode& node = tree_.nodes[child];
        if (((node.flags & syntax_flag_important) != 0U) != important) continue;
        const std::uint32_t property = node.tokens.first.value;
        property_ = clipped_theme_detail(token_text(property));
        fold_ascii(token_text(property), folded_);
        const PropertyClass kind = classify(folded_);
        if (kind == PropertyClass::custom || kind == PropertyClass::primitive) continue;
        // A window or popup rule holds its backdrop alone (owner decision
        // 2026-11-05): its surface has no part a control property styles.
        if (kind != PropertyClass::backdrop && rule_selects_surface()) {
            report(DiagnosticCode::property_not_admitted, property, property_ + " on a window or popup");
            continue;
        }
        if (important && !selection.selects && other.primitive != nullptr &&
            names_parameter((*other.primitive).descriptor(), folded_)) {
            // The parameter would select its primitive at the important
            // level, which its rule did not write.
            report(DiagnosticCode::value_form_not_admitted, property,
                   "!important on a parameter of a primitive selected without it");
            return false;
        }
        if (!lower_declaration(node, kind, selection)) {
            if (!undefined_read_) return false;
            undefined_read_ = false;
        }
    }
    property_.clear();
    return true;
}

bool CompilePass::select_primitive(const SyntaxNode& node, PrimitiveSelection& selection) {
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const WideKeyword wide = wide_keyword();
    const ValueComponent& first = value_.components[0];
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (wide == WideKeyword::initial) {
        selection.selects = true;
        selection.primitive = nullptr;
        return true;
    }
    if (drop_unknown_function()) {
        selection.dropped = true;
        return true;
    }
    if (value_.components.size() != 1U || first.kind != ValueComponentKind::keyword) {
        report(DiagnosticCode::value_wrong_kind, first.token, "a primitive's name");
        return false;
    }
    fold_ascii(first.text, folded_);
    if (folded_ == "built-in") {
        selection.selects = true;
        selection.primitive = nullptr;
        return true;
    }
    const std::string registered = registered_name_from_kebab(folded_);
    const EnginePrimitive* const primitive = registered.empty() ? nullptr : input_.registry.find(registered);
    if (primitive == nullptr) {
        report(DiagnosticCode::unknown_primitive, first.token, clipped_theme_detail(first.text));
        selection.dropped = true;
        return true;
    }
    const PrimitiveDescriptor& descriptor = (*primitive).descriptor();
    if (descriptor.cost.per_device_pixel == 0U) {
        report(DiagnosticCode::primitive_cost_unbounded, first.token, std::string(descriptor.name));
        selection.dropped = true;
        return true;
    }
    if (descriptor.cost.per_device_pixel > maximum_operations_per_device_pixel) {
        report(DiagnosticCode::primitive_over_budget, first.token,
               std::to_string(descriptor.cost.per_device_pixel));
        selection.dropped = true;
        return true;
    }
    for (const LoweredSelector& lowered : lowered_selectors_) {
        const std::optional<PartRole> role = lowered.selector.role;
        if (role && !paints_role(descriptor, *role)) {
            // R2-08: a primitive that cannot paint the selected role.
            report(DiagnosticCode::value_wrong_kind, first.token,
                   "the primitive does not paint the role " + std::string(part_role_name(*role)));
            return false;
        }
    }
    selection.selects = true;
    selection.primitive = primitive;
    return true;
}

bool CompilePass::lower_declaration(const SyntaxNode& node, PropertyClass kind, const PrimitiveSelection& selection) {
    const std::uint32_t property = node.tokens.first.value;
    switch (kind) {
    case PropertyClass::opacity:
    case PropertyClass::scale: return lower_opacity_or_scale(node, kind);
    case PropertyClass::translate: return lower_translate(node);
    case PropertyClass::blend:
    case PropertyClass::isolation: return lower_keyword_property(node, kind);
    case PropertyClass::material: return lower_material(node);
    case PropertyClass::font: return lower_font(node);
    case PropertyClass::effect: return lower_effect(node);
    case PropertyClass::effect_colour: return lower_effect_colour(node);
    case PropertyClass::backdrop: return lower_backdrop(node);
    case PropertyClass::transition: return lower_transition(node);
    case PropertyClass::animation: return lower_animation(node);
    case PropertyClass::later:
        report(DiagnosticCode::not_yet_built, property, property_);
        return false;
    case PropertyClass::not_admitted:
        report(DiagnosticCode::property_not_admitted, property, property_);
        return true;
    case PropertyClass::custom:
    case PropertyClass::primitive:
    case PropertyClass::other:
    default: // Not a listed PropertyClass: lowered below as an ordinary declaration.
        break;
    }
    if (selection.primitive != nullptr) {
        const PrimitiveDescriptor& descriptor = (*selection.primitive).descriptor();
        const std::string registered = registered_name_from_kebab(folded_);
        const std::optional<std::size_t> parameter =
            registered.empty() ? std::optional<std::size_t>{} : find_parameter(descriptor, registered);
        if (parameter) return lower_parameter(node, descriptor, *parameter);
    }
    // The parameters of a dropped primitive go with it.
    if (!selection.selects && selection.dropped) return true;
    if (folded_ == "color-scheme") {
        // A theme's variants are the sheet's whole (T4 theme variants).
        report(DiagnosticCode::property_not_admitted, property, "color-scheme outside :root");
        return true;
    }
    report(DiagnosticCode::unknown_property, property, property_);
    return true;
}

bool CompilePass::lower_parameter(const SyntaxNode& node, const PrimitiveDescriptor& descriptor,
                                  std::size_t parameter) {
    parameter_names_.clear();
    for (const PrimitiveParameterDeclaration& declared : descriptor.parameters) {
        parameter_names_.push_back(declared.name);
    }
    if (!read_value(node)) return false;
    const PrimitiveParameterDeclaration& declared = descriptor.parameters[parameter];
    LoweredDeclaration lowered{};
    lowered.kind = LoweredKind::parameter;
    lowered.parameter = parameter;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (wide == WideKeyword::initial) {
        if (declared.kind == PrimitiveParameterKind::number) {
            lowered.declaration = RuleDeclaration::number_value(declared.name, declared.default_number);
        } else if (declared.kind == PrimitiveParameterKind::colour) {
            lowered.declaration = RuleDeclaration::colour_value(declared.name, declared.default_colour);
        } else {
            const std::size_t choice = static_cast<std::size_t>(declared.default_number);
            lowered.declaration = RuleDeclaration::choice_value(declared.name, declared.choices[choice]);
        }
        add_lowered(std::move(lowered));
        return true;
    }
    if (drop_unknown_function()) return true;
    if (value_.components.size() != 1U) {
        report(DiagnosticCode::value_syntax, value_.components[1].token, "one value");
        return false;
    }
    if (declared.kind == PrimitiveParameterKind::number) {
        if (first.kind == ValueComponentKind::number && first.unit == ValueUnit::em) {
            // A length in em (P8c), held within the range at the part's em.
            lowered.declaration = RuleDeclaration::number_value(declared.name, 0.0);
            lowered.declaration.number_em = first.number;
            add_lowered(std::move(lowered));
            return true;
        }
        const bool length_or_number = first.unit == ValueUnit::none || first.unit == ValueUnit::px;
        if (first.kind == ValueComponentKind::number && length_or_number) {
            if (first.number < declared.minimum || first.number > declared.maximum) {
                report(DiagnosticCode::value_out_of_range, first.token, clipped_theme_detail(token_text(first.token)));
                return false;
            }
            lowered.declaration = RuleDeclaration::number_value(declared.name, first.number);
            add_lowered(std::move(lowered));
            return true;
        }
        if (first.kind == ValueComponentKind::expression) {
            const ValueProgram program = value_.programs[first.program];
            const bool program_unit = program.unit == ValueUnit::none || program.unit == ValueUnit::px;
            if (!program_unit) {
                report(DiagnosticCode::value_wrong_kind, first.token, "a number or a length");
                return false;
            }
            const std::span<const ExpressionInstruction> instructions = value_.program_instructions(program);
            for (const ExpressionInstruction instruction : instructions) {
                if (instruction.operation != ExpressionOperation::parameter) continue;
                const PrimitiveParameterKind read = descriptor.parameters[instruction.index].kind;
                if (read != PrimitiveParameterKind::number) {
                    report(DiagnosticCode::value_wrong_kind, first.token,
                           "param() of " + std::string(descriptor.parameters[instruction.index].name) +
                               ", not a number");
                    return false;
                }
            }
            lowered.declaration =
                RuleDeclaration::expression_value(declared.name, instructions, declared.default_number);
            add_lowered(std::move(lowered));
            return true;
        }
        report(DiagnosticCode::value_wrong_kind, first.token, "a number or a length");
        return false;
    }
    if (declared.kind == PrimitiveParameterKind::colour) {
        std::optional<Color> colour{};
        if (first.kind == ValueComponentKind::colour) colour = first.colour;
        if (first.kind == ValueComponentKind::keyword) colour = named_colour(first.text);
        if (!colour) {
            report(DiagnosticCode::value_wrong_kind, first.token, "a colour");
            return false;
        }
        lowered.declaration = RuleDeclaration::colour_value(declared.name, *colour);
        std::uint32_t literal = first.token;
        lowered.declaration.origin = value_colour_origin(value_, literal);
        add_lowered(std::move(lowered));
        return true;
    }
    if (!keyword_of(first, "a keyword")) return false;
    fold_ascii(first.text, folded_);
    const std::string registered = registered_name_from_kebab(folded_);
    const std::optional<std::size_t> choice =
        registered.empty() ? std::optional<std::size_t>{} : find_choice(declared, registered);
    if (!choice) {
        // R2-07: a keyword the engine lacks is an error, or a warning when
        // the sheet declares a newer engine.
        const DiagnosticCode code =
            newer_engine_ ? DiagnosticCode::keyword_from_newer_engine : DiagnosticCode::keyword_unknown;
        report(code, first.token, clipped_theme_detail(first.text));
        return newer_engine_;
    }
    lowered.declaration = RuleDeclaration::choice_value(declared.name, declared.choices[*choice]);
    add_lowered(std::move(lowered));
    return true;
}

bool CompilePass::lower_opacity_or_scale(const SyntaxNode& node, PropertyClass kind) {
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const bool scale = kind == PropertyClass::scale;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    double target = 1.0;
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (wide == WideKeyword::none) {
        if (drop_unknown_function()) return true;
        const std::size_t count = value_.components.size();
        if (scale && (count == 2U || count == 3U)) {
            // The engine scales a part uniformly.
            report(DiagnosticCode::value_form_not_admitted, value_.components[1].token, "a scale on each axis");
            return false;
        }
        if (count != 1U) {
            report(DiagnosticCode::value_syntax, value_.components[1].token, "one value");
            return false;
        }
        const bool none = scale && first.kind == ValueComponentKind::keyword && equals_ascii_folded(first.text, "none");
        if (!none && !fraction_of(first, target)) return false;
    }
    const double minimum = scale ? minimum_presentational_scale : 0.0;
    const double maximum = scale ? maximum_presentational_scale : 1.0;
    if (target < minimum || target > maximum) {
        report(DiagnosticCode::value_out_of_range, first.token, clipped_theme_detail(token_text(first.token)));
        return false;
    }
    LoweredDeclaration lowered{};
    lowered.kind = scale ? LoweredKind::scale : LoweredKind::opacity;
    lowered.transitions[0] = still_machine(scale ? scale_property : opacity_property, target);
    lowered.transition_count = 1U;
    add_lowered(std::move(lowered));
    return true;
}

bool CompilePass::lower_translate(const SyntaxNode& node) {
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    std::array<double, 2> offsets{0.0, 0.0};
    // An offset in em (P8c), taken at the part's em.
    std::array<double, 2> em_offsets{0.0, 0.0};
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    const bool none = first.kind == ValueComponentKind::keyword && equals_ascii_folded(first.text, "none");
    if (wide == WideKeyword::none && !none) {
        if (drop_unknown_function()) return true;
        const std::size_t count = value_.components.size();
        if (count == 3U) {
            report(DiagnosticCode::value_form_not_admitted, value_.components[2].token, "a translation in depth");
            return false;
        }
        if (count > 3U) {
            report(DiagnosticCode::value_syntax, value_.components[3].token, "one or two lengths");
            return false;
        }
        for (std::size_t axis = 0U; axis < count; ++axis) {
            const ValueComponent& component = value_.components[axis];
            const bool number = component.kind == ValueComponentKind::number;
            const bool em = number && component.unit == ValueUnit::em;
            const bool length = number && (component.unit == ValueUnit::px || em ||
                                            (component.unit == ValueUnit::none && component.number == 0.0));
            if (number && component.unit == ValueUnit::percent) {
                // A percentage of the part's box; the engine translates by
                // logical units.
                report(DiagnosticCode::value_form_not_admitted, component.token, "a percentage");
                return false;
            }
            if (component.kind == ValueComponentKind::expression) {
                report(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
                return false;
            }
            if (!length) {
                report(DiagnosticCode::value_wrong_kind, component.token, "a length");
                return false;
            }
            if (em) {
                em_offsets[axis] = component.number;
                continue;
            }
            if (std::abs(component.number) > maximum_presentational_translate) {
                report(DiagnosticCode::value_out_of_range, component.token,
                       clipped_theme_detail(token_text(component.token)));
                return false;
            }
            offsets[axis] = component.number;
        }
    }
    LoweredDeclaration lowered{};
    lowered.kind = LoweredKind::translate;
    lowered.transitions[0] = still_machine(translate_x_property, offsets[0], em_offsets[0]);
    lowered.transitions[1] = still_machine(translate_y_property, offsets[1], em_offsets[1]);
    lowered.transition_count = 2U;
    add_lowered(std::move(lowered));
    return true;
}

bool CompilePass::lower_keyword_property(const SyntaxNode& node, PropertyClass kind) {
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const bool blend = kind == PropertyClass::blend;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    std::string_view mode = blend_modes[0];
    if (wide == WideKeyword::none) {
        if (drop_unknown_function()) return true;
        if (value_.components.size() != 1U) {
            report(DiagnosticCode::value_syntax, value_.components[1].token, "one keyword");
            return false;
        }
        if (!keyword_of(first, "a keyword")) return false;
        fold_ascii(first.text, folded_);
        bool known = false;
        if (blend) {
            for (const std::string_view name : blend_modes) {
                if (name != folded_) continue;
                known = true;
                mode = name;
            }
            if (!known && css_blend_mode_not_admitted(folded_)) {
                report(DiagnosticCode::value_form_not_admitted, first.token, clipped_theme_detail(first.text));
                return false;
            }
        } else {
            known = folded_ == "auto" || folded_ == "isolate";
        }
        if (!known) {
            const DiagnosticCode code =
                newer_engine_ ? DiagnosticCode::keyword_from_newer_engine : DiagnosticCode::keyword_unknown;
            report(code, first.token, clipped_theme_detail(first.text));
            return newer_engine_;
        }
    }
    // `isolation` has no effect: every blend already draws in its own
    // group (CSS_COVERAGE section 7).
    if (!blend) return true;
    LoweredDeclaration lowered{};
    lowered.kind = LoweredKind::blend;
    lowered.declaration = RuleDeclaration::choice_value(blend_property, mode);
    add_lowered(std::move(lowered));
    return true;
}

bool CompilePass::lower_font(const SyntaxNode& node) {
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::outside) {
        report(DiagnosticCode::value_form_not_admitted, first.token, clipped_theme_detail(first.text));
        return false;
    }
    // `initial` is the ui role, which a part no rule sets has.
    std::string_view name = text_role_name(TextRole::ui);
    if (wide == WideKeyword::inherit) name = font_inherit_name;
    if (wide == WideKeyword::none) {
        if (drop_unknown_function()) return true;
        if (value_.components.size() != 1U) {
            report(DiagnosticCode::value_form_not_admitted, value_.components[1].token,
                   "font takes a text role alone");
            return false;
        }
        if (!keyword_of(first, "a text role")) return false;
        fold_ascii(first.text, folded_);
        const std::optional<TextRole> role = text_role_from_name(folded_);
        if (!role) {
            const DiagnosticCode code =
                newer_engine_ ? DiagnosticCode::keyword_from_newer_engine : DiagnosticCode::keyword_unknown;
            report(code, first.token, clipped_theme_detail(first.text));
            return newer_engine_;
        }
        name = text_role_name(*role);
    }
    LoweredDeclaration lowered{};
    lowered.kind = LoweredKind::font;
    lowered.declaration = RuleDeclaration::choice_value(font_property, name);
    add_lowered(std::move(lowered));
    return true;
}

bool CompilePass::lower_effect(const SyntaxNode& node) {
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::outside) {
        report(DiagnosticCode::value_form_not_admitted, first.token, clipped_theme_detail(first.text));
        return false;
    }
    // A part inherits no effect from its control.
    if (wide == WideKeyword::inherit) {
        report(DiagnosticCode::value_wrong_kind, first.token, "an effect's name or none");
        return false;
    }
    // `initial` is none: a part no rule gives an effect draws none.
    std::string name{effect_none_name};
    if (wide == WideKeyword::none) {
        if (drop_unknown_function()) return true;
        if (value_.components.size() != 1U) {
            report(DiagnosticCode::value_form_not_admitted, value_.components[1].token,
                   "effect takes one effect's name alone");
            return false;
        }
        if (!keyword_of(first, "an effect's name")) return false;
        fold_ascii(first.text, folded_);
        if (folded_ != effect_none_name) {
            // The registry is the control's: the rules check the name's
            // form only, and the control checks the effect when it
            // attaches it.
            name = registered_name_from_kebab(folded_);
            if (!effect_name_valid(name)) {
                const DiagnosticCode code =
                    newer_engine_ ? DiagnosticCode::keyword_from_newer_engine : DiagnosticCode::keyword_unknown;
                report(code, first.token, clipped_theme_detail(first.text));
                return newer_engine_;
            }
        }
    }
    LoweredDeclaration lowered{};
    lowered.kind = LoweredKind::effect;
    lowered.declaration = RuleDeclaration::choice_value(effect_property, name);
    add_lowered(std::move(lowered));
    return true;
}

bool CompilePass::lower_effect_colour(const SyntaxNode& node) {
    // folded_ holds the property's name, which classify() found.
    const bool primary = folded_ == "effect-primary";
    parameter_names_.clear();
    bare_side_ = ColourRoleSide::foreground;
    const bool read = read_value(node);
    bare_side_.reset();
    if (!read) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::outside) {
        report(DiagnosticCode::value_form_not_admitted, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (drop_unknown_function()) return true;
    if (value_.components.size() != 1U) {
        report(DiagnosticCode::value_form_not_admitted, value_.components[1].token,
               "an effect colour takes one colour role alone");
        return false;
    }
    // Only a role: an effect's colours follow the palette (EP-4).
    const bool role = first.kind == ValueComponentKind::colour && first.origin.source == ColourSource::role;
    if (wide != WideKeyword::none || !role) {
        report(DiagnosticCode::value_wrong_kind, first.token, "a colour role, role(<name>)");
        return false;
    }
    LoweredDeclaration lowered{};
    lowered.kind = primary ? LoweredKind::effect_primary : LoweredKind::effect_secondary;
    lowered.declaration =
        RuleDeclaration::colour_value(primary ? effect_primary_property : effect_secondary_property, first.colour);
    lowered.declaration.origin = first.origin;
    add_lowered(std::move(lowered));
    return true;
}

// `backdrop:` (owner decision 2026-11-05): `none` (or `initial`), or an
// ordered list of blur(<length>), saturate(<number> | <percentage>) and
// tint(<colour> [/ <alpha>]), the chain applied in order over what lies
// beneath a window or popup. A role colour in tint() keeps its role. In a
// rule that selects anything but surfaces the declaration is dropped
// (backdrop_not_on_window); a second blur(), an amount out of its range or
// more than BackdropDeclaration::maximum_steps operations refuse it.
bool CompilePass::lower_backdrop(const SyntaxNode& node) {
    const std::uint32_t property = node.tokens.first.value;
    if (!rule_selects_only_surfaces()) {
        report(DiagnosticCode::backdrop_not_on_window, property, property_);
        return true;
    }
    parameter_names_.clear();
    // role(<pair>) gives its background: a tint is laid as a fill.
    bare_side_ = ColourRoleSide::background;
    const bool read = read_value(node);
    bare_side_.reset();
    if (!read) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        report(DiagnosticCode::value_form_not_admitted, first.token, clipped_theme_detail(first.text));
        return false;
    }
    BackdropDeclaration chain{};
    const std::size_t count = value_.components.size();
    const bool written_none =
        count == 1U && first.kind == ValueComponentKind::keyword && equals_ascii_folded(first.text, "none");
    const bool none = wide == WideKeyword::initial || written_none;
    bool blurred = false;
    for (std::size_t index = 0U; index < count && !none; index = value_.components[index].subtree_end) {
        const ValueComponent& component = value_.components[index];
        std::optional<BackdropStepKind> kind{};
        if (component.kind == ValueComponentKind::function) {
            if (equals_ascii_folded(component.text, "blur")) kind = BackdropStepKind::blur;
            if (equals_ascii_folded(component.text, "saturate")) kind = BackdropStepKind::saturate;
            if (equals_ascii_folded(component.text, "tint")) kind = BackdropStepKind::tint;
        }
        if (!kind) {
            report(DiagnosticCode::value_wrong_kind, component.token, "blur(), saturate() or tint()");
            return false;
        }
        if (chain.length == BackdropDeclaration::maximum_steps) {
            report(DiagnosticCode::value_out_of_range, component.token,
                   "more than " + std::to_string(BackdropDeclaration::maximum_steps) + " backdrop operations");
            return false;
        }
        if (*kind == BackdropStepKind::blur && blurred) {
            report(DiagnosticCode::value_out_of_range, component.token, "a second blur()");
            return false;
        }
        if (*kind == BackdropStepKind::blur) blurred = true;
        if (!read_backdrop_step(index, *kind, chain.steps[chain.length])) return false;
        chain.length = chain.length + 1U;
    }
    backdrop_ = chain;
    return true;
}

bool CompilePass::read_backdrop_step(std::size_t function, BackdropStepKind kind, BackdropStep& step) {
    const ValueComponent& written = value_.components[function];
    // The function's arguments: its children, each a component or a
    // function with its own; at most three (a colour, a slash, an alpha).
    std::array<std::size_t, 3> arguments{};
    std::size_t argument_count = 0U;
    for (std::size_t index = function + 1U; index < written.subtree_end;
         index = value_.components[index].subtree_end) {
        if (argument_count == arguments.size()) {
            report(DiagnosticCode::value_syntax, value_.components[index].token, clipped_theme_detail(written.text));
            return false;
        }
        arguments[argument_count] = index;
        argument_count = argument_count + 1U;
    }
    for (std::size_t argument = 0U; argument < argument_count; ++argument) {
        const ValueComponent& component = value_.components[arguments[argument]];
        if (component.kind == ValueComponentKind::expression) {
            report(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
            return false;
        }
    }
    step.kind = kind;
    if (kind == BackdropStepKind::blur) {
        // One length: px, or a unitless 0.
        const ValueComponent* const length = argument_count == 1U ? &value_.components[arguments[0]] : nullptr;
        const bool number = length != nullptr && (*length).kind == ValueComponentKind::number;
        const bool unitless_zero = number && (*length).unit == ValueUnit::none && (*length).number == 0.0;
        const bool pixels = number && ((*length).unit == ValueUnit::px || unitless_zero);
        if (!pixels) {
            report(DiagnosticCode::value_wrong_kind, written.token, "blur() takes one length");
            return false;
        }
        const double radius = (*length).number;
        if (!(radius >= 0.0 && radius <= maximum_backdrop_blur)) {
            report(DiagnosticCode::value_out_of_range, (*length).token, "a blur() radius from 0 to " + std::to_string(static_cast<int>(maximum_backdrop_blur)) + "px");
            return false;
        }
        step.amount = radius;
        return true;
    }
    if (kind == BackdropStepKind::saturate) {
        // One factor: a number, or a percentage of 1.
        const ValueComponent* const factor = argument_count == 1U ? &value_.components[arguments[0]] : nullptr;
        const bool number = factor != nullptr && (*factor).kind == ValueComponentKind::number;
        const bool plain = number && (*factor).unit == ValueUnit::none;
        const bool percent = number && (*factor).unit == ValueUnit::percent;
        if (!plain && !percent) {
            report(DiagnosticCode::value_wrong_kind, written.token, "saturate() takes a number or a percentage");
            return false;
        }
        const double amount = percent ? (*factor).number / 100.0 : (*factor).number;
        if (!(amount >= 0.0 && amount <= maximum_backdrop_saturation)) {
            report(DiagnosticCode::value_out_of_range, (*factor).token, "a saturate() factor from 0 to " + std::to_string(static_cast<int>(maximum_backdrop_saturation)));
            return false;
        }
        step.amount = amount;
        return true;
    }
    // tint(<colour> [/ <alpha>]): the alpha, a number from 0 to 1 or a
    // percentage, scales the colour's own.
    const bool slashed =
        argument_count == 3U && value_.components[arguments[1]].kind == ValueComponentKind::slash;
    if (argument_count != 1U && !slashed) {
        report(DiagnosticCode::value_syntax, written.token, "tint() takes a colour and an optional / alpha");
        return false;
    }
    const ValueComponent& colour = value_.components[arguments[0]];
    if (colour.kind == ValueComponentKind::colour) {
        step.colour = colour.colour;
        step.origin = colour.origin;
    } else if (colour.kind == ValueComponentKind::keyword && named_colour(colour.text).has_value()) {
        step.colour = *named_colour(colour.text);
    } else {
        report(DiagnosticCode::value_wrong_kind, colour.token, "a colour");
        return false;
    }
    if (slashed) {
        const ValueComponent& alpha = value_.components[arguments[2]];
        const bool number = alpha.kind == ValueComponentKind::number;
        const bool plain = number && alpha.unit == ValueUnit::none;
        const bool percent = number && alpha.unit == ValueUnit::percent;
        if (!plain && !percent) {
            report(DiagnosticCode::value_wrong_kind, alpha.token, "an alpha, a number or a percentage");
            return false;
        }
        const double fraction = percent ? alpha.number / 100.0 : alpha.number;
        if (!(fraction >= 0.0 && fraction <= 1.0)) {
            report(DiagnosticCode::value_out_of_range, alpha.token, "a tint() alpha from 0 to 1");
            return false;
        }
        const double scaled = std::round(static_cast<double>(step.colour.alpha) * fraction);
        step.colour.alpha = static_cast<std::uint8_t>(scaled);
    }
    step.amount = 0.0;
    return true;
}

// A material property's fields (P6b), or the recipe's (P6f): its CSS-wide
// `initial` resets each field it sets; any other value is lowered by the
// material lowering. A recipe property in a rule naming a part or a role
// is dropped with a warning, since the recipe's fields are the control's.
bool CompilePass::lower_material(const SyntaxNode& node) {
    // folded_ holds the property's name, which classify() found listed.
    const MaterialProperty property = *material_property(folded_);
    const bool recipe = recipe_property(property);
    if (recipe && rule_selects_part()) {
        report(DiagnosticCode::property_not_admitted, node.tokens.first.value, property_ + " on a part or a role");
        return true;
    }
    // A font is a layout input, and a control's states change without a
    // layout: the font longhands are the control's at rest (T4 fonts).
    if (font_property_of(property) && rule_has_states()) {
        report(DiagnosticCode::property_not_admitted, node.tokens.first.value, property_ + " in a state rule");
        return true;
    }
    parameter_names_.clear();
    // role(<pair>) gives its foreground in `color`, its background in
    // `background` and `background-color`.
    if (property == MaterialProperty::color) bare_side_ = ColourRoleSide::foreground;
    const bool background = property == MaterialProperty::background || property == MaterialProperty::background_color;
    if (background) bare_side_ = ColourRoleSide::background;
    const bool read = read_value(node);
    bare_side_.reset();
    if (!read) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    // `color: inherit`, and `color: currentcolor`, which CSS reads as it
    // (P8c, D7): the parent control's text colour, resolved at paint.
    const bool current = value_.components.size() == 1U && first.kind == ValueComponentKind::keyword &&
                         equals_ascii_folded(first.text, "currentcolor");
    if (property == MaterialProperty::color && (wide == WideKeyword::inherit || current)) {
        RecipeDeclaration inherited{};
        inherited.field = RecipeField::text;
        inherited.inherit = true;
        add_recipe_field(inherited);
        return true;
    }
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (wide == WideKeyword::initial) {
        material_property_fields(property, material_fields_);
        for (const MaterialField field : material_fields_) {
            MaterialDeclaration reset{};
            reset.field = field;
            reset.initial = true;
            add_material(std::move(reset));
        }
        const RecipePropertyFields recipe_fields = recipe_property_fields(property);
        for (std::size_t index = 0U; index < recipe_fields.count; ++index) {
            RecipeDeclaration reset{};
            reset.field = recipe_fields.fields[index];
            reset.initial = true;
            add_recipe_field(reset);
        }
        return true;
    }
    // A gradient whose em lengths or current colours the rules take stays
    // authored, so the walks reach them.
    const std::optional<std::uint32_t> em_token = first_em_length(value_);
    material_lowering_.em = 0.0;
    material_lowering_.current_colour = Color::rgba(0, 0, 0, 255);
    material_lowering_.keep_gradients = em_token.has_value() || names_current_colour(value_);
    lower_material_value(property, value_, node.content.first.value, input_.images, material_lowering_);
    if (material_lowering_.outcome != MaterialLoweringOutcome::lowered) return report_material_problem();
    const bool reads_current = material_lowering_.read_current_colour;
    if ((em_token || reads_current) &&
        !lower_relative(property, node.content.first.value, em_token, reads_current)) {
        return material_lowering_.outcome == MaterialLoweringOutcome::dropped;
    }
    // Each field drawn at a colour position remembers where its colour
    // came from (EC4); a literal at a text or edge position loses, under
    // forced high contrast, the relation its author meant (W201).
    std::uint32_t literal = node.content.first.value;
    const ColourOrigin origin = value_colour_origin(value_, literal);
    bool unremappable = false;
    for (MaterialDeclaration& declaration : material_lowering_.declarations) {
        const ColourPosition position = material_field_colour_position(declaration.field);
        if (position != ColourPosition::none) declaration.origin = origin;
        if (position == ColourPosition::edge) unremappable = true;
        add_material(std::move(declaration));
    }
    for (RecipeDeclaration& declaration : material_lowering_.recipe_fields) {
        const ColourPosition position = recipe_field_colour_position(declaration.field);
        if (position != ColourPosition::none) declaration.origin = origin;
        if (position == ColourPosition::text || position == ColourPosition::edge) unremappable = true;
        add_recipe_field(declaration);
    }
    if (unremappable && origin.source == ColourSource::literal) {
        report(DiagnosticCode::literal_colour_unremappable, literal, clipped_theme_detail(token_text(literal)));
    }
    return true;
}

bool CompilePass::report_material_problem() {
    MaterialLoweringProblem& problem = material_lowering_.problem;
    // A keyword the running engine lacks, in a sheet for a newer one, is
    // dropped with a warning (R2-07).
    const bool newer = newer_engine_ && problem.code == DiagnosticCode::keyword_unknown;
    const DiagnosticCode code = newer ? DiagnosticCode::keyword_from_newer_engine : problem.code;
    report(code, problem.token, std::move(problem.detail));
    return newer || material_lowering_.outcome == MaterialLoweringOutcome::dropped;
}

bool CompilePass::lower_relative(MaterialProperty property, std::uint32_t start,
                                 std::optional<std::uint32_t> em_token, bool current) {
    em_materials_.assign(material_lowering_.declarations.begin(), material_lowering_.declarations.end());
    em_recipe_fields_.assign(material_lowering_.recipe_fields.begin(), material_lowering_.recipe_fields.end());
    if (em_token && !lower_em_pass(property, start)) {
        if (material_lowering_.outcome == MaterialLoweringOutcome::lowered) {
            report(DiagnosticCode::value_form_not_admitted, *em_token, "an em length here");
            material_lowering_.outcome = MaterialLoweringOutcome::refused;
        }
        return false;
    }
    if (current && !lower_current_colour_pass(property, start)) {
        if (material_lowering_.outcome == MaterialLoweringOutcome::lowered) {
            report(DiagnosticCode::value_form_not_admitted, start, "currentcolor here");
            material_lowering_.outcome = MaterialLoweringOutcome::refused;
        }
        return false;
    }
    material_lowering_.declarations.swap(em_materials_);
    material_lowering_.recipe_fields.swap(em_recipe_fields_);
    return true;
}

// Each length lowered at an em of 0 is its pixels; lowered again at an em
// of 1, it differs by its em coefficient, exactly, since a component is in
// one unit. A length the walk does not list (material_declaration_lengths)
// that em changes is not admitted.
bool CompilePass::lower_em_pass(MaterialProperty property, std::uint32_t start) {
    material_lowering_.em = 1.0;
    lower_material_value(property, value_, start, input_.images, material_lowering_);
    material_lowering_.em = 0.0;
    if (material_lowering_.outcome != MaterialLoweringOutcome::lowered) {
        static_cast<void>(report_material_problem());
        return false;
    }
    bool admitted = em_materials_.size() == material_lowering_.declarations.size() &&
                    em_recipe_fields_.size() == material_lowering_.recipe_fields.size();
    for (std::size_t index = 0U; admitted && index < em_materials_.size(); ++index) {
        MaterialDeclaration& pixels = em_materials_[index];
        MaterialDeclaration& at_one = material_lowering_.declarations[index];
        material_declaration_lengths(pixels, em_pixels_);
        material_declaration_lengths(at_one, em_at_one_);
        admitted = em_pixels_.size() == em_at_one_.size();
        if (!admitted) break;
        set_material_declaration_lengths(at_one, em_pixels_);
        admitted = at_one == pixels;
        if (em_coefficients(pixels.em_lengths)) continue;
        pixels.em_lengths.clear();
    }
    for (std::size_t index = 0U; admitted && index < em_recipe_fields_.size(); ++index) {
        RecipeDeclaration& pixels = em_recipe_fields_[index];
        RecipeDeclaration& at_one = material_lowering_.recipe_fields[index];
        recipe_declaration_lengths(pixels, em_pixels_);
        recipe_declaration_lengths(at_one, em_at_one_);
        admitted = em_pixels_.size() == em_at_one_.size();
        if (!admitted) break;
        set_recipe_declaration_lengths(at_one, em_pixels_);
        admitted = at_one == pixels;
        if (em_coefficients(pixels.em_lengths)) continue;
        pixels.em_lengths.clear();
    }
    return admitted;
}

// Each colour lowered at the current colour black is its value; lowered
// again at white, a colour that differs is the current colour, since a
// written colour does not depend on it. A colour the walk does not list
// (material_declaration_colours) that changes is not admitted.
bool CompilePass::lower_current_colour_pass(MaterialProperty property, std::uint32_t start) {
    material_lowering_.current_colour = Color::rgba(255, 255, 255, 255);
    lower_material_value(property, value_, start, input_.images, material_lowering_);
    material_lowering_.current_colour = Color::rgba(0, 0, 0, 255);
    if (material_lowering_.outcome != MaterialLoweringOutcome::lowered) {
        static_cast<void>(report_material_problem());
        return false;
    }
    bool admitted = em_materials_.size() == material_lowering_.declarations.size() &&
                    em_recipe_fields_.size() == material_lowering_.recipe_fields.size();
    for (std::size_t index = 0U; admitted && index < em_materials_.size(); ++index) {
        MaterialDeclaration& base = em_materials_[index];
        MaterialDeclaration& at_white = material_lowering_.declarations[index];
        material_declaration_colours(base, base_colours_);
        material_declaration_colours(at_white, other_colours_);
        admitted = base_colours_.size() == other_colours_.size() &&
                   base_colours_.size() <= std::numeric_limits<std::uint16_t>::max();
        if (!admitted) break;
        set_material_declaration_colours(at_white, base_colours_);
        at_white.em_lengths = base.em_lengths;
        admitted = at_white == base;
        base.current_colours.clear();
        for (std::size_t position = 0U; position < base_colours_.size(); ++position) {
            if (base_colours_[position] == other_colours_[position]) continue;
            base.current_colours.push_back(static_cast<std::uint16_t>(position));
        }
    }
    for (std::size_t index = 0U; admitted && index < em_recipe_fields_.size(); ++index) {
        RecipeDeclaration& base = em_recipe_fields_[index];
        RecipeDeclaration& at_white = material_lowering_.recipe_fields[index];
        base.current_colour = !(base.colour == at_white.colour);
        at_white.colour = base.colour;
        at_white.em_lengths = base.em_lengths;
        at_white.current_colour = base.current_colour;
        admitted = at_white == base;
    }
    return admitted;
}

bool CompilePass::em_coefficients(std::vector<double>& coefficients) const {
    coefficients.resize(em_pixels_.size());
    bool any = false;
    for (std::size_t index = 0U; index < em_pixels_.size(); ++index) {
        coefficients[index] = em_at_one_[index] - em_pixels_[index];
        if (coefficients[index] != 0.0) any = true;
    }
    return any;
}

// `transition` and its longhands (P7a): CSS-wide `initial` resets the
// lists the property sets; any other value is lowered into the rule's
// lists, which become curves when the rule is emitted.
bool CompilePass::lower_transition(const SyntaxNode& node) {
    // folded_ holds the property's name, which classify() found listed.
    const TransitionProperty property = *transition_property(folded_);
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (wide == WideKeyword::initial) {
        reset_transition_lists(property, motion_.written);
        motion_.written.token = node.content.first.value;
        return true;
    }
    const MotionLoweringOutcome outcome = lower_transition_value(property, value_, node.content.first.value,
                                                                 motion_.written, motion_.problem);
    if (outcome == MotionLoweringOutcome::lowered) return true;
    MotionLoweringProblem& problem = motion_.problem;
    report(problem.code, problem.token, std::move(problem.detail));
    return outcome == MotionLoweringOutcome::dropped;
}

bool CompilePass::apply_transitions(const PrimitiveSelection& selection, ThemeRule& lowered) {
    motion_.rule_machines.clear();
    const PrimitiveDescriptor* const primitive =
        selection.primitive == nullptr ? nullptr : &(*selection.primitive).descriptor();
    const MotionLoweringOutcome built =
        build_declared_transitions(motion_.written, primitive, motion_.declared, motion_.problem);
    if (built != MotionLoweringOutcome::lowered) {
        MotionLoweringProblem& problem = motion_.problem;
        report(problem.code, problem.token, std::move(problem.detail));
        return false;
    }
    // A static value's machine takes its property's declared curve and
    // becomes the transition's resting state; a declared curve with no
    // static value is a machine of its own, whose states the grouping adds.
    for (PropertyTransition& machine : lowered.transitions) {
        MachineOrigin origin{};
        origin.machine = motion_.rule_machines.size();
        for (const DeclaredTransition& declared : motion_.declared) {
            if (declared.property != machine.property) continue;
            machine.curve = declared.curve;
            origin.curved = true;
            origin.token = declared.token;
        }
        motion_.rule_machines.push_back(origin);
    }
    for (const DeclaredTransition& declared : motion_.declared) {
        bool held = false;
        for (const PropertyTransition& machine : lowered.transitions) {
            if (machine.property == declared.property) held = true;
        }
        if (held) continue;
        PropertyTransition machine{};
        machine.property = declared.property;
        machine.curve = declared.curve;
        lowered.transitions.push_back(std::move(machine));
        MachineOrigin origin{};
        origin.machine = motion_.rule_machines.size();
        origin.curved = true;
        origin.token = declared.token;
        motion_.rule_machines.push_back(origin);
    }
    // The engine runs at most maximum_part_transitions machines a part and
    // would drop the rest; only a primitive with two animated parameters
    // can reach it.
    if (lowered.transitions.size() > maximum_part_transitions) {
        report(DiagnosticCode::too_many_layers, motion_.written.token,
               std::to_string(maximum_part_transitions) + " transitions of one part");
        return false;
    }
    return true;
}

// `animation` and its longhands (P7b), as lower_transition does.
bool CompilePass::lower_animation(const SyntaxNode& node) {
    // folded_ holds the property's name, which classify() found listed.
    const AnimationProperty property = *animation_property(folded_);
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const ValueComponent& first = value_.components[0];
    const WideKeyword wide = wide_keyword();
    if (wide == WideKeyword::inherit || wide == WideKeyword::outside) {
        const DiagnosticCode code =
            wide == WideKeyword::inherit ? DiagnosticCode::value_wrong_kind : DiagnosticCode::value_form_not_admitted;
        report(code, first.token, clipped_theme_detail(first.text));
        return false;
    }
    if (wide == WideKeyword::initial) {
        reset_animation_lists(property, motion_.animations_written);
        motion_.animations_written.token = node.content.first.value;
        return true;
    }
    const MotionLoweringOutcome outcome = lower_animation_value(property, value_, node.content.first.value,
                                                                motion_.animations_written, motion_.problem);
    if (outcome == MotionLoweringOutcome::lowered) return true;
    MotionLoweringProblem& problem = motion_.problem;
    report(problem.code, problem.token, std::move(problem.detail));
    return outcome == MotionLoweringOutcome::dropped;
}

bool CompilePass::apply_animations(const PrimitiveSelection& selection, ThemeRule& lowered) {
    if (!animations_written(motion_.animations_written)) return true;
    const PrimitiveDescriptor* const primitive =
        selection.primitive == nullptr ? nullptr : &(*selection.primitive).descriptor();
    MotionLoweringProblem& problem = motion_.problem;
    const MotionLoweringOutcome built = build_declared_animations(motion_.animations_written, motion_.keyframes,
                                                                  primitive, motion_.animations, problem);
    if (built != MotionLoweringOutcome::lowered) {
        report(problem.code, problem.token, std::move(problem.detail));
        return false;
    }
    // A move takes its property's machine in the rule, or a machine of its
    // own: one state, entered from `from` through the stops to `to`, whose
    // absent end is the rule's own value or the property's rest.
    for (const DeclaredMove& move : motion_.animations.moves) {
        std::optional<std::size_t> held{};
        for (std::size_t machine = 0U; machine < lowered.transitions.size(); ++machine) {
            if (lowered.transitions[machine].property == move.property) held = machine;
        }
        if (held && motion_.rule_machines[*held].curved) {
            // Strict for now (owner, 2026-10-27): the machine's entry would
            // replay on every return to the rule's own state.
            report(DiagnosticCode::value_form_not_admitted, move.token,
                   "an animation and a transition of " + move.property + " in one rule");
            return false;
        }
        PresentationState state{};
        state.enter_from = move.from;
        state.stops = move.stops;
        if (move.to) {
            state.target = *move.to;
        } else if (held) {
            state.target = lowered.transitions[*held].states[0].target;
            state.target_em = lowered.transitions[*held].states[0].target_em;
        } else if (move.parameter) {
            const std::optional<double> own = own_number(lowered, move.property);
            if (!own) {
                report(DiagnosticCode::value_form_not_admitted, move.token,
                       move.property + ": keyframes without `to` need the rule's own value");
                return false;
            }
            state.target = *own;
            state.target_em = own_number_em(lowered, move.property);
        } else {
            state.target = engine_rest_value(move.property);
        }
        if (!held) {
            PropertyTransition made{};
            made.property = move.property;
            lowered.transitions.push_back(std::move(made));
            MachineOrigin origin{};
            origin.machine = motion_.rule_machines.size();
            motion_.rule_machines.push_back(origin);
            held = lowered.transitions.size() - 1U;
        }
        PropertyTransition& machine = lowered.transitions[*held];
        machine.curve = move.curve;
        machine.states.clear();
        machine.states.push_back(std::move(state));
        motion_.rule_machines[*held].animated = true;
        motion_.rule_machines[*held].token = move.token;
    }
    for (const DeclaredLoop& loop : motion_.animations.loops) {
        if (!apply_loop(loop, lowered)) return false;
    }
    lowered.ambient = motion_.animations.ambient;
    if (lowered.transitions.size() > maximum_part_transitions) {
        report(DiagnosticCode::too_many_layers, motion_.animations_written.token,
               std::to_string(maximum_part_transitions) + " transitions of one part");
        return false;
    }
    return true;
}

// A loop becomes the parameter's declaration: an expression of
// time_in_state through its keyframes, an absent end taking the rule's own
// value. It replaces that value, as a running animation overrides a
// declaration in CSS.
bool CompilePass::apply_loop(const DeclaredLoop& loop, ThemeRule& lowered) {
    std::vector<MotionStop>& frames = motion_.frames;
    frames = loop.frames;
    const bool starts = !frames.empty() && frames.front().offset == 0.0;
    const bool ends = !frames.empty() && frames.back().offset == 1.0;
    if (!starts || !ends) {
        const std::optional<double> own = own_number(lowered, loop.parameter);
        if (!own) {
            report(DiagnosticCode::value_form_not_admitted, loop.token,
                   loop.parameter + ": a loop's keyframes without `from` or `to` need the rule's own value");
            return false;
        }
        if (own_number_em(lowered, loop.parameter) != 0.0) {
            // A loop is an expression of time, which reads no part's em.
            report(DiagnosticCode::value_form_not_admitted, loop.token, "an em length in a looped animation");
            return false;
        }
        MotionStop edge{};
        edge.value = *own;
        if (!starts) frames.insert(frames.begin(), edge);
        edge.offset = 1.0;
        if (!ends) frames.push_back(edge);
    }
    const AmbientEffect& ambient = *motion_.animations.ambient;
    const MotionLoweringOutcome built =
        loop_program(frames, loop.timing, ambient.period, loop.token, motion_.program, motion_.problem);
    if (built != MotionLoweringOutcome::lowered) {
        MotionLoweringProblem& problem = motion_.problem;
        report(problem.code, problem.token, std::move(problem.detail));
        return false;
    }
    for (std::size_t index = 0U; index < lowered.declarations.size(); ++index) {
        if (lowered.declarations[index].property != loop.parameter) continue;
        lowered.declarations.erase(lowered.declarations.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    lowered.declarations.push_back(RuleDeclaration::expression_value(loop.parameter, motion_.program, frames[0].value));
    return true;
}

bool CompilePass::collect_keyframes(std::size_t index) {
    const SyntaxNode& rule = tree_.nodes[index];
    const std::uint32_t keyword = rule.tokens.first.value;
    rule_ = clipped_theme_detail(range_text(TokenRange{rule.tokens.first, rule.content.end}));
    property_.clear();
    // The name: one name or string, not a CSS-wide keyword or `none`.
    std::optional<std::uint32_t> named{};
    for (std::uint32_t token = rule.content.first.value; token < rule.content.end.value; ++token) {
        const TokenKind kind = tokens_.tokens[token].kind;
        if (kind == TokenKind::whitespace || kind == TokenKind::comment) continue;
        if (named || (kind != TokenKind::ident && kind != TokenKind::string)) {
            report(DiagnosticCode::value_syntax, token, "one keyframes name");
            return false;
        }
        named = token;
    }
    if (!named) {
        report(DiagnosticCode::value_syntax, keyword, "a keyframes name");
        return false;
    }
    KeyframesDefinition definition{};
    definition.token = *named;
    const std::string_view text = token_text(*named);
    if (tokens_.tokens[*named].kind == TokenKind::string) {
        definition.name = std::string(text.substr(1U, text.size() >= 2U ? text.size() - 2U : 0U));
    } else {
        fold_ascii(text, folded_);
        const bool reserved = folded_ == "none" || folded_ == "initial" || folded_ == "inherit" ||
                              folded_ == "unset" || folded_ == "default" || folded_ == "revert" ||
                              folded_ == "revert-layer";
        if (reserved) {
            report(DiagnosticCode::value_syntax, *named, clipped_theme_detail(text));
            return false;
        }
        definition.name = std::string(text);
    }
    if (definition.name.empty()) {
        report(DiagnosticCode::value_syntax, *named, "a keyframes name");
        return false;
    }
    std::vector<double> offsets{};
    const std::size_t end = rule.subtree_end.value;
    for (std::size_t block = index + 1U; block < end; block = tree_.nodes[block].subtree_end.value) {
        const SyntaxNode& keyframe = tree_.nodes[block];
        if (!keyframe_offsets(keyframe, offsets)) return false;
        const std::size_t block_end = keyframe.subtree_end.value;
        for (std::size_t child = block + 1U; child < block_end; child = tree_.nodes[child].subtree_end.value) {
            if (!keyframe_declaration(tree_.nodes[child], offsets, definition)) return false;
        }
    }
    property_.clear();
    // A later definition of a name replaces an earlier one, as in CSS.
    for (std::size_t kept = 0U; kept < motion_.keyframes.size(); ++kept) {
        if (motion_.keyframes[kept].name != definition.name) continue;
        motion_.keyframes.erase(motion_.keyframes.begin() + static_cast<std::ptrdiff_t>(kept));
        break;
    }
    motion_.keyframes.push_back(std::move(definition));
    return true;
}

// `from`, `to` and percentages from 0% to 100%, separated by commas.
bool CompilePass::keyframe_offsets(const SyntaxNode& block, std::vector<double>& offsets) {
    offsets.clear();
    bool expecting = true;
    std::uint32_t last = block.content.first.value;
    for (std::uint32_t token = block.content.first.value; token < block.content.end.value; ++token) {
        const Token& read = tokens_.tokens[token];
        if (read.kind == TokenKind::whitespace || read.kind == TokenKind::comment) continue;
        last = token;
        if (!expecting) {
            if (read.kind != TokenKind::comma) {
                report(DiagnosticCode::selector_syntax, token, clipped_theme_detail(token_text(token)));
                return false;
            }
            expecting = true;
            continue;
        }
        double offset = 0.0;
        if (read.kind == TokenKind::ident) {
            fold_ascii(token_text(token), folded_);
            if (folded_ != "from" && folded_ != "to") {
                report(DiagnosticCode::selector_syntax, token, clipped_theme_detail(token_text(token)));
                return false;
            }
            offset = folded_ == "to" ? 1.0 : 0.0;
        } else if (read.kind == TokenKind::percentage) {
            const double percent = tokens_.numbers[read.number].value;
            if (!(percent >= 0.0 && percent <= 100.0)) {
                report(DiagnosticCode::value_out_of_range, token, clipped_theme_detail(token_text(token)));
                return false;
            }
            offset = percent / 100.0;
        } else {
            report(DiagnosticCode::selector_syntax, token, clipped_theme_detail(token_text(token)));
            return false;
        }
        offsets.push_back(offset);
        expecting = false;
    }
    if (expecting) {
        report(DiagnosticCode::selector_syntax, last, "a keyframe offset");
        return false;
    }
    return true;
}

// A keyframe's declaration: opacity, scale and translate as their static
// values are read, or a parameter's number. Other properties do not move.
bool CompilePass::keyframe_declaration(const SyntaxNode& node, std::span<const double> offsets,
                                       KeyframesDefinition& definition) {
    const std::uint32_t property = node.tokens.first.value;
    property_ = clipped_theme_detail(token_text(property));
    fold_ascii(token_text(property), folded_);
    const PropertyClass kind = classify(folded_);
    const std::uint32_t at = node.content.first.value < node.content.end.value ? node.content.first.value : property;
    if (kind == PropertyClass::opacity || kind == PropertyClass::scale || kind == PropertyClass::translate) {
        declarations_.clear();
        const bool read = kind == PropertyClass::translate ? lower_translate(node) : lower_opacity_or_scale(node, kind);
        if (!read) return false;
        // A dropped value (an unknown function, reported) adds nothing.
        if (declarations_.empty()) return true;
        const LoweredDeclaration& lowered = declarations_.back();
        for (std::size_t machine = 0U; machine < lowered.transition_count; ++machine) {
            const PropertyTransition& still = lowered.transitions[machine];
            if (still.states[0].target_em != 0.0) {
                // A keyframe's value is a number of the move or the loop,
                // which reads no part's em.
                report(DiagnosticCode::value_form_not_admitted, at, "an em length in @keyframes");
                return false;
            }
            for (const double offset : offsets) {
                add_keyframe(definition, still.property, false, offset, still.states[0].target, at);
            }
        }
        declarations_.clear();
        return true;
    }
    if (kind == PropertyClass::not_admitted) {
        report(DiagnosticCode::property_not_admitted, property, property_);
        return true;
    }
    if (kind == PropertyClass::later) {
        report(DiagnosticCode::not_yet_built, property, property_);
        return false;
    }
    if (kind != PropertyClass::other) {
        report(DiagnosticCode::value_form_not_admitted, property,
               property_ + ": keyframes move opacity, scale, translate and primitive parameters");
        return false;
    }
    // A parameter, checked against the primitive of the rule that plays
    // the keyframes.
    const std::string registered = registered_name_from_kebab(folded_);
    if (registered.empty()) {
        report(DiagnosticCode::unknown_property, property, property_);
        return true;
    }
    parameter_names_.clear();
    if (!read_value(node)) return false;
    const ValueComponent& first = value_.components[0];
    if (value_.components.size() != 1U) {
        report(DiagnosticCode::value_syntax, value_.components[1].token, "one value");
        return false;
    }
    if (first.kind == ValueComponentKind::expression) {
        report(DiagnosticCode::value_form_not_admitted, first.token, "an expression that reads inputs");
        return false;
    }
    if (first.kind == ValueComponentKind::number && first.unit == ValueUnit::em) {
        report(DiagnosticCode::value_form_not_admitted, first.token, "an em length in @keyframes");
        return false;
    }
    const bool number = first.kind == ValueComponentKind::number &&
                        (first.unit == ValueUnit::none || first.unit == ValueUnit::px);
    if (!number) {
        report(DiagnosticCode::value_wrong_kind, first.token, "a number or a length");
        return false;
    }
    for (const double offset : offsets) add_keyframe(definition, registered, true, offset, first.number, first.token);
    return true;
}

void CompilePass::add_recipe_field(const RecipeDeclaration& declaration) {
    // As for the material fields, the later declaration of a field wins.
    for (std::size_t index = 0U; index < recipe_fields_.size(); ++index) {
        if (recipe_fields_[index].field != declaration.field) continue;
        recipe_fields_.erase(recipe_fields_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    recipe_fields_.push_back(declaration);
}

bool CompilePass::rule_has_states() const noexcept {
    for (const LoweredSelector& lowered : lowered_selectors_) {
        if (!lowered.selector.states.empty()) return true;
    }
    return false;
}

bool CompilePass::rule_selects_surface() const noexcept {
    for (const LoweredSelector& lowered : lowered_selectors_) {
        if (lowered.selector.surface != SurfaceKind::none) return true;
    }
    return false;
}

bool CompilePass::rule_selects_only_surfaces() const noexcept {
    for (const LoweredSelector& lowered : lowered_selectors_) {
        if (lowered.selector.surface == SurfaceKind::none) return false;
    }
    return !lowered_selectors_.empty();
}

bool CompilePass::rule_selects_part() const noexcept {
    for (const LoweredSelector& lowered : lowered_selectors_) {
        if (!lowered.selector.part.empty() || lowered.selector.role.has_value()) return true;
    }
    return false;
}

void CompilePass::add_material(MaterialDeclaration declaration) {
    // As in CSS, the later declaration of a longhand wins, whichever
    // property (a shorthand or the longhand) set it.
    for (std::size_t index = 0U; index < materials_.size(); ++index) {
        if (materials_[index].field != declaration.field) continue;
        materials_.erase(materials_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    materials_.push_back(std::move(declaration));
}

bool CompilePass::emit_rule(const EmittedRule& emitted, std::uint32_t at) {
    const PrimitiveSelection& selection = emitted.selection;
    ThemeRule lowered{};
    lowered.label = rule_;
    lowered.media = media_;
    if (selection.selects) {
        const std::string_view name =
            selection.primitive == nullptr ? built_in_primitive_name : (*selection.primitive).descriptor().name;
        lowered.declarations.push_back(RuleDeclaration::primitive(name));
    } else if (emitted.implicit_material) {
        lowered.declarations.push_back(RuleDeclaration::primitive(material_primitive_name));
    }
    const std::size_t selections = lowered.declarations.size();
    for (LoweredDeclaration& declaration : declarations_) {
        const bool declared = declaration.kind == LoweredKind::parameter || declaration.kind == LoweredKind::blend ||
                              declaration.kind == LoweredKind::font || declaration.kind == LoweredKind::effect ||
                              declaration.kind == LoweredKind::effect_primary ||
                              declaration.kind == LoweredKind::effect_secondary;
        if (declared) {
            lowered.declarations.push_back(std::move(declaration.declaration));
            continue;
        }
        for (std::size_t index = 0U; index < declaration.transition_count; ++index) {
            lowered.transitions.push_back(std::move(declaration.transitions[index]));
        }
    }
    if (!apply_transitions(selection, lowered)) return false;
    if (!apply_animations(selection, lowered)) return false;
    lowered.materials = materials_;
    lowered.recipe_fields = recipe_fields_;
    lowered.backdrop = backdrop_;
    lowered.scoped_conditions = variations_.copy_conditions;
    // An inline style's own rules (P12b: alone, or lowered after a theme)
    // are not the theme's readers.
    const bool style_rule = input_.form == ThemeSheetForm::inline_style || in_inline_segment(source_position_);
    if (!style_rule) lowered.reads = variations_.read_names;
    lowered.source_position = positions_ && !style_rule ? source_position_ : 0U;
    // A borrowed selection alone declares nothing of the rule's own.
    const std::size_t owned = emitted.borrowed ? selections : 0U;
    if (lowered.declarations.size() == owned && lowered.transitions.empty() && lowered.materials.empty() &&
        lowered.recipe_fields.empty() && !lowered.ambient && !lowered.backdrop) {
        return true;
    }
    std::size_t bytes = lowered_record_bytes + lowered.label.size();
    for (const RuleDeclaration& declaration : lowered.declarations) bytes += declaration_bytes(declaration);
    for (const MaterialDeclaration& declaration : lowered.materials) bytes += material_bytes(declaration);
    for (const PropertyTransition& transition : lowered.transitions) bytes += transition_bytes(transition);
    bytes += lowered.recipe_fields.size() * lowered_record_bytes;
    if (lowered.backdrop) bytes += lowered_record_bytes;
    for (const ScopedCondition& condition : lowered.scoped_conditions) {
        bytes += lowered_record_bytes + condition.name.size();
    }
    for (const LoweredSelector& selector : lowered_selectors_) {
        if (bytes > budget_.bytes) {
            report(DiagnosticCode::lowered_sheet_too_large, at, std::to_string(maximum_lowered_bytes));
            return false;
        }
        budget_.bytes -= bytes;
        work_.rules_emitted += 1U;
        work_.emitted_bytes += bytes;
        sheet_.rules.push_back(lowered);
        ThemeRule& rule = sheet_.rules.back();
        rule.selector = selector.selector;
        rule.level = emitted.important ? CascadeLevel::important_theme_sheet : selector.level;
        if (style_rule) {
            rule.level = emitted.important ? CascadeLevel::important_inline_style : CascadeLevel::inline_style;
        }
        if (input_.user_theme) {
            rule.level = emitted.important ? CascadeLevel::important_user_theme : CascadeLevel::user_theme;
        }
        // A given specificity only where CSS's differs from the engine's
        // own count of the selector (EC2: an `:is()` argument's).
        if (!(selector.specificity == rule_specificity(selector.selector))) rule.specificity = selector.specificity;
        for (const MachineOrigin& machine : motion_.rule_machines) {
            MachineOrigin origin = machine;
            origin.rule = sheet_.rules.size() - 1U;
            motion_.origins.push_back(origin);
        }
    }
    return true;
}

bool CompilePass::read_value(const SyntaxNode& node) {
    const std::size_t first_record = diagnostics_.records().size();
    ValueContext context{};
    context.parameters = parameter_names_;
    context.published_values = input_.published_values.size();
    context.resources = input_.resources;
    context.palette = reading_palette_ ? nullptr : &palette_.in_use;
    context.bare_side = bare_side_;
    context.system_colours = &input_.system_colours;
    const SubstitutionInput substitution{.source = input_.source,
                                         .tokens = tokens_,
                                         .table = custom_properties_,
                                         .reserved = reserved_,
                                         .cursor = cursor_,
                                         .choices = variations_.choices,
                                         .theme_names = input_.theme_names};
    const SubstitutionOutcome substituted =
        substitution_.substitute(substitution, node.content, substituted_, diagnostics_);
    work_.substituted_tokens += substitution_.visited();
    work_.value_tokens += substituted_.size();
    if (substituted == SubstitutionOutcome::undefined && undefined_drops_) undefined_read_ = true;
    if (substituted == SubstitutionOutcome::undefined && !undefined_drops_) {
        // Read where nothing can be left undeclared (the palette, a
        // keyframe): a name defined only where nothing holds.
        report(DiagnosticCode::variable_undefined, node.content.first.value, "no definition holds here");
    }
    bool read = substituted == SubstitutionOutcome::substituted;
    if (read) {
        const ValueInput value_input{.source = input_.source,
                                     .tokens = tokens_,
                                     .written = node.content,
                                     .substituted = substituted_,
                                     .context = context,
                                     .cursor = cursor_};
        read = reader_.read(value_input, value_, diagnostics_) == ValueOutcome::read;
        rule_roles_read_ = rule_roles_read_ | value_.roles_read;
    }
    diagnostics_.name_context(first_record, rule_, property_);
    return read;
}

WideKeyword CompilePass::wide_keyword() const noexcept {
    if (value_.components.size() != 1U) return WideKeyword::none;
    const ValueComponent& only = value_.components[0];
    if (only.kind != ValueComponentKind::keyword) return WideKeyword::none;
    if (equals_ascii_folded(only.text, "initial")) return WideKeyword::initial;
    if (equals_ascii_folded(only.text, "inherit")) return WideKeyword::inherit;
    const bool outside = equals_ascii_folded(only.text, "unset") || equals_ascii_folded(only.text, "revert") ||
                         equals_ascii_folded(only.text, "revert-layer");
    return outside ? WideKeyword::outside : WideKeyword::none;
}

bool CompilePass::drop_unknown_function() {
    for (const ValueComponent& component : value_.components) {
        if (component.kind != ValueComponentKind::function) continue;
        report(DiagnosticCode::unknown_function, component.token, clipped_theme_detail(component.text));
        return true;
    }
    return false;
}

bool CompilePass::fraction_of(const ValueComponent& component, double& fraction) {
    if (component.kind == ValueComponentKind::expression) {
        report(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
        return false;
    }
    const bool number = component.kind == ValueComponentKind::number;
    if (number && component.unit == ValueUnit::none) {
        fraction = component.number;
        return true;
    }
    if (number && component.unit == ValueUnit::percent) {
        fraction = component.number / 100.0;
        return true;
    }
    report(DiagnosticCode::value_wrong_kind, component.token, "a number or a percentage");
    return false;
}

bool CompilePass::keyword_of(const ValueComponent& component, std::string_view expected) {
    if (component.kind == ValueComponentKind::keyword) return true;
    report(DiagnosticCode::value_wrong_kind, component.token, std::string(expected));
    return false;
}

void CompilePass::add_lowered(LoweredDeclaration lowered) {
    // As in CSS, the later of two declarations of one property wins.
    for (std::size_t index = 0U; index < declarations_.size(); ++index) {
        const LoweredDeclaration& earlier = declarations_[index];
        const bool same = earlier.kind == lowered.kind &&
                          (lowered.kind != LoweredKind::parameter || earlier.parameter == lowered.parameter);
        if (!same) continue;
        declarations_.erase(declarations_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    declarations_.push_back(std::move(lowered));
}

std::string_view CompilePass::token_text(std::uint32_t index) const noexcept {
    const Token& token = tokens_.tokens[index];
    const std::string_view text = input_.source.text.substr(token.offset.value, token.length.value);
    return text;
}

std::string_view CompilePass::range_text(TokenRange range) const noexcept {
    if (range.first.value >= range.end.value) return {};
    const Token& first = tokens_.tokens[range.first.value];
    const Token& last = tokens_.tokens[range.end.value - 1U];
    const std::size_t end = static_cast<std::size_t>(last.offset.value) + last.length.value;
    const std::string_view text = input_.source.text.substr(first.offset.value, end - first.offset.value);
    return text;
}

void CompilePass::report(DiagnosticCode code, std::uint32_t token, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(input_.source.file);
    const ByteOffset offset = tokens_.tokens.empty() ? ByteOffset{} : tokens_.tokens[token].offset;
    diagnostic.position = cursor_.locate(offset);
    diagnostic.rule = rule_;
    diagnostic.property = property_;
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
}

} // namespace

CompileOutcome ThemeCompiler::compile(const ThemeCompileInput& input, ThemeRuleSheet& sheet,
                                      ThemeDiagnosticList& diagnostics) {
    sheet.rules.clear();
    sheet.published_values.clear();
    sheet.requires_engine = current_engine_version;
    sheet.colour_schemes.reset();
    CompilePass pass(input, sheet, diagnostics, tokens_, lines_, tree_, custom_properties_, selectors_, substitution_,
                     reader_, lowered_selectors_, substituted_, value_, parameter_names_, declarations_, materials_,
                     material_lowering_, material_fields_, recipe_fields_, motion_, media_queries_, palette_,
                     variations_, folded_, work_);
    const CompileOutcome outcome = pass.run();
    if (outcome == CompileOutcome::refused) {
        sheet.rules.clear();
        sheet.published_values.clear();
        sheet.requires_engine = current_engine_version;
        sheet.colour_schemes.reset();
    }
    return outcome;
}

void ThemeCompiler::append_defined_names(std::vector<std::string>& names) const {
    const std::span<const CustomPropertyDefinition> definitions = custom_properties_.definitions();
    for (std::size_t index = 0U; index < definitions.size(); ++index) {
        const bool first_of_name = index == 0U || definitions[index].name_index != definitions[index - 1U].name_index;
        if (first_of_name) names.push_back(std::string(definitions[index].name));
    }
}

void ThemeCompiler::append_inline_as_sheet(std::string_view text, std::string& output,
                                           std::vector<InlineSheetPiece>& pieces) const {
    const std::vector<SyntaxNode>& nodes = tree_.nodes;
    const std::vector<Token>& tokens = tokens_.tokens;
    for (std::size_t index = 1U; index < nodes.size(); index = nodes[index].subtree_end.value) {
        const SyntaxNode& rule = nodes[index];
        if (rule.kind != SyntaxNodeKind::style_rule) continue;
        output += "*";
        if (rule.content.first.value < rule.content.end.value) {
            const Token& first = tokens[rule.content.first.value];
            const Token& last = tokens[rule.content.end.value - 1U];
            const std::size_t start = first.offset.value;
            const std::size_t end = last.offset.value + last.length.value;
            pieces.push_back(InlineSheetPiece{.written = output.size(), .style = start, .length = end - start});
            output += text.substr(start, end - start);
        }
        output += " {\n";
        const std::size_t rule_end = rule.subtree_end.value;
        for (std::size_t child = index + 1U; child < rule_end; child = nodes[child].subtree_end.value) {
            const SyntaxNode& declaration = nodes[child];
            if (declaration.tokens.first.value >= declaration.tokens.end.value) continue;
            const Token& first = tokens[declaration.tokens.first.value];
            const Token& last = tokens[declaration.tokens.end.value - 1U];
            const std::size_t start = first.offset.value;
            const std::size_t end = last.offset.value + last.length.value;
            output += "  ";
            pieces.push_back(InlineSheetPiece{.written = output.size(), .style = start, .length = end - start});
            output += text.substr(start, end - start);
            // A declaration written without its `;` (the last of a block)
            // is given one; an empty one after a written `;` makes no node.
            output += ";\n";
        }
        output += "}\n";
    }
}

} // namespace gui_forms::theme_language
