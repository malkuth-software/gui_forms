// expect: reject effect code may not use this 'for' loop: its loop variable 'index' is written in the body
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class LoopVariableWritten final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_loop_variable_written",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void LoopVariableWritten::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    for (int index = 0; index < 8; ++index) {
        if (parent.fraction > 0.5) {
            index = 0;
        }
        phase_ = phase_ + 1.0;
    }
    canvas.fill_rect({0.0, 0.0, phase_, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_loop_variable_written_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<LoopVariableWritten>();
    return entry;
}

} // namespace effect_case
