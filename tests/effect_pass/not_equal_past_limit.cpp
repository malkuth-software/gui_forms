// expect: reject effect code may not use this 'for' loop: its loop variable never fails the condition
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class NotEqualPastLimit final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_not_equal_past_limit",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void NotEqualPastLimit::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    // B-gf-tests-misc-02-F1: started past its limit, a '!=' loop counting up
    // runs until the variable wraps.
    for (std::uint32_t index = 5U; index != 3U; ++index) {
        phase_ = phase_ + parent.fraction;
    }
    canvas.fill_rect({0.0, 0.0, phase_, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_not_equal_past_limit_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<NotEqualPastLimit>();
    return entry;
}

} // namespace effect_case
