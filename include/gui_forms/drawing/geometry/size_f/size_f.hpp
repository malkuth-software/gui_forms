#pragma once

#include "gui_forms/export/application_export.hpp"

namespace gui_drawing {

struct GUI_FORMS_API SizeF final {
    double width{};
    double height{};
    friend constexpr bool operator==(const SizeF& left,
                                     const SizeF& right) noexcept {
        return left.width == right.width && left.height == right.height;
    }
};

} // namespace gui_drawing
