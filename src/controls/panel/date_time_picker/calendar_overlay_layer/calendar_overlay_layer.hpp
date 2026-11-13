#pragma once

#include "gui_forms/controls/panel.hpp"

namespace gui_forms {

class CalendarOverlayLayer final : public Panel {
public:
    explicit CalendarOverlayLayer(StableId stable_id);
    [[nodiscard]] Event<>& dismissed() noexcept { return dismissed_; }

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;

private:
    Event<> dismissed_;
};

} // namespace gui_forms
