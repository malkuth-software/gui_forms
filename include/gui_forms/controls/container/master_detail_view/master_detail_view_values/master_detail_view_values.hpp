#pragma once

// MasterDetailView's value types, apart from the control so its capabilities
// header can name them.

#include <cstdint>

namespace gui_forms {

enum class MasterDetailDisplayMode : std::uint8_t {
    automatic,
    side_by_side,
    master_only,
    detail_only,
};

} // namespace gui_forms
