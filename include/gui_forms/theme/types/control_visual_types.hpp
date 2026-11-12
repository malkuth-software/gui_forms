#pragma once

#include "gui_forms/export/application_export.hpp"

#include <cstdint>

// A control's visual role and state as the theme reads them: its recipe
// family, its surface state, its validation status and its interaction
// state. Apart from theme_types.hpp so that control.hpp, which holds a
// ControlVisualStatus and takes a VisualInteraction, does not parse the
// theme (time-trace study of cp28, action 2).

namespace gui_forms {

// The recipe families a theme supplies. `tool_tip` (owner decision RB-38,
// design language 12.9) is the face and edge of plain and enhanced
// tooltips, beside `card` and `menu_item`. `link` and `visited_link` (owner
// decision W-Q5) are a hyperlink's text, chosen by the link's visited flag;
// hover keeps the normal link colour, and a link draws no plate. `notice`
// (T4, W-4) is the face of an InfoBanner or a Toast; its severity is the
// control's own custom state, exactly one of information, warning, success
// and error, and its keyline takes the severity's colour. `badge` (T4, W-4)
// is a Badge's count or dot chip, which has normal and disabled looks only.
enum class ControlVisualRole : std::uint8_t {
    window, panel, card, button, accent_button, command_button, choice,
    editor, menu_item, selection, progress, tool_tip, link, visited_link,
    notice, badge, count,
};
enum class ControlSurfaceState : std::uint8_t {
    normal, hot, pressed, pending, invalid, disabled, deactivated, count,
};
enum class ControlVisualStatus : std::uint8_t { normal, pending, invalid };

// A control's interaction state as it asks for its visual context
// (Control::visual_context, audit A13): named fields, all false by default.
struct GUI_FORMS_API VisualInteraction final {
    bool hovered{};
    bool pressed{};
    bool selected{};
    bool focused{};
    bool defaulted{};
};

struct GUI_FORMS_API ControlVisualContext final {
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

} // namespace gui_forms
