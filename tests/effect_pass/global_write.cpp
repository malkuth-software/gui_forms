// expect: reject effect code may not write the global variable 'effect_case::frames_drawn'
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

// A count every instance would share: a global, which effect code may not write.
double frames_drawn = 0.0;

class GlobalWrite final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_global_write",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void GlobalWrite::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    frames_drawn = 1.0;
    const gui_forms::Color black = gui_forms::Color::rgba(0, 0, 0, 255);
    canvas.fill_rect({0.0, 0.0, frame.part.width * parent.fraction, frame.part.height}, black);
}

gui_forms::EffectEntry case_global_write_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<GlobalWrite>();
    return entry;
}

} // namespace effect_case
