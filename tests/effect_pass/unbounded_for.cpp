// expect: reject effect code may not use this 'for' loop: its condition is not a comparison of the loop variable with a constant
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class UnboundedFor final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_unbounded_for",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void UnboundedFor::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    for (std::uint32_t tick = 0U; tick < frame.ticks; ++tick) {
        phase_ = phase_ + parent.fraction;
    }
    canvas.fill_rect({0.0, 0.0, phase_, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_unbounded_for_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<UnboundedFor>();
    return entry;
}

} // namespace effect_case
