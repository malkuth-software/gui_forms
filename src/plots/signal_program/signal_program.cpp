// Signal programs. Every function from a program's run down obeys the
// effect rules (gui_forms/engine/effect/effect.hpp): bounded loops only, no
// recursion, allocation, globals, clocks or host maths. The plugin's effect
// pass verifies them through the entries in signal_entries below.

#include "gui_forms/plots/signal_program/signal_program.hpp"

// Built without the gui_forms plugin's effect pass (GUI_FORMS_EFFECT_VERIFY
// OFF, or a toolchain that cannot load the plugin), CMake defines this, and
// the programs' bounds go unchecked.
#if defined(GUI_FORMS_EFFECTS_UNVERIFIED)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma message("signal programs are unverified: their bounds are not checked")
#else
#warning "signal programs are unverified: their bounds are not checked"
#endif
#endif

#include "gui_forms/engine/effect/effect.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <string_view>
#include <system_error>

namespace gui_forms::plots {

namespace {

constexpr double two_pi = 6.283185307179586477;

// The registered programs, in SignalKind order: one make_signal_entry
// instantiation each, which the effect pass verifies.
constexpr std::array<SignalEntry, 6> signal_entries{
    make_signal_entry<SineSignal>(),  make_signal_entry<SquareSignal>(), make_signal_entry<SawSignal>(),
    make_signal_entry<ChirpSignal>(), make_signal_entry<NoiseSignal>(),  make_signal_entry<OscillatorBank>(),
};

// The sample index of sample `sample` of tick `tick`.
std::uint64_t sample_index(const SignalFrame& frame, std::uint64_t tick, std::uint64_t sample) noexcept {
    const std::uint64_t result = tick * frame.samples_per_tick + sample;
    return result;
}

double sample_time(const SignalFrame& frame, std::uint64_t index) noexcept {
    const double result = static_cast<double>(index) / frame.sample_rate();
    return result;
}

// The fraction of a cycle of `frequency` at `time` seconds, in [0, 1): the
// cycles less their whole part (effect_fraction; 0 past 2^52 cycles). The
// time comes from the sample index, so it never accumulates error.
double cycle_fraction(double frequency, double time) noexcept {
    const double fraction = effect_fraction(frequency * time);
    return fraction;
}

double sine_of_fraction(double fraction, double phase) noexcept {
    const double value = effect_sine(two_pi * fraction + phase);
    return value;
}

bool usable(const SignalFrame& frame) noexcept {
    const bool result = frame.samples_per_tick != 0U && frame.samples_per_tick <= maximum_samples_per_tick &&
                        frame.tick_count <= maximum_ticks_per_frame;
    return result;
}

} // namespace

// Each run: the frame's ticks, then each tick's samples, over bounded ranges
// (usable() has already refused counts past their capacities).

void SineSignal::run(const SignalFrame& frame, SignalSink& sink) const noexcept {
    if (!usable(frame)) return;
    for (const std::size_t tick : bounded_range<maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample : bounded_range<maximum_samples_per_tick>(frame.samples_per_tick)) {
            const std::uint64_t index = sample_index(frame, frame.first_tick + tick, sample);
            const double time = sample_time(frame, index);
            const double value = offset + amplitude * sine_of_fraction(cycle_fraction(frequency, time), phase);
            static_cast<void>(sink.push(time, value));
        }
    }
}

void SquareSignal::run(const SignalFrame& frame, SignalSink& sink) const noexcept {
    if (!usable(frame)) return;
    for (const std::size_t tick : bounded_range<maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample : bounded_range<maximum_samples_per_tick>(frame.samples_per_tick)) {
            const std::uint64_t index = sample_index(frame, frame.first_tick + tick, sample);
            const double time = sample_time(frame, index);
            const double fraction = cycle_fraction(frequency, time);
            const double value = offset + (fraction < duty ? amplitude : -amplitude);
            static_cast<void>(sink.push(time, value));
        }
    }
}

void SawSignal::run(const SignalFrame& frame, SignalSink& sink) const noexcept {
    if (!usable(frame)) return;
    for (const std::size_t tick : bounded_range<maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample : bounded_range<maximum_samples_per_tick>(frame.samples_per_tick)) {
            const std::uint64_t index = sample_index(frame, frame.first_tick + tick, sample);
            const double time = sample_time(frame, index);
            const double fraction = cycle_fraction(frequency, time);
            const double value = offset + amplitude * (2.0 * fraction - 1.0);
            static_cast<void>(sink.push(time, value));
        }
    }
}

void ChirpSignal::run(const SignalFrame& frame, SignalSink& sink) const noexcept {
    if (!usable(frame) || !(sweep_seconds > 0.0)) return;
    for (const std::size_t tick : bounded_range<maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample : bounded_range<maximum_samples_per_tick>(frame.samples_per_tick)) {
            const std::uint64_t index = sample_index(frame, frame.first_tick + tick, sample);
            const double time = sample_time(frame, index);
            const double within = effect_fraction(time / sweep_seconds) * sweep_seconds;
            // Phase in cycles: f0 t + (f1 - f0) t² / 2T, within one sweep.
            const double cycles =
                frequency * within + (end_frequency - frequency) * within * within / (2.0 * sweep_seconds);
            const double fraction = effect_fraction(cycles);
            const double value = amplitude * sine_of_fraction(fraction, 0.0);
            static_cast<void>(sink.push(time, value));
        }
    }
}

void NoiseSignal::run(const SignalFrame& frame, SignalSink& sink) const noexcept {
    if (!usable(frame)) return;
    for (const std::size_t tick : bounded_range<maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample : bounded_range<maximum_samples_per_tick>(frame.samples_per_tick)) {
            const std::uint64_t index = sample_index(frame, frame.first_tick + tick, sample);
            const std::uint64_t bits = EffectRandom::hash(seed, 0U, index) >> 11U;
            const double unit = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
            const double value = offset + amplitude * (2.0 * unit - 1.0);
            static_cast<void>(sink.push(sample_time(frame, index), value));
        }
    }
}

void OscillatorBank::run(const SignalFrame& frame, SignalSink& sink) const noexcept {
    if (!usable(frame)) return;
    for (const std::size_t tick : bounded_range<maximum_ticks_per_frame>(frame.tick_count)) {
        for (const std::size_t sample : bounded_range<maximum_samples_per_tick>(frame.samples_per_tick)) {
            const std::uint64_t index = sample_index(frame, frame.first_tick + tick, sample);
            const double time = sample_time(frame, index);
            double value = offset;
            // count is clamped to maximum_oscillators by the range.
            for (const std::size_t partial : bounded_range<maximum_oscillators>(count)) {
                const Oscillator& oscillator = oscillators[partial];
                const double fraction = cycle_fraction(oscillator.frequency, time);
                value = value + oscillator.amplitude * sine_of_fraction(fraction, oscillator.phase);
            }
            static_cast<void>(sink.push(time, value));
        }
    }
}

void run_signal(const SignalProgram& program, const SignalFrame& frame, SignalSink& sink) noexcept {
    switch (program.kind) {
    case SignalKind::sine:
        program.sine.run(frame, sink);
        return;
    case SignalKind::square:
        program.square.run(frame, sink);
        return;
    case SignalKind::saw:
        program.saw.run(frame, sink);
        return;
    case SignalKind::chirp:
        program.chirp.run(frame, sink);
        return;
    case SignalKind::noise:
        program.noise.run(frame, sink);
        return;
    case SignalKind::oscillators:
        program.oscillators.run(frame, sink);
        return;
    }
}

const SignalDeclaration& signal_declaration(SignalKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= signal_entries.size()) return SineSignal::declaration;
    return *signal_entries[index].declaration;
}

// -- SignalClock -------------------------------------------------------------------

SignalFrame SignalClock::advance(FrameTime now, std::uint32_t samples_per_tick) noexcept {
    SignalFrame frame{};
    frame.samples_per_tick = samples_per_tick;
    const std::chrono::duration<double> tick_length(1.0 / signal_tick_rate);
    if (!started_) {
        // Tick next_tick_ is due now.
        const std::chrono::duration<double> elapsed = tick_length * static_cast<double>(next_tick_);
        origin_ = now - std::chrono::duration_cast<FrameInterval>(elapsed);
        started_ = true;
    }
    const std::chrono::duration<double> since = now - origin_;
    if (since.count() < 0.0) return frame;
    // Ticks whose time has come: those at or before now.
    const double due_ticks = std::floor(since.count() * signal_tick_rate) + 1.0;
    const std::uint64_t due = static_cast<std::uint64_t>(due_ticks);
    if (due <= next_tick_) return frame;
    std::uint64_t pending = due - next_tick_;
    if (pending > maximum_ticks_per_frame) {
        // Skip the excess: run the newest ticks.
        skipped_ = skipped_ + (pending - maximum_ticks_per_frame);
        next_tick_ = due - maximum_ticks_per_frame;
        pending = maximum_ticks_per_frame;
    }
    frame.first_tick = next_tick_;
    frame.tick_count = static_cast<std::uint32_t>(pending);
    next_tick_ = next_tick_ + pending;
    return frame;
}

// -- parsing -------------------------------------------------------------------------

namespace {

std::string_view trim(std::string_view text) noexcept {
    std::size_t first = 0U;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) first = first + 1U;
    std::size_t end = text.size();
    while (end > first && (text[end - 1U] == ' ' || text[end - 1U] == '\t')) end = end - 1U;
    return text.substr(first, end - first);
}

// A number with an optional unit suffix `unit` (case-sensitive: Hz, s);
// false unless the whole text is consumed.
bool parse_value(std::string_view text, std::string_view unit, double& value) noexcept {
    std::string_view digits = trim(text);
    if (!unit.empty() && digits.size() > unit.size() && digits.substr(digits.size() - unit.size()) == unit) {
        digits = digits.substr(0U, digits.size() - unit.size());
    }
    double parsed = 0.0;
    const std::from_chars_result read = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
    if (read.ec != std::errc{} || read.ptr != digits.data() + digits.size() || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

bool parse_count(std::string_view text, std::uint64_t& value) noexcept {
    const std::string_view digits = trim(text);
    std::uint64_t parsed = 0U;
    const std::from_chars_result read = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
    if (read.ec != std::errc{} || read.ptr != digits.data() + digits.size()) return false;
    value = parsed;
    return true;
}

bool kind_named(std::string_view name, SignalKind& kind) noexcept {
    const std::array<SignalKind, 6> kinds{SignalKind::sine,  SignalKind::square, SignalKind::saw,
                                          SignalKind::chirp, SignalKind::noise,  SignalKind::oscillators};
    for (const SignalKind candidate : kinds) {
        if (signal_declaration(candidate).name != name) continue;
        kind = candidate;
        return true;
    }
    return false;
}

// Applies one `name value` parameter to `program`.
SignalParseOutcome apply_parameter(std::string_view name, std::string_view value, SignalProgram& program,
                                   std::uint32_t& seen) noexcept {
    // One bit per parameter name, so none is written twice (oscillators'
    // repeated frequency and amplitude excepted).
    const std::array<std::string_view, 10> names{"frequency", "amplitude", "phase", "offset", "duty",
                                                 "end-frequency", "sweep", "seed", "samples", ""};
    std::size_t bit = names.size();
    for (std::size_t index = 0U; index + 1U < names.size(); ++index) {
        if (names[index] == name) bit = index;
    }
    if (bit == names.size()) return SignalParseOutcome::unknown_parameter;
    const bool repeatable = program.kind == SignalKind::oscillators && (bit == 0U || bit == 1U);
    const std::uint32_t mask = 1U << bit;
    if (!repeatable && (seen & mask) != 0U) return SignalParseOutcome::unknown_parameter;
    seen = seen | mask;
    if (name == "samples") {
        std::uint64_t samples = 0U;
        if (!parse_count(value, samples) || samples == 0U || samples > maximum_samples_per_tick) {
            return SignalParseOutcome::invalid_value;
        }
        program.samples_per_tick = static_cast<std::uint32_t>(samples);
        return SignalParseOutcome::parsed;
    }
    if (name == "seed") {
        if (program.kind != SignalKind::noise) return SignalParseOutcome::unknown_parameter;
        std::uint64_t seed = 0U;
        if (!parse_count(value, seed)) return SignalParseOutcome::invalid_value;
        program.noise.seed = seed;
        return SignalParseOutcome::parsed;
    }
    const bool frequency_like = name == "frequency" || name == "end-frequency";
    const std::string_view unit = frequency_like ? std::string_view("Hz") : (name == "sweep" ? "s" : "");
    double number = 0.0;
    if (!parse_value(value, unit, number)) return SignalParseOutcome::invalid_value;
    switch (program.kind) {
    case SignalKind::sine:
        if (name == "frequency") program.sine.frequency = number;
        else if (name == "amplitude") program.sine.amplitude = number;
        else if (name == "phase") program.sine.phase = number;
        else if (name == "offset") program.sine.offset = number;
        else return SignalParseOutcome::unknown_parameter;
        return SignalParseOutcome::parsed;
    case SignalKind::square:
        if (name == "frequency") program.square.frequency = number;
        else if (name == "amplitude") program.square.amplitude = number;
        else if (name == "duty") program.square.duty = number;
        else if (name == "offset") program.square.offset = number;
        else return SignalParseOutcome::unknown_parameter;
        return SignalParseOutcome::parsed;
    case SignalKind::saw:
        if (name == "frequency") program.saw.frequency = number;
        else if (name == "amplitude") program.saw.amplitude = number;
        else if (name == "offset") program.saw.offset = number;
        else return SignalParseOutcome::unknown_parameter;
        return SignalParseOutcome::parsed;
    case SignalKind::chirp:
        if (name == "frequency") program.chirp.frequency = number;
        else if (name == "end-frequency") program.chirp.end_frequency = number;
        else if (name == "sweep") program.chirp.sweep_seconds = number;
        else if (name == "amplitude") program.chirp.amplitude = number;
        else return SignalParseOutcome::unknown_parameter;
        return SignalParseOutcome::parsed;
    case SignalKind::noise:
        if (name == "amplitude") program.noise.amplitude = number;
        else if (name == "offset") program.noise.offset = number;
        else return SignalParseOutcome::unknown_parameter;
        return SignalParseOutcome::parsed;
    case SignalKind::oscillators: {
        OscillatorBank& bank = program.oscillators;
        if (name == "offset") {
            bank.offset = number;
            return SignalParseOutcome::parsed;
        }
        if (name == "frequency") {
            if (bank.count == maximum_oscillators) return SignalParseOutcome::invalid_value;
            bank.oscillators[bank.count] = Oscillator{number, 1.0, 0.0};
            bank.count = bank.count + 1U;
            return SignalParseOutcome::parsed;
        }
        // An amplitude applies to the latest oscillator.
        if (name == "amplitude" && bank.count != 0U) {
            bank.oscillators[bank.count - 1U].amplitude = number;
            return SignalParseOutcome::parsed;
        }
        return SignalParseOutcome::unknown_parameter;
    }
    }
    return SignalParseOutcome::unknown_parameter;
}

} // namespace

SignalParseOutcome parse_signal_source(std::string_view text, SignalProgram& program) noexcept {
    std::string_view body = trim(text);
    constexpr std::string_view opening = "signal(";
    if (body.substr(0U, opening.size()) == opening) {
        if (body.empty() || body.back() != ')') return SignalParseOutcome::malformed;
        body = body.substr(opening.size(), body.size() - opening.size() - 1U);
    }
    SignalProgram parsed{};
    std::size_t comma = body.find(',');
    const std::string_view kind_name = trim(body.substr(0U, comma));
    if (kind_name.empty()) return SignalParseOutcome::malformed;
    if (!kind_named(kind_name, parsed.kind)) return SignalParseOutcome::unknown_kind;
    std::uint32_t seen = 0U;
    while (comma != std::string_view::npos) {
        const std::size_t start = comma + 1U;
        comma = body.find(',', start);
        const std::string_view parameter =
            trim(body.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        const std::size_t space = parameter.find(' ');
        if (space == std::string_view::npos) return SignalParseOutcome::malformed;
        const SignalParseOutcome applied =
            apply_parameter(parameter.substr(0U, space), parameter.substr(space + 1U), parsed, seen);
        if (applied != SignalParseOutcome::parsed) return applied;
    }
    program = parsed;
    return SignalParseOutcome::parsed;
}

} // namespace gui_forms::plots
