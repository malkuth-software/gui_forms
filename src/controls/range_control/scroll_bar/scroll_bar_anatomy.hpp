#pragma once

#include "gui_forms/anatomy.hpp"
#include "gui_forms/controls/scroll_bar.hpp"

#include <array>

namespace gui_forms::scroll_bar_detail {

// The states a scroll bar's arrows and track take on their own (hot under
// the pointer, pressed while held), and those of its thumb, which also
// captures the pointer while dragged.
inline constexpr std::array<AnatomyState, 2> pressable_part_states{{
    AnatomyState::hot, AnatomyState::pressed}};
inline constexpr std::array<AnatomyState, 3> thumb_part_states{{
    AnatomyState::hot, AnatomyState::pressed, AnatomyState::captured}};

// Parts and states ScrollBar, HScrollBar and VScrollBar share. Each type
// publishes them under its own registry id.
inline constexpr std::array<PartField<ScrollBar::Parts>, 8> part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ScrollBar::Parts::region},
    {.declaration = {.name = "decrement_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = pressable_part_states},
     .member = &ScrollBar::Parts::decrement_arrow},
    {.declaration = {.name = "increment_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = pressable_part_states},
     .member = &ScrollBar::Parts::increment_arrow},
    {.declaration = {.name = "decrement_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = pressable_part_states},
     .member = &ScrollBar::Parts::decrement_glyph},
    {.declaration = {.name = "increment_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = pressable_part_states},
     .member = &ScrollBar::Parts::increment_glyph},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {},
                     .states = pressable_part_states},
     .member = &ScrollBar::Parts::track},
    {.declaration = {.name = "thumb", .role = PartRole::thumb, .composed_from = {},
                     .states = thumb_part_states},
     .member = &ScrollBar::Parts::thumb},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ScrollBar::Parts::focus},
}};

inline constexpr std::array<StateField<ScrollBar::States>, 7> state_fields{{
    {.state = AnatomyState::normal, .member = &ScrollBar::States::normal},
    {.state = AnatomyState::pressed, .member = &ScrollBar::States::pressed},
    {.state = AnatomyState::captured, .member = &ScrollBar::States::captured},
    {.state = AnatomyState::enabled, .member = &ScrollBar::States::enabled},
    {.state = AnatomyState::disabled, .member = &ScrollBar::States::disabled},
    {.state = AnatomyState::focused, .member = &ScrollBar::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ScrollBar::States::focus_visible},
}};

inline constexpr std::array<PartDeclaration, 8> parts =
    part_declarations(part_fields);
inline constexpr std::array<AnatomyState, 7> states =
    state_declarations(state_fields);

} // namespace gui_forms::scroll_bar_detail
