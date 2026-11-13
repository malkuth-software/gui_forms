#pragma once

#include <cstdint>

namespace gui_forms {

// Whether a ComboBox's field takes text, as Windows Forms' ComboBoxStyle
// (its Simple style, a list always shown under the field, is not offered).
enum class ComboBoxStyle : std::uint8_t {
    drop_down_list,  // the field shows the selected item and takes no text
    drop_down,       // the field is a TextBox: any text, or an item from the list
};

// How a drop_down-style ComboBox's list follows what the user types.
enum class ComboBoxFilter : std::uint8_t {
    none,       // the list shows every item
    prefix,     // the list shows the items that start with the typed text
    substring,  // the list shows the items that contain the typed text
};

} // namespace gui_forms
