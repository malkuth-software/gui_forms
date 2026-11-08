#pragma once

#include "gui_forms/types/geometry/geometry.hpp"

#include <cstdint>

namespace gui_forms {

struct CardLayout final {
    Insets padding{12.0, 10.0, 12.0, 10.0};
    double section_gap{8.0};
    double header_extent{30.0};
    double footer_extent{30.0};

    friend constexpr bool operator==(const CardLayout& left,
                                     const CardLayout& right) noexcept {
        return left.padding == right.padding &&
               left.section_gap == right.section_gap &&
               left.header_extent == right.header_extent &&
               left.footer_extent == right.footer_extent;
    }
};

// Controls whether activation is purely notificational or also updates the
// retained selection state before Card::activated is published. The default
// preserves the original manual-selection behavior.
enum class CardSelectionBehavior : std::uint8_t {
    manual,
    select_on_activation,
    toggle_on_activation,
};

} // namespace gui_forms
