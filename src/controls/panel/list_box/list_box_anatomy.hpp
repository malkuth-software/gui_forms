#pragma once

#include "../panel_anatomy.hpp"
#include "gui_forms/controls/list_box.hpp"

#include <array>

namespace gui_forms::list_box_detail {

// ListBox and CheckedListBox: the scrollable parts, the face and edge the
// Panel paints when styled, the editor well and the clipped content.
inline constexpr std::array<PartField<ListBox::Parts>, 4> extra_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &ListBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &ListBox::Parts::edge},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &ListBox::Parts::well},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &ListBox::Parts::content},
}};

inline constexpr std::array<PartDeclaration, scrollable_detail::part_count + 4> parts =
    joined_declarations(scrollable_detail::parts, part_declarations(extra_fields));

} // namespace gui_forms::list_box_detail
