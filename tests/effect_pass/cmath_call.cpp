// expect: reject effect code may not call '[^']*sin': <cmath> \(the host's libm\)
#include <cmath>
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class CmathCall final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_cmath_call",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void CmathCall::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    const double wave = std::sin(parent.fraction);
    canvas.fill_rect({0.0, 0.0, frame.part.width * wave, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_cmath_call_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<CmathCall>();
    return entry;
}

} // namespace effect_case
