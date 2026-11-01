#pragma once

#include <chrono>
#include <string>

namespace gui_forms {

// How a window's one tooltip presenter behaves (owner, 2026-10-28). Every
// window has one presenter; it shows the plain tooltip text of controls
// (Control::tool_tip_text) and of authored parts (the semantic tool_tip of
// a strip item or a tab header). The ToolTip component sets these through
// the window it was made for.
//
// - initial_delay: how long the pointer rests over a thing, or keyboard
//   focus stays on it, before its tooltip shows.
// - reshow_delay: the rest that shows the next tooltip while the pointer
//   moves on from one that showed, within initial_delay of its closing.
// - auto_pop_delay: how long a tooltip stays; zero keeps it until the
//   pointer leaves its thing, a press, or focus moves.
// - maximum_width: the widest a tooltip's text runs before it wraps, in
//   logical units, from 80 to 2048.
// - show_on_hover and show_on_focus: whether a resting pointer, and
//   keyboard focus, show tooltips.
// - show_when_disabled: whether a disabled control's or item's tooltip
//   shows.
struct ToolTipSettings final {
    std::chrono::milliseconds initial_delay{500};
    std::chrono::milliseconds reshow_delay{100};
    std::chrono::milliseconds auto_pop_delay{5000};
    double maximum_width{336.0};
    bool show_on_hover{true};
    bool show_on_focus{true};
    bool show_when_disabled{false};
};

// A window's tooltip opened or closed. `target_stable_id` names the control
// whose tooltip it is, and `item_id` the authored part (a strip item's
// stable ID or a tab header's semantic ID), or is empty for the control
// itself. `text` is what it shows. `keyboard_initiated` is true when
// keyboard focus showed it.
struct ToolTipEvent final {
    std::string target_stable_id{};
    std::string item_id{};
    std::string text{};
    bool shown{};
    bool keyboard_initiated{};
};

} // namespace gui_forms
