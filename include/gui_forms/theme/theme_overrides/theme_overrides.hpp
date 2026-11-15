#pragma once
// Typed theme overrides (the ImGui-class demonstrator's I6, coordinator
// ruling 5): run-time styling written as values instead of theme-language
// text. An entry names a stock control type, optionally one of its parts and
// one of its states, and a property of the theme language's material and
// recipe families with a typed value; a colour-role entry redefines one of
// the design language's colour roles. Every name is checked against the
// stock control anatomies (stock_control_anatomies()) when the entry is
// set, so a misspelt control, part or state is refused there, with a
// ThemeOverrideCheck, instead of surfacing later as a compiler warning.
//
// The overrides write one program theme sheet (sheet_text), compiled as any
// sheet is; Control::set_theme_overrides compiles it for a control's
// subtree through Control::set_theme_sheet. The sheet is an intermediate
// form: callers never write or parse it.
//
// Threads and memory: a value type; no state is shared.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The properties an entry may set, with the theme-language property each
// writes and the value it takes.
enum class ThemeOverrideProperty : std::uint8_t {
    // `background-color` (material fill): a colour.
    background_colour,
    // `border-color` (material edge): a colour.
    border_colour,
    // `color` (the recipe's text): a colour.
    text_colour,
    // `border-radius`: a length in logical units, 0 through 64.
    corner_radius,
    // `border-width`: a length in logical units, 0 through 16.
    border_width,
    // `opacity`: a fraction, 0 through 1.
    opacity,
};

// What setting an entry did. Anything but accepted changes nothing.
enum class ThemeOverrideCheck : std::uint8_t {
    accepted,
    // The control type is not a stock control's id (manifests/controls.json).
    unknown_control,
    // The part is not one of that control's parts.
    unknown_part,
    // The state is not one the control, or the named part, takes.
    unknown_state,
    // A colour given for a length property or the reverse; or a colour
    // role given one colour where it is a pair, or two where it is single.
    wrong_value_kind,
    // A length outside its range, or a value that is not finite.
    value_out_of_range,
    // The overrides already hold maximum_entries entries.
    too_many,
};

// Where an entry applies: a stock control type by its id (`button`,
// `track_bar`), optionally one of its parts by name (`face`, empty for the
// control as a whole) and one of its states (none for every state).
struct GUI_FORMS_API ThemeOverrideTarget final {
    std::string control{};
    std::string part{};
    std::optional<AnatomyState> state{};

    friend bool operator==(const ThemeOverrideTarget& left, const ThemeOverrideTarget& right) noexcept {
        return left.control == right.control && left.part == right.part && left.state == right.state;
    }
};

class GUI_FORMS_API ThemeOverrides final {
public:
    // The most entries, roles and properties together, a set holds.
    static constexpr std::size_t maximum_entries = 4096U;

    // Redefines a colour role: `foreground` alone for a single role,
    // `foreground` on `background` for a paired one (colour_role_paired).
    // Setting a role again replaces its colours in place.
    ThemeOverrideCheck set_role_colour(ColourRole role, Color foreground, std::optional<Color> background = {});
    // Sets a colour property (background_colour, border_colour or
    // text_colour) for `target`. Setting the same target and property
    // again replaces the value in place.
    ThemeOverrideCheck set_colour(const ThemeOverrideTarget& target, ThemeOverrideProperty property, Color colour);
    // Sets a length or fraction property (corner_radius, border_width or
    // opacity) for `target`, within the property's range.
    ThemeOverrideCheck set_length(const ThemeOverrideTarget& target, ThemeOverrideProperty property, double value);
    // Removes every entry.
    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept;
    // The number of entries, roles included.
    [[nodiscard]] std::size_t size() const noexcept;

    // The program theme sheet the entries write: the roles in one `:root`
    // block, then one rule per target in the order targets were first set,
    // its properties in the order they were first set. Deterministic: the
    // same entries write the same text. Empty overrides write an empty
    // sheet.
    [[nodiscard]] std::string sheet_text() const;

private:
    struct RoleEntry final {
        ColourRole role{};
        Color foreground{};
        std::optional<Color> background{};
    };
    struct PropertyEntry final {
        ThemeOverrideTarget target{};
        ThemeOverrideProperty property{};
        Color colour{};
        double length{};
    };

    // Checks the target's names against the stock anatomies.
    [[nodiscard]] static ThemeOverrideCheck check_target(const ThemeOverrideTarget& target) noexcept;
    // Stores the entry, replacing one of the same target and property.
    ThemeOverrideCheck store(PropertyEntry entry);

    std::vector<RoleEntry> roles_{};
    std::vector<PropertyEntry> properties_{};
};

} // namespace gui_forms
