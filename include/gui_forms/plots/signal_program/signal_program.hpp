#pragma once

// Signal programs: generated data for plots (IMGUI_IMPLOT_STUDY B.3, P13;
// owner decision 11: an effect-program sibling). The effect-program
// pattern turned to data: one class per program, its parameters and its
// trivially copyable state, with a bounded run() that writes at most the
// sink's fixed capacity of samples per frame. Programs feed series; they
// never draw.
//
// Time: fixed ticks of 1/60 s (signal_tick_rate), at most four per frame
// (no catch-up beyond: a plot that missed more skips them, and its samples
// keep their true times, so the gap shows as a straight line). Each tick
// writes `samples_per_tick` samples; sample n (counted from the program's
// start) is at time n / (60 · samples_per_tick) and depends only on n, so
// grouping ticks into frames differently never changes the samples.
//
// Determinism: sines through engine_sine_cosine with the phase reduced to
// one cycle first (its bound holds at any run length); noise from
// EffectRandom's hash keyed by the sample index; no library randomness, no
// allocation, no indirect calls. The clock is the motion clock's or a live
// sub-surface's frame time (LiveSurfacePacer), so tests install a
// MotionTimeSource or pass times directly.

#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace gui_forms::plots {

inline constexpr double signal_tick_rate = 60.0;
inline constexpr std::uint32_t maximum_ticks_per_frame = 4U;
inline constexpr std::uint32_t maximum_samples_per_tick = 256U;
inline constexpr std::size_t signal_sink_capacity =
    static_cast<std::size_t>(maximum_ticks_per_frame) * maximum_samples_per_tick;

// The ticks one frame runs: [first_tick, first_tick + tick_count).
struct SignalFrame final {
    std::uint64_t first_tick{};
    std::uint32_t tick_count{};
    std::uint32_t samples_per_tick{16U};

    [[nodiscard]] double sample_rate() const noexcept {
        const double result = signal_tick_rate * static_cast<double>(samples_per_tick);
        return result;
    }
};

// Where a program writes: fixed capacity, times in seconds from the
// program's start. Reused frame to frame (clear() first).
struct SignalSink final {
    std::array<double, signal_sink_capacity> times{};
    std::array<double, signal_sink_capacity> values{};
    std::size_t count{};

    void clear() noexcept { count = 0U; }
    // False (nothing written) when full.
    bool push(double time, double value) noexcept {
        if (count == signal_sink_capacity) return false;
        times[count] = time;
        values[count] = value;
        count = count + 1U;
        return true;
    }
};

// A program's declaration: its registered name and its declared budget in
// operations per sample (the effect plugin's measure, per sample instead of
// per device pixel).
struct SignalDeclaration final {
    std::string_view name{};
    std::uint32_t operations_per_sample{};
};

struct SineSignal final {
    static constexpr SignalDeclaration declaration{"sine", 40U};
    double frequency{1.0};
    double amplitude{1.0};
    // Radians.
    double phase{};
    double offset{};
    void run(const SignalFrame& frame, SignalSink& sink) const noexcept;
};

struct SquareSignal final {
    static constexpr SignalDeclaration declaration{"square", 12U};
    double frequency{1.0};
    double amplitude{1.0};
    // The fraction of each cycle at +amplitude.
    double duty{0.5};
    double offset{};
    void run(const SignalFrame& frame, SignalSink& sink) const noexcept;
};

struct SawSignal final {
    static constexpr SignalDeclaration declaration{"saw", 10U};
    double frequency{1.0};
    double amplitude{1.0};
    double offset{};
    void run(const SignalFrame& frame, SignalSink& sink) const noexcept;
};

// A linear chirp from `frequency` to `end_frequency` over each sweep of
// `sweep_seconds`, repeating.
struct ChirpSignal final {
    static constexpr SignalDeclaration declaration{"chirp", 48U};
    double frequency{1.0};
    double end_frequency{10.0};
    double sweep_seconds{5.0};
    double amplitude{1.0};
    void run(const SignalFrame& frame, SignalSink& sink) const noexcept;
};

// Uniform noise in [-amplitude, amplitude] (plus offset), keyed by sample.
struct NoiseSignal final {
    static constexpr SignalDeclaration declaration{"noise", 16U};
    double amplitude{1.0};
    double offset{};
    std::uint64_t seed{0x5eedU};
    void run(const SignalFrame& frame, SignalSink& sink) const noexcept;
};

inline constexpr std::size_t maximum_oscillators = 8U;

struct Oscillator final {
    double frequency{1.0};
    double amplitude{1.0};
    double phase{};
};

// A sum of up to maximum_oscillators sines.
struct OscillatorBank final {
    static constexpr SignalDeclaration declaration{"oscillators", 40U * maximum_oscillators};
    std::array<Oscillator, maximum_oscillators> oscillators{};
    std::size_t count{};
    double offset{};
    void run(const SignalFrame& frame, SignalSink& sink) const noexcept;
};

static_assert(std::is_trivially_copyable_v<SineSignal> && std::is_trivially_copyable_v<OscillatorBank>,
              "signal programs' state must be trivially copyable");

enum class SignalKind : std::uint8_t { sine, square, saw, chirp, noise, oscillators };

// One program of any kind, by value: the kind chooses which member is live
// (a tagged record rather than a callable, so no indirect call runs).
struct SignalProgram final {
    SignalKind kind{SignalKind::sine};
    SineSignal sine{};
    SquareSignal square{};
    SawSignal saw{};
    ChirpSignal chirp{};
    NoiseSignal noise{};
    OscillatorBank oscillators{};
    // Samples per tick (1 to maximum_samples_per_tick).
    std::uint32_t samples_per_tick{16U};
};

// Runs the live program for `frame` into `sink` (not cleared first).
void run_signal(const SignalProgram& program, const SignalFrame& frame, SignalSink& sink) noexcept;
[[nodiscard]] const SignalDeclaration& signal_declaration(SignalKind kind) noexcept;

// Converts frame times to ticks. Starts at the first advance() (tick 0 at
// that time); each advance runs the ticks due since, at most
// maximum_ticks_per_frame of them, skipping the excess.
class SignalClock final {
public:
    // The ticks due at `now`; an empty frame when none are.
    SignalFrame advance(FrameTime now, std::uint32_t samples_per_tick) noexcept;
    // Forgets the start (motion paused, the plot hidden): the next advance
    // runs the tick after the last one run, at that advance's time, so the
    // samples continue without a jump and without catching up.
    void rebase() noexcept { started_ = false; }
    [[nodiscard]] std::uint64_t next_tick() const noexcept { return next_tick_; }
    [[nodiscard]] std::uint64_t skipped_ticks() const noexcept { return skipped_; }

private:
    FrameTime origin_{};
    std::uint64_t next_tick_{};
    std::uint64_t skipped_{};
    bool started_{};
};

enum class SignalParseOutcome : std::uint8_t {
    parsed,
    // Not `signal(<kind>, ...)` or `<kind>, ...`.
    malformed,
    unknown_kind,
    // A parameter the kind does not take, or one written twice.
    unknown_parameter,
    // A value that is not a finite number (with its unit, Hz or s, where
    // one is admitted).
    invalid_value,
};

// Parses a form's source declaration, `signal(sine, frequency 2Hz,
// amplitude 1)` (or the same without `signal(` and `)`), into `program`
// (written only when parsed). Parameters by kind: sine frequency,
// amplitude, phase, offset; square frequency, amplitude, duty, offset; saw
// frequency, amplitude, offset; chirp frequency, end-frequency, sweep,
// amplitude; noise amplitude, offset, seed; oscillators frequency and
// amplitude repeated (one oscillator per frequency), offset. All take
// samples (per tick). A form names data, not code: the kind must be
// registered here.
SignalParseOutcome parse_signal_source(std::string_view text, SignalProgram& program) noexcept;

} // namespace gui_forms::plots
