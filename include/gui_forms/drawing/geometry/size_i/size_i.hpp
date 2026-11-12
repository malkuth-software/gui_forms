#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>

namespace gui_drawing {

struct GUI_FORMS_API SizeI final {
    std::int32_t width{};
    std::int32_t height{};
    friend constexpr bool operator==(const SizeI& left,
                                     const SizeI& right) noexcept {
        return left.width == right.width && left.height == right.height;
    }
};

} // namespace gui_drawing
