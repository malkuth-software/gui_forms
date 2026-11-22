#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/panel.hpp"

#include <unordered_map>

#include <stxlib/optional.hpp>

namespace gui_forms {

class GUI_FORMS_API ScaledPanel : public Panel {
public:
    explicit ScaledPanel(StableId stable_id, Size initial_design_size = {1.0, 1.0});

    void add_at(Control::Ptr child, Rect design_bounds);
    void set_design_bounds(const Control& child, Rect design_bounds);
    [[nodiscard]] stxlib::Optional<Rect> design_bounds(const Control& child) const;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Its children are stretched from its design space (Control::design_size,
    // 1 x 1 while that is zero) to its arranged size, so its design content
    // always fills its viewport: an explicit stretch, recorded (stage VA,
    // decision 10). Reports the stretch's smaller axis ratio; none while
    // design_size is zero or it has no extent.
    [[nodiscard]] stxlib::Optional<double> applied_viewport_scale() const noexcept override;

private:
    using SlotMap = std::unordered_map<std::uint64_t, Rect>;
    void reconcile_slots();
    SlotMap slots_;
};

} // namespace gui_forms
