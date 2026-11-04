// expect: accept
#include "effect_case.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace effect_case {

// A verified helper: a counted loop with constant bounds.
double summed_weights(double scale) {
    double sum = 0.0;
    for (int index = 0; index < 8; ++index) {
        sum = sum + scale * static_cast<double>(index);
    }
    return sum;
}

// The table every instance shares: constexpr, so reading it is allowed.
constexpr std::array<double, 4> case_widths{1.0, 2.0, 3.0, 4.0};

class Accepted final {
public:
    using parent_type = CaseParent;
    static constexpr gui_forms::EffectDeclaration declaration{
        .name = "case_passes",
        .cost = {.fixed = 4000U, .per_device_pixel = 4U},
        .setup_cost = 2000U,
        .state_budget = 512U,
    };
    void setup(const View& parent, const gui_forms::EffectSetup& setup);
    void run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas);

private:
    gui_forms::EffectArray<double, 32> offsets_{};
    double phase_{};
};

void Accepted::setup(const View& parent, const gui_forms::EffectSetup& setup) {
    gui_forms::EffectRandom random(setup.seed, gui_forms::EffectRandom::setup_stream);
    for (const std::size_t index : gui_forms::bounded_range<32U>(32U)) {
        offsets_.items[index] = random.next_unit() * setup.part.width * parent.fraction;
    }
    offsets_.count = 32U;
}

void Accepted::run(const View& parent, const gui_forms::EffectFrame& frame, gui_forms::EffectCanvas& canvas) {
    if (!frame.still) phase_ = gui_forms::effect_fraction(phase_ + 0.01 * static_cast<double>(frame.ticks));
    const double weight = summed_weights(parent.fraction);
    for (const double width : case_widths) {
        canvas.fill_rect({phase_ * frame.part.width, 0.0, width, weight}, gui_forms::Color::rgba(0, 0, 0, 255));
    }
    for (const std::size_t index : gui_forms::bounded_range<32U>(offsets_.count)) {
        canvas.stroke_line({offsets_.items[index], 0.0}, {offsets_.items[index], frame.part.height},
                           gui_forms::Color::rgba(255, 255, 255, 128), 1.0);
    }
    // The host's colour roles (EffectFrame::colours) are frame fields:
    // reading them is free.
    canvas.fill_rect({0.0, 0.0, 2.0, 2.0}, frame.colours.primary);
    canvas.fill_rect({2.0, 0.0, 2.0, 2.0}, frame.colours.secondary);
    for (std::uint32_t step = 10U; step > 0U; step -= 2U) {
        canvas.fill_circle({static_cast<double>(step), 1.0}, 1.0, gui_forms::Color::rgba(1, 2, 3, 255));
    }
}

gui_forms::EffectEntry case_passes_entry() {
    const gui_forms::EffectEntry entry = gui_forms::make_effect_entry<Accepted>();
    return entry;
}

} // namespace effect_case
