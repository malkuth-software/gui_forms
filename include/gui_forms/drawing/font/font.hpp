#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/object/drawing_object.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

#include <string>

namespace gui_drawing {

struct GUI_FORMS_API FontSnapshot final {
    std::string family;
    double size{12.0};
    std::uint32_t style{};
    GraphicsUnit unit{GraphicsUnit::point};
    std::uint8_t charset{1};
};

class GUI_FORMS_API Font final : public DrawingObject {
public:
    Font(std::string family, double size,
         FontStyle style = FontStyle::regular,
         GraphicsUnit unit = GraphicsUnit::point,
         std::uint8_t charset = 1);
    Font(const Font& source, FontStyle style);

    [[nodiscard]] FontSnapshot snapshot() const;
    [[nodiscard]] std::int32_t deterministic_height() const;

private:
    FontSnapshot value_;
};

} // namespace gui_drawing
