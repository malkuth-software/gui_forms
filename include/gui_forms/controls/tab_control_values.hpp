#pragma once

// The value types of TabControl's properties, apart from the control so its
// capabilities header can name them.

#include <cstdint>

namespace gui_forms {

enum class TabAlignment : std::uint8_t {
    top,
    bottom,
    left,
    right,
};

// How the tabs are drawn (until stage T4 draws Plan Paint's tab stack, each
// is a recipe family of the by-role paint path): `normal`, flat at rest with
// a plate when hot or selected (the menu-item family); `buttons`, a button's
// plate on every tab; `flat_buttons`, a command button's lighter plate.
enum class TabAppearance : std::uint8_t {
    normal,
    buttons,
    flat_buttons,
};

// How wide each tab is (Windows Forms TabSizeMode). `normal`: its caption
// plus padding. `fill_to_right`: the row shared equally among the tabs.
// `fixed`: `item_size` along the row, whatever the caption.
enum class TabSizeMode : std::uint8_t {
    normal,
    fill_to_right,
    fixed,
};

} // namespace gui_forms
