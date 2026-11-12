#pragma once

#include "../basic/number_text.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::scrolling_detail {

inline constexpr double comparison_epsilon = 0.0001;

[[nodiscard]] inline bool approximately_greater(
    double left, double right) noexcept {
    return left > right + comparison_epsilon;
}

[[nodiscard]] inline bool parse_finite_number(
    std::string_view text, double& value) noexcept {
    return number_text::parse_finite_decimal(text, value);
}

} // namespace gui_forms::scrolling_detail
