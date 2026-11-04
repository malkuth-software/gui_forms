// expect: reject effect code may not use a 'while' loop
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class WhileLoop final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_while_loop",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void WhileLoop::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    double x = 0.0;
    while (x < parent.fraction) {
        x = x + 0.25;
    }
    canvas.fill_rect({0.0, 0.0, x, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_while_loop_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<WhileLoop>();
    return entry;
}

} // namespace effect_case
