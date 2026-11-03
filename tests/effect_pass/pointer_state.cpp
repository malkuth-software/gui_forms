// expect: reject effect 'case_pointer_state': its state member 'borrowed_' is a pointer or reference
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class PointerState final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_pointer_state",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    const double* borrowed_{};
    double phase_{};
};

void PointerState::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    const gui_forms::Color black = gui_forms::Color::rgba(0, 0, 0, 255);
    canvas.fill_rect({0.0, 0.0, frame.part.width * parent.fraction, frame.part.height}, black);
}

gui_forms::EffectEntry case_pointer_state_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<PointerState>();
    return entry;
}

} // namespace effect_case
