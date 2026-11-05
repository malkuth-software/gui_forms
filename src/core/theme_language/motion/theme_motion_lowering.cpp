#include "theme_motion_lowering.hpp"

#include "../source_text/theme_source_text.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A spring's overshoot is part of the curve the engine runs, so it is
// computed without fused multiply-add.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::theme_language {

namespace {

// -- the names --------------------------------------------------------------------

struct NamedTransitionProperty final {
    std::string_view name{};
    TransitionProperty property{TransitionProperty::transition};
};

constexpr std::array<NamedTransitionProperty, 5> transition_properties{{
    {.name = "transition", .property = TransitionProperty::transition},
    {.name = "transition-delay", .property = TransitionProperty::delay},
    {.name = "transition-duration", .property = TransitionProperty::duration},
    {.name = "transition-property", .property = TransitionProperty::property},
    {.name = "transition-timing-function", .property = TransitionProperty::timing_function},
}};

struct NamedAnimationProperty final {
    std::string_view name{};
    AnimationProperty property{AnimationProperty::animation};
};

constexpr std::array<NamedAnimationProperty, 11> animation_properties{{
    {.name = "animation", .property = AnimationProperty::animation},
    {.name = "animation-delay", .property = AnimationProperty::delay},
    {.name = "animation-direction", .property = AnimationProperty::direction},
    {.name = "animation-duration", .property = AnimationProperty::duration},
    {.name = "animation-fill-mode", .property = AnimationProperty::fill_mode},
    {.name = "animation-frame-rate", .property = AnimationProperty::frame_rate},
    {.name = "animation-iteration-count", .property = AnimationProperty::iteration_count},
    {.name = "animation-name", .property = AnimationProperty::name},
    {.name = "animation-play-state", .property = AnimationProperty::play_state},
    {.name = "animation-rest-pose", .property = AnimationProperty::rest_pose},
    {.name = "animation-timing-function", .property = AnimationProperty::timing_function},
}};

// The keywords of the three animation longhands that admit only their
// initial value (PLAN.md section 1.8): the initial value first.
enum class AnimationKeywordGroup : std::uint8_t { direction, fill_mode, play_state };

struct AnimationKeyword final {
    std::string_view name{};
    AnimationKeywordGroup group{AnimationKeywordGroup::direction};
    bool initial{};
};

constexpr std::array<AnimationKeyword, 10> animation_keywords{{
    {.name = "normal", .group = AnimationKeywordGroup::direction, .initial = true},
    {.name = "reverse", .group = AnimationKeywordGroup::direction, .initial = false},
    {.name = "alternate", .group = AnimationKeywordGroup::direction, .initial = false},
    {.name = "alternate-reverse", .group = AnimationKeywordGroup::direction, .initial = false},
    {.name = "none", .group = AnimationKeywordGroup::fill_mode, .initial = true},
    {.name = "forwards", .group = AnimationKeywordGroup::fill_mode, .initial = false},
    {.name = "backwards", .group = AnimationKeywordGroup::fill_mode, .initial = false},
    {.name = "both", .group = AnimationKeywordGroup::fill_mode, .initial = false},
    {.name = "running", .group = AnimationKeywordGroup::play_state, .initial = true},
    {.name = "paused", .group = AnimationKeywordGroup::play_state, .initial = false},
}};

[[nodiscard]] const AnimationKeyword* animation_keyword(std::string_view name) noexcept {
    for (const AnimationKeyword& keyword : animation_keywords) {
        if (equals_ascii_folded(name, keyword.name)) return &keyword;
    }
    return nullptr;
}

// The loops' frame rate when none is written: the ambient cap.
constexpr double default_loop_frame_rate = maximum_ambient_frames_per_second;

// The timing keywords and what each lowers to: the engine's own easings
// for `linear`, `ease` (its cubic ease) and `ease-in-out` (smoothstep),
// CSS's exact cubic-bezier() for `ease-in` and `ease-out` (R2-17), and
// one step for `step-start` and `step-end`.
struct TimingKeyword final {
    std::string_view name{};
    MotionEasing easing{MotionEasing::linear};
    std::array<double, 4> bezier{0.0, 0.0, 1.0, 1.0};
    MotionStepPosition step_position{MotionStepPosition::end};
};

constexpr std::array<TimingKeyword, 7> timing_keywords{{
    {.name = "linear", .easing = MotionEasing::linear, .bezier = {0.0, 0.0, 1.0, 1.0},
     .step_position = MotionStepPosition::end},
    {.name = "ease", .easing = MotionEasing::ease, .bezier = {0.0, 0.0, 1.0, 1.0},
     .step_position = MotionStepPosition::end},
    {.name = "ease-in-out", .easing = MotionEasing::smoothstep, .bezier = {0.0, 0.0, 1.0, 1.0},
     .step_position = MotionStepPosition::end},
    {.name = "ease-in", .easing = MotionEasing::cubic_bezier, .bezier = {0.42, 0.0, 1.0, 1.0},
     .step_position = MotionStepPosition::end},
    {.name = "ease-out", .easing = MotionEasing::cubic_bezier, .bezier = {0.0, 0.0, 0.58, 1.0},
     .step_position = MotionStepPosition::end},
    {.name = "step-start", .easing = MotionEasing::steps, .bezier = {0.0, 0.0, 1.0, 1.0},
     .step_position = MotionStepPosition::start},
    {.name = "step-end", .easing = MotionEasing::steps, .bezier = {0.0, 0.0, 1.0, 1.0},
     .step_position = MotionStepPosition::end},
}};

[[nodiscard]] const TimingKeyword* timing_keyword(std::string_view name) noexcept {
    for (const TimingKeyword& keyword : timing_keywords) {
        if (equals_ascii_folded(name, keyword.name)) return &keyword;
    }
    return nullptr;
}

// The timing functions written as functions.
enum class TimingFunctionName : std::uint8_t { cubic_bezier, steps, follow, spring, decay };

struct NamedTimingFunction final {
    std::string_view name{};
    TimingFunctionName function{TimingFunctionName::cubic_bezier};
};

constexpr std::array<NamedTimingFunction, 5> timing_functions{{
    {.name = "cubic-bezier", .function = TimingFunctionName::cubic_bezier},
    {.name = "steps", .function = TimingFunctionName::steps},
    {.name = "follow", .function = TimingFunctionName::follow},
    {.name = "spring", .function = TimingFunctionName::spring},
    {.name = "decay", .function = TimingFunctionName::decay},
}};

[[nodiscard]] const NamedTimingFunction* timing_function(std::string_view name) noexcept {
    for (const NamedTimingFunction& function : timing_functions) {
        if (equals_ascii_folded(name, function.name)) return &function;
    }
    return nullptr;
}

// A time in seconds as a frame interval, rounded to the clock's tick and
// at most the engine's ceiling, which every curve is clamped to anyway.
[[nodiscard]] FrameInterval interval_of(double seconds) noexcept {
    const double ceiling = std::chrono::duration<double>(motion_duration_ceiling).count();
    const double bounded = std::min(seconds, ceiling);
    const FrameInterval interval = std::chrono::round<FrameInterval>(std::chrono::duration<double>(bounded));
    return interval;
}

// A spring's overshoot for the damping ratio `damping` (0 < damping <= 1;
// owner decision, 2026-10-26): e^(-πζ / √(1 − ζ²)), none at 1.
[[nodiscard]] double spring_overshoot(double damping) noexcept {
    if (damping >= 1.0) return 0.0;
    const double pi = 3.14159265358979323846;
    const double exponent = -pi * damping / std::sqrt(1.0 - damping * damping);
    const double overshoot = std::exp(exponent);
    return overshoot;
}

// Components [first, end) of a value, at one nesting level.
struct ComponentRange final {
    std::size_t first{};
    std::size_t end{};
};

// One transition of the shorthand as read.
struct SingleTransition final {
    bool named{};
    bool none{};
    TransitionTarget target{};
    std::size_t times{};
    FrameInterval duration{};
    FrameInterval delay{};
    bool timed{};
    TimingFunction timing{};
};

// One animation of the shorthand as read: each longhand's value, and
// whether it was written.
struct SingleAnimation final {
    bool named{};
    AnimationName name{};
    std::size_t times{};
    double duration{};
    double delay{};
    bool timed{};
    TimingFunction timing{};
    bool counted{};
    AnimationRuns runs{AnimationRuns::once};
    std::array<bool, 3> grouped{};
};

// One lowering: reads `value_` and fills a written record's lists through
// staging lists, which replace them only when the whole value is read.
// Each reading function returns false once it has recorded a problem.
class MotionReader final {
public:
    MotionReader(const ThemeValue& value, std::uint32_t start, MotionLoweringProblem& problem) noexcept
        : value_(value), start_(start), problem_(problem) {}

    [[nodiscard]] MotionLoweringOutcome lower(TransitionProperty property, WrittenTransitions& written);
    [[nodiscard]] MotionLoweringOutcome lower_animation(AnimationProperty property, WrittenAnimations& written);

private:
    bool fail(DiagnosticCode code, std::uint32_t token, std::string detail);
    [[nodiscard]] std::size_t next(std::size_t index) const noexcept;
    void split(ComponentRange range, std::vector<ComponentRange>& parts) const;
    [[nodiscard]] bool keyword(std::size_t index, std::string_view lower_name) const noexcept;
    bool read_time(std::size_t index, bool negatives, double& seconds);
    bool read_number(std::size_t index, double& number);
    bool read_timing(std::size_t index, TimingFunction& timing);
    bool read_bezier(std::size_t function, TimingFunction& timing);
    bool read_steps(std::size_t function, TimingFunction& timing);
    bool read_physical(std::size_t function, TimingFunctionName name, TimingFunction& timing);
    bool read_target(std::size_t index, TransitionTarget& target);
    bool read_single(ComponentRange range, SingleTransition& single);
    // Whether `index` names a timing function, by keyword or function.
    [[nodiscard]] bool timing_component(std::size_t index) const noexcept;

    bool lower_shorthand();
    bool lower_properties();
    bool lower_times(bool delays);
    bool lower_timings();

    bool read_name(std::size_t index, AnimationName& name);
    bool read_runs(std::size_t index, AnimationRuns& runs);
    // An animation keyword of `group` other than its initial value is E405.
    bool admit_keyword(std::size_t index, const AnimationKeyword& keyword);
    bool read_single_animation(ComponentRange range, SingleAnimation& single);
    bool lower_animation_shorthand();
    bool lower_names();
    bool lower_seconds(bool delays);
    bool lower_runs();
    bool lower_initial_only(AnimationKeywordGroup group);
    bool lower_frame_rates();
    bool lower_poses();
    // Splits value_ into its comma-separated parts, each one component;
    // reports `shape` and returns false otherwise.
    bool single_components(std::string_view shape, std::vector<ComponentRange>& parts);

    const ThemeValue& value_;
    std::uint32_t start_{};
    MotionLoweringProblem& problem_;
    bool dropped_{};
    std::vector<ComponentRange> parts_{};
    std::vector<TransitionTarget> properties_{};
    std::vector<FrameInterval> durations_{};
    std::vector<TimingFunction> timings_{};
    std::vector<FrameInterval> delays_{};
    bool none_{};
    std::vector<AnimationName> names_{};
    std::vector<double> seconds_{};
    std::vector<double> delay_seconds_{};
    std::vector<AnimationRuns> runs_{};
    std::vector<double> frame_rates_{};
    std::vector<double> poses_{};
};

bool MotionReader::fail(DiagnosticCode code, std::uint32_t token, std::string detail) {
    dropped_ = diagnostic_severity(code) == DiagnosticSeverity::warning;
    problem_.code = code;
    problem_.token = token;
    problem_.detail = std::move(detail);
    return false;
}

std::size_t MotionReader::next(std::size_t index) const noexcept {
    const std::size_t after = value_.components[index].subtree_end;
    return after;
}

// The comma-separated parts of `range`, into `parts`, which is cleared
// first. An empty part is kept, for its reader to refuse.
void MotionReader::split(ComponentRange range, std::vector<ComponentRange>& parts) const {
    parts.clear();
    std::size_t first = range.first;
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        if (value_.components[index].kind != ValueComponentKind::comma) continue;
        parts.push_back(ComponentRange{first, index});
        first = index + 1U;
    }
    parts.push_back(ComponentRange{first, range.end});
}

bool MotionReader::keyword(std::size_t index, std::string_view lower_name) const noexcept {
    const ValueComponent& component = value_.components[index];
    const bool matches = component.kind == ValueComponentKind::keyword && equals_ascii_folded(component.text, lower_name);
    return matches;
}

// A <time> (s or ms, read in seconds); negative only with `negatives`.
bool MotionReader::read_time(std::size_t index, bool negatives, double& seconds) {
    const ValueComponent& component = value_.components[index];
    if (component.kind == ValueComponentKind::expression) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
    }
    if (component.kind != ValueComponentKind::number || component.unit != ValueUnit::seconds) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a time");
    }
    if (component.number < 0.0) {
        // A negative delay starts a move part of the way through, which the
        // engine's moves do not; a negative duration is no time at all.
        const DiagnosticCode code = negatives ? DiagnosticCode::value_form_not_admitted
                                              : DiagnosticCode::value_out_of_range;
        return fail(code, component.token, clipped_theme_detail(component.text));
    }
    seconds = component.number;
    return true;
}

bool MotionReader::read_number(std::size_t index, double& number) {
    const ValueComponent& component = value_.components[index];
    if (component.kind == ValueComponentKind::expression) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
    }
    if (component.kind != ValueComponentKind::number || component.unit != ValueUnit::none) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a number");
    }
    number = component.number;
    return true;
}

bool MotionReader::timing_component(std::size_t index) const noexcept {
    const ValueComponent& component = value_.components[index];
    if (component.kind == ValueComponentKind::keyword) return timing_keyword(component.text) != nullptr;
    if (component.kind == ValueComponentKind::function) return timing_function(component.text) != nullptr;
    return false;
}

// cubic-bezier(x1, y1, x2, y2): each x within [0, 1], as CSS requires;
// the engine clamps each y to its overshoot.
bool MotionReader::read_bezier(std::size_t function, TimingFunction& timing) {
    const ValueComponent& written = value_.components[function];
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{function + 1U, written.subtree_end}, arguments);
    if (arguments.size() != 4U) return fail(DiagnosticCode::value_syntax, written.token, "four numbers");
    for (std::size_t position = 0U; position < 4U; ++position) {
        const ComponentRange argument = arguments[position];
        if (argument.first == argument.end || next(argument.first) != argument.end) {
            return fail(DiagnosticCode::value_syntax, written.token, "four numbers");
        }
        double number = 0.0;
        if (!read_number(argument.first, number)) return false;
        const bool across = position == 0U || position == 2U;
        if (across && !(number >= 0.0 && number <= 1.0)) {
            const ValueComponent& component = value_.components[argument.first];
            return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
        }
        timing.bezier[position] = number;
    }
    timing.kind = MotionKind::timed;
    timing.easing = MotionEasing::cubic_bezier;
    return true;
}

// steps(n[, jump-start | jump-end | start | end]): 1 to maximum_motion_steps
// levels. jump-none and jump-both are CSS's, not the engine's.
bool MotionReader::read_steps(std::size_t function, TimingFunction& timing) {
    const ValueComponent& written = value_.components[function];
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{function + 1U, written.subtree_end}, arguments);
    if (arguments.size() > 2U) return fail(DiagnosticCode::value_syntax, written.token, "a count and a position");
    for (const ComponentRange argument : arguments) {
        if (argument.first == argument.end || next(argument.first) != argument.end) {
            return fail(DiagnosticCode::value_syntax, written.token, "a count and a position");
        }
    }
    double count = 0.0;
    if (!read_number(arguments[0].first, count)) return false;
    const ValueComponent& counted = value_.components[arguments[0].first];
    const bool whole = std::floor(count) == count;
    if (!whole || count < 1.0 || count > static_cast<double>(maximum_motion_steps)) {
        return fail(DiagnosticCode::value_out_of_range, counted.token, clipped_theme_detail(counted.text));
    }
    timing.kind = MotionKind::timed;
    timing.easing = MotionEasing::steps;
    timing.steps = static_cast<std::uint32_t>(count);
    timing.step_position = MotionStepPosition::end;
    if (arguments.size() == 1U) return true;
    const std::size_t position = arguments[1].first;
    const ValueComponent& placed = value_.components[position];
    if (keyword(position, "jump-start") || keyword(position, "start")) {
        timing.step_position = MotionStepPosition::start;
        return true;
    }
    if (keyword(position, "jump-end") || keyword(position, "end")) return true;
    if (keyword(position, "jump-none") || keyword(position, "jump-both")) {
        return fail(DiagnosticCode::value_form_not_admitted, placed.token, clipped_theme_detail(placed.text));
    }
    return fail(DiagnosticCode::keyword_unknown, placed.token, clipped_theme_detail(placed.text));
}

// follow(<time>), decay(<time>) and spring(<time>, <damping>), the
// engine's physical curves (C2). A spring's damping is its damping ratio,
// from the ratio that overshoots by maximum_motion_overshoot (about
// 0.627) to 1, critically damped (owner decision, 2026-10-26).
bool MotionReader::read_physical(std::size_t function, TimingFunctionName name, TimingFunction& timing) {
    const ValueComponent& written = value_.components[function];
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{function + 1U, written.subtree_end}, arguments);
    const std::size_t expected = name == TimingFunctionName::spring ? 2U : 1U;
    const std::string shape = name == TimingFunctionName::spring ? "a time and a damping ratio" : "a time";
    if (arguments.size() != expected) return fail(DiagnosticCode::value_syntax, written.token, shape);
    for (const ComponentRange argument : arguments) {
        if (argument.first == argument.end || next(argument.first) != argument.end) {
            return fail(DiagnosticCode::value_syntax, written.token, shape);
        }
    }
    double seconds = 0.0;
    if (!read_time(arguments[0].first, false, seconds)) return false;
    timing.constant = interval_of(seconds);
    timing.easing = MotionEasing::linear;
    if (name == TimingFunctionName::follow) {
        timing.kind = MotionKind::follow;
        return true;
    }
    if (name == TimingFunctionName::decay) {
        timing.kind = MotionKind::decay;
        return true;
    }
    double damping = 0.0;
    if (!read_number(arguments[1].first, damping)) return false;
    const double overshoot = damping > 0.0 ? spring_overshoot(damping) : 1.0;
    if (!(damping > 0.0 && damping <= 1.0) || overshoot > maximum_motion_overshoot) {
        const ValueComponent& component = value_.components[arguments[1].first];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    timing.kind = MotionKind::spring;
    timing.overshoot = overshoot;
    return true;
}

bool MotionReader::read_timing(std::size_t index, TimingFunction& timing) {
    const ValueComponent& component = value_.components[index];
    timing = TimingFunction{};
    timing.token = component.token;
    if (component.kind == ValueComponentKind::keyword) {
        const TimingKeyword* const named = timing_keyword(component.text);
        if (named == nullptr) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        }
        timing.kind = MotionKind::timed;
        timing.easing = (*named).easing;
        timing.bezier = (*named).bezier;
        timing.step_position = (*named).step_position;
        return true;
    }
    if (component.kind != ValueComponentKind::function) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a timing function");
    }
    const NamedTimingFunction* const function = timing_function(component.text);
    if (function == nullptr) {
        return fail(DiagnosticCode::unknown_function, component.token, clipped_theme_detail(component.text));
    }
    switch ((*function).function) {
    case TimingFunctionName::cubic_bezier: return read_bezier(index, timing);
    case TimingFunctionName::steps: return read_steps(index, timing);
    case TimingFunctionName::follow:
    case TimingFunctionName::spring:
    case TimingFunctionName::decay: return read_physical(index, (*function).function, timing);
    }
    return fail(DiagnosticCode::value_wrong_kind, component.token, "a timing function");
}

// A property a transition moves: the engine's presentational ones, or a
// parameter's kebab-case name. `all` would move every property, which the
// engine's machines cannot; `none` is read by the callers.
bool MotionReader::read_target(std::size_t index, TransitionTarget& target) {
    const ValueComponent& component = value_.components[index];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a property");
    }
    target = TransitionTarget{};
    target.token = component.token;
    if (keyword(index, "all")) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "all: a transition names its property");
    }
    if (keyword(index, "opacity")) {
        target.name = std::string(opacity_property);
        return true;
    }
    if (keyword(index, "scale")) {
        target.name = std::string(scale_property);
        return true;
    }
    if (keyword(index, "face")) {
        target.name = std::string(face_property);
        return true;
    }
    if (keyword(index, "translate")) {
        target.kind = TransitionTargetKind::translate;
        return true;
    }
    std::string folded(component.text);
    for (char& character : folded) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    target.kind = TransitionTargetKind::parameter;
    target.name = registered_name_from_kebab(folded);
    if (target.name.empty()) target.name = folded;
    return true;
}

// One transition of the shorthand: a property, up to two times (the
// duration, then the delay) and a timing function, each at most once and
// in any order.
bool MotionReader::read_single(ComponentRange range, SingleTransition& single) {
    single = SingleTransition{};
    if (range.first == range.end) {
        const std::uint32_t token = range.first < value_.components.size() ? value_.components[range.first].token
                                                                            : start_;
        return fail(DiagnosticCode::value_syntax, token, "a transition");
    }
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number || component.kind == ValueComponentKind::expression) {
            if (single.times == 2U) return fail(DiagnosticCode::value_syntax, component.token, "two times");
            double seconds = 0.0;
            if (!read_time(index, single.times == 1U, seconds)) return false;
            if (single.times == 0U) {
                single.duration = interval_of(seconds);
            } else {
                single.delay = interval_of(seconds);
            }
            single.times = single.times + 1U;
            continue;
        }
        if (timing_component(index) || (component.kind == ValueComponentKind::function)) {
            if (single.timed) return fail(DiagnosticCode::value_syntax, component.token, "one timing function");
            single.timed = true;
            if (!read_timing(index, single.timing)) return false;
            continue;
        }
        if (single.named) return fail(DiagnosticCode::value_syntax, component.token, "one property");
        single.named = true;
        if (keyword(index, "none")) {
            single.none = true;
            continue;
        }
        if (!read_target(index, single.target)) return false;
    }
    if (!single.named) {
        const ValueComponent& first = value_.components[range.first];
        return fail(DiagnosticCode::value_form_not_admitted, first.token, "a transition names its property");
    }
    return true;
}

bool MotionReader::lower_shorthand() {
    split(ComponentRange{0U, value_.components.size()}, parts_);
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        SingleTransition single{};
        if (!read_single(part, single)) return false;
        if (single.none) {
            if (parts.size() != 1U) {
                return fail(DiagnosticCode::value_syntax, value_.components[part.first].token, "none alone");
            }
            none_ = true;
        } else {
            properties_.push_back(single.target);
        }
        durations_.push_back(single.duration);
        TimingFunction timing = single.timing;
        if (!single.timed) timing = TimingFunction{};
        timings_.push_back(timing);
        delays_.push_back(single.delay);
    }
    return true;
}

bool MotionReader::lower_properties() {
    split(ComponentRange{0U, value_.components.size()}, parts_);
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        if (part.first == part.end || next(part.first) != part.end) {
            const std::uint32_t token = part.first < value_.components.size() ? value_.components[part.first].token
                                                                               : start_;
            return fail(DiagnosticCode::value_syntax, token, "one property");
        }
        if (keyword(part.first, "none")) {
            if (parts.size() != 1U) {
                return fail(DiagnosticCode::value_syntax, value_.components[part.first].token, "none alone");
            }
            none_ = true;
            continue;
        }
        TransitionTarget target{};
        if (!read_target(part.first, target)) return false;
        properties_.push_back(std::move(target));
    }
    return true;
}

bool MotionReader::lower_times(bool delays) {
    split(ComponentRange{0U, value_.components.size()}, parts_);
    const std::vector<ComponentRange> parts = parts_;
    std::vector<FrameInterval>& times = delays ? delays_ : durations_;
    for (const ComponentRange part : parts) {
        if (part.first == part.end || next(part.first) != part.end) {
            const std::uint32_t token = part.first < value_.components.size() ? value_.components[part.first].token
                                                                               : start_;
            return fail(DiagnosticCode::value_syntax, token, "one time");
        }
        double seconds = 0.0;
        if (!read_time(part.first, delays, seconds)) return false;
        times.push_back(interval_of(seconds));
    }
    return true;
}

bool MotionReader::lower_timings() {
    split(ComponentRange{0U, value_.components.size()}, parts_);
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        if (part.first == part.end || next(part.first) != part.end) {
            const std::uint32_t token = part.first < value_.components.size() ? value_.components[part.first].token
                                                                               : start_;
            return fail(DiagnosticCode::value_syntax, token, "one timing function");
        }
        TimingFunction timing{};
        if (!read_timing(part.first, timing)) return false;
        timings_.push_back(timing);
    }
    return true;
}

MotionLoweringOutcome MotionReader::lower(TransitionProperty property, WrittenTransitions& written) {
    if (value_.components.empty()) {
        fail(DiagnosticCode::value_syntax, start_, "a value");
        return MotionLoweringOutcome::refused;
    }
    bool read = false;
    switch (property) {
    case TransitionProperty::transition: read = lower_shorthand(); break;
    case TransitionProperty::property: read = lower_properties(); break;
    case TransitionProperty::duration: read = lower_times(false); break;
    case TransitionProperty::timing_function: read = lower_timings(); break;
    case TransitionProperty::delay: read = lower_times(true); break;
    // A value outside the enumeration reads nothing: `read` stays false.
    default: break;
    }
    if (!read) return dropped_ ? MotionLoweringOutcome::dropped : MotionLoweringOutcome::refused;
    written.token = start_;
    const bool shorthand = property == TransitionProperty::transition;
    if (shorthand || property == TransitionProperty::property) {
        written.properties_set = true;
        written.properties = std::move(properties_);
        if (none_) written.properties.clear();
    }
    if (shorthand || property == TransitionProperty::duration) {
        written.durations_set = true;
        written.durations = std::move(durations_);
    }
    if (shorthand || property == TransitionProperty::timing_function) {
        written.timings_set = true;
        written.timings = std::move(timings_);
    }
    if (shorthand || property == TransitionProperty::delay) {
        written.delays_set = true;
        written.delays = std::move(delays_);
    }
    return MotionLoweringOutcome::lowered;
}

// -- animations --------------------------------------------------------------------

// A keyframes name: a name that is no keyword of CSS's (`none` is read
// by the callers), or a string.
bool MotionReader::read_name(std::size_t index, AnimationName& name) {
    const ValueComponent& component = value_.components[index];
    const bool word = component.kind == ValueComponentKind::keyword || component.kind == ValueComponentKind::string;
    if (!word || component.text.empty()) return fail(DiagnosticCode::value_wrong_kind, component.token, "a keyframes name");
    if (component.kind == ValueComponentKind::keyword) {
        const bool reserved = equals_ascii_folded(component.text, "initial") ||
                              equals_ascii_folded(component.text, "inherit") ||
                              equals_ascii_folded(component.text, "unset") ||
                              equals_ascii_folded(component.text, "default");
        if (reserved) return fail(DiagnosticCode::value_syntax, component.token, clipped_theme_detail(component.text));
    }
    name.name = std::string(component.text);
    name.token = component.token;
    return true;
}

// An iteration count: 1 runs once, `infinite` loops; the engine repeats
// nothing else (E405), and a negative count is E403.
bool MotionReader::read_runs(std::size_t index, AnimationRuns& runs) {
    const ValueComponent& component = value_.components[index];
    if (keyword(index, "infinite")) {
        runs = AnimationRuns::loop;
        return true;
    }
    double count = 0.0;
    if (!read_number(index, count)) return false;
    if (count < 0.0) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    if (count != 1.0) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an iteration count other than 1 or infinite");
    }
    runs = AnimationRuns::once;
    return true;
}

bool MotionReader::admit_keyword(std::size_t index, const AnimationKeyword& keyword) {
    if (keyword.initial) return true;
    const ValueComponent& component = value_.components[index];
    return fail(DiagnosticCode::value_form_not_admitted, component.token, clipped_theme_detail(component.text));
}

// One animation of the shorthand: up to two times (the duration, then the
// delay), a timing function, an iteration count, a direction, a fill mode,
// a play state and a name, each at most once and in any order. As CSS
// reads it, a keyword is a longhand's while that longhand is unwritten,
// and a name only after that; so `none` is first the fill mode.
bool MotionReader::read_single_animation(ComponentRange range, SingleAnimation& single) {
    single = SingleAnimation{};
    if (range.first == range.end) {
        const std::uint32_t token = range.first < value_.components.size() ? value_.components[range.first].token
                                                                            : start_;
        return fail(DiagnosticCode::value_syntax, token, "an animation");
    }
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        const bool time = (component.kind == ValueComponentKind::number && component.unit == ValueUnit::seconds) ||
                          component.kind == ValueComponentKind::expression;
        if (time) {
            if (single.times == 2U) return fail(DiagnosticCode::value_syntax, component.token, "two times");
            double seconds = 0.0;
            if (!read_time(index, single.times == 1U, seconds)) return false;
            if (single.times == 0U) {
                single.duration = seconds;
            } else {
                single.delay = seconds;
            }
            single.times = single.times + 1U;
            continue;
        }
        const bool count = component.kind == ValueComponentKind::number || keyword(index, "infinite");
        if (count && !single.counted) {
            single.counted = true;
            if (!read_runs(index, single.runs)) return false;
            continue;
        }
        if ((timing_component(index) || component.kind == ValueComponentKind::function) && !single.timed) {
            single.timed = true;
            if (!read_timing(index, single.timing)) return false;
            continue;
        }
        const AnimationKeyword* const grouped =
            component.kind == ValueComponentKind::keyword ? animation_keyword(component.text) : nullptr;
        if (grouped != nullptr && !single.grouped[static_cast<std::size_t>((*grouped).group)]) {
            single.grouped[static_cast<std::size_t>((*grouped).group)] = true;
            if (!admit_keyword(index, *grouped)) return false;
            continue;
        }
        if (single.named) return fail(DiagnosticCode::value_syntax, component.token, "one keyframes name");
        single.named = true;
        if (keyword(index, "none")) continue;
        if (!read_name(index, single.name)) return false;
    }
    return true;
}

bool MotionReader::lower_animation_shorthand() {
    split(ComponentRange{0U, value_.components.size()}, parts_);
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        SingleAnimation single{};
        if (!read_single_animation(part, single)) return false;
        names_.push_back(single.name);
        seconds_.push_back(single.duration);
        TimingFunction timing = single.timing;
        if (!single.timed) timing = TimingFunction{};
        timings_.push_back(timing);
        delay_seconds_.push_back(single.delay);
        runs_.push_back(single.runs);
    }
    return true;
}

bool MotionReader::single_components(std::string_view shape, std::vector<ComponentRange>& parts) {
    split(ComponentRange{0U, value_.components.size()}, parts);
    for (const ComponentRange part : parts) {
        if (part.first == part.end || next(part.first) != part.end) {
            const std::uint32_t token = part.first < value_.components.size() ? value_.components[part.first].token
                                                                               : start_;
            return fail(DiagnosticCode::value_syntax, token, std::string(shape));
        }
    }
    return true;
}

bool MotionReader::lower_names() {
    if (!single_components("one keyframes name", parts_)) return false;
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        AnimationName name{};
        if (!keyword(part.first, "none") && !read_name(part.first, name)) return false;
        names_.push_back(std::move(name));
    }
    return true;
}

bool MotionReader::lower_seconds(bool delays) {
    if (!single_components("one time", parts_)) return false;
    const std::vector<ComponentRange> parts = parts_;
    std::vector<double>& times = delays ? delay_seconds_ : seconds_;
    for (const ComponentRange part : parts) {
        double seconds = 0.0;
        if (!read_time(part.first, delays, seconds)) return false;
        times.push_back(seconds);
    }
    return true;
}

bool MotionReader::lower_runs() {
    if (!single_components("one iteration count", parts_)) return false;
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        AnimationRuns runs = AnimationRuns::once;
        if (!read_runs(part.first, runs)) return false;
        runs_.push_back(runs);
    }
    return true;
}

bool MotionReader::lower_initial_only(AnimationKeywordGroup group) {
    if (!single_components("one keyword", parts_)) return false;
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        const ValueComponent& component = value_.components[part.first];
        const AnimationKeyword* const named =
            component.kind == ValueComponentKind::keyword ? animation_keyword(component.text) : nullptr;
        if (named == nullptr || (*named).group != group) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        }
        if (!admit_keyword(part.first, *named)) return false;
    }
    return true;
}

// animation-frame-rate: frames a second, above 0 and at most the ambient
// cap.
bool MotionReader::lower_frame_rates() {
    if (!single_components("one frame rate", parts_)) return false;
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        double rate = 0.0;
        if (!read_number(part.first, rate)) return false;
        if (!(rate > 0.0 && rate <= maximum_ambient_frames_per_second)) {
            const ValueComponent& component = value_.components[part.first];
            return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
        }
        frame_rates_.push_back(rate);
    }
    return true;
}

// animation-rest-pose: a time within the loop's period (checked when the
// animations are built).
bool MotionReader::lower_poses() {
    if (!single_components("one time", parts_)) return false;
    const std::vector<ComponentRange> parts = parts_;
    for (const ComponentRange part : parts) {
        double seconds = 0.0;
        if (!read_time(part.first, false, seconds)) return false;
        poses_.push_back(seconds);
    }
    return true;
}

MotionLoweringOutcome MotionReader::lower_animation(AnimationProperty property, WrittenAnimations& written) {
    if (value_.components.empty()) {
        fail(DiagnosticCode::value_syntax, start_, "a value");
        return MotionLoweringOutcome::refused;
    }
    bool read = false;
    switch (property) {
    case AnimationProperty::animation: read = lower_animation_shorthand(); break;
    case AnimationProperty::name: read = lower_names(); break;
    case AnimationProperty::duration: read = lower_seconds(false); break;
    case AnimationProperty::timing_function: read = lower_timings(); break;
    case AnimationProperty::delay: read = lower_seconds(true); break;
    case AnimationProperty::iteration_count: read = lower_runs(); break;
    case AnimationProperty::direction: read = lower_initial_only(AnimationKeywordGroup::direction); break;
    case AnimationProperty::fill_mode: read = lower_initial_only(AnimationKeywordGroup::fill_mode); break;
    case AnimationProperty::play_state: read = lower_initial_only(AnimationKeywordGroup::play_state); break;
    case AnimationProperty::frame_rate: read = lower_frame_rates(); break;
    case AnimationProperty::rest_pose: read = lower_poses(); break;
    // A value outside the enumeration reads nothing: `read` stays false.
    default: break;
    }
    if (!read) return dropped_ ? MotionLoweringOutcome::dropped : MotionLoweringOutcome::refused;
    written.token = start_;
    const bool shorthand = property == AnimationProperty::animation;
    if (shorthand || property == AnimationProperty::name) {
        written.names_set = true;
        written.names = std::move(names_);
    }
    if (shorthand || property == AnimationProperty::duration) {
        written.durations_set = true;
        written.durations = std::move(seconds_);
    }
    if (shorthand || property == AnimationProperty::timing_function) {
        written.timings_set = true;
        written.timings = std::move(timings_);
    }
    if (shorthand || property == AnimationProperty::delay) {
        written.delays_set = true;
        written.delays = std::move(delay_seconds_);
    }
    if (shorthand || property == AnimationProperty::iteration_count) {
        written.runs_set = true;
        written.runs = std::move(runs_);
    }
    if (property == AnimationProperty::frame_rate) {
        written.frame_rates_set = true;
        written.frame_rates = std::move(frame_rates_);
    }
    if (property == AnimationProperty::rest_pose) {
        written.poses_set = true;
        written.poses = std::move(poses_);
    }
    return MotionLoweringOutcome::lowered;
}

// The curve of one transition: its timing function with its duration and
// delay. The physical curves keep their own time, so they take no
// duration.
bool curve_of(const TimingFunction& timing, FrameInterval duration, FrameInterval delay, MotionCurve& curve,
              MotionLoweringProblem& problem) {
    switch (timing.kind) {
    case MotionKind::timed:
        if (timing.easing == MotionEasing::cubic_bezier) {
            curve = MotionCurve::cubic_bezier(duration, timing.bezier[0], timing.bezier[1], timing.bezier[2],
                                              timing.bezier[3]);
        } else if (timing.easing == MotionEasing::steps) {
            curve = MotionCurve::stepped(duration, timing.steps, timing.step_position);
        } else {
            curve = MotionCurve::timed(duration, timing.easing);
        }
        break;
    case MotionKind::follow:
    case MotionKind::spring:
    case MotionKind::decay:
        if (duration != FrameInterval{}) {
            problem.code = DiagnosticCode::value_form_not_admitted;
            problem.token = timing.token;
            problem.detail = "a duration with follow(), spring() or decay(), whose own time sets the pace";
            return false;
        }
        if (timing.kind == MotionKind::follow) curve = MotionCurve::follow(timing.constant);
        if (timing.kind == MotionKind::decay) curve = MotionCurve::decay(timing.constant);
        if (timing.kind == MotionKind::spring) curve = MotionCurve::spring(timing.constant, timing.overshoot);
        break;
    // A value outside the enumeration keeps the caller's curve.
    default: break;
    }
    if (delay != FrameInterval{}) curve = curve.delayed(delay);
    return true;
}

void declare(std::string_view property, const MotionCurve& curve, std::uint32_t token,
             std::vector<DeclaredTransition>& declared) {
    for (std::size_t index = 0U; index < declared.size(); ++index) {
        if (declared[index].property != property) continue;
        declared.erase(declared.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    DeclaredTransition transition{};
    transition.property = std::string(property);
    transition.curve = curve;
    transition.token = token;
    declared.push_back(std::move(transition));
}

// Whether `refining` names the same control and part as `host`, with
// every state of `host`'s and more, under the same media condition: a
// state joined to a machine of another condition would hold in windows
// its own rule does not.
bool refines(const ThemeRule& refining, const ThemeRule& host) noexcept {
    const RuleSelector& left = refining.selector;
    const RuleSelector& right = host.selector;
    if (refining.level != host.level || refining.specificity != host.specificity) return false;
    if (!(refining.media == host.media)) return false;
    if (left.control_type != right.control_type || left.stable_id != right.stable_id ||
        left.classes != right.classes || left.part != right.part || left.role != right.role) {
        return false;
    }
    if (left.states.size() <= right.states.size()) return false;
    for (const AnatomyState state : right.states) {
        if (std::find(left.states.begin(), left.states.end(), state) == left.states.end()) return false;
    }
    return true;
}

// A refinement found for one host, by its rule's precedence: a static
// value's machine (origin `origin`) or a parameter's declaration (at
// `position` of the rule's declarations), and the value it sets.
struct Refinement final {
    RuleSpecificity specificity{};
    std::size_t rule{};
    std::size_t origin{};
    bool declaration{};
    std::size_t position{};
    // The state it becomes, but for its `when`: its value, and an
    // animation's entry value and stops.
    PresentationState state{};
};

// A declaration of a rule, to remove once absorbed.
struct DeclarationPosition final {
    std::size_t rule{};
    std::size_t declaration{};

    friend bool operator==(DeclarationPosition left, DeclarationPosition right) noexcept {
        return left.rule == right.rule && left.declaration == right.declaration;
    }
};

// The later position first, so that removing one keeps the earlier ones'.
struct LaterPosition final {
    bool operator()(DeclarationPosition left, DeclarationPosition right) const noexcept {
        if (left.rule != right.rule) return left.rule > right.rule;
        return left.declaration > right.declaration;
    }
};

// The primitive a rule selects by name, or empty when it selects none.
std::string_view selected_primitive(const ThemeRule& rule) noexcept {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == primitive_property) return declaration.name;
    }
    return {};
}

// Whether `property` is one of the engine's presentational properties
// rather than a primitive's parameter.
bool engine_property(std::string_view property) noexcept {
    const bool engine = property == opacity_property || property == scale_property || property == face_property ||
                        property == translate_x_property || property == translate_y_property;
    return engine;
}

// The more precedent refinement first: the higher specificity, then the
// later rule.
struct MorePrecedent final {
    bool operator()(const Refinement& left, const Refinement& right) const noexcept {
        if (!(left.specificity == right.specificity)) return right.specificity < left.specificity;
        return left.rule > right.rule;
    }
};

// One animation's settings, at its position in the rule's lists.
struct AnimationSettings final {
    const AnimationName* name{};
    double duration{};
    TimingFunction timing{};
    double delay{};
    AnimationRuns runs{AnimationRuns::once};
    double frame_rate{default_loop_frame_rate};
    double pose{};
};

void fail_at(MotionLoweringProblem& problem, DiagnosticCode code, std::uint32_t token, std::string detail) {
    problem.code = code;
    problem.token = token;
    problem.detail = std::move(detail);
}

// The parameter of `primitive` that `track` moves, checked: a number
// parameter (animated when `animated`), every frame within its range.
// Null after a problem.
const PrimitiveParameterDeclaration* track_parameter(const KeyframeTrack& track, const PrimitiveDescriptor* primitive,
                                                     bool animated, std::uint32_t token,
                                                     MotionLoweringProblem& problem) {
    const std::optional<std::size_t> found =
        primitive == nullptr ? std::optional<std::size_t>{} : find_parameter(*primitive, track.property);
    if (!found) {
        fail_at(problem, DiagnosticCode::value_form_not_admitted, token,
                track.property + ": keyframes move opacity, scale, translate and the parameters of the rule's "
                                 "primitive");
        return nullptr;
    }
    const PrimitiveParameterDeclaration& parameter = (*primitive).parameters[*found];
    if (parameter.kind != PrimitiveParameterKind::number || (animated && !parameter.animated)) {
        fail_at(problem, DiagnosticCode::value_wrong_kind, token, animated ? "an animated parameter" : "a number parameter");
        return nullptr;
    }
    for (std::size_t frame = 0U; frame < track.frames.size(); ++frame) {
        const double value = track.frames[frame].value;
        if (value < parameter.minimum || value > parameter.maximum) {
            fail_at(problem, DiagnosticCode::value_out_of_range, track.tokens[frame], track.property);
            return nullptr;
        }
    }
    return &parameter;
}

// Removes an earlier animation's move or loop of `property`: the later
// animation of a property wins, as in CSS.
void replace_animated(DeclaredAnimations& animations, std::string_view property) {
    for (std::size_t index = 0U; index < animations.moves.size(); ++index) {
        if (animations.moves[index].property != property) continue;
        animations.moves.erase(animations.moves.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    for (std::size_t index = 0U; index < animations.loops.size(); ++index) {
        if (animations.loops[index].parameter != property) continue;
        animations.loops.erase(animations.loops.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
}

// An animation run once: each track becomes a move, its curve the
// animation's (clamped to the motion ceiling, as a transition's is).
bool add_moves(const KeyframesDefinition& definition, const AnimationSettings& settings,
               const PrimitiveDescriptor* primitive, DeclaredAnimations& animations, MotionLoweringProblem& problem) {
    const std::uint32_t token = (*settings.name).token;
    MotionCurve curve{};
    if (!curve_of(settings.timing, interval_of(settings.duration), interval_of(settings.delay), curve, problem)) {
        return false;
    }
    for (const KeyframeTrack& track : definition.tracks) {
        if (track.parameter && track_parameter(track, primitive, true, token, problem) == nullptr) return false;
        DeclaredMove move{};
        move.property = track.property;
        move.parameter = track.parameter;
        move.curve = curve;
        move.token = token;
        for (const MotionStop& frame : track.frames) {
            if (frame.offset == 0.0) {
                move.from = frame.value;
            } else if (frame.offset == 1.0) {
                move.to = frame.value;
            } else {
                move.stops.push_back(frame);
            }
        }
        if (move.stops.size() > maximum_motion_stops) {
            fail_at(problem, DiagnosticCode::too_many_layers, token,
                    std::to_string(maximum_motion_stops) + " keyframes between from and to");
            return false;
        }
        replace_animated(animations, move.property);
        animations.moves.push_back(std::move(move));
    }
    return true;
}

// An animation that loops: each track becomes a loop of a number
// parameter, and the animation the rule's ambient effect, which every
// loop of the rule shares.
bool add_loops(const KeyframesDefinition& definition, const AnimationSettings& settings,
               const PrimitiveDescriptor* primitive, DeclaredAnimations& animations, MotionLoweringProblem& problem) {
    const std::uint32_t token = (*settings.name).token;
    if (settings.delay != 0.0) {
        fail_at(problem, DiagnosticCode::value_form_not_admitted, token, "a delay of a looping animation");
        return false;
    }
    const double longest = std::chrono::duration<double>(maximum_loop_period).count();
    if (!(settings.duration > 0.0 && settings.duration <= longest)) {
        fail_at(problem, DiagnosticCode::value_out_of_range, token,
                "a loop's duration, above 0s and at most " + std::to_string(static_cast<int>(longest)) + "s");
        return false;
    }
    if (!(settings.pose < settings.duration)) {
        fail_at(problem, DiagnosticCode::value_out_of_range, token, "a rest pose within the loop's duration");
        return false;
    }
    AmbientEffect ambient{};
    ambient.period = std::chrono::round<FrameInterval>(std::chrono::duration<double>(settings.duration));
    ambient.frames_per_second = settings.frame_rate;
    ambient.pose = std::chrono::round<FrameInterval>(std::chrono::duration<double>(settings.pose));
    if (animations.ambient && !(*animations.ambient == ambient)) {
        fail_at(problem, DiagnosticCode::value_form_not_admitted, token,
                "loops of one rule with different durations, frame rates or rest poses");
        return false;
    }
    animations.ambient = ambient;
    for (const KeyframeTrack& track : definition.tracks) {
        if (!track.parameter) {
            fail_at(problem, DiagnosticCode::value_form_not_admitted, token,
                    track.property + " in a looping animation, which moves the primitive's parameters");
            return false;
        }
        if (track_parameter(track, primitive, false, token, problem) == nullptr) return false;
        DeclaredLoop loop{};
        loop.parameter = track.property;
        loop.frames = track.frames;
        loop.timing = settings.timing;
        loop.token = token;
        replace_animated(animations, loop.parameter);
        animations.loops.push_back(std::move(loop));
    }
    return true;
}

// Whether `rule` declares nothing: no value, no scoped custom property
// definition (P12) and no backdrop.
bool rule_empty(const ThemeRule& rule) noexcept {
    const bool empty = rule.declarations.empty() && rule.transitions.empty() && !rule.ambient.has_value() &&
                       rule.materials.empty() && rule.recipe_fields.empty() && rule.scoped_definitions.empty() &&
                       !rule.backdrop.has_value();
    return empty;
}

} // namespace

std::optional<TransitionProperty> transition_property(std::string_view name) noexcept {
    for (const NamedTransitionProperty& named : transition_properties) {
        if (named.name == name) return named.property;
    }
    return std::nullopt;
}

void clear_written_transitions(WrittenTransitions& written) noexcept {
    written.properties_set = false;
    written.properties.clear();
    written.durations_set = false;
    written.durations.clear();
    written.timings_set = false;
    written.timings.clear();
    written.delays_set = false;
    written.delays.clear();
    written.token = 0U;
}

bool transitions_written(const WrittenTransitions& written) noexcept {
    const bool any = written.properties_set || written.durations_set || written.timings_set || written.delays_set;
    return any;
}

MotionLoweringOutcome lower_transition_value(TransitionProperty property, const ThemeValue& value, std::uint32_t start,
                                             WrittenTransitions& written, MotionLoweringProblem& problem) {
    MotionReader reader(value, start, problem);
    const MotionLoweringOutcome outcome = reader.lower(property, written);
    return outcome;
}

void reset_transition_lists(TransitionProperty property, WrittenTransitions& written) noexcept {
    const bool shorthand = property == TransitionProperty::transition;
    if (shorthand || property == TransitionProperty::property) {
        written.properties_set = true;
        written.properties.clear();
    }
    if (shorthand || property == TransitionProperty::duration) {
        written.durations_set = false;
        written.durations.clear();
    }
    if (shorthand || property == TransitionProperty::timing_function) {
        written.timings_set = false;
        written.timings.clear();
    }
    if (shorthand || property == TransitionProperty::delay) {
        written.delays_set = false;
        written.delays.clear();
    }
}

std::optional<AnimationProperty> animation_property(std::string_view name) noexcept {
    for (const NamedAnimationProperty& named : animation_properties) {
        if (named.name == name) return named.property;
    }
    return std::nullopt;
}

void clear_written_animations(WrittenAnimations& written) noexcept {
    written.names_set = false;
    written.names.clear();
    written.durations_set = false;
    written.durations.clear();
    written.timings_set = false;
    written.timings.clear();
    written.delays_set = false;
    written.delays.clear();
    written.runs_set = false;
    written.runs.clear();
    written.frame_rates_set = false;
    written.frame_rates.clear();
    written.poses_set = false;
    written.poses.clear();
    written.token = 0U;
}

bool animations_written(const WrittenAnimations& written) noexcept {
    for (const AnimationName& name : written.names) {
        if (!name.name.empty()) return true;
    }
    return false;
}

MotionLoweringOutcome lower_animation_value(AnimationProperty property, const ThemeValue& value, std::uint32_t start,
                                            WrittenAnimations& written, MotionLoweringProblem& problem) {
    MotionReader reader(value, start, problem);
    const MotionLoweringOutcome outcome = reader.lower_animation(property, written);
    return outcome;
}

void reset_animation_lists(AnimationProperty property, WrittenAnimations& written) noexcept {
    const bool shorthand = property == AnimationProperty::animation;
    if (shorthand || property == AnimationProperty::name) {
        written.names_set = false;
        written.names.clear();
    }
    if (shorthand || property == AnimationProperty::duration) {
        written.durations_set = false;
        written.durations.clear();
    }
    if (shorthand || property == AnimationProperty::timing_function) {
        written.timings_set = false;
        written.timings.clear();
    }
    if (shorthand || property == AnimationProperty::delay) {
        written.delays_set = false;
        written.delays.clear();
    }
    if (shorthand || property == AnimationProperty::iteration_count) {
        written.runs_set = false;
        written.runs.clear();
    }
    if (property == AnimationProperty::frame_rate) {
        written.frame_rates_set = false;
        written.frame_rates.clear();
    }
    if (property == AnimationProperty::rest_pose) {
        written.poses_set = false;
        written.poses.clear();
    }
}

void add_keyframe(KeyframesDefinition& definition, std::string_view property, bool parameter, double offset,
                  double value, std::uint32_t token) {
    KeyframeTrack* track = nullptr;
    for (KeyframeTrack& existing : definition.tracks) {
        if (existing.property == property && existing.parameter == parameter) track = &existing;
    }
    if (track == nullptr) {
        KeyframeTrack made{};
        made.property = std::string(property);
        made.parameter = parameter;
        definition.tracks.push_back(std::move(made));
        track = &definition.tracks.back();
    }
    KeyframeTrack& kept = *track;
    std::size_t position = 0U;
    while (position < kept.frames.size() && kept.frames[position].offset < offset) position = position + 1U;
    if (position < kept.frames.size() && kept.frames[position].offset == offset) {
        kept.frames[position].value = value;
        kept.tokens[position] = token;
        return;
    }
    MotionStop frame{};
    frame.offset = offset;
    frame.value = value;
    kept.frames.insert(kept.frames.begin() + static_cast<std::ptrdiff_t>(position), frame);
    kept.tokens.insert(kept.tokens.begin() + static_cast<std::ptrdiff_t>(position), token);
}

MotionLoweringOutcome build_declared_animations(const WrittenAnimations& written,
                                                std::span<const KeyframesDefinition> keyframes,
                                                const PrimitiveDescriptor* primitive, DeclaredAnimations& animations,
                                                MotionLoweringProblem& problem) {
    animations.moves.clear();
    animations.loops.clear();
    animations.ambient.reset();
    for (std::size_t index = 0U; index < written.names.size(); ++index) {
        const AnimationName& name = written.names[index];
        if (name.name.empty()) continue;
        const KeyframesDefinition* definition = nullptr;
        for (const KeyframesDefinition& candidate : keyframes) {
            if (candidate.name == name.name) definition = &candidate;
        }
        if (definition == nullptr) {
            problem.code = DiagnosticCode::keyframes_undefined;
            problem.token = name.token;
            problem.detail = clipped_theme_detail(name.name);
            return MotionLoweringOutcome::refused;
        }
        AnimationSettings settings{};
        settings.name = &name;
        if (!written.durations.empty()) settings.duration = written.durations[index % written.durations.size()];
        if (!written.timings.empty()) settings.timing = written.timings[index % written.timings.size()];
        if (!written.delays.empty()) settings.delay = written.delays[index % written.delays.size()];
        if (!written.runs.empty()) settings.runs = written.runs[index % written.runs.size()];
        if (!written.frame_rates.empty()) {
            settings.frame_rate = written.frame_rates[index % written.frame_rates.size()];
        }
        if (!written.poses.empty()) settings.pose = written.poses[index % written.poses.size()];
        if (settings.timing.kind != MotionKind::timed) {
            problem.code = DiagnosticCode::value_form_not_admitted;
            problem.token = settings.timing.token;
            problem.detail = "follow(), spring() or decay() in an animation, whose keyframes are placed in time";
            return MotionLoweringOutcome::refused;
        }
        const bool built = settings.runs == AnimationRuns::once
                               ? add_moves(*definition, settings, primitive, animations, problem)
                               : add_loops(*definition, settings, primitive, animations, problem);
        if (!built) return MotionLoweringOutcome::refused;
    }
    return MotionLoweringOutcome::lowered;
}

MotionLoweringOutcome loop_program(std::span<const MotionStop> frames, const TimingFunction& timing,
                                   FrameInterval period, std::uint32_t token,
                                   std::vector<ExpressionInstruction>& program, MotionLoweringProblem& problem) {
    program.clear();
    const bool linear = timing.easing == MotionEasing::linear;
    const bool smooth = timing.easing == MotionEasing::smoothstep;
    const bool stepped = timing.easing == MotionEasing::steps;
    if (timing.kind != MotionKind::timed || !(linear || smooth || stepped)) {
        problem.code = DiagnosticCode::value_form_not_admitted;
        problem.token = timing.token == 0U ? token : timing.token;
        problem.detail = "a looping animation's timing function: linear, ease-in-out or steps()";
        return MotionLoweringOutcome::refused;
    }
    // Each interval costs: linear 11 instructions, ease-in-out 7, steps 6
    // a step; the first value 1.
    std::size_t cost = 1U;
    for (std::size_t interval = 0U; interval + 1U < frames.size(); ++interval) {
        if (linear) cost += 11U;
        if (smooth) cost += 7U;
        if (stepped) cost += 6U * static_cast<std::size_t>(timing.steps);
    }
    if (frames.size() < 2U || cost > maximum_expression_operations) {
        problem.code = DiagnosticCode::expression_over_budget;
        problem.token = token;
        problem.detail = std::to_string(cost) + " instructions; at most " +
                         std::to_string(maximum_expression_operations);
        return MotionLoweringOutcome::refused;
    }
    const double seconds = std::chrono::duration<double>(period).count();
    const ExpressionInstruction time = ExpressionInstruction::input(engine_input_position(EngineInput::time_in_state));
    // v0 plus, for each interval k, (v[k+1] - v[k]) times its eased
    // progress, which is 0 before the interval and 1 after it: the value
    // within interval k is v[k] plus its part of the change.
    program.push_back(ExpressionInstruction::number(frames[0].value));
    for (std::size_t interval = 0U; interval + 1U < frames.size(); ++interval) {
        const double begin = frames[interval].offset * seconds;
        const double end = frames[interval + 1U].offset * seconds;
        const double change = frames[interval + 1U].value - frames[interval].value;
        if (linear) {
            program.push_back(time);
            program.push_back(ExpressionInstruction::number(begin));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::subtract));
            program.push_back(ExpressionInstruction::number(end - begin));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::divide));
            program.push_back(ExpressionInstruction::number(0.0));
            program.push_back(ExpressionInstruction::number(1.0));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::clamp));
            program.push_back(ExpressionInstruction::number(change));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::multiply));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::add));
            continue;
        }
        if (smooth) {
            program.push_back(ExpressionInstruction::number(begin));
            program.push_back(ExpressionInstruction::number(end));
            program.push_back(time);
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::smoothstep));
            program.push_back(ExpressionInstruction::number(change));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::multiply));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::add));
            continue;
        }
        // steps(n): n jumps of change / n, at the end of each nth of the
        // interval (jump-end) or at its start (jump-start), as the engine's
        // stepped easing places them.
        const double width = (end - begin) / static_cast<double>(timing.steps);
        const double jump = change / static_cast<double>(timing.steps);
        const double first = timing.step_position == MotionStepPosition::start ? 0.0 : 1.0;
        for (std::uint32_t step = 0U; step < timing.steps; ++step) {
            const double at = begin + (first + static_cast<double>(step)) * width;
            program.push_back(time);
            program.push_back(ExpressionInstruction::number(at));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::greater_equal));
            program.push_back(ExpressionInstruction::number(jump));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::multiply));
            program.push_back(ExpressionInstruction::apply(ExpressionOperation::add));
        }
    }
    return MotionLoweringOutcome::lowered;
}

MotionLoweringOutcome build_declared_transitions(const WrittenTransitions& written, const PrimitiveDescriptor* primitive,
                                                 std::vector<DeclaredTransition>& declared,
                                                 MotionLoweringProblem& problem) {
    declared.clear();
    if (!transitions_written(written)) return MotionLoweringOutcome::lowered;
    if (!written.properties_set) {
        // CSS's initial property is `all`, which the engine cannot move.
        problem.code = DiagnosticCode::value_form_not_admitted;
        problem.token = written.token;
        problem.detail = "a transition names its properties (transition-property)";
        return MotionLoweringOutcome::refused;
    }
    for (std::size_t index = 0U; index < written.properties.size(); ++index) {
        const TransitionTarget& target = written.properties[index];
        FrameInterval duration{};
        if (!written.durations.empty()) duration = written.durations[index % written.durations.size()];
        TimingFunction timing{};
        if (!written.timings.empty()) timing = written.timings[index % written.timings.size()];
        FrameInterval delay{};
        if (!written.delays.empty()) delay = written.delays[index % written.delays.size()];
        if (target.kind == TransitionTargetKind::parameter) {
            const std::optional<std::size_t> found =
                primitive == nullptr ? std::optional<std::size_t>{} : find_parameter(*primitive, target.name);
            if (!found) {
                problem.code = DiagnosticCode::value_form_not_admitted;
                problem.token = target.token;
                problem.detail = target.name + ": transitions move opacity, scale, translate, face and the animated "
                                               "parameters of the rule's primitive";
                return MotionLoweringOutcome::refused;
            }
            const PrimitiveParameterDeclaration& parameter = (*primitive).parameters[*found];
            if (parameter.kind != PrimitiveParameterKind::number || !parameter.animated) {
                problem.code = DiagnosticCode::value_wrong_kind;
                problem.token = target.token;
                problem.detail = "an animated parameter";
                return MotionLoweringOutcome::refused;
            }
        }
        MotionCurve curve{};
        if (!curve_of(timing, duration, delay, curve, problem)) return MotionLoweringOutcome::refused;
        if (target.kind == TransitionTargetKind::translate) {
            declare(translate_x_property, curve, target.token, declared);
            declare(translate_y_property, curve, target.token, declared);
        } else {
            declare(target.name, curve, target.token, declared);
        }
    }
    return MotionLoweringOutcome::lowered;
}

MotionLoweringOutcome group_transitions(ThemeRuleSheet& sheet, std::span<const MachineOrigin> origins,
                                        MotionLoweringProblem& problem) {
    std::vector<bool> absorbed(origins.size(), false);
    std::vector<DeclarationPosition> absorbed_declarations{};
    std::vector<Refinement> refinements{};
    for (const MachineOrigin& host : origins) {
        if (!host.curved) continue;
        const ThemeRule& host_rule = sheet.rules[host.rule];
        const std::string& property = host_rule.transitions[host.machine].property;
        const std::string_view primitive = selected_primitive(host_rule);
        refinements.clear();
        for (std::size_t index = 0U; index < origins.size(); ++index) {
            const MachineOrigin& candidate = origins[index];
            if (candidate.curved || candidate.rule == host.rule) continue;
            const ThemeRule& rule = sheet.rules[candidate.rule];
            const PropertyTransition& refining = rule.transitions[candidate.machine];
            if (refining.property != property || !refines(rule, host_rule)) continue;
            // A parameter belongs to the primitive its rule selects.
            if (!engine_property(property) && selected_primitive(rule) != primitive) continue;
            if (candidate.animated && !(refining.curve == host_rule.transitions[host.machine].curve)) {
                // Owner decision, 2026-10-27: the machine runs one curve.
                problem.code = DiagnosticCode::value_form_not_admitted;
                problem.token = candidate.token;
                problem.detail = "an animation joining a transition of " + property +
                                 " takes the transition's duration, timing function and delay";
                return MotionLoweringOutcome::refused;
            }
            Refinement refinement{};
            refinement.specificity = rule.specificity ? *rule.specificity : rule_specificity(rule.selector);
            refinement.rule = candidate.rule;
            refinement.origin = index;
            refinement.state = refining.states[0];
            refinements.push_back(std::move(refinement));
        }
        // A parameter's static values are declarations of rules selecting
        // the same primitive.
        for (std::size_t index = 0U; index < sheet.rules.size() && !primitive.empty(); ++index) {
            const ThemeRule& rule = sheet.rules[index];
            if (index == host.rule || selected_primitive(rule) != primitive || !refines(rule, host_rule)) continue;
            const RuleSpecificity specificity = rule.specificity ? *rule.specificity : rule_specificity(rule.selector);
            for (std::size_t position = 0U; position < rule.declarations.size(); ++position) {
                const RuleDeclaration& declaration = rule.declarations[position];
                if (declaration.property != property || declaration.kind != RuleValueKind::number) continue;
                Refinement refinement{};
                refinement.specificity = specificity;
                refinement.rule = index;
                refinement.declaration = true;
                refinement.position = position;
                refinement.state.target = declaration.number;
                refinement.state.target_em = declaration.number_em;
                refinements.push_back(std::move(refinement));
            }
        }
        PropertyTransition& machine = sheet.rules[host.rule].transitions[host.machine];
        if (refinements.size() + machine.states.size() > maximum_presentation_states) {
            problem.code = DiagnosticCode::too_many_layers;
            problem.token = host.token;
            problem.detail = std::to_string(maximum_presentation_states) + " states of one transition";
            return MotionLoweringOutcome::refused;
        }
        std::sort(refinements.begin(), refinements.end(), MorePrecedent{});
        std::vector<PresentationState> states{};
        states.reserve(refinements.size() + machine.states.size());
        for (Refinement& refinement : refinements) {
            PresentationState state = std::move(refinement.state);
            state.when = sheet.rules[refinement.rule].selector.states;
            states.push_back(std::move(state));
            if (refinement.declaration) {
                absorbed_declarations.push_back(DeclarationPosition{refinement.rule, refinement.position});
            } else {
                absorbed[refinement.origin] = true;
            }
        }
        for (PresentationState& own : machine.states) states.push_back(std::move(own));
        machine.states = std::move(states);
    }
    // Remove the absorbed machines and the curved ones left with nothing to
    // move, from the last machine of each rule, so that the positions of
    // the ones still to remove hold; then the absorbed declarations, the
    // same way.
    for (std::size_t index = origins.size(); index > 0U; --index) {
        const MachineOrigin& origin = origins[index - 1U];
        std::vector<PropertyTransition>& machines = sheet.rules[origin.rule].transitions;
        const PropertyTransition& machine = machines[origin.machine];
        const bool idle = origin.curved && machine.states.empty() && machine.property != face_property;
        if (!absorbed[index - 1U] && !idle) continue;
        machines.erase(machines.begin() + static_cast<std::ptrdiff_t>(origin.machine));
    }
    std::sort(absorbed_declarations.begin(), absorbed_declarations.end(), LaterPosition{});
    const std::vector<DeclarationPosition>::iterator unique_end =
        std::unique(absorbed_declarations.begin(), absorbed_declarations.end());
    absorbed_declarations.erase(unique_end, absorbed_declarations.end());
    for (const DeclarationPosition& position : absorbed_declarations) {
        std::vector<RuleDeclaration>& declarations = sheet.rules[position.rule].declarations;
        declarations.erase(declarations.begin() + static_cast<std::ptrdiff_t>(position.declaration));
    }
    std::vector<ThemeRule> kept{};
    kept.reserve(sheet.rules.size());
    for (ThemeRule& rule : sheet.rules) {
        if (!rule_empty(rule)) kept.push_back(std::move(rule));
    }
    sheet.rules = std::move(kept);
    return MotionLoweringOutcome::lowered;
}

} // namespace gui_forms::theme_language
