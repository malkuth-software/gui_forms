// expect: accept
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/plots/signal_program/signal_program.hpp"

#include <cstddef>

// A signal program within its budget: bounded ranges, a visible helper, no host maths.

namespace effect_case {

struct AcceptedSignal final {
    static constexpr gui_forms::plots::SignalDeclaration declaration{"case_signal_passes", 16U};
    double level{1.0};
    void run(const gui_forms::plots::SignalFrame& frame, gui_forms::plots::SignalSink& sink) const noexcept;
};

void AcceptedSignal::run(const gui_forms::plots::SignalFrame& frame, gui_forms::plots::SignalSink& sink) const noexcept {
    for (const std::size_t tick : gui_forms::bounded_range<gui_forms::plots::maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample :
             gui_forms::bounded_range<gui_forms::plots::maximum_samples_per_tick>(frame.samples_per_tick)) {
            const double time = static_cast<double>(frame.first_tick + tick) + static_cast<double>(sample);
            const double value = level * time;
            static_cast<void>(sink.push(time, value));
        }
    }
}

gui_forms::plots::SignalEntry case_signal_passes_entry() {
    const gui_forms::plots::SignalEntry entry = gui_forms::plots::make_signal_entry<AcceptedSignal>();
    return entry;
}

} // namespace effect_case
