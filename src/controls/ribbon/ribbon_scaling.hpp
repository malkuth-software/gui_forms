#pragma once

// Ribbon scaling definitions checked when a ribbon is defined (stage RB,
// phase 2b; owner decisions 5, 27, 29, 32). Private to the ribbon.

#include "gui_forms/controls/ribbon.hpp"

#include <vector>

namespace gui_forms::ribbon_detail {

// A group's effective layouts: its authored layouts after validation, or,
// when it has none, layouts generated from its commands. Throws
// std::invalid_argument for an invalid authored layout. Measures nothing.
[[nodiscard]] std::vector<RibbonGroupLayout> prepare_group_layouts(const RibbonGroupSpec& group);

// Checks a tab's authored reduction order against its groups' effective
// layouts (`layouts`, one entry per group) and galleries. Throws
// std::invalid_argument for a step naming a group not on the tab, a layout
// step past a group's last layout, a gallery step on a group without a
// gallery or after the group collapsed, or more gallery steps than the
// gallery's column range allows.
void validate_reduction_order(const RibbonTabSpec& tab, const std::vector<std::vector<RibbonGroupLayout>>& layouts);

// The image size of `command` (index `index` of its group) in `layout`.
[[nodiscard]] RibbonCommandSize layout_command_size(const RibbonGroupLayout& layout, const RibbonCommandSpec& command,
                                                    std::size_t index) noexcept;

} // namespace gui_forms::ribbon_detail
