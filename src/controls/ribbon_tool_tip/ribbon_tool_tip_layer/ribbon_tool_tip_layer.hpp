#pragma once

#include "gui_forms/control.hpp"

namespace gui_forms {

class RibbonToolTipLayer final : public Control {
public:
    explicit RibbonToolTipLayer(StableId stable_id);
    [[nodiscard]] bool hit_test_local(Point point) const override;
};

} // namespace gui_forms
