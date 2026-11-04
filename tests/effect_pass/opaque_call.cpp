// expect: reject effect code may not call 'effect_case::defined_elsewhere': its body is not visible to the effect pass
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

// Declared only: its body is in no file the pass can see.
double defined_elsewhere(double value);

class OpaqueCall final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_opaque_call",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void OpaqueCall::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    const double width = defined_elsewhere(frame.part.width);
    canvas.fill_rect({0.0, 0.0, width * parent.fraction, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_opaque_call_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<OpaqueCall>();
    return entry;
}

} // namespace effect_case
