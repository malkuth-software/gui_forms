#pragma once

// The material properties' lowering (stage T3, PLAN.md P6b to P6f): CSS's
// background, border, border-radius, box-shadow, filter and mask-image
// families and the language's glow and surface-finish, read from a
// declaration's value, become the engine's material fields (EC1), and
// CSS's color, outline family and text-shadow the recipe's own fields
// (RecipeField, P6f); each longhand is one field, so that each cascades as
// CSS cascades it. The compiler reads the value and handles the CSS-wide
// keywords; this lowers what is left into a result record and reports
// nothing itself.

#include "../diagnostics/theme_diagnostics.hpp"
#include "../value/theme_value.hpp"

#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The CSS properties lowered onto material fields, then those lowered onto
// recipe fields (recipe_property): each family's shorthands, then its
// longhands. The edges' and the corners' come in CSS's order (top, right,
// bottom, left; top-left, top-right, bottom-right, bottom-left), each
// edge's in the order of its fields (style, width, colour).
enum class MaterialProperty : std::uint8_t {
    background,
    background_color,
    background_image,
    background_blend_mode,
    background_position,
    background_size,
    background_repeat,
    border,
    border_top,
    border_right,
    border_bottom,
    border_left,
    border_style,
    border_width,
    border_color,
    border_top_style,
    border_top_width,
    border_top_color,
    border_right_style,
    border_right_width,
    border_right_color,
    border_bottom_style,
    border_bottom_width,
    border_bottom_color,
    border_left_style,
    border_left_width,
    border_left_color,
    border_image,
    border_radius,
    border_top_left_radius,
    border_top_right_radius,
    border_bottom_right_radius,
    border_bottom_left_radius,
    box_shadow,
    filter,
    glow,
    glow_placement,
    mask_image,
    reflection,
    surface_finish,
    // The four inner lines one unit inside the border (T4 floor sheet,
    // owner option 1): `keylines: <top> <right> <bottom> <left>`.
    keylines,
    color,
    outline,
    outline_color,
    outline_offset,
    outline_style,
    outline_width,
    text_shadow,
    // The font longhands (T4 fonts): recipe fields, the control's primary
    // text font.
    font_family,
    font_size,
    font_style,
    font_weight,
    letter_spacing,
    // The recipe's other colours (T4 floor sheet, owner option 1).
    muted_color,
    glyph_color,
    default_outline,
};

// An image resource the theme's manifest lists, bound to the engine's image
// (T3 P6d): `url("resource:<resource>")` draws `image`, of `pixel_size`
// source pixels, at `pixels_per_logical_pixel` source pixels per logical
// unit as its own size (CSS's natural size; 2 for an image drawn for twice
// the density). The manifest's loader binds each (P10); `resource`
// borrows the binding's storage.
struct ThemeImageResource final {
    std::string_view resource{};
    ImageId image{};
    Size pixel_size{};
    double pixels_per_logical_pixel{1.0};
};

// The property `name` (lower case) names, or none for another property.
[[nodiscard]] std::optional<MaterialProperty> material_property(std::string_view name) noexcept;

// Whether `name`, in any ASCII case, is one of CSS's blend modes the
// engine does not composite (CSS_COVERAGE section 7): an error, not an
// unknown keyword.
[[nodiscard]] bool css_blend_mode_not_admitted(std::string_view name) noexcept;

// The material fields `property` sets, in order, into `fields`, which is
// cleared first: what CSS's `initial` resets. None for a recipe property.
void material_property_fields(MaterialProperty property, std::vector<MaterialField>& fields);

// Whether `property` sets the recipe's own fields (P6f) rather than the
// material's: they are the control's, so a rule naming a part or a role
// cannot declare them.
[[nodiscard]] bool recipe_property(MaterialProperty property) noexcept;

// The recipe fields a recipe property sets, in order (`count` of the
// capacity are live): what CSS's `initial` resets. None for a material
// property.
struct RecipePropertyFields final {
    std::array<RecipeField, 3> fields{};
    std::size_t count{};
};
[[nodiscard]] RecipePropertyFields recipe_property_fields(MaterialProperty property) noexcept;

enum class MaterialLoweringOutcome : std::uint8_t {
    // `declarations` hold the fields the value sets.
    lowered,
    // A warning (`problem`): the declaration is dropped, the rule kept.
    dropped,
    // An error (`problem`) refuses the sheet.
    refused,
};

// What one lowering found wrong: the diagnostic the compiler reports, at
// the sheet token `token`.
struct MaterialLoweringProblem final {
    DiagnosticCode code{DiagnosticCode::value_syntax};
    std::uint32_t token{};
    std::string detail{};
};

// One declaration's lowering, caller-owned and reused: a lowering
// replaces the contents and keeps the capacity.
struct MaterialLowering final {
    // What the lowering reads an em length as, in logical units (P8c): the
    // compiler lowers a value with em lengths at 0 and at 1, and the
    // difference of each length is its em coefficient; with
    // `keep_gradients`, no gradient is resolved at the lowering, so that
    // each keeps its lengths.
    double em{};
    bool keep_gradients{};
    // What the lowering reads CSS's currentcolor as (written, or a shadow's
    // or a glow's default): the compiler lowers again at another colour, and
    // the colours that differ are current; `read_current_colour` says the
    // lowering read it.
    Color current_colour{Color::rgba(0, 0, 0, 255)};
    bool read_current_colour{};
    MaterialLoweringOutcome outcome{MaterialLoweringOutcome::refused};
    std::vector<MaterialDeclaration> declarations{};
    // A recipe property's fields.
    std::vector<RecipeDeclaration> recipe_fields{};
    MaterialLoweringProblem problem{};
};

// Lowers `value`, a value of `property` that is not a CSS-wide keyword,
// into `lowering`. `start` is the token the value starts at, where a
// problem with the whole value points; `images` binds the image resources
// a value may name. `value` and `images` are borrowed for the call; the
// declarations own what they hold.
void lower_material_value(MaterialProperty property, const ThemeValue& value, std::uint32_t start,
                          std::span<const ThemeImageResource> images, MaterialLowering& lowering);

} // namespace gui_forms::theme_language
