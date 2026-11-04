// expect: reject effect code may not allocate: 'new' and 'delete' are refused
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class Allocation final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_allocation",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void Allocation::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    double* const width = new double(frame.part.width);
    canvas.fill_rect({0.0, 0.0, *width * parent.fraction, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
    delete width;
}

gui_forms::EffectEntry case_allocation_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<Allocation>();
    return entry;
}

} // namespace effect_case
