#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/theme_rules/cascade_facts.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// A control's resolved primary text font, apart from theme.hpp so that
// control.hpp, whose text font cache holds one, does not parse the theme
// (time-trace study of cp28, action 2).

namespace gui_forms {

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
struct GUI_FORMS_API ResolvedFont final {
    FontSpec font{};
    std::array<CascadeLevel, font_field_count> levels{};
    std::array<bool, font_field_count> themed{};
    bool from_code{};

    friend bool operator==(const ResolvedFont& left, const ResolvedFont& right) noexcept {
        return left.font == right.font && left.levels == right.levels && left.themed == right.themed &&
               left.from_code == right.from_code;
    }
};

} // namespace gui_forms
