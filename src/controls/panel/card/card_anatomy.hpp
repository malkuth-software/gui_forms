#pragma once

#include "../panel_anatomy.hpp"
#include "gui_forms/controls/card.hpp"

#include <array>

namespace gui_forms::card_detail {

// Card and ReviewCard: Panel's parts with the card's interaction states.
inline constexpr std::array<StateField<Card::States>, 9> state_fields{{
    {.state = AnatomyState::normal, .member = &Card::States::normal},
    {.state = AnatomyState::hot, .member = &Card::States::hot},
    {.state = AnatomyState::pressed, .member = &Card::States::pressed},
    {.state = AnatomyState::captured, .member = &Card::States::captured},
    {.state = AnatomyState::selected, .member = &Card::States::selected},
    {.state = AnatomyState::enabled, .member = &Card::States::enabled},
    {.state = AnatomyState::disabled, .member = &Card::States::disabled},
    {.state = AnatomyState::focused, .member = &Card::States::focused},
    {.state = AnatomyState::focus_visible, .member = &Card::States::focus_visible},
}};

inline constexpr std::array<AnatomyState, 9> states =
    state_declarations(state_fields);

} // namespace gui_forms::card_detail
