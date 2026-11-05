#include "range_control_rendering.hpp"

#include <cmath>
#include <stdexcept>

namespace gui_forms::range_control_detail {

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

} // namespace gui_forms::range_control_detail
