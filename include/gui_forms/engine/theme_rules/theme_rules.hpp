#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/authored_gradient/authored_gradient.hpp"
#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The cascade (T2 plan, phase E4): which primitive draws a part, and with
// which parameters. Theme data is rules; each rule has a selector, a cascade
// level and declarations. T3 fills the rules by parsing theme documents and
// FD fills the inline styles; until then they are C++ data (tests).
//
// The levels, lowest first. A declaration at a higher level wins over every
// declaration at a lower one; within a level, the more specific selector
// wins, and among equals the later rule. `built_in` and
// `forced_high_contrast` are not authored: the built-in recipe is what a
// part falls back to when no rule selects a primitive, and forced high
// contrast is the engine's own pass over the result.
//
// The important levels (T3 engine change EC6) hold a sheet's `!important`
// declarations: a theme's above every inline style's ordinary ones, and an
// inline style's above all. So an important declaration wins whichever side
// it is on, and where neither or both are important the inline style wins
// (the owner's rule E1). Forced high contrast sits between them: under it,
// only the `important_inline_style` declarations that are not colours
// stand, and every colour is the high-contrast pass's.
//
// The user levels (T3 user themes, notes:planning/stages/T3-theme-language/
// USER_THEMES.md section 4) hold a user theme layered under the program's:
// its ordinary declarations below every program declaration, its
// `!important` ones above the program's ordinary declarations and below
// its `!important` ones. A user theme's rules are a set of their own
// (CompiledThemeRules::compile_user, PartFacts::user_rules).
//
// The base levels (selector step 1, owner ruling of 2026-11-08) hold the
// GUI.Forms base sheet when a program's packages are compiled onto it, as
// CSS's user-agent origin: its ordinary declarations below every user and
// program declaration whatever their specificity, and its `!important`
// ones below the user theme's and the program's.
//
// `class_rule` and `stable_id_rule` are for rules written as data: the
// theme language puts every sheet rule at `theme_sheet`, where its CSS
// specificity alone orders it (selector step 1).
enum class CascadeLevel : std::uint8_t {
    built_in,
    base_sheet,
    user_theme,
    theme_sheet,
    class_rule,
    stable_id_rule,
    inline_style,
    important_base_sheet,
    important_user_theme,
    important_theme_sheet,
    forced_high_contrast,
    important_inline_style,
    count,
};

[[nodiscard]] std::string_view cascade_level_name(CascadeLevel level) noexcept;

// Which set a theme's rules are compiled as: a program's theme
// (CompiledThemeRules::compile_with_built_in) or a user theme layered under
// it (CompiledThemeRules::compile_user).
enum class ThemeRuleSet : std::uint8_t { program, user };

// Whether `name` is a valid style class name: 1 to 64 characters of
// lowercase ASCII letters, digits, '-' and '_', the first a letter.
[[nodiscard]] bool valid_style_class_name(std::string_view name) noexcept;

// What a rule applies to. Every field it sets must match; an empty field
// matches anything. No combinators: a selector names one part of one
// control.
// - `control_type`: the control's anatomy id (ControlAnatomy::control_id,
//   its id in manifests/controls.json).
// - `stable_id`: the control's stable ID.
// - `classes`: classes the control must all have (Control::classes).
// - `part`: the part's declared name. A part is known by name only when it
//   is painted with a key (PartPaint::key); an unkeyed part never matches a
//   selector that names one.
// - `role`: the part's role.
// - `states`: states the part must all be in.
// - `surface`: a surface selector (`window`, `popup`; owner decision
//   2026-11-04) names a surface, not a control: its classes and ID are the
//   surface root control's, and it sets no control type, part, role or
//   state. A surface rule never matches a control's part, nor a control
//   rule a surface (PartFacts::surface).
// The surface a rule may select (owner decision 2026-11-04): none (a
// control), a window (a top-level window, an owned window or a floating
// panel in its own window) or a popup (a native, alert-like owned window;
// owner, 2026-11-04). Menus, drop-downs and tooltips are overlays drawn in
// the surface's own pixels, styled by control rules, never a popup.
enum class SurfaceKind : std::uint8_t { none, window, popup };

// One argument of a selector's `:not()` (selector step 2): a compound of a
// control type, a stable ID, classes, states and a family, each optional;
// it excludes the parts it matches. It names no part, role or surface.
struct RuleExclusion final {
    std::string control_type{};
    std::string stable_id{};
    std::vector<std::string> classes{};
    std::vector<AnatomyState> states{};
    std::string family{};

    friend bool operator==(const RuleExclusion& left, const RuleExclusion& right) noexcept {
        return left.control_type == right.control_type && left.stable_id == right.stable_id &&
               left.classes == right.classes && left.states == right.states && left.family == right.family;
    }
};

// How a selector's ancestor compound relates to the compound on its right
// (selector step 4): CSS's descendant combinator (whitespace) or child
// combinator (`>`).
enum class AncestorCombinator : std::uint8_t { descendant, child };

// One ancestor compound of a selector (selector step 4): a control type, a
// stable ID and classes, each optional, matched against an ancestor
// control. `combinator` relates it to the compound on its right: the
// subject's for the nearest, else the next nearer ancestor compound's.
// Ancestor compounds name no state, family, part, role or surface, so a
// rule's ancestors change only when an ancestor's type, ID or classes do,
// or a control moves.
struct RuleAncestor final {
    std::string control_type{};
    std::string stable_id{};
    std::vector<std::string> classes{};
    AncestorCombinator combinator{AncestorCombinator::descendant};

    friend bool operator==(const RuleAncestor& left, const RuleAncestor& right) noexcept {
        return left.control_type == right.control_type && left.stable_id == right.stable_id &&
               left.classes == right.classes && left.combinator == right.combinator;
    }
};

// At most this many ancestor compounds in one selector, and ancestors are
// matched within this depth of the subject: a control nested deeper than
// maximum_ancestor_depth below an ancestor is not matched against it.
inline constexpr std::size_t maximum_rule_ancestors = 8U;
inline constexpr std::size_t maximum_ancestor_depth = 256U;

// One ancestor control as a selector's ancestor compounds read it:
// borrowed from the control for the resolution.
struct AncestorFacts final {
    std::string_view control_type{};
    std::string_view stable_id{};
    std::span<const std::string> classes{};
};

// The keys an ancestor chain holds, hashed into 256 bits: a rule whose
// ancestor keys are not all present cannot match, and is rejected without
// walking the chain (CSS engines' ancestor Bloom filter).
struct AncestorBloom final {
    std::array<std::uint64_t, 4> bits{};

    // Adds `key`, written as a selector writes it: `type`, `#id` or `.class`.
    void add(std::string_view prefix, std::string_view key) noexcept;
    [[nodiscard]] bool holds(const AncestorBloom& needed) const noexcept;
};

// `family`, when set, is a control visual family's registered name (the
// theme's ControlVisualRole, such as `accent_button`): the selector matches
// only parts resolved for that family (PartFacts::family). `excluded`
// holds the arguments of its `:not()` selectors: it matches no part any of them
// matches.
struct RuleSelector final {
    std::string control_type{};
    std::string stable_id{};
    std::vector<std::string> classes{};
    std::string part{};
    std::optional<PartRole> role{};
    std::vector<AnatomyState> states{};
    SurfaceKind surface{SurfaceKind::none};
    std::string family{};
    std::vector<RuleExclusion> excluded{};
    // The selector's ancestor compounds, nearest first (selector step 4).
    std::vector<RuleAncestor> ancestors{};

    friend bool operator==(const RuleSelector& left, const RuleSelector& right) noexcept {
        return left.control_type == right.control_type && left.stable_id == right.stable_id &&
               left.classes == right.classes && left.part == right.part && left.role == right.role &&
               left.states == right.states && left.surface == right.surface && left.family == right.family &&
               left.excluded == right.excluded && left.ancestors == right.ancestors;
    }
};

// A selector's specificity, CSS's three columns compared in order: the
// stable ID; the classes, the states and the family (pseudo-classes); the
// control type or surface and the part (its name or role, one each, as
// pseudo-elements). A `:not()` adds its most specific argument's, and each
// ancestor compound its own.
struct RuleSpecificity final {
    std::uint32_t ids{};
    std::uint32_t classes{};
    std::uint32_t types{};

    friend constexpr bool operator==(RuleSpecificity left, RuleSpecificity right) noexcept {
        return left.ids == right.ids && left.classes == right.classes && left.types == right.types;
    }
    friend constexpr bool operator<(RuleSpecificity left, RuleSpecificity right) noexcept {
        if (left.ids != right.ids) return left.ids < right.ids;
        if (left.classes != right.classes) return left.classes < right.classes;
        return left.types < right.types;
    }
};

[[nodiscard]] RuleSpecificity rule_specificity(const RuleSelector& selector) noexcept;

// What a window is, as a rule's media condition reads it (T3 EC3): its
// presentation settings and its scale. Every window evaluates a theme's
// conditions against its own facts, so windows sharing a theme may differ.
struct MediaFacts final {
    // PresentationSettings::colour_scheme is ColourScheme::dark.
    bool dark_scheme{};
    // PresentationSettings::high_contrast.
    bool forced_colours{};
    // PresentationSettings::reduced_motion.
    bool reduced_motion{};
    // The window's scale: device pixels per logical unit.
    double resolution{1.0};

    friend bool operator==(const MediaFacts& left, const MediaFacts& right) noexcept {
        return left.dark_scheme == right.dark_scheme && left.forced_colours == right.forced_colours &&
               left.reduced_motion == right.reduced_motion && left.resolution == right.resolution;
    }
};

// A rule's media condition (EC3): every feature it names must hold for the
// window, and an unnamed feature always holds, so the default condition
// holds everywhere. The resolution bounds are inclusive, in device pixels
// per logical unit; each is finite and above zero. A condition no window
// meets, such as a minimum above the maximum, is admitted: its rule never
// matches. The theme language's `@media` blocks lower to these (one rule a
// query of a comma list).
struct MediaCondition final {
    // `prefers-color-scheme`: dark (true) or light (false).
    std::optional<bool> dark_scheme{};
    // `forced-colors`: active (true) or none (false).
    std::optional<bool> forced_colours{};
    // `prefers-reduced-motion`: reduce (true) or no-preference (false).
    std::optional<bool> reduced_motion{};
    // `min-resolution` and `max-resolution`.
    std::optional<double> minimum_resolution{};
    std::optional<double> maximum_resolution{};

    friend bool operator==(const MediaCondition& left, const MediaCondition& right) noexcept {
        return left.dark_scheme == right.dark_scheme && left.forced_colours == right.forced_colours &&
               left.reduced_motion == right.reduced_motion && left.minimum_resolution == right.minimum_resolution &&
               left.maximum_resolution == right.maximum_resolution;
    }
};

// Whether `condition` names nothing (it holds for every window).
[[nodiscard]] bool media_condition_unconditional(const MediaCondition& condition) noexcept;
// Whether `condition`'s resolution bounds are finite and above zero.
[[nodiscard]] bool media_condition_valid(const MediaCondition& condition) noexcept;
// Whether a window with `facts` meets `condition`.
[[nodiscard]] bool media_condition_holds(const MediaCondition& condition, const MediaFacts& facts) noexcept;

// The name of the declaration that selects a part's primitive. Its value is
// a registered primitive's name, or `built_in` for the built-in recipe.
inline constexpr std::string_view primitive_property = "primitive";
inline constexpr std::string_view built_in_primitive_name = "built_in";

enum class RuleValueKind : std::uint8_t {
    // A name: the primitive property's value.
    name,
    number,
    colour,
    // A bounded expression (E5) giving a number parameter's value from the
    // part's inputs, states and the primitive's other parameters (E6).
    expression,
};

// The colour roles (T3 EC4, design language §5.4, owner decision D1 of
// 2026-10-28): vocabulary atoms a theme's palette gives colours, so that
// forced high contrast can remap a colour by what it means. The first 17
// are §5.4's pairs, a foreground on a background; the other 9 are single
// colours (the relief ramp, the lines, and focus, which §5.4 sets "on any
// surface"). Their names are the
// enumerators' in kebab case (colour_role_name).
enum class ColourRole : std::uint8_t {
    ink_on_paper,
    muted_ink_on_paper,
    ink_on_soft_paper,
    ink_on_chrome,
    ink_on_chrome_low,
    ink_on_near_white_stock,
    selection_ink_on_selection,
    ink_on_gold,
    on_accent_on_accent,
    signal_on_paper,
    success_on_paper,
    pending_ink_on_tint,
    invalid_ink_on_tint,
    disabled_ink_on_chrome,
    disabled_ink_on_paper,
    phosphor_on_instrument_black,
    command_link_ink_on_paper,
    chrome_highlight,
    chrome,
    chrome_middle,
    chrome_low,
    ordinary_line,
    dark_line,
    shadow,
    selection_line,
    focus,
    count,
};

inline constexpr std::size_t colour_role_count = static_cast<std::size_t>(ColourRole::count);

// The role's name in kebab case, such as `ink-on-paper`; empty for count.
[[nodiscard]] std::string_view colour_role_name(ColourRole role) noexcept;
// The role `name` (kebab case, exact) names, or none.
[[nodiscard]] std::optional<ColourRole> colour_role_from_name(std::string_view name) noexcept;
// Whether the role is a pair (a foreground on a background) rather than a
// single colour.
[[nodiscard]] bool colour_role_paired(ColourRole role) noexcept;

// Which colour of a pair; a single role's colour is its foreground.
enum class ColourRoleSide : std::uint8_t { foreground, background };

// Application colour roles (owner request 2026-11-09, the product ports):
// roles a theme package declares beside the built-in ones, for colours its
// program means, such as `syntax-keyword`. A sheet declares them in an
// unconditional `:root` block,
//   :root { color-roles: syntax-keyword from ink-on-paper, ... }
// each with the built-in role it falls back to, which also gives its
// meaning: a pair or a single colour, and the colour forced high contrast
// shows for it unless the sheet declares a `forced-colors` variant. Its
// colours are then declared like a built-in role's, in `:root` and under
// `@media (prefers-color-scheme: dark)` or `(forced-colors: active)`; a
// role without an unconditional declaration of its own starts from its
// fallback's declarations, its own after them. Rules read it with role(),
// and program code through Theme::application_role or
// Control::theme_role_colours. Names are 1 to 64 characters of lowercase
// ASCII letters, digits and '-', the first a letter, none a built-in
// role's; at most maximum_application_colour_roles in one theme.
inline constexpr std::size_t maximum_application_colour_roles = 32U;
inline constexpr std::size_t maximum_application_colour_role_name = 64U;

// One declaration of an application role's colours: its foreground and,
// for a pair, its background, where `media` holds.
struct ApplicationColourRoleVariant final {
    MediaCondition media{};
    Color foreground{};
    Color background{};

    friend bool operator==(const ApplicationColourRoleVariant& left,
                           const ApplicationColourRoleVariant& right) noexcept {
        return left.media == right.media && left.foreground == right.foreground &&
               left.background == right.background;
    }
};

// An application role as the theme compiled it: its name, its fallback
// built-in role, whether it is a pair (its fallback's shape), and its
// declarations in sheet order from its last unconditional one (the base
// colours, where there is one), each later one holding where its condition
// does. A role without an unconditional declaration carries its fallback's
// declarations before its own.
struct ApplicationColourRole final {
    std::string name{};
    ColourRole fallback{ColourRole::ink_on_paper};
    bool paired{};
    std::vector<ApplicationColourRoleVariant> variants{};

    friend bool operator==(const ApplicationColourRole& left, const ApplicationColourRole& right) noexcept {
        return left.name == right.name && left.fallback == right.fallback && left.paired == right.paired &&
               left.variants == right.variants;
    }
};

// Whether `name` is a valid application role name (above), whatever the
// theme declares.
[[nodiscard]] bool application_colour_role_name_valid(std::string_view name) noexcept;

// Where a declaration's colour came from (EC4): none (the declaration
// holds no colour, or is `initial`); a literal (a hexadecimal, functional
// or named colour, or a colour mixed from others); or a role. A
// declaration holding a literal anywhere is a literal; one holding only
// role colours carries the first of them. Forced high contrast remaps a
// role's colour by the role, and a literal by its position.
enum class ColourSource : std::uint8_t { none, literal, role };

struct ColourOrigin final {
    ColourSource source{ColourSource::none};
    ColourRole role{ColourRole::ink_on_paper};
    ColourRoleSide side{ColourRoleSide::foreground};

    friend constexpr bool operator==(ColourOrigin left, ColourOrigin right) noexcept {
        return left.source == right.source && left.role == right.role && left.side == right.side;
    }
};

// A surface's backdrop as a theme declares it (`backdrop:`, owner decision
// 2026-11-04): an ordered chain of operations over what lies beneath the
// surface, applied in order. Blur asks the compositor for its blurred
// backing (the system material; owner, 2026-11-04): GUI.Forms never samples
// other windows' pixels. Blur's amount is a radius in logical units
// (0 to maximum_backdrop_blur), saturate's a factor (0 to
// maximum_backdrop_saturation, 1 unchanged); a tint composites `colour`
// (straight alpha, source over) and keeps where it came from, so that a
// role colour follows the scheme and forced high contrast. A chain holds at
// most maximum_steps operations and at most one blur; the compiler refuses
// any other.
enum class BackdropStepKind : std::uint8_t { blur, saturate, tint };

inline constexpr double maximum_backdrop_blur = 32.0;
inline constexpr double maximum_backdrop_saturation = 4.0;

struct BackdropStep final {
    BackdropStepKind kind{BackdropStepKind::tint};
    double amount{};
    Color colour{};
    ColourOrigin origin{};

    friend bool operator==(const BackdropStep& left, const BackdropStep& right) noexcept {
        return left.kind == right.kind && left.amount == right.amount && left.colour == right.colour &&
               left.origin == right.origin;
    }
};

struct BackdropDeclaration final {
    static constexpr std::size_t maximum_steps = 8U;
    // The first `length` entries are the chain, in order; the rest are
    // value-initialized and never read.
    std::array<BackdropStep, maximum_steps> steps{};
    std::size_t length{};

    friend bool operator==(const BackdropDeclaration& left, const BackdropDeclaration& right) noexcept {
        if (left.length != right.length) return false;
        for (std::size_t index = 0U; index < left.length && index < maximum_steps; ++index) {
            if (!(left.steps[index] == right.steps[index])) return false;
        }
        return true;
    }
};

// Whether `backdrop` is a chain the engine admits: its length within
// capacity, every amount finite and in its range, and at most one blur.
[[nodiscard]] bool backdrop_declaration_valid(const BackdropDeclaration& backdrop) noexcept;

// What a theme's band does while lit (`band-effect`): nothing more, or
// flash (the window's flash limiter bounds it; gui_forms/window/band/
// window_band.hpp).
enum class BandEffect : std::uint8_t { none, flash };

// A `window` rule's band (owner, 2026-11-08: the window outer band):
// `band-width` (logical units, before device-pixel snapping),
// `band-color` (the rest colour), `band-lit-color` and `band-effect`. Each
// field is set only by its own declaration; the cascade takes each field
// from the last matching rule that sets it.
struct BandDeclaration final {
    std::optional<double> width{};
    std::optional<Color> rest_colour{};
    std::optional<Color> lit_colour{};
    std::optional<BandEffect> effect{};

    [[nodiscard]] bool empty() const noexcept {
        const bool none = !width && !rest_colour && !lit_colour && !effect;
        return none;
    }

    friend bool operator==(const BandDeclaration& left, const BandDeclaration& right) noexcept {
        return left.width == right.width && left.rest_colour == right.rest_colour &&
               left.lit_colour == right.lit_colour && left.effect == right.effect;
    }
};

// The widest `band-width` a theme may declare, in logical units (the same
// as gui_forms::maximum_band_width, which a window checks).
inline constexpr double maximum_theme_band_width = 32.0;

// Whether `band` is one the engine admits: a width, if set, finite from 0
// to maximum_theme_band_width; an effect, if set, in the enumeration.
[[nodiscard]] bool band_declaration_valid(const BandDeclaration& band) noexcept;

// Where a colour is drawn, as forced high contrast reads it: a fill (a
// plate), an edge (a border, a keyline or a ring) or text; `none` for a
// colour the high-contrast pass drops (a shadow, a glow, an image).
enum class ColourPosition : std::uint8_t { none, fill, edge, text };

// One declaration: a property and its value. Every property but `primitive`
// is a parameter of the primitive the same rule selects, so a rule that sets
// parameters names its primitive too.
//
// An expression's program reads inputs by position (engine_input_position,
// published_value_position), states, and the primitive's number parameters
// by their declaration position. A parameter it reads has the value the
// cascade gives it before any expression, so expressions never read one
// another. `number` is the expression's fallback. Its value is clamped to
// the parameter's range.
struct RuleDeclaration final {
    std::string property{};
    RuleValueKind kind{RuleValueKind::number};
    std::string name{};
    double number{};
    Color colour{};
    std::vector<ExpressionInstruction> program{};
    // A colour parameter's origin (EC4).
    ColourOrigin origin{};
    // A number parameter's em coefficient (T3 P8c): its value is `number`
    // plus the part's em times it, held within the parameter's range when
    // the part is painted. The rules check `number` against the range only
    // where this is 0.
    double number_em{};

    [[nodiscard]] static RuleDeclaration primitive(std::string_view primitive_name);
    [[nodiscard]] static RuleDeclaration number_value(std::string_view parameter, double value);
    [[nodiscard]] static RuleDeclaration colour_value(std::string_view parameter, Color value);
    // A choice parameter's value by the choice's name, such as a shape.
    [[nodiscard]] static RuleDeclaration choice_value(std::string_view parameter, std::string_view choice);
    [[nodiscard]] static RuleDeclaration expression_value(std::string_view parameter,
                                                          std::span<const ExpressionInstruction> program,
                                                          double fallback);

    friend bool operator==(const RuleDeclaration& left, const RuleDeclaration& right) noexcept {
        return left.property == right.property && left.kind == right.kind && left.name == right.name &&
               left.number == right.number && left.colour == right.colour && left.program == right.program &&
               left.origin == right.origin && left.number_em == right.number_em;
    }
};

// The engine's presentational property (T2 GIVEN C1-1): one multiplier,
// from 0 to 1, on the part's whole composite. It is no vocabulary atom and
// no primitive parameter; only a transition sets it, and a part without one
// is opaque.
inline constexpr std::string_view opacity_property = "opacity";

// The engine's presentational scale (T2 GIVEN D5-1, E8): the part drawn at
// this multiple of its size about its centre, within the overflow its
// control grants (PartPaint::granted_overflow), its hit region and layout
// unchanged. Like opacity it is no atom and no parameter; only a
// transition sets it, and a part without one is drawn at 1. It is a
// capability: the built-in rules never use it, and a part granted no
// overflow never grows.
inline constexpr std::string_view scale_property = "scale";
inline constexpr double minimum_presentational_scale = 0.5;
inline constexpr double maximum_presentational_scale = 2.0;

// The engine's face cross-fade (E7r form a): when a primitive-drawn part's
// face changes between discrete states (its raster is replaced), the old
// face fades into the new over the transition's curve, each baked once and
// blended through a paint group (old at 1 - t, new at t, added). The
// transition names no states: it runs on every change of face. A change
// while a fade runs fades from the face shown last to the new one. The
// two faces' rasters must have the same pixel extent and display scale:
// a new face baked at another size (such as a part resting at a popped
// size) replaces the old one without a fade. The presentational scale
// does not stop a fade: while it moves, both faces are stretched over the
// same rectangle (E7r form b). Like scale, no built-in rule uses it.
inline constexpr std::string_view face_property = "face";

// The engine's presentational translation (E9.6, CSS `translate`): the
// part drawn moved by this many logical units across or down, within the
// overflow its control grants, its hit region, layout and text slot
// unchanged. The part's raster is composited at the offset, so a move
// bakes nothing. Like scale it is no atom and no parameter; only a
// transition sets it, a part without one rests at 0, and a part granted
// no overflow (beyond what its scale uses) never moves. A machine's values
// lie within ±maximum_presentational_translate; the grant clamps further.
inline constexpr std::string_view translate_x_property = "translate_x";
inline constexpr std::string_view translate_y_property = "translate_y";
inline constexpr double maximum_presentational_translate = 32.0;

// How a part joins what lies beneath it (E9.6, CSS `mix-blend-mode`).
enum class PartBlend : std::uint8_t { normal, multiply, screen };

// The engine's blend property (E9.6): a declaration of this name, whose
// value is the name `normal`, `multiply` or `screen`, draws the part
// through a paint group of that composite (PaintGroupComposite), whatever
// draws it, so that it multiplies or screens what was painted beneath it.
// Like opacity it is no atom and no primitive parameter, and a rule that
// declares it needs no primitive; the last matching rule that declares it
// wins. The group's bounds are the part's extent (its bounds grown by its
// primitive's outsets and its granted overflow): nothing the part draws
// beyond them is shown. The forced high-contrast pass draws no blend.
inline constexpr std::string_view blend_property = "blend";

// The effect a part draws (effect programs, design note section 7): a
// declaration of this property whose value is a registered effect's name
// (snake_case, as EffectDeclaration::name spells it) selects that effect
// for the part, and the name `none` (effect_none_name) selects none. Like
// blend it is no atom and no primitive parameter, and a rule that declares
// it needs no primitive; the last matching rule that declares it wins. The
// rules check only the name's form: the control hosting the part checks
// the effect against its registry when it attaches it (the effect's parent
// type, its budget and its verification record), and a refused effect
// leaves the part to its primitive alone (EffectSelectionOutcome).
inline constexpr std::string_view effect_property = "effect";
inline constexpr std::string_view effect_none_name = "none";

// The colours a part's effect draws with (EP-4, theme-variants direction):
// a primary and a secondary colour role, never a literal, so that an
// effect follows the theme's palette in each colour scheme (the rule
// copies the palette's media variants make) and the forced high-contrast
// pass remaps it by its role. A declaration of either holds a colour
// whose origin is a role (ColourSource::role); the last matching rule that
// declares each wins. Like effect_property, neither needs a primitive. A
// part whose rules declare none takes its host control's own colours.
inline constexpr std::string_view effect_primary_property = "effect_primary";
inline constexpr std::string_view effect_secondary_property = "effect_secondary";

// One effect colour as the rules resolve it: the role's colour in the
// part's palette and the role it came from.
struct EffectColourDeclaration final {
    Color colour{};
    ColourOrigin origin{};

    friend bool operator==(const EffectColourDeclaration& left, const EffectColourDeclaration& right) noexcept {
        return left.colour == right.colour && left.origin == right.origin;
    }
};

// The text roles (T3 P8c, owner decision D5 of 2026-10-28): the theme's
// typography tokens (ThemeTypographyTokens) a part's text takes, `ui`
// being the controls' own (`control`). A part's em is its role's size
// times its window's text scale.
enum class TextRole : std::uint8_t { ui, field, caption, heading, title, monospace, count };

inline constexpr std::size_t text_role_count = static_cast<std::size_t>(TextRole::count);

// The role's name in a sheet: `ui`, `field`, `caption`, `heading`,
// `title`, `monospace`.
[[nodiscard]] std::string_view text_role_name(TextRole role) noexcept;
// The role `name` names exactly, if any.
[[nodiscard]] std::optional<TextRole> text_role_from_name(std::string_view name) noexcept;

// The engine's font property (T3 P8c, D6): a declaration of this name,
// whose value is a name (a text role's, or font_inherit_name), sets the
// part's text role. The last matching rule's wins; a part no rule sets has
// the ui role. It is recorded in the part's resolution, where it gives the
// part its em; controls' text does not read it yet.
inline constexpr std::string_view font_property = "font";
inline constexpr std::string_view font_inherit_name = "inherit";

// A part's font as the cascade resolved it: a text role, or, with
// `inherit` (D7), the parent control's, which the caller resolves at
// paint.
struct FontDeclaration final {
    TextRole role{TextRole::ui};
    bool inherit{};

    friend constexpr bool operator==(FontDeclaration left, FontDeclaration right) noexcept {
        return left.role == right.role && left.inherit == right.inherit;
    }
};

// What a transition moves: a primitive parameter, or one of the engine's
// presentational properties.
enum class PresentationProperty : std::uint8_t {
    parameter,
    opacity,
    scale,
    face,
    translate_x,
    translate_y,
};

// The most states one presentational state machine has, and the most
// transitions one part runs: opacity, scale, face and both translations,
// with room for a parameter.
inline constexpr std::size_t maximum_presentation_states = 8;
inline constexpr std::size_t maximum_part_transitions = 6;

// One state of a part's presentational state machine (E7; design language
// §9.6). The machine is in the first of its states whose `when` states the
// part's published states all include (an empty `when` always matches), and
// moves its property to `target`. A part whose states match none rests at
// the property's own value: its cascade value, or 1 for opacity.
//
// When the machine enters this state from another, or the part first
// appears in it, `enter_from`, if set, stands the property at that value
// first; this is how a part fades in when it appears (the docking tiles,
// T2 GIVEN C2-1). Otherwise every move starts from the value shown, so a
// transition retargets and never restarts. A state of another machine of
// the property (another rule's) is another state: a part arriving in this
// state from it enters it (T3 P7b, an animation in a rule of its own).
//
// `stops` (E9.6, multi-stop @keyframes), at most maximum_motion_stops,
// make the move into this state pass each stop's value at its offset of
// the duration, offsets strictly increasing within (0, 1), values within
// the property's range; they need a timed curve. They play when the move
// into the state begins: on entering it from another state, or on
// appearing in it from `enter_from`. A part that appears in the state
// without an entry value stands at its target, as before. An animated
// parameter shows each stop at its nearest keyframe level, as any moving
// value (E7r).
//
// The machine is presentational only: it reads the published states and
// writes nothing back, so it can neither create an observable state nor
// delay behaviour.
//
// `target_em` and `enter_from_em` (T3 P8c, owner decision D4) are em
// coefficients, as each stop's value_em: the value is the number plus the
// part's em times its coefficient. Only a translation's and a number
// parameter's machine takes them.
struct PresentationState final {
    std::vector<AnatomyState> when{};
    double target{};
    std::optional<double> enter_from{};
    std::vector<MotionStop> stops{};
    double target_em{};
    double enter_from_em{};

    friend bool operator==(const PresentationState& left, const PresentationState& right) noexcept {
        return left.when == right.when && left.target == right.target && left.enter_from == right.enter_from &&
               left.stops == right.stops && left.target_em == right.target_em &&
               left.enter_from_em == right.enter_from_em;
    }
};

// A transition on one resolved property: `opacity`, or a number parameter
// of the rule's primitive that the primitive declares animated (such as
// the bulge's amplitude), so that its frames never remake the part's
// preparation. It runs on the window's MotionClock with `curve`; every
// duration is clamped to the 400 ms ceiling. Under reduced motion and in an
// occluded or minimized window it jumps to its end (GIVEN D2-1, D4-1).
struct PropertyTransition final {
    std::string property{};
    std::vector<PresentationState> states{};
    MotionCurve curve{};

    friend bool operator==(const PropertyTransition& left, const PropertyTransition& right) noexcept {
        return left.property == right.property && left.states == right.states && left.curve == right.curve;
    }
};

// The ambient budget (design language §9.4, the accepted starting tokens):
// at most 30 frames per second, over a region of at most 10 % of the window
// on everyday surfaces. A surface the application marks ceremonial
// (Control::ceremonial, GIVEN B3-1) lifts the area cap for the parts on
// it.
inline constexpr double maximum_ambient_frames_per_second = 30.0;
inline constexpr double maximum_ambient_window_fraction = 0.10;

// An ambient effect (E7): its part repaints as time passes, at most
// `frames_per_second`, and reads time_in_state within one `period` (it
// wraps), so that the effect cycles. Its region is the part's extent. It
// runs only while a primitive draws the part and the part has a record,
// and stops in an occluded or minimized window, under reduced motion and
// while the application's motion switch is off
// (PresentationSettings::ambient_motion); the part then rests at `pose`,
// the time within the period it shows when still, such as an activity
// object's static pose.
struct AmbientEffect final {
    FrameInterval period{};
    double frames_per_second{};
    FrameInterval pose{};

    friend bool operator==(const AmbientEffect& left, const AmbientEffect& right) noexcept {
        return left.period == right.period && left.frames_per_second == right.frames_per_second &&
               left.pose == right.pose;
    }
};

// An image layer's size (T3 P6d, CSS `background-size`), applied when the
// material is assembled. The engine keeps an image's proportions, so one
// length sets the size:
// - natural: the image's own size, its layer's image_scale as written
//   (CSS `auto`);
// - width or height: `length` across or down, a fraction of the box's
//   side plus logical units, the other side in proportion;
// - cover and contain: the smallest size that covers the box and the
//   largest that fits in it;
// - stretch: the box (CSS `100% 100%`, MaterialImageMode::stretch).
enum class MaterialImageSizing : std::uint8_t { natural, width, height, cover, contain, stretch };

struct MaterialImageSize final {
    MaterialImageSizing sizing{MaterialImageSizing::natural};
    BoxLength length{};
    friend constexpr bool operator==(const MaterialImageSize& left, const MaterialImageSize& right) noexcept {
        return left.sizing == right.sizing && left.length == right.length;
    }
};

// An image layer's position (CSS `background-position`): each axis's
// fraction is the layer's image_anchor, of the room the box leaves around
// one tile, and its units its image_offset.
struct MaterialImagePosition final {
    BoxLength across{};
    BoxLength down{};
    friend constexpr bool operator==(const MaterialImagePosition& left, const MaterialImagePosition& right) noexcept {
        return left.across == right.across && left.down == right.down;
    }
};

// An image layer's repetition (CSS `background-repeat`): tiled, or one
// tile alone (MaterialImageMode::single).
enum class MaterialImageRepeat : std::uint8_t { repeat, no_repeat };

// How a drawn edge's line is made (T3 P6e, CSS `border-style`): one line
// in its MaterialBorderStyle, or, drawn as two keylines on the edge in
// place of the border (each edge's pair in CSS's order, top first, the
// outer line first, so a later edge's line covers an earlier one's in the
// corner square they share):
// - double_line: two lines each a third of the width, a third apart;
// - groove and ridge: two halves, one in the edge's colour and one in its
//   shade (each colour channel halved, rounded down): a groove's outer
//   half is the shade on the top and left edges and the colour on the
//   bottom and right, and a ridge's the reverse.
enum class MaterialEdgeLines : std::uint8_t { single, double_line, groove, ridge };

// The material fields a rule may set (T3 engine change EC1, owner R2-02).
// Each cascades on its own, as a parameter does: a part's material is the
// recipe material its primitive draws (the cascade's floor), with each
// field the winning matching rule sets in its place. The fields are
// SurfaceMaterial's, split where CSS's longhands are, so that each
// cascades as CSS cascades it:
// - the fills are CSS's background-color, background-image,
//   background-blend-mode, background-position, background-size and
//   background-repeat (owner C6, 2026-10-26: they combine across rules,
//   the colour under the image layers);
// - the border is its twelve longhands, each edge's style, width and
//   colour, in that order, the edges in CSS's order (`border` sets all
//   twelve);
// - the radii are the four corners, in CSS's order (`border-radius` sets
//   all four).
enum class MaterialField : std::uint8_t {
    background_colour,
    fills,
    fill_blend_modes,
    fill_positions,
    fill_sizes,
    fill_repeats,
    shadows,
    glows,
    glow_placements,
    border_top_style,
    border_top_width,
    border_top_colour,
    border_right_style,
    border_right_width,
    border_right_colour,
    border_bottom_style,
    border_bottom_width,
    border_bottom_colour,
    border_left_style,
    border_left_width,
    border_left_colour,
    border_image,
    keylines,
    corner_top_left,
    corner_top_right,
    corner_bottom_right,
    corner_bottom_left,
    colour_filter,
    drop_shadow,
    filter_blur,
    finish,
    mask,
    reflection,
    count,
};

inline constexpr std::size_t material_field_count = static_cast<std::size_t>(MaterialField::count);

[[nodiscard]] std::string_view material_field_name(MaterialField field) noexcept;
// Where the field's colours are drawn (EC4): the background colour and the
// fills are a fill, the border colours and keylines an edge; every other
// field none.
[[nodiscard]] ColourPosition material_field_colour_position(MaterialField field) noexcept;

// One material field a rule sets and its value. Only the members `field`
// names are read; the others stay empty:
// - background_colour: `colour`, a solid layer under the image layers;
// - fills: the image layers (gradients, images, patterns), bottom first,
//   at most SurfaceMaterial::maximum_fill_layers, empty for none; and
//   `gradients`, empty, or one per layer: a gradient as a sheet wrote it
//   (T3 P6b), resolved for the part's box when the material is assembled,
//   in place of its layer; an entry of kind `solid` keeps its layer as
//   written;
// - fill_blend_modes: `blend_modes`, in CSS's order (the top image
//   layer's first), repeated over the image layers as CSS repeats a short
//   list; empty for normal;
// - fill_positions, fill_sizes, fill_repeats: `image_positions`,
//   `image_sizes`, `image_repeats`, in the same order and repeated the
//   same way, read by the image fills (not a nine-patch) among the image
//   layers: a gradient layer covers the box;
// - shadows, glows, keylines: the lists;
// - glow_placements: `glow_placements`, in the glows' order, repeated
//   over them as CSS repeats a short list (`glow-placement`);
// - an edge's style: `border_style`, empty for none (CSS's `none`), and
//   `edge_lines`; its width: `number`, 0 for none; its colour: `colour`;
// - border_image: `border_image`, a nine-patch image fill, empty for none;
// - a corner: `number`, its radius in logical units, or, with `relative`,
//   a fraction of the box's shorter side (CSS's percentage, R2-11),
//   resolved when the material is assembled for a part;
// - colour_filter: `colour_filter`; drop_shadow: `drop_shadow`, empty for
//   none; mask: `mask`, empty for none, and `gradients`, empty or one
//   entry, the mask's authored gradient as for a fill layer;
// - filter_blur and finish: `number`;
// - reflection: `reflection`, empty for none (stage T5, C7).
// `initial`, set, is CSS's `initial`: the field keeps the floor's value,
// and the declaration still wins over those beneath it; its value members
// are not read.
struct MaterialDeclaration final {
    MaterialField field{MaterialField::fills};
    bool initial{};
    std::vector<MaterialFillLayer> fills{};
    std::vector<AuthoredGradient> gradients{};
    std::vector<MaterialBlendMode> blend_modes{};
    std::vector<MaterialImagePosition> image_positions{};
    std::vector<MaterialImageSize> image_sizes{};
    std::vector<MaterialImageRepeat> image_repeats{};
    std::vector<MaterialShadow> shadows{};
    std::vector<MaterialGlow> glows{};
    std::vector<MaterialGlowPlacement> glow_placements{};
    std::optional<MaterialBorderStyle> border_style{};
    MaterialEdgeLines edge_lines{MaterialEdgeLines::single};
    std::optional<MaterialFillLayer> border_image{};
    Color colour{};
    std::vector<MaterialKeyline> keylines{};
    double number{};
    bool relative{};
    MaterialColourFilter colour_filter{};
    std::optional<MaterialDropShadow> drop_shadow{};
    std::optional<MaterialFillLayer> mask{};
    std::optional<MaterialReflection> reflection{};
    // Where the declaration's colours came from (EC4).
    ColourOrigin origin{};
    // Its lengths in em (T3 P8c, owner decision D4): empty, or one
    // coefficient for each length material_declaration_lengths lists, in
    // its order; each length is its value plus the part's em times its
    // coefficient (add_material_em).
    std::vector<double> em_lengths{};
    // Its colours that are CSS's currentcolor (P8c): positions, increasing,
    // among the colours material_declaration_colours lists; each takes the
    // part's text colour when the material is assembled.
    std::vector<std::uint16_t> current_colours{};

    friend bool operator==(const MaterialDeclaration& left, const MaterialDeclaration& right) noexcept {
        return left.field == right.field && left.initial == right.initial && left.fills == right.fills &&
               left.gradients == right.gradients && left.blend_modes == right.blend_modes &&
               left.image_positions == right.image_positions && left.image_sizes == right.image_sizes &&
               left.image_repeats == right.image_repeats && left.shadows == right.shadows &&
               left.glows == right.glows && left.glow_placements == right.glow_placements &&
               left.border_style == right.border_style && left.edge_lines == right.edge_lines &&
               left.border_image == right.border_image &&
               left.colour == right.colour && left.keylines == right.keylines &&
               left.number == right.number && left.relative == right.relative &&
               left.colour_filter == right.colour_filter && left.drop_shadow == right.drop_shadow &&
               left.mask == right.mask && left.reflection == right.reflection && left.origin == right.origin &&
               left.em_lengths == right.em_lengths &&
               left.current_colours == right.current_colours;
    }
};

// The lengths of `declaration` in logical units, into `lengths` (cleared
// first), in a fixed order: its number where it is a length (a border
// width, a corner's radius that is not relative, the filter's blur); each
// authored gradient's centre, radii and stop positions (their units); the
// image positions' and the image sizes' units; each shadow's offset, blur
// and spread; each glow's blur and spread; each keyline's width and inset;
// the drop shadow's offset and blur; the reflection's gap and reach.
void material_declaration_lengths(const MaterialDeclaration& declaration, std::vector<double>& lengths);
// Sets the lengths of `declaration` from `lengths`, in that order; a length
// past the end of `lengths` is kept.
void set_material_declaration_lengths(MaterialDeclaration& declaration, std::span<const double> lengths) noexcept;
// Adds `em` times each of the declaration's em_lengths to its length.
void add_material_em(MaterialDeclaration& declaration, double em) noexcept;

// The colours of `declaration`, into `colours` (cleared first), in a fixed
// order: its colour; each authored gradient's stop colours; each shadow's,
// glow's and keyline's colour; the drop shadow's.
void material_declaration_colours(const MaterialDeclaration& declaration, std::vector<Color>& colours);
// Sets them from `colours`, in that order; one past its end is kept.
void set_material_declaration_colours(MaterialDeclaration& declaration, std::span<const Color> colours) noexcept;
// Sets each of the declaration's current_colours to `colour`.
void set_material_current_colour(MaterialDeclaration& declaration, Color colour) noexcept;

// Replaces `material` with `floor` and the declarations of `fields` (the
// winning declaration of each field, by the field's position, null where
// none) in place: a part's assembled material (EC1); an `initial`
// declaration keeps the floor's value. The fills are composed when any
// fill field is declared: the colour layer (the declared colour, else the
// floor's bottom layer when it is solid) under the image layers (the
// declared ones, each authored gradient resolved for `box`, else the
// floor's others), which take the declared blend modes, and whose image
// fills take the declared sizes (resolved for `box`), then repetitions,
// then positions; a colour of alpha 0 adds no layer, and with no layer at
// all the fill is one transparent solid. More than
// SurfaceMaterial::maximum_fill_layers is no material. The corners are
// composed when any is declared: each is its declaration's, else the
// floor's (its corner_radii's, else its corner_radius); four equal corners
// are corner_radius alone, and others corner_radii, with corner_radius the
// smallest (the built-in path rounds every corner by it). The glows take
// the declared placements; a mask's authored gradient is resolved for
// `box`. The border's
// edges are assembled
// together when any of their fields is declared: each starts as the
// floor's (its single edge, else its uniform border; an edge the floor
// lacks has the default MaterialBorder's width and colour and is not
// drawn), takes its declared style, width and colour, and is drawn unless
// its style is none or its width 0. Four equal edges are drawn as one
// uniform border, which keeps a dash and rounded corners, and unequal ones
// as single edges, which a material admits only with square corners
// (valid_surface_material); an edge drawn as two lines (MaterialEdgeLines)
// is not a border edge but its two keylines, after the declared ones. A
// border image is the top fill layer, and the border (its edges and their
// lines) is not drawn, as CSS draws a border image in place of the
// border's style. `box` is the part's box in logical units: relative
// corners are fractions of its shorter side.
// What a part gives its material's relative values (P8c): its em, and
// its text colour, which CSS's currentcolor names.
struct MaterialAssemblyContext final {
    double em{};
    Color current_colour{};
};

// With `context`, each declaration's em lengths are first added at the
// part's em (add_material_em) and its current colours set
// (set_material_current_colour); the overload without it assembles at an
// em of 0, its current colours as written.
void assemble_material(const SurfaceMaterial& floor,
                       const std::array<const MaterialDeclaration*, material_field_count>& fields, Size box,
                       MaterialAssemblyContext context, SurfaceMaterial& material);
void assemble_material(const SurfaceMaterial& floor,
                       const std::array<const MaterialDeclaration*, material_field_count>& fields, Size box,
                       SurfaceMaterial& material);

// Whether assembling `fields` reads the part's em: a declaration with em
// lengths.
[[nodiscard]] bool material_fields_read_em(
    const std::array<const MaterialDeclaration*, material_field_count>& fields) noexcept;
// Whether assembling `fields` reads the part's text colour: a declaration
// with current colours.
[[nodiscard]] bool material_fields_read_current_colour(
    const std::array<const MaterialDeclaration*, material_field_count>& fields) noexcept;

// Whether assembling `fields` reads the part's box: a relative corner, an
// authored gradient, of a fill or the mask, other than a fixed one
// (authored_gradient_fixed), or an image size of the box (a fraction of
// its side, cover or contain).
[[nodiscard]] bool material_fields_read_box(
    const std::array<const MaterialDeclaration*, material_field_count>& fields) noexcept;

// The fields of a control's own recipe (ControlVisualRecipe) beside its
// material that a rule may set (T3 P6f; design language §2, §3 and §14).
// They are the control's, not a part's: a rule naming a role sets none,
// and a rule naming a part only the text colours (text, muted_text, glyph,
// fill_text) and the dial and scrim fields (stroke_width, needle_shape,
// scrim_colour),
// which the part's own resolution reads (Theme::resolve with its name);
// any other is dropped (RuleDiagnosticKind::recipe_field_on_part). They
// cascade as
// the material fields do, each on its own, from every matching rule, over
// the theme's built-in recipe for the control's role and state (the floor,
// Theme::resolve); a field no rule sets keeps the floor's value.
enum class RecipeField : std::uint8_t {
    // CSS `color`: the recipe's text colour.
    text,
    // CSS `outline-style`, `outline-color`, `outline-width` and
    // `outline-offset`: the focus ring, drawn while the control shows
    // focus.
    outline_style,
    outline_colour,
    outline_width,
    outline_offset,
    // CSS `text-shadow`: the shadow under the recipe's text (data only
    // until the text stage draws it).
    text_shadow,
    // The control's primary text font (T4 fonts, owner ruling of
    // 2026-11-04): CSS `font-family` (a bundled FontRole, never a host
    // font), `font-size`, `font-weight`, `font-style` and `letter-spacing`.
    // The recipe does not draw them: the control reads them when it
    // measures, at rest (Theme::resolve_font).
    font_family,
    font_size,
    font_weight,
    font_style,
    letter_spacing,
    // The recipe's other text colours (T4 floor sheet, owner option 1):
    // `muted-color`, its secondary text, and `glyph-color`, the colour of
    // its glyphs (check marks, arrows), each drawn as text.
    muted_text,
    glyph,
    // `default-outline: <width> <colour>`: the default button's second
    // perimeter (ControlVisualRecipe::default_width and default_ring).
    default_outline_width,
    default_outline_colour,
    // `fill-text-color` (the node engine and value bar study, section 3.6):
    // the colour of the recipe's text where it lies over a fill, such as a
    // ValueBar's label over its value fill; drawn as text. Unset, the text
    // over a fill takes the fill's own selected text colour.
    fill_text,
    // The dial parts' geometry and the scrim (row 2.5; owner option (a),
    // 2026-11-09), admitted on a named part as on the control:
    // `stroke-width`, the arc's thickness and the needle's width at its
    // hub (ControlVisualRecipe::stroke_width); `needle-shape`, the
    // needle's outline (NeedleShape); and `scrim-color`, the colour, with
    // its opacity, a scrim dims what lies behind with.
    stroke_width,
    needle_shape,
    scrim_colour,
    // A part's metrics and shape (look integration; RB, T4 rendering
    // reference section 4), read by the control that lays the part out
    // and paints it (Theme::resolve with the part's name; the Ribbon's tab
    // row, tabs and command plane): CSS `height` (block_size), `min-width`
    // (minimum_inline_size), `padding-inline`, `gap` (signed: a negative
    // gap overlaps neighbouring parts, as folder tabs stack), `margin-top`
    // and `margin-bottom` (signed: a negative bottom margin runs the part
    // into what lies below it); `taper: <length> [<colour>]`, the trailing edge
    // leaning in by the length at the part's top (a folder tab's edge),
    // drawn in the colour; `flare: <length> <colour>`, the bottom
    // corners flaring outward by the length in a concave lip; and
    // `edge-shadow: <length> <colour>`, a soft shadow falling off over the
    // length beyond the trailing edge, following its taper. A control
    // keeps its own metric where no rule sets one; every stock theme
    // leaves the shape fields unset.
    block_size,
    minimum_inline_size,
    padding_inline,
    gap,
    margin_block_start,
    margin_block_end,
    taper,
    taper_colour,
    flare,
    flare_colour,
    edge_shadow_width,
    edge_shadow_colour,
    count,
};

inline constexpr std::size_t recipe_field_count = static_cast<std::size_t>(RecipeField::count);

// The field's snake_case name, as diagnostics name it; empty for `count`.
[[nodiscard]] std::string_view recipe_field_name(RecipeField field) noexcept;
// Where the field's colour is drawn (EC4): the text, muted text and glyph
// colours are text, and the outline and default outline colours edges;
// every other field none.
[[nodiscard]] ColourPosition recipe_field_colour_position(RecipeField field) noexcept;

// How the focus ring is drawn (CSS `outline-style`): the theme's own ring
// inside the control's bounds (CSS's `auto`), an outline outside them,
// offset by the outline offset (CSS's `solid`), the same outline as dots
// (CSS's `dotted`), or none (a width of 0).
enum class RecipeOutlineStyle : std::uint8_t { ring, solid, none, dotted };

// A needle's outline (CSS-like `needle-shape`): a line of the stroke width
// from hub to tip (`line`), a blade narrowing from the stroke width at the
// hub to a point at the tip (`tapered`), or a round dot of the stroke width
// at the tip alone, with no hub (`dot`). A line and a blade have a round
// hub one and a half stroke widths across.
enum class NeedleShape : std::uint8_t { line, tapered, dot };

// What a recipe admits (valid_recipe): an outline at most 16 logical units
// wide, offset at most 64 either way; a text shadow offset at most 16 on
// each axis and blurred by at most 8.
inline constexpr double maximum_recipe_outline_width = 16.0;
inline constexpr double maximum_recipe_outline_offset = 64.0;
// An arc or needle at most 32 logical units thick (stroke-width).
inline constexpr double maximum_recipe_stroke_width = 32.0;
inline constexpr double maximum_text_shadow_blur = 8.0;
inline constexpr double maximum_text_shadow_offset = 16.0;
// What a part metric admits (RecipeField::block_size to edge_shadow_width):
// a size at most 512 logical units, a margin within +-64, a gap from -64 to
// 512, a taper, flare or edge shadow at most 64.
inline constexpr double maximum_part_metric = 512.0;
inline constexpr double maximum_part_margin = 64.0;
inline constexpr double maximum_part_shape = 64.0;
// What a font field admits (T4 fonts): a size of 4 to 96 logical units
// once its em part is resolved, a weight of 100 to 900, and a letter
// spacing valid_font_spec admits at the size.
inline constexpr double minimum_rule_font_size = 4.0;
inline constexpr double maximum_rule_font_size = 96.0;
inline constexpr double minimum_rule_font_weight = 100.0;
inline constexpr double maximum_rule_font_weight = 900.0;

// One recipe field's declared value. `initial` is CSS's `initial`: the
// floor's value, and the declaration still wins over those beneath it; its
// value members are not read. Otherwise, by field:
// - text, outline_colour, muted_text, glyph, default_outline_colour and
//   fill_text: `colour`;
// - outline_style: `outline_style`;
// - outline_width and default_outline_width (0 to
//   maximum_recipe_outline_width) and outline_offset
//   (within ±maximum_recipe_outline_offset): `number`, in logical units;
// - text_shadow: `shadowed`, false for none; its `shadow_offset`,
//   `shadow_blur` (the shadows' blur-radius convention) and `colour`;
// - font_family: `font_role`; font_style: `italic`;
// - font_size (in logical units, with em_lengths in the control's own
//   font size), font_weight and letter_spacing (logical units, with
//   em_lengths in the resolved size): `number`.
struct RecipeDeclaration final {
    RecipeField field{RecipeField::text};
    bool initial{};
    Color colour{};
    double number{};
    RecipeOutlineStyle outline_style{RecipeOutlineStyle::ring};
    NeedleShape needle_shape{NeedleShape::tapered};
    bool shadowed{};
    Point shadow_offset{};
    double shadow_blur{};
    // Where the declaration's colour came from (EC4).
    ColourOrigin origin{};
    // Its lengths in em (P8c), as MaterialDeclaration::em_lengths, for the
    // lengths recipe_declaration_lengths lists.
    std::vector<double> em_lengths{};
    // CSS's `inherit` on the text colour (P8c, owner decision D7): the
    // parent control's cascade result, resolved at paint.
    bool inherit{};
    // Whether `colour` is CSS's currentcolor: the control's text colour.
    bool current_colour{};
    FontRole font_role{FontRole::control};
    bool italic{};

    friend bool operator==(const RecipeDeclaration& left, const RecipeDeclaration& right) noexcept {
        return left.field == right.field && left.initial == right.initial && left.colour == right.colour &&
               left.font_role == right.font_role && left.italic == right.italic &&
               left.number == right.number && left.outline_style == right.outline_style &&
               left.needle_shape == right.needle_shape &&
               left.shadowed == right.shadowed && left.shadow_offset == right.shadow_offset &&
               left.shadow_blur == right.shadow_blur && left.origin == right.origin &&
               left.em_lengths == right.em_lengths && left.inherit == right.inherit &&
               left.current_colour == right.current_colour;
    }
};

// The lengths of a recipe declaration, into `lengths` (cleared first): the
// outline width's or offset's number; the text shadow's offset and blur.
void recipe_declaration_lengths(const RecipeDeclaration& declaration, std::vector<double>& lengths);
void set_recipe_declaration_lengths(RecipeDeclaration& declaration, std::span<const double> lengths) noexcept;
void add_recipe_em(RecipeDeclaration& declaration, double em) noexcept;

// Whether a declaration's current_colours are increasing positions among
// its colours: the rules drop it otherwise.
[[nodiscard]] bool current_colours_fit(const MaterialDeclaration& declaration);

// Whether a declaration's em_lengths are empty, or one finite coefficient
// for each of its lengths: the rules drop it otherwise
// (material_field_invalid, recipe_field_invalid).
[[nodiscard]] bool em_lengths_fit(const MaterialDeclaration& declaration);
[[nodiscard]] bool em_lengths_fit(const RecipeDeclaration& declaration);

// Whether `declaration` is one a recipe admits: a field below `count`, and
// a finite value within its field's range.
[[nodiscard]] bool recipe_declaration_admitted(const RecipeDeclaration& declaration) noexcept;

// One rule. `label` names it in diagnostics (T3: its source position).
// `requires_engine`, when set, is the engine version the rule needs, in
// place of its sheet's. Its transitions and its ambient effect cascade as
// declarations do: for each property, the transition of the last matching
// rule that declares one (for a parameter, among the rules selecting the
// part's primitive), and the ambient effect of the last matching rule that
// declares one. Its material fields (EC1) cascade the same way, each on
// its own, from every matching rule, whatever primitive it selects: they
// change a part only where its primitive draws a theme material. Its
// recipe fields (RecipeField) cascade the same way for the control as a
// whole.
//
// `specificity`, when set, is the rule's specificity in place of the one
// its selector gives (T3 engine change EC2): a rule expanded from CSS's
// `:is()` takes its most specific argument's. A class or stable-ID rule
// with a given specificity then needs a class or an ID in it rather than
// in its selector; an inline style still needs its selector's stable ID.
// Custom properties defined in more than one place (T3 P12, EC7; owner
// decision of 2026-10-29), resolved per part when the theme loads: each
// definition of such a name is a scoped definition of the rule that
// declares it, at the rule's level, selector and media; and a rule that
// reads such names is lowered once per combination of their candidate
// definitions, each copy holding the combination as its conditions. A
// part's winning definition of a name is the last matching one in
// ascending precedence, or none; a copy applies to a part only where every
// condition names the part's winner. A name defined once is substituted
// when the theme loads, and is not scoped. Names are written with their
// `--`; a definition is identified by a number unique in the sheet.
inline constexpr std::uint32_t custom_property_undefined = 0xFFFF'FFFFU;

// The most scoped names one compilation's conditions may read (E515): eight
// times the token-heavy test theme's 8 (theme_custom_property_css_tests).
inline constexpr std::size_t maximum_scoped_custom_properties = 64U;

// The most custom property names a theme's rules read (P12b,
// ThemeRule::reads): a sheet defines at most 1,024.
inline constexpr std::size_t maximum_read_custom_properties = 1024U;

// A definition made in a control's inline style (P12b) is numbered at or
// above this, a theme's below it, so the two never share a number and the
// engine tells an inline definition's win apart.
inline constexpr std::uint32_t inline_definition_base = 0x8000'0000U;

struct ScopedDefinition final {
    std::string name{};
    std::uint32_t definition{};

    friend bool operator==(const ScopedDefinition& left, const ScopedDefinition& right) noexcept {
        return left.name == right.name && left.definition == right.definition;
    }
};

// A copy's condition: the part's winning definition of `name` is
// `definition`, or, for custom_property_undefined, no definition matches.
struct ScopedCondition final {
    std::string name{};
    std::uint32_t definition{custom_property_undefined};

    friend bool operator==(const ScopedCondition& left, const ScopedCondition& right) noexcept {
        return left.name == right.name && left.definition == right.definition;
    }
};

// The layer of the cascade a rule was written in (T4 cascade spec, item
// 5): the built-in recipe, a library pack (an imported package, or the
// GUI.Forms base sheet under a program's packages), the project theme (a
// program's own packages), a user theme, inline code, forced high contrast
// (the engine's own pass, never a rule's) or the fallback theme (a dark
// window showing GUI.Forms' own theme, W403).
enum class ThemeLayer : std::uint8_t {
    built_in,
    library_pack,
    project_theme,
    user_theme,
    inline_code,
    forced_high_contrast,
    fallback_theme,
    count,
};

[[nodiscard]] std::string_view theme_layer_name(ThemeLayer layer) noexcept;

// Where a rule was written (T4 cascade spec, item 5), for an editor's
// queries; paint never reads it. `layer` as above; `pack` and `pack_hash`
// name a library pack or a project package (its manifest's name and the
// SHA-256 of its manifest, 64 lowercase hexadecimal digits), empty
// elsewhere; `file`, `line` and `column` (from 1; 0 for none) place the
// rule's first token in its own sheet, and `offset` is that token's byte
// offset in the text compiled (a closure's combined sheets until the
// package compiler places it); `setter` names the C++ setter of inline
// code that is not a written style (empty otherwise).
struct RuleProvenance final {
    ThemeLayer layer{ThemeLayer::project_theme};
    std::string pack{};
    std::string pack_hash{};
    std::string file{};
    std::uint32_t line{};
    std::uint32_t column{};
    std::uint32_t offset{};
    std::string setter{};

    friend bool operator==(const RuleProvenance& left, const RuleProvenance& right) noexcept {
        return left.layer == right.layer && left.pack == right.pack && left.pack_hash == right.pack_hash &&
               left.file == right.file && left.line == right.line && left.column == right.column &&
               left.offset == right.offset && left.setter == right.setter;
    }
};

struct ThemeRule final {
    std::string label{};
    CascadeLevel level{CascadeLevel::theme_sheet};
    RuleSelector selector{};
    std::optional<RuleSpecificity> specificity{};
    std::vector<RuleDeclaration> declarations{};
    std::optional<EngineVersion> requires_engine{};
    std::vector<PropertyTransition> transitions{};
    std::optional<AmbientEffect> ambient{};
    std::vector<MaterialDeclaration> materials{};
    std::vector<RecipeDeclaration> recipe_fields{};
    // The windows the rule holds in (EC3); by default, all.
    MediaCondition media{};
    // Scoped custom properties (P12): the definitions the rule declares,
    // and the conditions under which the rule, a copy, applies.
    std::vector<ScopedDefinition> scoped_definitions{};
    std::vector<ScopedCondition> scoped_conditions{};
    // Inline custom properties (P12b). A theme's rule lists the custom
    // properties it reads, through any number of definitions: where a
    // control's inline definition of one wins for a part, the rule is
    // passed over there and the inline style's copy of it applies instead.
    // `source_position` is the position of the style rule the rule was
    // lowered from in the theme's source (its syntax node, from 1), which
    // places such a copy among the theme's rules; 0 for none.
    std::vector<std::string> reads{};
    std::uint32_t source_position{};
    // A surface rule's backdrop (`backdrop:`), or none.
    std::optional<BackdropDeclaration> backdrop{};
    // A window rule's band (`band-*`), or none.
    std::optional<BandDeclaration> band{};
    // Where the rule was written. Not compared: two rules that cascade
    // alike are equal wherever they were written.
    RuleProvenance provenance{};

    friend bool operator==(const ThemeRule& left, const ThemeRule& right) noexcept {
        return left.label == right.label && left.level == right.level && left.selector == right.selector &&
               left.specificity == right.specificity && left.declarations == right.declarations &&
               left.requires_engine == right.requires_engine && left.transitions == right.transitions &&
               left.ambient == right.ambient && left.materials == right.materials &&
               left.recipe_fields == right.recipe_fields && left.media == right.media &&
               left.scoped_definitions == right.scoped_definitions &&
               left.scoped_conditions == right.scoped_conditions && left.reads == right.reads &&
               left.source_position == right.source_position && left.backdrop == right.backdrop &&
               left.band == right.band;
    }
};

// A theme's rules, as authored, with the engine version the theme declares
// it needs and the values an application publishes that its expressions
// read (E6; Window::publish_value). The name at position n of the list is
// read at published_value_position(n). Each is 1 to 64 characters of lowercase
// ASCII letters, digits, '-' and '_', the first a letter, and is listed
// once; at most maximum_published_values.
struct ThemeRuleSheet final {
    EngineVersion requires_engine{current_engine_version};
    std::vector<ThemeRule> rules{};
    std::vector<std::string> published_values{};
    // The colour schemes the sheet declares (`:root { color-scheme: ... }`,
    // T4 theme variants), or none where it declares none. Theme::create
    // takes a declared set as the theme's (ThemeDefinition::colour_schemes).
    std::optional<ColourSchemeSet> colour_schemes{};
    // The application colour roles the sheet declares (`color-roles`), in
    // declaration order; at most maximum_application_colour_roles.
    std::vector<ApplicationColourRole> application_roles{};

    friend bool operator==(const ThemeRuleSheet& left, const ThemeRuleSheet& right) noexcept {
        return left.requires_engine == right.requires_engine && left.rules == right.rules &&
               left.published_values == right.published_values && left.colour_schemes == right.colour_schemes &&
               left.application_roles == right.application_roles;
    }
};

// Budgets (GIVEN F1-1): a part's primitive costs at most 512 declared
// operations per device pixel, and an animated part (one whose primitive has
// an animated parameter) at most 4 million per frame. A part beyond either
// keeps the built-in recipe; a primitive declaring no per-pixel cost cannot
// be bounded and is refused too.
inline constexpr std::uint32_t maximum_operations_per_device_pixel = 512U;
inline constexpr std::uint64_t maximum_animated_operations_per_frame = 4'000'000U;

// Keyframes (E7r, owner 2026-10-22: 8 to 12). While an animated parameter's
// transition runs, the part shows the nearest of this many levels evenly spaced
// over the parameter's declared range, each baked once and shared; settled,
// it shows its exact value. When a transition starts, the levels it will
// pass are baked at once: at most this many bakes, each within the animated
// budget above, so that none is baked while it runs.
inline constexpr std::size_t animated_keyframe_levels = 10U;
// The most frames in an ambient effect's loop (E7r): its time in the period
// steps through at most this many, or its frame rate's count for one
// period if fewer, each baked once, the first time it is shown, and shared.
inline constexpr std::size_t maximum_ambient_loop_frames = 24U;

enum class RuleDiagnosticKind : std::uint8_t {
    // The rule is set aside: it needs a newer engine than this one. Its
    // parts fall back to lower rules or the built-in recipe. Not an error.
    needs_newer_engine,
    // The rule's level cannot be authored (built_in, forced_high_contrast).
    level_not_authored,
    // A class rule names no class, or a stable-ID rule or inline style
    // (either level) no stable ID; the rule is dropped.
    selector_missing_level_field,
    // A selector's class or state is invalid; the rule is dropped.
    selector_invalid,
    // The declarations below are dropped one at a time; the rest of the
    // rule stays.
    unknown_primitive,
    primitive_role_mismatch,
    primitive_over_budget,
    primitive_cost_unbounded,
    primitive_value_not_a_name,
    parameter_without_primitive,
    unknown_parameter,
    parameter_wrong_kind,
    parameter_not_finite,
    parameter_out_of_range,
    duplicate_declaration,
    // An expression that does not compile (RuleDiagnostic::expression names
    // the defect), or that reads a colour parameter; the declaration is
    // dropped.
    expression_invalid,
    expression_reads_colour,
    // A rule left with no declarations after the drops above is dropped.
    rule_empty,
    // A backdrop (ThemeRule::backdrop) on a rule that selects no surface, or
    // one that is not a valid chain (backdrop_declaration_valid); the
    // backdrop is dropped.
    backdrop_invalid,
    // A band (ThemeRule::band) on a rule that selects no window, or one
    // band_declaration_valid refuses; the band is dropped.
    band_invalid,
    // A transition (E7) is dropped: its property is neither opacity nor a
    // parameter of the rule's primitive; the parameter is not animated or
    // not a number; or it has no states or more than
    // maximum_presentation_states, a state outside the vocabulary, or a
    // target or entry value that is not finite or outside the property's
    // range; or a state's stops are too many, out of order, outside the
    // property's range, or on a curve that is not timed. A rule with more than maximum_part_transitions keeps the first.
    transition_property_unknown,
    transition_property_not_animated,
    transition_invalid,
    // The ambient effect is dropped: more than 30 frames per second (or
    // none), or a period or pose that is not a positive period with the
    // pose inside it.
    ambient_over_frame_cap,
    ambient_invalid,
    // A blend declaration (blend_property) whose value is not one of its
    // names is dropped.
    blend_invalid,
    // A font declaration whose value is not a text role's name or
    // `inherit` (P8c).
    font_invalid,
    // An effect declaration (effect_property) whose value is not a name
    // of lowercase ASCII letters, digits and '_', the first a letter, of at
    // most maximum_effect_name_length characters.
    effect_invalid,
    // An effect colour declaration (effect_primary_property,
    // effect_secondary_property) whose value is not a colour taken from a
    // colour role.
    effect_colour_invalid,
    // A published value's name is invalid, repeats an earlier one or is
    // past maximum_published_values. The diagnostic, of the sheet rather
    // than a rule, names it in `property`; its position stays, and reads as
    // not published.
    published_value_invalid,
    // A material field whose value no material admits
    // (valid_surface_material), such as a ninth fill or a negative radius.
    material_field_invalid,
    // A recipe field the recipe does not admit
    // (recipe_declaration_admitted), such as an outline 20 units wide.
    recipe_field_invalid,
    // A recipe field in a rule whose selector names a role, or one other
    // than the text colours in a rule naming a part: the recipe's fields
    // are the control's, so the field is dropped.
    recipe_field_on_part,
    // The rule's media condition has a resolution bound that is not finite
    // or not above zero (media_condition_valid); the rule is dropped.
    media_invalid,
    // The rule's conditions read a scoped name past
    // maximum_scoped_custom_properties, or a scoped definition or
    // condition names no custom property; the rule is dropped.
    scoped_property_invalid,
    // A user theme's opacity below user_theme_minimum_opacity, in a state's
    // target, entry value or stop, is raised to it; the declaration stays.
    user_opacity_raised,
};

[[nodiscard]] std::string_view rule_diagnostic_name(RuleDiagnosticKind kind) noexcept;

// The least opacity a user theme gives a part (USER_THEMES.md section 8,
// decision 7), so that no user theme hides a control outright.
inline constexpr double user_theme_minimum_opacity = 0.5;

// The rule position of a diagnostic about the sheet itself rather than one
// of its rules.
inline constexpr std::size_t sheet_diagnostic = static_cast<std::size_t>(-1);

struct RuleDiagnostic final {
    RuleDiagnosticKind kind{RuleDiagnosticKind::rule_empty};
    // The rule's position in the sheet and its label (sheet_diagnostic and
    // empty for the sheet).
    std::size_t rule{};
    std::string label{};
    // The declaration's property, empty for a whole-rule diagnostic.
    std::string property{};
    // For expression_invalid, why the expression does not compile.
    ExpressionDefect expression{ExpressionDefect::none};
};

// A sheet compiled against a registry: the rules that survived, each
// declaration checked, in the order the cascade visits them. Immutable once
// made; a ThemeEngine owns one. Compiling never fails as a whole: an invalid
// declaration or rule is dropped with a diagnostic and the rest stands, so a
// broken rule cannot take the theme's other rules down with it.
class CompiledThemeRules final {
public:
    CompiledThemeRules();
    CompiledThemeRules(const CompiledThemeRules& other);
    CompiledThemeRules& operator=(const CompiledThemeRules& other);
    CompiledThemeRules(CompiledThemeRules&& other) noexcept = default;
    CompiledThemeRules& operator=(CompiledThemeRules&& other) noexcept = default;
    ~CompiledThemeRules() = default;

    // Compiles `sheet` against `registry`, which must outlive the result.
    [[nodiscard]] static CompiledThemeRules compile(const ThemeRuleSheet& sheet, const PrimitiveRegistry& registry);
    // Compiles the engine's built-in rules (built_in_theme_rules, at
    // CascadeLevel::built_in, below every authored rule) and then `sheet`.
    // The theme engine's rules are made this way. Diagnostics name the
    // sheet's rules by their positions in `sheet`.
    [[nodiscard]] static CompiledThemeRules compile_with_built_in(const ThemeRuleSheet& sheet,
                                                                  const PrimitiveRegistry& registry);
    // Compiles one control's inline style (T3 P9, R2-10): rules at
    // `inline_style` and `important_inline_style` only, each without a
    // stable ID, since the control owns them and consults them for itself
    // alone (PartFacts::inline_rules). The sheet publishes no values.
    // Compiled against `theme` (P12b), the style also holds copies of the
    // theme's rules that read its custom properties (each with a
    // source_position, at the theme's authored levels): the theme's rules
    // are then passed over wherever the style's definition of a name they
    // read wins, while the style is used with `theme` itself (the same
    // compilation, by serial). `theme` is read during the call only.
    [[nodiscard]] static CompiledThemeRules compile_inline(const ThemeRuleSheet& sheet,
                                                           const PrimitiveRegistry& registry,
                                                           const CompiledThemeRules* theme = nullptr);
    // Compiles a user theme (USER_THEMES.md section 4): rules at
    // `user_theme` and `important_user_theme` only, a set of their own
    // that every part of a program's windows consults beside its theme's
    // (PartFacts::user_rules). Its opacity is at least
    // user_theme_minimum_opacity. It publishes no values, so its
    // expressions read the engine inputs alone, and its custom properties
    // are its own.
    [[nodiscard]] static CompiledThemeRules compile_user(const ThemeRuleSheet& sheet,
                                                         const PrimitiveRegistry& registry);
    // The serial of the theme an inline style was compiled against, 0 for
    // none (P12b).
    [[nodiscard]] std::uint64_t theme_serial() const noexcept { return theme_serial_; }
    // How many custom property names the rules read (P12b).
    [[nodiscard]] std::size_t read_name_count() const noexcept { return read_names_.size(); }
    // Whether the rules are a control's inline style (compile_inline).
    [[nodiscard]] bool inline_style() const noexcept { return inline_style_; }
    // Whether the rules are a user theme (compile_user).
    [[nodiscard]] bool user_theme() const noexcept { return user_theme_; }
    // How many of the rules are the engine's built-in rules.
    [[nodiscard]] std::size_t built_in_rule_count() const noexcept { return built_in_rules_; }

    // Whether a rule has ancestor compounds (selector step 4): its facts
    // need the control's ancestors, and a change to an ancestor's type, ID
    // or classes, or a move, re-resolves the control's subtree.
    [[nodiscard]] bool uses_ancestors() const noexcept { return uses_ancestors_; }

    [[nodiscard]] bool empty() const noexcept { return rules_.empty(); }
    [[nodiscard]] std::size_t rule_count() const noexcept { return rules_.size(); }
    // How many scoped custom property names the rules' conditions read
    // (P12).
    [[nodiscard]] std::size_t scoped_name_count() const noexcept { return scoped_names_.size(); }
    [[nodiscard]] const std::vector<RuleDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

    // The published values the sheet's expressions read, in position order
    // (an invalid name is kept empty, so later positions stay).
    [[nodiscard]] const std::vector<std::string>& published_values() const noexcept { return published_values_; }
    // The position of the published value `name`, or none when the sheet
    // does not read it.
    [[nodiscard]] std::optional<ExpressionInputIndex> published_value_input(std::string_view name) const noexcept;
    // How many inputs the sheet's expressions are evaluated with: the
    // engine inputs and the sheet's published values.
    [[nodiscard]] std::size_t input_count() const noexcept {
        const std::size_t count = engine_input_count + published_values_.size();
        return count;
    }

    // What a part is: the facts its selectors are matched against. The
    // views are borrowed for the call.
    struct PartFacts final {
        std::string_view control_type{};
        std::string_view stable_id{};
        std::span<const std::string> classes{};
        // Empty for an unkeyed part.
        std::string_view part{};
        PartRole role{PartRole::region};
        AnatomyStateSet states{};
        // The part's window (EC3): a rule matches only where its media
        // condition holds.
        MediaFacts media{};
        // The part's control's inline style (compile_inline), or null: its
        // rules join the cascade at their levels, after the theme's rules
        // of the same level and specificity. Borrowed for the call.
        const CompiledThemeRules* inline_rules{};
        // The program's user theme (compile_user), or null: its rules join
        // the cascade at the user levels. Borrowed for the call.
        const CompiledThemeRules* user_rules{};
        // Forced high contrast's pass (E1): the part's drawing (its
        // primitive and parameters, its material and recipe fields) comes
        // from the important_inline_style rules alone, every lower level
        // lying under the pass; its presentation (transitions, ambient
        // effect, blend and font) from every matching rule.
        bool forced_high_contrast{};
        // The surface whose rules are resolved, or none for a control's
        // part: a rule matches only facts of its own surface kind.
        SurfaceKind surface{SurfaceKind::none};
        // The control visual family the part is resolved for (RuleSelector::
        // family), or empty where none is known: a `:family()` rule then
        // does not match.
        std::string_view family{};
        // The part's control's ancestors, its parent first, and their keys
        // (fill_ancestor_facts): read only by rules with ancestor compounds
        // (uses_ancestors). Borrowed for the call.
        std::span<const AncestorFacts> ancestors{};
        AncestorBloom ancestor_bloom{};
    };

    // Why a part resolved as it did.
    enum class Outcome : std::uint8_t {
        // A rule selected a primitive; `primitive` and `parameters` are set.
        primitive,
        // No rule selects a primitive for the part.
        no_rule,
        // The winning rule selects `built_in`.
        built_in_selected,
        // The selected primitive does not paint the part's role.
        role_mismatch,
    };

    // One state of a compiled machine (see PresentationState).
    struct CompiledPresentationState final {
        AnatomyStateSet when{};
        double target{};
        std::optional<double> enter_from{};
        std::vector<MotionStop> stops{};
        double target_em{};
        double enter_from_em{};
    };

    // A checked transition: an engine property, or the parameter at
    // `parameter`. `machine` numbers it among the compilation's machines,
    // so that a part can tell one machine from another (PresentationState:
    // a state of another machine is another state).
    struct CompiledTransition final {
        PresentationProperty property{PresentationProperty::parameter};
        std::size_t parameter{};
        std::vector<CompiledPresentationState> states{};
        MotionCurve curve{};
        std::size_t machine{};
        // Whether a state's value is in em (P8c).
        bool reads_em{};
    };

    struct Resolution final {
        Outcome outcome{Outcome::no_rule};
        const EnginePrimitive* primitive{};
        PrimitiveParameterBlock parameters{};
        // The level of the declaration that chose the primitive (or
        // built_in); built_in when no rule did.
        CascadeLevel level{CascadeLevel::built_in};
        // The expression whose value a parameter takes, by the parameter's
        // position, where the winning declaration of the parameter is one;
        // null elsewhere. The expressions belong to the compiled rules.
        std::array<const EngineExpression*, maximum_primitive_parameters> expressions{};
        // Each number parameter's em coefficient (P8c), by its position:
        // the engine adds the part's em times it to the value, within the
        // parameter's range. `parameters_read_em` is whether any is not 0.
        std::array<double, maximum_primitive_parameters> parameter_em{};
        bool parameters_read_em{};
        // Where each colour parameter's winning declaration took its colour
        // (EC4), by the parameter's position; none for a default.
        std::array<ColourOrigin, maximum_primitive_parameters> parameter_origins{};
        // The inputs the part reads (E6): the primitive's declared inputs
        // and every input its expressions read, as expression input bits.
        std::uint64_t inputs_read{};
        // The part's transitions (E7): its opacity's and its scale's,
        // whatever draws it, and its primitive's parameters', when a
        // primitive does. They
        // belong to the compiled rules. `transition_count` are live.
        std::array<const CompiledTransition*, maximum_part_transitions> transitions{};
        std::size_t transition_count{};
        // The part's ambient effect, or null; it belongs to the rules.
        const AmbientEffect* ambient{};
        // The part's blend (blend_property): the last matching rule's.
        PartBlend blend{PartBlend::normal};
        // The part's font (font_property, P8c): the last matching rule's,
        // else the ui role.
        FontDeclaration font{};
        // The part's effect (effect_property): the last matching rule's
        // name, which belongs to the compiled rules, and whether any rule
        // declared one; `none` is declared with an empty name.
        std::string_view effect{};
        bool effect_declared{};
        // The part's effect colours (effect_primary_property,
        // effect_secondary_property): the last matching rule's of each, or
        // none.
        std::optional<EffectColourDeclaration> effect_primary{};
        std::optional<EffectColourDeclaration> effect_secondary{};
        // The winning declaration of each material field (EC1), by the
        // field's position, or null where no matching rule sets it; they
        // belong to the compiled rules. `materials_authored` is whether any
        // is set.
        std::array<const MaterialDeclaration*, material_field_count> materials{};
        bool materials_authored{};
        // The serial of the part's inline style (PartFacts::inline_rules),
        // or 0: the declarations above may belong to it.
        std::uint64_t inline_serial{};
        // The serial of the part's user theme (PartFacts::user_rules), or
        // 0, likewise.
        std::uint64_t user_serial{};
    };

    // The winning declaration of each recipe field (RecipeField) of the
    // rules matching a control, by the field's position, or null where no
    // matching rule sets it; they belong to the compiled rules. `authored`
    // is whether any is set.
    struct RecipeResolution final {
        std::array<const RecipeDeclaration*, recipe_field_count> fields{};
        // The cascade level of each winning declaration's rule, by the same
        // position; built_in where `fields` holds null. An authored colour
        // (PartPaint::authored_foreground) stands under the important ones.
        std::array<CascadeLevel, recipe_field_count> levels{};
        bool authored{};
    };

    // The recipe fields' cascade for a control: `facts` name no part and
    // the region role. Linear in the number of rules, and at once when no
    // rule sets a recipe field.
    [[nodiscard]] RecipeResolution resolve_recipe_fields(const PartFacts& facts) const noexcept;

    // Where one resolved field came from (T4 cascade spec, item 5).
    // `authored` is whether a rule set it; where none did, the built-in
    // recipe's value stands, at `built_in` in the built-in layer. `source`
    // and `selector` are the winning rule's provenance and selector, which
    // belong to the compiled rules (this table, or the facts' inline style
    // or user theme) and are null where no rule set the field; `origin` is
    // a colour's (ColourOrigin, none for a value that is not a colour).
    struct FieldProvenance final {
        bool authored{};
        CascadeLevel level{CascadeLevel::built_in};
        ThemeLayer layer{ThemeLayer::built_in};
        const RuleProvenance* source{};
        const RuleSelector* selector{};
        RuleSpecificity specificity{};
        ColourOrigin origin{};
    };

    // The provenance of every field of the part `facts` name: its
    // primitive parameters (by position; `parameter_count` are the chosen
    // primitive's, none when no rule selects one), its material fields and
    // its recipe fields (by their positions), as resolve(),
    // resolve_recipe_fields() and the primitive's parameters resolve them.
    struct PartProvenance final {
        std::array<FieldProvenance, maximum_primitive_parameters> parameters{};
        std::size_t parameter_count{};
        std::array<FieldProvenance, material_field_count> materials{};
        std::array<FieldProvenance, recipe_field_count> recipe_fields{};
    };

    // Fills `result` with the provenance of the part `facts` name, for an
    // editor's query: linear in the number of rules, allocating nothing.
    // Paint never calls it. The borrows in `result` stay valid while these
    // rules and the facts' inline style and user theme live.
    void resolve_provenance(const PartFacts& facts, PartProvenance& result) const noexcept;

    // The winning backdrop of the surface `facts` name (PartFacts::surface,
    // its root control's classes and ID): the last matching rule's, which
    // belongs to the compiled rules or to the user theme's, or null where no
    // matching rule declares one. `backdrop: none` wins as a chain of length
    // 0. Linear in the number of rules.
    [[nodiscard]] const BackdropDeclaration* resolve_backdrop(const PartFacts& facts) const noexcept;
    // The band of the window `facts` name (PartFacts::surface window, its
    // root control's classes and ID): each field from the last matching
    // rule that sets it, among the compiled rules and the user theme's.
    // Empty where none does, and for any surface but a window. Linear in
    // the number of rules.
    [[nodiscard]] BandDeclaration resolve_band(const PartFacts& facts) const noexcept;

    // The cascade for one part: the winning primitive declaration, then
    // each of that primitive's parameters from the winning declaration of
    // it among the matching rules that select the same primitive, else its
    // declared default; and each material field from its winning
    // declaration among all the matching rules. Linear in the number of
    // rules; tables are small, and a theme with no rules returns at once.
    [[nodiscard]] Resolution resolve(const PartFacts& facts) const noexcept;

    // This compilation's serial number, unique among the compilations of
    // the process: a cache keyed by the addresses of the rules'
    // declarations (an assembled material, EC1) compares it first. A copy
    // takes a new one, since its declarations lie elsewhere; a move keeps
    // it with the declarations.
    [[nodiscard]] std::uint64_t serial() const noexcept { return serial_; }

private:
    // A checked declaration: the primitive a rule selects (null for
    // built_in) or one parameter's value, by its index in the primitive's
    // declarations.
    struct CompiledDeclaration final {
        bool selects_primitive{};
        const EnginePrimitive* primitive{};
        std::size_t parameter{};
        PrimitiveParameterValue value{};
        // An expression declaration: `program` gives the value.
        bool expression{};
        EngineExpression program{};
        // A number value's em coefficient (P8c).
        double em{};
        // A colour value's origin (EC4).
        ColourOrigin origin{};
    };

    struct CompiledScoped final {
        std::uint32_t name{};
        std::uint32_t definition{custom_property_undefined};
    };

    // A part's winning definition of each scoped name, by its position:
    // the theme's names among the theme's definitions, and an inline
    // style's names among the style's (which holds the theme's
    // definitions of its names too, P12b); and, by the theme's read_names_
    // positions, the names whose inline definition wins, one bit each.
    struct ScopedWinners final {
        std::array<std::uint32_t, maximum_scoped_custom_properties> definitions{};
        std::array<std::uint32_t, maximum_scoped_custom_properties> inline_definitions{};
        std::array<std::uint64_t, maximum_read_custom_properties / 64U> inline_won{};
        // The user theme's names among its own definitions.
        std::array<std::uint32_t, maximum_scoped_custom_properties> user_definitions{};
    };

    struct CompiledRule final {
        CascadeLevel level{CascadeLevel::theme_sheet};
        RuleSpecificity specificity{};
        // Position in the authored sheet; later wins among equals.
        std::size_t order{};
        RuleSelector selector{};
        AnatomyStateSet states{};
        // The keys the selector's ancestor compounds name (selector step 4).
        AncestorBloom ancestor_keys{};
        // The primitive the rule selects, null when it selects built_in or
        // none; `selects` tells the two apart.
        bool selects{};
        const EnginePrimitive* primitive{};
        std::vector<CompiledDeclaration> parameters{};
        std::vector<CompiledTransition> transitions{};
        std::optional<AmbientEffect> ambient{};
        std::optional<PartBlend> blend{};
        std::optional<FontDeclaration> font{};
        // The rule's effect name (effect_property); empty for `none`.
        std::optional<std::string> effect{};
        // The rule's effect colours.
        std::optional<EffectColourDeclaration> effect_primary{};
        std::optional<EffectColourDeclaration> effect_secondary{};
        MediaCondition media{};
        // Checked material fields, at most one per field.
        std::vector<MaterialDeclaration> materials{};
        // Checked recipe fields, at most one per field.
        std::vector<RecipeDeclaration> recipe_fields{};
        // A surface rule's checked backdrop.
        std::optional<BackdropDeclaration> backdrop{};
        // A window rule's checked band.
        std::optional<BandDeclaration> band{};
        // Scoped custom properties (P12), by position in scoped_names_.
        std::vector<CompiledScoped> scoped_definitions{};
        std::vector<CompiledScoped> scoped_conditions{};
        // The names it reads, by position in read_names_ (P12b), and its
        // ThemeRule::source_position.
        std::vector<std::uint32_t> reads{};
        std::uint32_t source_position{};
        // ThemeRule::provenance, its layer the built-in one in the
        // engine's own table.
        RuleProvenance provenance{};
    };

    // Compiles `sheet`'s rules into `result` at their authored levels, or,
    // for the engine's own table, at CascadeLevel::built_in.
    static void add_rules(CompiledThemeRules& result, const ThemeRuleSheet& sheet, const PrimitiveRegistry& registry,
                          bool engine_table);
    // Numbers every compiled machine (CompiledTransition::machine), in rule
    // order, once the rules are final.
    void number_machines() noexcept;
    // One field's provenance from the rule whose declaration of it won,
    // drawn in `origin` (FieldProvenance; the borrows are the rule's).
    [[nodiscard]] static FieldProvenance field_provenance(const CompiledRule& rule, ColourOrigin origin) noexcept;
    // The winning rule's primitive and parameters into `result`.
    void resolve_primitive(const PartFacts& facts, const ScopedWinners& winners, const CompiledRule& chosen,
                           Resolution& result) const noexcept;
    // The resolved transitions, ambient effect and blend of the matching
    // rules into `result`.
    void resolve_transitions(const PartFacts& facts, const ScopedWinners& winners, Resolution& result) const noexcept;
    // The part's winning definition of each scoped name, the theme's and
    // its inline style's, and the names whose inline definition wins.
    void resolve_scoped(const PartFacts& facts, ScopedWinners& winners) const noexcept;
    // A rule's read names, mapped to read_names_ (which add_rules collects
    // first); false, with a diagnostic, for a rule to drop.
    static bool add_reads(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                          const CompiledThemeRules& result, CompiledRule& compiled);
    // The position of `name` in read_names_, if it is there.
    [[nodiscard]] std::optional<std::size_t> read_name_position(std::string_view name) const noexcept;
    // A rule's scoped definitions and conditions, mapped to scoped_names_;
    // false, with a diagnostic, for a rule to drop.
    static bool add_scoped(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                           CompiledThemeRules& result, CompiledRule& compiled);
    // The position of `name` in scoped_names_, if it is there.
    [[nodiscard]] std::optional<std::size_t> scoped_name_position(std::string_view name) const noexcept;
    // Adds the names `sheet`'s conditions read to scoped_names_.
    void collect_scoped_names(const ThemeRuleSheet& sheet);
    // A rule's transitions and ambient effect, checked; a defect drops the
    // one transition or the effect with a diagnostic.
    static void add_transitions(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                                CompiledRule& compiled);
    static void add_ambient(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                            CompiledRule& compiled);
    // A rule's blend declaration, checked; a defect drops it with a
    // diagnostic.
    static void add_blend(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                          const RuleDeclaration& declaration, CompiledRule& compiled);
    // A rule's effect colour declaration, checked likewise.
    static void add_effect_colour(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                                  const RuleDeclaration& declaration, CompiledRule& compiled);
    // A rule's effect declaration, checked likewise.
    static void add_effect(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                           const RuleDeclaration& declaration, CompiledRule& compiled);
    // A rule's font declaration (P8c), checked likewise.
    static void add_font(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                         const RuleDeclaration& declaration, CompiledRule& compiled);
    // A rule's material fields, checked; a defect or a second declaration
    // of one field drops it with a diagnostic.
    static void add_materials(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                              CompiledRule& compiled);
    // The winning material fields of the matching rules into `result`.
    void resolve_materials(const PartFacts& facts, const ScopedWinners& winners, Resolution& result) const noexcept;
    // A rule's recipe fields, checked; a defect, a second declaration of
    // one field, or a selector naming a part or a role drops it with a
    // diagnostic.
    static void add_recipe_fields(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                                  CompiledRule& compiled);
    // A rule's backdrop, checked; one on a rule that selects no surface, or
    // an invalid chain, is dropped with a diagnostic.
    static void add_backdrop(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                             CompiledRule& compiled);
    // A rule's band, checked; one on a rule that selects no window, or an
    // invalid one, is dropped with a diagnostic.
    static void add_band(std::vector<RuleDiagnostic>& diagnostics, std::size_t index, const ThemeRule& rule,
                         CompiledRule& compiled);

    [[nodiscard]] static bool matches(const CompiledRule& rule, const PartFacts& facts) noexcept;
    // Whether one `:not()` argument matches the part: every field it names
    // holds.
    [[nodiscard]] static bool exclusion_matches(const RuleExclusion& exclusion, const PartFacts& facts) noexcept;
    // Whether the selector's ancestor compounds match the part's ancestors.
    [[nodiscard]] static bool ancestors_match(const CompiledRule& rule, const PartFacts& facts) noexcept;
    // These rules, a part's user theme's and its inline style's, merged in
    // ascending precedence.
    class RuleWalk;
    // Raises a user theme's opacity to user_theme_minimum_opacity.
    static void raise_user_opacity(std::vector<RuleDiagnostic>& diagnostics, std::size_t index,
                                   const ThemeRule& rule, CompiledRule& compiled);

    // Sorted by precedence, lowest first: level, then specificity, then
    // order. The cascade's winner for a property is the last match.
    std::vector<CompiledRule> rules_{};
    std::vector<RuleDiagnostic> diagnostics_{};
    std::vector<std::string> published_values_{};
    std::size_t built_in_rules_{};
    std::uint64_t serial_{};
    // Whether any rule sets a recipe field (resolve_recipe_fields).
    bool recipe_fields_set_{};
    // Whether the rules are a control's inline style.
    bool inline_style_{};
    // Whether the rules are a user theme.
    bool user_theme_{};
    bool uses_ancestors_{};
    // The scoped custom property names the rules' conditions read (P12),
    // at most maximum_scoped_custom_properties.
    std::vector<std::string> scoped_names_{};
    // The custom property names the rules read (P12b), at most
    // maximum_read_custom_properties.
    std::vector<std::string> read_names_{};
    // An inline style's (P12b): the serial of the theme compiled against,
    // and for each of its scoped names the position of the name in that
    // theme's read_names_, or no_read_name.
    static constexpr std::uint32_t no_read_name = 0xFFFF'FFFFU;
    std::uint64_t theme_serial_{};
    std::vector<std::uint32_t> theme_read_positions_{};
    // An inline style's (P12b): for each of its scoped names the position
    // of the name in that theme's scoped_names_, or no_read_name. Where
    // none of the style's definitions holds for a part, the theme's winner
    // does, as for a name the style reads but does not define.
    std::vector<std::uint32_t> theme_scoped_positions_{};
};

// The engine's built-in rules (E7), compiled below every theme's: the
// docking drop tiles' fade (T2 GIVEN C2-1) and the ribbon's dithered rise
// (D3-1, G4-1). Static storage, made once on first use.
[[nodiscard]] const ThemeRuleSheet& built_in_theme_rules();

} // namespace gui_forms
