// expect: reject effect code may not make a virtual call \('effect_case::Shape::area'\)
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const { return 1.0; }
};

class Square final : public Shape {
public:
    double area() const override { return 4.0; }
};

class VirtualCall final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_virtual_call",
        .cost = {.fixed = 1000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    double phase_{};
};

void VirtualCall::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    const Square square;
    const Shape& shape = square;
    const double area = shape.area();
    canvas.fill_rect({0.0, 0.0, area * parent.fraction, frame.part.height}, gui_forms::Color::rgba(0, 0, 0, 255));
}

gui_forms::EffectEntry case_virtual_call_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<VirtualCall>();
    return entry;
}

} // namespace effect_case
