#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/scroll_bar.hpp"

namespace gui_forms {

class GUI_FORMS_API HScrollBar final : public ScrollBar {
public:
    explicit HScrollBar(StableId stable_id);

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
};

} // namespace gui_forms
