#pragma once

#include "gui_forms/export/application_export.hpp"

#include <cstdint>
#include <string_view>

// The cascade's levels and the media facts a rule's condition reads, apart
// from theme_rules.hpp so that control.hpp, whose resolution caches hold
// them, does not parse the rules (time-trace study of cp28, action 2).

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

[[nodiscard]] GUI_FORMS_API std::string_view cascade_level_name(CascadeLevel level) noexcept;

// What a window is, as a rule's media condition reads it (T3 EC3): its
// presentation settings and its scale. Every window evaluates a theme's
// conditions against its own facts, so windows sharing a theme may differ.
struct GUI_FORMS_API MediaFacts final {
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

} // namespace gui_forms
