#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace gui_forms::render::text {

// Injected failures, each at one append call: the face's shaper fails to
// open or the run's scratch to grow (std::bad_alloc), STXlib refuses the run
// (std::runtime_error), or storage fails after a run was shaped.
enum class TextLayoutFailure {
    none, shaper_open, glyph_storage, shape_refused, after_shape,
};

// Private diagnostic-only vocabulary. One snapshot belongs to one engine and
// is accessed only on that engine's executor. No global last-result state.
enum class TextLayoutPhase : std::size_t {
    store_graphemes, fallback, bidi, intersections, append_total,
    shaper_open, buffer_setup, shape, glyph_output, count,
};
struct TextLayoutDiagnostics final {
    std::array<std::uint64_t, static_cast<std::size_t>(TextLayoutPhase::count)> nanoseconds{};
    std::uint64_t graphemes{};
    std::uint64_t segments{};
    std::uint64_t directions{};
    std::uint64_t intersection_pairs{};
    std::uint64_t visual_runs{};
    std::uint64_t append_calls{};
};

// Fixed counters only: no allocation or logging. Nested append subphases are
// intentionally included in append_total; do not sum overlapping phases.
class TextLayoutPhaseTimer final {
public:
    TextLayoutPhaseTimer(TextLayoutDiagnostics& counters, TextLayoutPhase phase) noexcept
        : counters_(counters), phase_(static_cast<std::size_t>(phase)),
          start_(std::chrono::steady_clock::now()) {}
    TextLayoutPhaseTimer(const TextLayoutPhaseTimer&) = delete;
    TextLayoutPhaseTimer& operator=(const TextLayoutPhaseTimer&) = delete;
    ~TextLayoutPhaseTimer() { stop(); }
    void stop() noexcept {
        if (stopped_) { return; }
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::duration elapsed = end - start_;
        const std::chrono::nanoseconds duration = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
        const std::chrono::nanoseconds::rep count = duration.count();
        if (count > 0) {
            const std::uint64_t nanoseconds = static_cast<std::uint64_t>(count);
            counters_.nanoseconds[phase_] += nanoseconds;
        }
        stopped_ = true;
    }
private:
    TextLayoutDiagnostics& counters_;
    std::size_t phase_{};
    std::chrono::steady_clock::time_point start_{};
    bool stopped_{};
};

} // namespace gui_forms::render::text
