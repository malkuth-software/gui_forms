#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text_shaping/types/text_shaping_types.hpp"

namespace gui_forms {

class GUI_FORMS_API TextShaper {
public:
    virtual ~TextShaper() = default;
    [[nodiscard]] virtual GlyphRun shape(const ShapingRequest& request) = 0;
};

} // namespace gui_forms
