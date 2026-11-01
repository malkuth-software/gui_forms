#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <cstddef>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

class Control;
namespace detail { class ThemeImageLease; struct ThemeAccess; struct ThemeSourceRecord; }

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
    [[nodiscard]] const ThemeDefinition& definition() const noexcept {
        return definition_;
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
