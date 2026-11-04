// expect: reject effect code may not recurse: 'effect_case::halved'
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

// Halves `value` `depth` times, by calling itself.
double halved(double value, int depth) {
    if (depth <= 0) return value;
    const double half = halved(value * 0.5, depth - 1);
    return half;
}

class Recursion final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_recursion",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void Recursion::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    const double width = halved(frame.part.width, 3);
    canvas.fill_rect({0.0, 0.0, width * parent.fraction, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_recursion_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<Recursion>();
    return entry;
}

} // namespace effect_case
