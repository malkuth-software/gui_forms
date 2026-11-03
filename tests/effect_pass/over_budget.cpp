// expect: reject effect 'case_over_budget': the static bound of run is [0-9]+ operations, over its declared fixed budget \(cost.fixed\) of 100.*this loop: 1000 passes
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class OverBudget final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_over_budget",
        .cost = {.fixed = 100U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void OverBudget::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    for (const std::size_t index : gui_forms::bounded_range<1000U>(static_cast<std::size_t>(frame.part.width))) {
        const double x = static_cast<double>(index) * parent.fraction;
        canvas.fill_rect({x, 0.0, 1.0, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
    }
}

gui_forms::EffectEntry case_over_budget_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<OverBudget>();
    return entry;
}

} // namespace effect_case
