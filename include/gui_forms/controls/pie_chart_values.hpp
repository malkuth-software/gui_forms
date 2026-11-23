#pragma once

#include <cstdint>

namespace gui_forms {

// Where a PieChart writes each slice's share (Windows Forms' PieLabelStyle).
enum class PieChartLabelStyle : std::uint8_t {
    inside,    // on the slice, halfway across its ring
    outside,   // beyond the slice's outer edge, at its middle angle
    disabled,  // no slice labels; the legend still names the slices
};

} // namespace gui_forms
