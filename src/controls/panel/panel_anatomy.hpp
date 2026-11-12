#pragma once

#include "../scrollable_control/scrollable_anatomy.hpp"
#include "gui_forms/controls/panel.hpp"

#include <array>

namespace gui_forms::panel_detail {

// Panel's own parts after the scrollable ones. Panel, ScaledPanel,
// AnchoredOverlayLayer, TabPage, MaterialPanel and Card publish this list.
inline constexpr std::array<PartField<Panel::Parts>, 2> extra_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &Panel::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &Panel::Parts::edge},
}};

inline constexpr std::array<PartDeclaration, scrollable_detail::part_count + 2> parts =
    joined_declarations(scrollable_detail::parts, part_declarations(extra_fields));

} // namespace gui_forms::panel_detail
