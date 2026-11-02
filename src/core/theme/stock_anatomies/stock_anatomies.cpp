#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include <array>

namespace gui_forms {

namespace {

// The generated table: `stock_anatomy_table` and the arrays its spans
// refer to, all constexpr with static storage.
#include "stock_anatomies.inc"

} // namespace

std::span<const ControlAnatomy> stock_control_anatomies() noexcept {
    const std::span<const ControlAnatomy> result(stock_anatomy_table);
    return result;
}

} // namespace gui_forms
