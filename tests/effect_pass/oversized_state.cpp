// expect: reject an effect's state \(its sizeof\) must fit its declared state_budget
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class OversizedState final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_oversized_state",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    gui_forms::EffectArray<double, 64> history_{};
};

void OversizedState::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    const gui_forms::Color black = gui_forms::Color::rgba(0, 0, 0, 255);
    canvas.fill_rect({0.0, 0.0, frame.part.width * parent.fraction, frame.part.height}, black);
}

gui_forms::EffectEntry case_oversized_state_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<OversizedState>();
    return entry;
}

} // namespace effect_case
