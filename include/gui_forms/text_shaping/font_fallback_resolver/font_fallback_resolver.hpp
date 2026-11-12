#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text_shaping/types/text_shaping_types.hpp"

#include <optional>

namespace gui_forms {

class GUI_FORMS_API FontFallbackResolver {
public:
    virtual ~FontFallbackResolver() = default;
    [[nodiscard]] virtual std::optional<FontFallbackMatch> resolve(
        const FontFallbackRequest& request) = 0;
};

} // namespace gui_forms
