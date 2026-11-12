#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>

namespace gui_drawing {

struct GUI_FORMS_API PointI final {
    std::int32_t x{};
    std::int32_t y{};
    void offset(std::int32_t dx, std::int32_t dy) noexcept;
    friend constexpr bool operator==(const PointI& left,
                                     const PointI& right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

} // namespace gui_drawing
