#pragma once

// The theme compiler (stage T3, PLAN.md P5): a sheet's text in, the engine's
// rule records out. It runs the stages in order (tokenize, parse, collect
// the custom properties, then per style rule lower its selectors and its
// declarations) and lowers each declaration onto today's engine: the
// `primitive` a rule selects, that primitive's parameters by name, the
// presentational properties as one-state machines, `mix-blend-mode`, the
// material and recipe properties, transitions and animations. An `@media`
// block's rules are lowered once for each query, carrying its condition.
// The parser and the records between the stages stay internal (E3); the
// compiler is what the package loader (P9) and the tools call.

#include "../custom_properties/theme_custom_properties.hpp"
#include "../diagnostics/theme_diagnostics.hpp"
#include "../material/theme_material_lowering.hpp"
#include "../media/theme_media_query.hpp"
#include "../motion/theme_motion_lowering.hpp"
#include "../selector/theme_selector.hpp"
#include "../source_text/theme_source_text.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "../tokenizer/theme_tokenizer.hpp"
#include "../value/theme_value.hpp"

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// What one compilation reads besides the sheet, all borrowed for the call:
// - `source`: the sheet's name and text;
// - `registry`: the primitives the rules may select;
// - `vocabulary`: the control types, parts and states selectors may name;
// - `published_values`: the names the theme's manifest publishes (R2-06),
//   which `var()` reads and which become the sheet's published values;
// - `resources`: the resource names the manifest lists, for `url()`;
// - `images`: those that are images, bound to the engine's images (P10's
//   loader binds them; until then the caller does);
// - `requires_engine`: the engine version the manifest declares (P9
//   supplies it; until then the caller does);
// - `form`: a theme sheet, or a control's inline style (P9, R2-10), whose
//   rules are at the inline levels with the control's selector implied
//   and whose blocks may name only the control's own parts and states (the
//   vocabulary is the control's anatomy alone). An inline style has no
//   palette, custom properties (E999 until P12), at-rules or resources.
struct ThemeCompileInput final {
    const ThemeSource& source;
    const PrimitiveRegistry& registry;
    SelectorVocabulary vocabulary{};
    std::span<const std::string> published_values{};
    std::span<const std::string_view> resources{};
    std::span<const ThemeImageResource> images{};
    EngineVersion requires_engine{current_engine_version};
    ThemeSheetForm form{ThemeSheetForm::sheet};
};

enum class CompileOutcome : std::uint8_t {
    // The sheet holds the lowered rules; warnings, if any, are diagnostics.
    compiled,
    // An error refused the theme (B2): the compilation stopped at it, so
    // it is the last diagnostic (the tokenizer reports every lexical one
    // before stopping), and the sheet is empty.
    refused,
};

// One kind of lowered declaration, for the cascade's last-wins rule within
// a rule: a later declaration of the same kind (and parameter) replaces an
// earlier one.
enum class LoweredKind : std::uint8_t { parameter, opacity, scale, translate, blend, font };

// A rule's declaration once lowered, kept until the rule's last
// declaration is read: the engine records it becomes, at most two
// (`translate` becomes one machine for each axis).
struct LoweredDeclaration final {
    LoweredKind kind{LoweredKind::parameter};
    // The parameter's position in the primitive's declarations (parameters
    // only).
    std::size_t parameter{};
    RuleDeclaration declaration{};
    std::array<PropertyTransition, 2> transitions{};
    std::size_t transition_count{};
};

// The motion storage (P7): a rule's written transition and animation
// lists and what they declare, the sheet's keyframes, and the origin of
// every machine of the sheet, for the grouping once every rule is lowered.
struct MotionStorage final {
    WrittenTransitions written{};
    std::vector<DeclaredTransition> declared{};
    WrittenAnimations animations_written{};
    DeclaredAnimations animations{};
    // The sheet's `@keyframes`, a name defined once (a later definition
    // replaces an earlier one).
    std::vector<KeyframesDefinition> keyframes{};
    // A loop's expression, being built.
    std::vector<ExpressionInstruction> program{};
    std::vector<MotionStop> frames{};
    std::vector<MachineOrigin> origins{};
    // The machines of the rule being emitted, `rule` unset.
    std::vector<MachineOrigin> rule_machines{};
    MotionLoweringProblem problem{};
};

// Compiles theme sheets. It keeps every stage's storage from sheet to
// sheet; one instance serves one compilation at a time.
// One `:root` declaration of a palette role (P8b): the role's colours where
// `media` holds.
struct PaletteVariant final {
    MediaCondition media{};
    RoleColours colours{};
};

// The theme's palette (P8b, R2-03) and what lowering its readers takes,
// reused from one compilation to the next.
struct PaletteStorage final {
    // Each role's declarations in sheet order, from its last unconditional
    // one, which an unconditional declaration replaces: the first, when it
    // is unconditional, is the role's base colours; each other holds where
    // its condition holds.
    std::array<std::vector<PaletteVariant>, colour_role_count> variants{};
    // Each role's base colours, where it has them.
    RolePalette base{};
    // The palette role() reads: the base, or a copy's variants.
    RolePalette in_use{};
    // The roles a rule reads that have conditional variants, and the
    // variant of each that the copy being lowered reads (0 for the base).
    std::vector<std::size_t> varying{};
    std::vector<std::size_t> choice{};
};

// How a custom property's value varies from part to part and window to
// window (P12, PLAN.md section 1.11; owner decision of 2026-10-29).
enum class Variation : std::uint8_t {
    // Defined in one place, or in the palette alone with one definition
    // holding everywhere: substituted where it is read, as one value.
    fixed,
    // Defined in the palette alone, under media conditions: a rule that
    // reads it is lowered once per combination of the conditions.
    media,
    // Defined in more than one place, some in a rule's scope: a rule that
    // reads it is lowered once per definition that may win for its parts
    // (and once for none, where none may), each copy conditioned on the
    // part's winner.
    scoped,
};

// One palette definition of a media name: its position among the name's
// definitions, holding where `media` holds.
struct MediaVariant final {
    std::uint32_t ordinal{};
    MediaCondition media{};
};

// What one selector of a rule can match, for the pruning of a scoped
// name's candidates: its control type and stable ID, each empty for any.
// Two selectors whose types or IDs are both given and differ match no part
// in common.
struct SelectorScope final {
    std::string control_type{};
    std::string stable_id{};
};

// A definition of a scoped name that may win for a part, or none
// (ordinal variable_undefined), with what its winning needs: copies whose
// candidates cannot win together are not made.
struct VariableCandidate final {
    std::uint32_t ordinal{variable_undefined};
    // The media condition it holds under, where it has exactly one.
    std::optional<MediaCondition> needs{};
    // The conditions under which a later palette definition replaces it.
    std::vector<MediaCondition> replaced_under{};
    // A scoped definition's rule node, which matches wherever it wins; 0
    // for a palette definition or none.
    std::uint32_t rule{};
    // A palette definition or none: no scoped definition of the name
    // matches where it wins.
    bool unscoped{};
    // The names its value reads, by name_index; a reader's copy decides
    // them only where this candidate is chosen.
    std::vector<std::uint32_t> reads{};
    // A scoped definition's rule's selectors: a reader none of whose
    // selectors can match a part in common with one of them never sees
    // it win. And the control types its rule selects whole (a selector of
    // a type alone, outside `@media`): a reader of only such a type always
    // matches it, so no palette definition, nor none, wins there.
    std::vector<SelectorScope> scopes{};
    std::vector<std::string> covers{};
};

struct NameVariation final {
    Variation kind{Variation::fixed};
    // fixed: the substitution's choice.
    std::uint32_t fixed{};
    // media: the palette definitions in cascade order (ordinary, then
    // `!important`, each in sheet order), from the last unconditional
    // one, which an unconditional definition replaces.
    std::vector<MediaVariant> variants{};
    // scoped: the definitions that may win, then none where no palette
    // definition holds everywhere; and the rule nodes of its scoped
    // definitions.
    std::vector<VariableCandidate> candidates{};
    std::vector<std::uint32_t> scoped_rules{};
    // The names its definitions read, by name_index.
    std::vector<std::uint32_t> reads{};
    // Whether a style rule's declaration reads it, through any number of
    // definitions.
    bool read{};
};

// The theme's custom property variations (P12) and what lowering their
// readers takes, reused from one compilation to the next.
struct VariationStorage final {
    // By name_index.
    std::vector<NameVariation> names{};
    // The substitution's choice of each name's definition: a fixed name's
    // own, the copy's for a name the rule reads, else variable_unassigned.
    std::vector<std::uint32_t> choices{};
    // A walk through the names a rule reads.
    std::vector<std::uint32_t> pending{};
    std::vector<std::uint32_t> visited{};
    std::vector<std::uint8_t> seen{};
    // The names the rule being lowered reads directly, deduplicated; the
    // media names it reads through any definitions and their distinct
    // conditions; and whether it may read a scoped name.
    std::vector<std::uint32_t> direct{};
    std::vector<std::uint32_t> media_read{};
    std::vector<MediaCondition> conditions{};
    bool reads_scoped{};
    // The copy being lowered: the scoped names decided so far, in the
    // order decided (each read by the rule or by a candidate chosen
    // above it), each one's chosen candidate, and every name a copy of
    // the rule assigned, to unassign after.
    std::vector<std::uint32_t> scoped_read{};
    std::vector<std::size_t> scoped_choice{};
    std::vector<std::uint32_t> assigned{};
    // The world being lowered, one bit per condition taken as holding.
    std::size_t world{};
    // The rule's selectors' scopes, and a selector lowering's scratch.
    std::vector<SelectorScope> reader_scopes{};
    std::vector<LoweredSelector> scope_selectors{};
    // The copy's conditions, for its rules.
    std::vector<ScopedCondition> copy_conditions{};
    // A rule's scoped definitions, ordinary and `!important`.
    std::vector<ScopedDefinition> ordinary{};
    std::vector<ScopedDefinition> important{};
};

class ThemeCompiler final {
public:
    // Replaces `sheet` with the lowered rules of `input.source` and appends
    // the diagnostics to `diagnostics`, in the order found. Each selector a
    // style rule expands into gives one rule, in written order, with the
    // rule's declarations; a rule left with nothing to declare gives none.
    // The first error stops the compilation (`refused`, `sheet` empty).
    // The sheet requires the declared engine version, or the running one
    // when the declared one is newer: what the running engine lacks has
    // then been dropped with a warning (R2-07), so the rest applies.
    [[nodiscard]] CompileOutcome compile(const ThemeCompileInput& input, ThemeRuleSheet& sheet,
                                         ThemeDiagnosticList& diagnostics);

private:
    ThemeTokenBuffer tokens_{};
    LineStartTable lines_{};
    ThemeSyntaxTree tree_{};
    CustomPropertyTable custom_properties_{};
    SelectorLowering selectors_{};
    VariableSubstitution substitution_{};
    ValueReader reader_{};
    std::vector<LoweredSelector> lowered_selectors_{};
    std::vector<ValueToken> substituted_{};
    ThemeValue value_{};
    std::vector<std::string_view> parameter_names_{};
    std::vector<LoweredDeclaration> declarations_{};
    std::vector<MaterialDeclaration> materials_{};
    MaterialLowering material_lowering_{};
    std::vector<MaterialField> material_fields_{};
    std::vector<RecipeDeclaration> recipe_fields_{};
    MotionStorage motion_{};
    std::vector<MediaQuery> media_queries_{};
    PaletteStorage palette_{};
    VariationStorage variations_{};
    std::string folded_{};
};

} // namespace gui_forms::theme_language
