// expect: reject effect code may not (take the address of 'effect_case::doubled'|call through a function pointer)
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

double doubled(double value) {
    const double result = value * 2.0;
    return result;
}

class FunctionPointer final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_function_pointer",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void FunctionPointer::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    double (*const scale)(double) = &doubled;
    const double width = scale(frame.part.width);
    canvas.fill_rect({0.0, 0.0, width * parent.fraction, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_function_pointer_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<FunctionPointer>();
    return entry;
}

} // namespace effect_case
