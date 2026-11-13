#pragma once

#include "../../scrollable_control/scrollable_anatomy.hpp"
#include "gui_forms/controls/group_box.hpp"

#include <array>

namespace gui_forms::group_box_detail {

// GroupBox and ScaledGroupBox: the scrollable parts, the face, the frame
// stroked below the caption, and the caption block.
inline constexpr std::array<PartField<GroupBox::Parts>, 3> extra_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &GroupBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &GroupBox::Parts::edge},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &GroupBox::Parts::caption},
}};

inline constexpr std::array<PartDeclaration, scrollable_detail::part_count + 3> parts =
    joined_declarations(scrollable_detail::parts, part_declarations(extra_fields));

} // namespace gui_forms::group_box_detail
