#pragma once

#include <cstdint>

namespace gui_forms {

// The grow and border styles of TableLayoutPanelCompat, apart from the class
// so its capabilities can name them.

enum class TableLayoutGrowStyle : std::uint8_t {
    fixed_size,
    add_rows,
    add_columns,
};

enum class TableCellBorderStyle : std::uint8_t {
    none,
    single,
    inset,
    outset,
};

} // namespace gui_forms
