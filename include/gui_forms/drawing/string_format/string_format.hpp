#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/object/drawing_object.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

namespace gui_drawing {

struct GUI_FORMS_API StringFormatSnapshot final {
    StringAlignment alignment{StringAlignment::near};
    StringAlignment line_alignment{StringAlignment::near};
    StringTrimming trimming{StringTrimming::none};
    std::uint32_t flags{};
};

class GUI_FORMS_API StringFormat final : public DrawingObject {
public:
    StringFormat() = default;
    explicit StringFormat(std::uint32_t flags);
    explicit StringFormat(const StringFormat& source);

    void set_alignment(StringAlignment alignment);
    void set_line_alignment(StringAlignment alignment);
    void set_trimming(StringTrimming trimming);
    void set_flags(std::uint32_t flags);
    [[nodiscard]] StringFormatSnapshot snapshot() const;

private:
    StringFormatSnapshot value_;
};

} // namespace gui_drawing
