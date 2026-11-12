#pragma once

#include "gui_forms/controls/scroll_bar.hpp"

namespace gui_forms {

class HScrollBar final : public ScrollBar {
public:
    explicit HScrollBar(StableId stable_id);

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
};

} // namespace gui_forms
