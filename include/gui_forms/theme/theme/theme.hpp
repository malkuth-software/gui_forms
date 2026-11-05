#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

class Control;
namespace detail { class ThemeImageLease; struct ThemeAccess; struct ThemeSourceRecord; }

// The fields of a control's primary text font a theme may set (T4 fonts),
// in RecipeField's order from font_family.
enum class FontField : std::uint8_t { family, size, weight, style, letter_spacing, count };
inline constexpr std::size_t font_field_count = static_cast<std::size_t>(FontField::count);

// A control's primary text font as resolved (Theme::resolve_font,
// Control::resolved_text_font), before the window's text scale: the font
// the control holds with the theme's font fields over it. `levels` says,
// per FontField, the cascade level of the rule that set the field
// (built_in where none did, the control's own value). `from_code` is set
// where code set the font (set_font): inline code, which wins over every
// theme field, so no level applies.
struct ResolvedFont final {
    FontSpec font{};
    std::array<CascadeLevel, font_field_count> levels{};
    std::array<bool, font_field_count> themed{};
    bool from_code{};

    friend bool operator==(const ResolvedFont& left, const ResolvedFont& right) noexcept {
        return left.font == right.font && left.levels == right.levels && left.themed == right.themed &&
               left.from_code == right.from_code;
    }
};

class Theme final {
public:
    // Validates `definition` and makes a theme holding its own copy, with
    // every family's high-contrast records derived from its ordinary ones.
    // Throws std::invalid_argument and makes nothing when the definition is
    // invalid. The definition is read, never copied onto the stack (see
    // ThemeDefinition).
    [[nodiscard]] static std::shared_ptr<const Theme> create(
        const ThemeDefinition& definition);
    [[nodiscard]] std::string_view id() const noexcept { return definition_.id; }
    [[nodiscard]] const BasicControlStyle& basic_style() const noexcept {
        return definition_.compatibility;
    }
    [[nodiscard]] const ThemeStructureTokens& structure() const noexcept {
        return definition_.structure;
    }
    [[nodiscard]] const ControlVisualRecipe& resolve(
        ControlVisualRole role, ControlVisualContext context) const noexcept;
    // `control`'s recipe for `role` in `context` (T3 P6f): the recipe above
    // (the floor) with the recipe fields (RecipeField) of the theme's rules
    // that match the control in `states` in place. The rules are matched
    // with no part and the region role. In forced high contrast, or where
    // the fields make no valid recipe, it is the floor. The first form's
    // states are `context`'s: normal; enabled or disabled; hot, pressed,
    // pending, invalid or deactivated by the surface state; selected;
    // focused and focus_visible when focused; preselect when defaulted.
    // Copies the floor when a field is set (see ResolvedControlRecipe).
    [[nodiscard]] ResolvedControlRecipe resolve(const Control& control, ControlVisualRole role,
                                                ControlVisualContext context) const;
    [[nodiscard]] ResolvedControlRecipe resolve(const Control& control, ControlVisualRole role,
                                                ControlVisualContext context, AnatomyStateSet states) const;
    // `control`'s primary text font (T4 fonts): `own`, the font it holds,
    // with the font fields (RecipeField::font_family to letter_spacing) of
    // the theme's rules matching it at rest in place: no part, the region
    // role, and the states of a default ControlVisualContext. Forced high
    // contrast keeps them: they are not colours. An em size is of `own`'s
    // size, an em spacing of the resolved size. Where the fields make no
    // font valid_font_spec admits, or a size outside minimum_rule_font_size
    // to maximum_rule_font_size, it is `own` with no field themed.
    [[nodiscard]] ResolvedFont resolve_font(const Control& control, FontSpec own) const noexcept;
    // Whether the rules matching `control` at rest (its type, classes and
    // stable ID, no part or role, the window's media and the control's user
    // theme and inline style) declare it clear (owner decision 2026-11-05):
    // `background: transparent`, a background colour of alpha 0 and no
    // visible background layer, so that the surface's backdrop shows through
    // it. A colour or layer left to the floor is not clear.
    [[nodiscard]] bool declares_clarity(const Control& control) const noexcept;
    [[nodiscard]] const ThemeDefinition& definition() const noexcept {
        return definition_;
    }
    // Whether the theme has a variant for `scheme` (T4 theme variants):
    // its colour_schemes, which its sheet's `color-scheme` sets.
    [[nodiscard]] bool supports(ColourScheme scheme) const noexcept {
        const bool held = definition_.colour_schemes.contains(scheme);
        return held;
    }
    // The ramp colour for an accent index; an index past the end of the
    // ramp wraps around it.
    [[nodiscard]] Color accent(AccentIndex index) const noexcept {
        const std::size_t position = static_cast<std::size_t>(index.value) % definition_.accent_ramp.size();
        return definition_.accent_ramp[position];
    }
    // The overlay shadow's layers (owner decision RB-37) for ordinary or
    // high-contrast presentation; empty when the theme casts none.
    [[nodiscard]] const std::vector<MaterialShadow>& overlay_shadow(bool high_contrast) const noexcept {
        if (high_contrast) return definition_.high_contrast_overlay_shadow;
        return definition_.overlay_shadow;
    }
    // The theme's engine, which paint_part calls for every part. The theme
    // owns it, so every window showing the theme shares it (T2 plan).
    [[nodiscard]] const ThemeEngine& engine() const noexcept { return engine_; }
    // The image a compiled user theme gives the program icon `identifier`
    // (its `icons/<identifier>.png`), held by the theme while it lives;
    // nullopt where it gives none (T3 user themes). Whether the program
    // lets the icon be replaced is the application's (ApplicationResources::
    // icon).
    [[nodiscard]] std::optional<ImageId> icon_override(std::string_view identifier) const noexcept;

private:
    // The engine compiles the definition's rules: a theme is made whole or
    // not at all, so the theme it replaces stays in force until it exists.
    // The copy is made straight into the theme's heap storage.
    explicit Theme(const ThemeDefinition& definition)
        : definition_(definition), engine_(definition_.rules, definition_.rule_set) {}
    friend struct detail::ThemeAccess;
    // Checks `definition` (throwing as create states) and makes the theme.
    [[nodiscard]] static std::unique_ptr<Theme> validated(const ThemeDefinition& definition);
    ThemeDefinition definition_;
    ThemeEngine engine_;
    // The images a compiled theme package loaded, each removed from its
    // registry when the last theme holding it is released.
    std::vector<std::shared_ptr<const detail::ThemeImageLease>> images_{};
    // The theme's source, kept so that a control's inline style using
    // custom properties can be compiled against it (T3 P12b): a compiled
    // package's, else the empty sheet's (never null once created).
    std::shared_ptr<const detail::ThemeSourceRecord> source_{};
};

[[nodiscard]] ThemeDefinition windows_professional_theme_definition();
[[nodiscard]] std::shared_ptr<const Theme> default_theme();

} // namespace gui_forms
