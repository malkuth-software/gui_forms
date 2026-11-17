#pragma once

// A control's runtime identity, apart from control.hpp so that headers which
// only name a control (presentation_types.hpp's live surfaces) need not
// include the whole control.

#include "gui_forms/export/application_export.hpp"

#include <compare>
#include <cstdint>

namespace gui_forms {

struct GUI_FORMS_API RuntimeId {
    std::uint64_t value{};
    friend constexpr bool operator==(const RuntimeId& left,
                                     const RuntimeId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const RuntimeId& left, const RuntimeId& right) noexcept {
        return left.value <=> right.value;
    }
};

} // namespace gui_forms
