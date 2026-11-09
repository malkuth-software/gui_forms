// expect: reject signal program 'case_signal_over_budget': the static bound of run is [0-9]+ operations, over its declared budget \(operations_per_sample 1 .* signal_sink_capacity 1024\) of 1024.*this loop: 256 passes
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/plots/signal_program/signal_program.hpp"

#include <cstddef>

// A signal program whose run exceeds operations_per_sample times the sink capacity.

namespace effect_case {

struct OverBudgetSignal final {
    static constexpr gui_forms::plots::SignalDeclaration declaration{"case_signal_over_budget", 1U};
    double level{1.0};
    void run(const gui_forms::plots::SignalFrame& frame, gui_forms::plots::SignalSink& sink) const noexcept;
};

void OverBudgetSignal::run(const gui_forms::plots::SignalFrame& frame, gui_forms::plots::SignalSink& sink) const noexcept {
    for (const std::size_t tick : gui_forms::bounded_range<gui_forms::plots::maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample :
             gui_forms::bounded_range<gui_forms::plots::maximum_samples_per_tick>(frame.samples_per_tick)) {
            const double time = static_cast<double>(frame.first_tick + tick) + static_cast<double>(sample);
            const double value = level * time;
            static_cast<void>(sink.push(time, value));
        }
    }
}

gui_forms::plots::SignalEntry case_signal_over_budget_entry() {
    const gui_forms::plots::SignalEntry entry = gui_forms::plots::make_signal_entry<OverBudgetSignal>();
    return entry;
}

} // namespace effect_case
