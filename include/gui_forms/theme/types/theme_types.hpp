#pragma once

#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/surface_material.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

struct BasicControlStyle final {
    Color face{Color::rgba(229, 234, 239)};
    Color face_light{Color::rgba(247, 249, 251)};
    Color paper{Color::rgba(255, 255, 255)};
    Color highlight{Color::rgba(255, 255, 255)};
    Color border{Color::rgba(148, 162, 175)};
    Color dark_border{Color::rgba(76, 94, 111)};
    Color text{Color::rgba(27, 39, 51)};
    Color disabled_text{Color::rgba(132, 143, 153)};
    Color accent{Color::rgba(38, 114, 185)};
    Color accent_light{Color::rgba(216, 235, 249)};
    Color link{Color::rgba(25, 82, 139)};
    Color visited_link{Color::rgba(93, 65, 145)};
    friend constexpr bool operator==(const BasicControlStyle& left,
                                     const BasicControlStyle& right) noexcept {
        return left.face == right.face && left.face_light == right.face_light &&
               left.paper == right.paper && left.highlight == right.highlight &&
               left.border == right.border &&
               left.dark_border == right.dark_border && left.text == right.text &&
               left.disabled_text == right.disabled_text &&
               left.accent == right.accent &&
               left.accent_light == right.accent_light &&
               left.link == right.link &&
               left.visited_link == right.visited_link;
    }
};

// The recipe families a theme supplies. `tool_tip` (owner decision RB-38,
// design language 12.9) is the face and edge of plain and enhanced
// tooltips, beside `card` and `menu_item`. `link` and `visited_link` (owner
// decision W-Q5) are a hyperlink's text, chosen by the link's visited flag;
// hover keeps the normal link colour, and a link draws no plate.
enum class ControlVisualRole : std::uint8_t {
    window, panel, card, button, accent_button, command_button, choice,
    editor, menu_item, selection, progress, tool_tip, link, visited_link, count,
};
enum class ControlSurfaceState : std::uint8_t {
    normal, hot, pressed, pending, invalid, disabled, deactivated, count,
};
enum class ControlVisualStatus : std::uint8_t { normal, pending, invalid };

struct ControlVisualContext final {
    ControlSurfaceState surface{ControlSurfaceState::normal};
    bool selected{};
    bool focused{};
    bool defaulted{};
    bool high_contrast{};
    // The window prefers the dark colour scheme (PresentationSettings::
    // colour_scheme, stage T4 phase 5). A theme with a derived dark variant
    // resolves its dark records; high contrast wins over it.
    bool dark{};
    friend constexpr bool operator==(const ControlVisualContext& left,
                                     const ControlVisualContext& right) noexcept {
        return left.surface == right.surface &&
               left.selected == right.selected && left.focused == right.focused &&
               left.defaulted == right.defaulted &&
               left.high_contrast == right.high_contrast && left.dark == right.dark;
    }
};
// A text shadow (E9.6, CSS `text-shadow`): the text's glyphs, moved by
// `offset` logical units (each axis within ±maximum_text_shadow_offset),
// blurred by `blur_radius` (the shadows' blur-radius convention, 0 to
// maximum_text_shadow_blur; both limits are the theme rules') and drawn in
// `color` under the glyphs. Data only in T2: the theme carries it, and the
// text stage (M4/M9) draws it, with a first-party blur so that it is the
// same on every host.
struct ControlTextShadow final {
    Point offset{};
    double blur_radius{};
    Color color{Color::rgba(0, 0, 0, 0)};

    friend bool operator==(const ControlTextShadow& left, const ControlTextShadow& right) noexcept {
        return left.offset == right.offset && left.blur_radius == right.blur_radius && left.color == right.color;
    }
};
struct ControlVisualRecipe final {
    SurfaceMaterial material;
    Color text{Color::rgba(27, 39, 51)};
    Color muted_text{Color::rgba(132, 143, 153)};
    Color glyph{Color::rgba(27, 39, 51)};
    Color focus_ring{Color::rgba(38, 114, 185)};
    Color default_ring{Color::rgba(25, 82, 139)};
    double focus_width{1.0};
    double focus_offset{};
    bool authored_focus_outline{};
    // Whether the focus ring, the house ring or the authored outline, is
    // drawn as dots (design language: the default theme's ring is dotted):
    // square dots one focus width across, one every two widths, on the
    // ring's square path (a dotted ring ignores the corner radius).
    bool dotted_focus{};
    double default_width{2.0};
    Point visual_offset{};
    Point pressed_content_offset{1.0, 1.0};
    // The shadow under the recipe's text, if any (E9.6, data only).
    std::optional<ControlTextShadow> text_shadow{};
    friend bool operator==(const ControlVisualRecipe& left,
                           const ControlVisualRecipe& right) noexcept(noexcept(
        left.material == right.material && left.text == right.text &&
        left.muted_text == right.muted_text && left.glyph == right.glyph &&
        left.focus_ring == right.focus_ring &&
        left.default_ring == right.default_ring &&
        left.focus_width == right.focus_width &&
        left.focus_offset == right.focus_offset &&
        left.authored_focus_outline == right.authored_focus_outline &&
        left.dotted_focus == right.dotted_focus &&
        left.default_width == right.default_width &&
        left.visual_offset == right.visual_offset &&
        left.pressed_content_offset == right.pressed_content_offset &&
        left.text_shadow == right.text_shadow)) {
        return left.material == right.material && left.text == right.text &&
               left.muted_text == right.muted_text &&
               left.glyph == right.glyph &&
               left.focus_ring == right.focus_ring &&
               left.default_ring == right.default_ring &&
               left.focus_width == right.focus_width &&
               left.focus_offset == right.focus_offset &&
               left.authored_focus_outline == right.authored_focus_outline &&
               left.dotted_focus == right.dotted_focus &&
               left.default_width == right.default_width &&
               left.visual_offset == right.visual_offset &&
               left.pressed_content_offset == right.pressed_content_offset &&
               left.text_shadow == right.text_shadow;
    }
};

// A control's recipe for one role and state (T3 P6f): the theme's built-in
// recipe (the floor, Theme::resolve(role, context), borrowed from the
// theme) or, when the theme's rules set a recipe field for the control
// (RecipeField), the floor's copy with those fields in place, which this
// value owns. The copy is made only then, so a theme whose rules set no
// recipe field costs nothing. A value for one paint or measurement: it
// borrows the theme, so it is not kept past the theme's use. `recipe()`
// borrows from the value, so it is read from a named value and never from
// a temporary.
class ResolvedControlRecipe final {
public:
    explicit ResolvedControlRecipe(const ControlVisualRecipe& floor) noexcept : floor_(&floor) {}
    ResolvedControlRecipe(const ControlVisualRecipe& floor, ControlVisualRecipe&& authored,
                          bool important_text = false) noexcept
        : floor_(&floor), authored_(std::move(authored)), important_text_(important_text) {}
    // The floor is borrowed, so it is never a temporary.
    explicit ResolvedControlRecipe(const ControlVisualRecipe&& floor) = delete;
    ResolvedControlRecipe(const ControlVisualRecipe&& floor, ControlVisualRecipe&& authored,
                          bool important_text = false) = delete;

    [[nodiscard]] const ControlVisualRecipe& recipe() const& noexcept {
        if (authored_) return *authored_;
        return *floor_;
    }
    const ControlVisualRecipe& recipe() const&& = delete;
    // Whether a rule's recipe field changed the floor.
    [[nodiscard]] bool authored() const noexcept { return authored_.has_value(); }
    // Whether the text colour was declared at an important level
    // (CascadeLevel::important_user_theme or above), where it stands over
    // a part's authored foreground (owner decision T4-1).
    [[nodiscard]] bool important_text() const noexcept { return important_text_; }

private:
    const ControlVisualRecipe* floor_;
    std::optional<ControlVisualRecipe> authored_{};
    bool important_text_{};
};

inline constexpr std::size_t control_visual_role_count =
    static_cast<std::size_t>(ControlVisualRole::count);
// Registered (snake_case) name of a recipe family, as the schema's
// vocabulary lists it; empty for `count`.
[[nodiscard]] std::string_view control_visual_role_name(ControlVisualRole role) noexcept;
inline constexpr std::size_t control_surface_state_count =
    static_cast<std::size_t>(ControlSurfaceState::count);

struct ControlStateRecipes final {
    std::array<ControlVisualRecipe, control_surface_state_count> values;
    [[nodiscard]] static ControlStateRecipes from_parts(
        const ControlVisualRecipe* values, std::size_t value_count);
    [[nodiscard]] const ControlVisualRecipe& resolve(
        ControlSurfaceState state) const noexcept {
        return values[static_cast<std::size_t>(state)];
    }
    friend bool operator==(const ControlStateRecipes& left,
                           const ControlStateRecipes& right) noexcept(noexcept(
        left.values == right.values)) {
        return left.values == right.values;
    }
};

[[nodiscard]] bool valid_control_visual_recipe(
    const ControlVisualRecipe& value) noexcept;
[[nodiscard]] bool valid_control_state_recipes(
    const ControlStateRecipes& value) noexcept;

struct ControlRoleRecipes final {
    std::array<ControlVisualRecipe, control_surface_state_count> ordinary;
    std::array<ControlVisualRecipe, control_surface_state_count> selected;
    std::array<ControlVisualRecipe, control_surface_state_count> high_contrast;
    std::array<ControlVisualRecipe, control_surface_state_count>
        high_contrast_selected;
    // The dark variant's records (stage T4 phase 5): derived from the
    // ordinary and selected ones when the theme asks for a derived dark
    // variant (ThemeDefinition::dark_variant), otherwise copies of them.
    // Theme::create fills them; authored values are replaced.
    std::array<ControlVisualRecipe, control_surface_state_count> dark;
    std::array<ControlVisualRecipe, control_surface_state_count> dark_selected;
    friend bool operator==(const ControlRoleRecipes& left,
                           const ControlRoleRecipes& right) noexcept(noexcept(
        left.ordinary == right.ordinary && left.selected == right.selected &&
        left.high_contrast == right.high_contrast &&
        left.high_contrast_selected == right.high_contrast_selected)) {
        return left.ordinary == right.ordinary &&
               left.selected == right.selected &&
               left.high_contrast == right.high_contrast &&
               left.high_contrast_selected == right.high_contrast_selected &&
               left.dark == right.dark && left.dark_selected == right.dark_selected;
    }
};

// The forced high-contrast pass (T2 plan, E4; design language 5.8):
// `recipes` with its high_contrast and high_contrast_selected records
// derived from its ordinary ones. GUI.Forms' own high-contrast mode keeps
// each state's geometry and cues (corner radius, edge width, focus and
// default rings, offsets) and rebuilds the material as one solid plate
// inside one solid edge from a near-colourless palette by state, dropping
// gradients, images, keylines, per-edge borders and shadows. Selected parts
// take a white plate and black text inside their state's edge.
// Theme::create applies it to every family, so authored high-contrast
// records are replaced.
[[nodiscard]] ControlRoleRecipes derive_high_contrast_recipes(ControlRoleRecipes recipes);

// The dark pass (stage T4 phase 5; design language 5.8 rule 3): `recipes`
// with its dark and dark_selected records derived from its ordinary and
// selected ones by the dark colour rule, for a family of role `family`.
// Geometry, cues and structure are kept; every colour is mapped by what it
// paints (a face by its family: stock, well, chrome or accent; text, lines,
// cues, highlights and shades by their own rules), so the light stays upper
// left and inset stays inset.
[[nodiscard]] ControlRoleRecipes derive_dark_recipes(ControlRoleRecipes recipes, ControlVisualRole family);

// The colour forced high contrast draws in place of one a theme declared
// (T3 EC4, owner decision D3 of 2026-10-28), for a part in `state`,
// `selected` or not, from the same palette as derive_high_contrast_recipes:
// - a literal, by its position: a fill takes the state's plate, an edge
//   its edge and text its text (a selected part's: a white plate and black
//   text); a colour with no position takes the plate;
// - a role's colour, by its side: a foreground takes the text, or the
//   edge in an edge position, and a background the plate; a single role
//   of the relief ramp (and `shadow`) takes the plate and a line the edge;
//   except that the disabled roles' foregrounds take the disabled state's
//   text or edge, `invalid-ink-on-tint`'s foreground the invalid edge,
//   `focus` and `selection-line` the hot edge, and the selection and
//   on-accent pairs black on white, as a selected part.
// The forced high-contrast pass that draws with it is T3 P9's (EC6).
[[nodiscard]] Color high_contrast_colour(ColourOrigin origin, ColourPosition position, ControlSurfaceState state,
                                         bool selected) noexcept;

// The recipes of every control visual role, indexed by ControlVisualRole,
// held out of line. They are most of a theme's description (one family of
// four state tables per role, some 100 KB), so holding them on the heap
// keeps a ThemeDefinition small enough to be an ordinary value.
//
// The table always holds exactly one family per role: it is made with
// default recipes, and a copy copies every recipe. Moving copies too (no
// move operations are declared), so a moved-from table still holds its
// recipes rather than none.
class ControlRoleTable final {
public:
    using Families = std::array<ControlRoleRecipes, control_visual_role_count>;

    // Allocates the families, value-initialized in place on the heap.
    ControlRoleTable() : families_(std::make_unique<Families>()) {}
    ControlRoleTable(const ControlRoleTable& other)
        : families_(std::make_unique<Families>(*other.families_)) {}
    // Copies every recipe into this table's existing storage.
    ControlRoleTable& operator=(const ControlRoleTable& other) {
        if (this != &other) *families_ = *other.families_;
        return *this;
    }

    // `role` is a ControlVisualRole's index, below control_visual_role_count.
    [[nodiscard]] ControlRoleRecipes& operator[](std::size_t role) noexcept { return (*families_)[role]; }
    [[nodiscard]] const ControlRoleRecipes& operator[](std::size_t role) const noexcept {
        return (*families_)[role];
    }
    [[nodiscard]] static constexpr std::size_t size() noexcept { return control_visual_role_count; }
    [[nodiscard]] Families::iterator begin() noexcept { return (*families_).begin(); }
    [[nodiscard]] Families::iterator end() noexcept { return (*families_).end(); }
    [[nodiscard]] Families::const_iterator begin() const noexcept { return (*families_).begin(); }
    [[nodiscard]] Families::const_iterator end() const noexcept { return (*families_).end(); }

    friend bool operator==(const ControlRoleTable& left, const ControlRoleTable& right) noexcept(
        noexcept(std::declval<const Families&>() == std::declval<const Families&>())) {
        return *left.families_ == *right.families_;
    }

private:
    // Never null.
    std::unique_ptr<Families> families_;
};

struct ThemeSpacingTokens final {
    double micro{2.0};
    double xsmall{4.0};
    double small{6.0};
    double medium{8.0};
    double large{12.0};
    double xlarge{16.0};
    double section{24.0};
    friend constexpr bool operator==(const ThemeSpacingTokens& left,
                                     const ThemeSpacingTokens& right) noexcept {
        return left.micro == right.micro && left.xsmall == right.xsmall &&
               left.small == right.small && left.medium == right.medium &&
               left.large == right.large && left.xlarge == right.xlarge &&
               left.section == right.section;
    }
};
struct ThemeGeometryTokens final {
    double compact_control_height{24.0};
    double control_height{30.0};
    double large_control_height{36.0};
    double minimum_touch_target{40.0};
    double splitter_width{3.0};
    double splitter_hit_width{9.0};
    double navigation_extent{260.0};
    double navigation_minimum{120.0};
    double content_minimum{240.0};
    double compact_breakpoint{720.0};
    friend constexpr bool operator==(const ThemeGeometryTokens& left,
                                     const ThemeGeometryTokens& right) noexcept {
        return left.compact_control_height == right.compact_control_height &&
               left.control_height == right.control_height &&
               left.large_control_height == right.large_control_height &&
               left.minimum_touch_target == right.minimum_touch_target &&
               left.splitter_width == right.splitter_width &&
               left.splitter_hit_width == right.splitter_hit_width &&
               left.navigation_extent == right.navigation_extent &&
               left.navigation_minimum == right.navigation_minimum &&
               left.content_minimum == right.content_minimum &&
               left.compact_breakpoint == right.compact_breakpoint;
    }
};
struct ThemeTypographyTokens final {
    FontSpec control{FontRole::control, 12.0, 400, false, 0.24};
    FontSpec field{FontRole::content, 12.0, 400, false};
    FontSpec caption{FontRole::content, 10.0, 400, false};
    FontSpec heading{FontRole::control, 14.0, 650, false, 0.18};
    FontSpec title{FontRole::control, 20.0, 700, false, 0.12};
    FontSpec monospace{FontRole::monospace, 12.0, 400, false};
    friend constexpr bool operator==(const ThemeTypographyTokens& left,
                                     const ThemeTypographyTokens& right) noexcept {
        return left.control == right.control && left.field == right.field &&
               left.caption == right.caption && left.heading == right.heading &&
               left.title == right.title &&
               left.monospace == right.monospace;
    }
};
struct ThemeMotionTokens final {
    std::chrono::milliseconds quick{100};
    std::chrono::milliseconds standard{180};
    std::chrono::milliseconds emphasized{280};
    std::chrono::milliseconds busy_cycle{1400};
    friend constexpr bool operator==(const ThemeMotionTokens& left,
                                     const ThemeMotionTokens& right) noexcept {
        return left.quick == right.quick &&
               left.standard == right.standard &&
               left.emphasized == right.emphasized &&
               left.busy_cycle == right.busy_cycle;
    }
};
struct ThemeStructureTokens final {
    ThemeSpacingTokens spacing;
    ThemeGeometryTokens geometry;
    ThemeTypographyTokens typography;
    ThemeMotionTokens motion;
    friend constexpr bool operator==(const ThemeStructureTokens& left,
                                     const ThemeStructureTokens& right) noexcept {
        return left.spacing == right.spacing &&
               left.geometry == right.geometry &&
               left.typography == right.typography &&
               left.motion == right.motion;
    }
};
// The accent ramp a theme offers parts that carry an AccentIndex (design
// language 8.5). Provisional: six translucent tints (amber, green, violet,
// orange, red, teal) laid over a part's material, until the default theme
// (stage T4) defines its own ramp and the engine (T2) its tint.
[[nodiscard]] inline std::vector<Color> provisional_accent_ramp() {
    std::vector<Color> ramp{
        Color::rgba(241, 196, 15, 72), Color::rgba(88, 170, 80, 72),
        Color::rgba(142, 108, 196, 72), Color::rgba(230, 126, 34, 72),
        Color::rgba(208, 72, 72, 72), Color::rgba(38, 166, 154, 72),
    };
    return ramp;
}

// The overlay shadow (owner decision RB-37, design language 12.9; the E4
// level of 3.4 scaled to small overlays): a soft shadow cast down and right,
// away from the one upper-left light, by surfaces that really overlap
// content (tooltips, popups, menus). The `shadow` part role paints it around
// the casting surface. Two layers, in the units of MaterialShadow (blur is
// a CSS blur radius): a wide soft layer and a close contact layer, each
// offset right by at most half its drop (3.3). Provisional starting tokens
// until the default theme (stage T4) owns them.
[[nodiscard]] inline std::vector<MaterialShadow> provisional_overlay_shadow() {
    std::vector<MaterialShadow> layers{
        MaterialShadow{.offset = {2.0, 4.0}, .blur_radius = 10.0, .spread = 0.0,
                       .color = Color::rgba(20, 30, 42, 64), .inset = false},
        MaterialShadow{.offset = {0.5, 1.0}, .blur_radius = 3.0, .spread = 0.0,
                       .color = Color::rgba(20, 30, 42, 56), .inset = false},
    };
    return layers;
}

// One backplane base: the material a window's backplane is drawn from
// while it is granted `backdrop` (ThemeWindowBackdrop).
struct ThemeBackdropBase final {
    WindowBackdrop backdrop{WindowBackdrop::transparent};
    SurfaceMaterial material{};

    friend bool operator==(const ThemeBackdropBase& left, const ThemeBackdropBase& right) noexcept {
        return left.backdrop == right.backdrop && left.material == right.material;
    }
};

// The theme's window backdrop (stage T5, owner decision T5-Q8, option 1):
// what a window showing the theme asks the compositor for, and the base its
// backplane is drawn from over each backdrop the window may be granted
// (notes:planning/stages/T5-compositor/PLAN.md §4.1).
struct ThemeWindowBackdrop final {
    // The request a window makes while its application has made none
    // (Window::request_backdrop). Value-initialized: `opaque` alone, so a
    // theme that says nothing asks for nothing. Theme::create refuses a
    // request window_backdrop_request_valid refuses.
    WindowBackdropRequest request{};
    // At most one base per non-opaque backdrop, in any order; held out of
    // line, so the definition stays small. Over a granted non-opaque
    // backdrop the window's backplane is drawn from that backdrop's base,
    // typically a partly clear tint over a material, or clear around a
    // drawn shape over `transparent`. Without one it is drawn from the
    // window recipe's material, as over `opaque`, which has no base.
    std::vector<ThemeBackdropBase> bases{};

    // The base for `backdrop`, or null when the theme gives none.
    [[nodiscard]] const SurfaceMaterial* base_for(WindowBackdrop backdrop) const noexcept {
        for (const ThemeBackdropBase& base : bases) {
            if (base.backdrop == backdrop) return &base.material;
        }
        return nullptr;
    }

    friend bool operator==(const ThemeWindowBackdrop& left, const ThemeWindowBackdrop& right) noexcept {
        return left.request == right.request && left.bases == right.bases;
    }
};

// Whether a theme has a dark variant (stage T4 phase 5): none (a dark
// colour scheme resolves the ordinary records) or derived from the
// ordinary records by the dark colour rule (derive_dark_recipes).
enum class ThemeDarkVariant : std::uint8_t { none, derived };

// A theme's whole authored description. Its role recipes are held out of
// line (ControlRoleTable), so the definition itself is a small value, but
// copying one still copies every recipe: pass it by reference, and let
// Theme::create make the theme's own copy.
struct ThemeDefinition final {
    std::string id{"windows-professional"};
    BasicControlStyle compatibility;
    ControlRoleTable roles;
    ThemeStructureTokens structure;
    // At least one colour; Theme::create refuses an empty ramp.
    std::vector<Color> accent_ramp{provisional_accent_ramp()};
    // The overlay shadow's layers, outer (not inset) and at most
    // SurfaceMaterial::maximum_shadows; empty casts none.
    std::vector<MaterialShadow> overlay_shadow{provisional_overlay_shadow()};
    // The overlay shadow in high contrast, under the same rules. Empty by
    // default: high contrast draws no cast shadow, and the overlay's
    // high-contrast edge separates it from the content below.
    std::vector<MaterialShadow> high_contrast_overlay_shadow{};
    // The theme's rules (the cascade, T2 E4): which primitive draws which
    // part. Empty by default: every part keeps the built-in recipe. They are
    // compiled when the theme is made; an invalid rule or declaration is
    // dropped with a diagnostic (Theme::engine().rules().diagnostics()).
    ThemeRuleSheet rules{};
    // Whether the rules are a program's theme or a user theme's (T3 user
    // themes): a user theme's rules are at the user levels only, and its
    // other fields are unused while it is layered under a program's.
    ThemeRuleSet rule_set{ThemeRuleSet::program};
    // The backdrop request and backplane bases (stage T5). Empty by
    // default: an opaque window, drawn as before.
    ThemeWindowBackdrop window_backdrop{};
    // The dark variant (stage T4 phase 5). None by default.
    ThemeDarkVariant dark_variant{ThemeDarkVariant::none};
    friend bool operator==(const ThemeDefinition& left,
                           const ThemeDefinition& right) noexcept(noexcept(
        left.id == right.id && left.compatibility == right.compatibility &&
        left.roles == right.roles && left.structure == right.structure)) {
        return left.id == right.id &&
               left.compatibility == right.compatibility &&
               left.roles == right.roles && left.structure == right.structure &&
               left.accent_ramp == right.accent_ramp &&
               left.overlay_shadow == right.overlay_shadow &&
               left.high_contrast_overlay_shadow == right.high_contrast_overlay_shadow &&
               left.rules == right.rules && left.rule_set == right.rule_set &&
               left.window_backdrop == right.window_backdrop &&
               left.dark_variant == right.dark_variant;
    }
};

} // namespace gui_forms
