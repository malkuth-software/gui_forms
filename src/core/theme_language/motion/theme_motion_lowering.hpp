#pragma once

// The motion lowering (stage T3, PLAN.md P7, section 1.8): CSS's
// `transition` and its four longhands, read from a rule's declarations,
// become the curves of the engine's presentational state machines
// (PropertyTransition); and, once every rule is lowered, each transition is
// grouped with the rules that refine its own (R2-18). `animation` and its
// longhands (P7b) play `@keyframes` once, as a machine state's entry
// value, stops and target, or as a loop, an expression of time_in_state
// under the rule's ambient effect. The compiler reads the values and
// handles the CSS-wide keywords; this lowers what is left into result
// records and reports nothing itself.

#include "../diagnostics/theme_diagnostics.hpp"
#include "../value/theme_value.hpp"

#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stxlib/optional.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The transition properties: the shorthand and CSS's four longhands.
enum class TransitionProperty : std::uint8_t { transition, property, duration, timing_function, delay };

// The property `name` (lower case) names, or none for another property.
[[nodiscard]] std::optional<TransitionProperty> transition_property(std::string_view name) noexcept;

// What one written transition moves:
// - engine: one of the engine's presentational properties, `name` being
//   opacity_property, scale_property or face_property;
// - translate: CSS's `translate`, the engine's two translations;
// - parameter: a parameter of the rule's primitive, `name` its registered
//   name, checked when the rule's transitions are built.
enum class TransitionTargetKind : std::uint8_t { engine, translate, parameter };

struct TransitionTarget final {
    TransitionTargetKind kind{TransitionTargetKind::engine};
    std::string name{};
    // Where the sheet names it.
    std::uint32_t token{};
};

// A timing function as written, without the transition's duration and
// delay: a timed easing (with its control points or steps), or one of the
// engine's physical curves with its own time (`constant`: follow's and
// decay's time constant, spring's response) and, for a spring, its
// overshoot.
struct TimingFunction final {
    MotionKind kind{MotionKind::timed};
    MotionEasing easing{MotionEasing::ease};
    std::array<double, 4> bezier{0.0, 0.0, 1.0, 1.0};
    std::uint32_t steps{1U};
    MotionStepPosition step_position{MotionStepPosition::end};
    FrameInterval constant{};
    double overshoot{};
    std::uint32_t token{};
};

// One rule's transition lists as its declarations wrote them, CSS's four
// longhands; the shorthand sets all four, and a later declaration replaces
// the lists it sets. A list not set takes CSS's initial value when the
// transitions are built: no properties, 0s, `ease`, 0s. `properties` empty
// with `properties_set` is `none`. Caller-owned and reused from rule to
// rule (clear_written_transitions).
struct WrittenTransitions final {
    bool properties_set{};
    std::vector<TransitionTarget> properties{};
    bool durations_set{};
    std::vector<FrameInterval> durations{};
    bool timings_set{};
    std::vector<TimingFunction> timings{};
    bool delays_set{};
    std::vector<FrameInterval> delays{};
    // Where the rule's last transition declaration starts, where a problem
    // with the rule's transitions as a whole points.
    std::uint32_t token{};
};

// Empties `written` for the next rule, keeping its capacity.
void clear_written_transitions(WrittenTransitions& written) noexcept;

// Whether the rule declared any transition property.
[[nodiscard]] bool transitions_written(const WrittenTransitions& written) noexcept;

enum class MotionLoweringOutcome : std::uint8_t {
    lowered,
    // A warning (`problem`): the declaration is dropped, the rule kept.
    dropped,
    // An error (`problem`) refuses the sheet.
    refused,
};

// What one lowering found wrong: the diagnostic the compiler reports, at
// the sheet token `token`.
struct MotionLoweringProblem final {
    DiagnosticCode code{DiagnosticCode::value_syntax};
    std::uint32_t token{};
    std::string detail{};
};

// Lowers `value`, a value of `property` that is not a CSS-wide keyword,
// into the lists of `written` it sets; on a problem `written` keeps its
// earlier lists. `start` is the token the value starts at. `value` is
// borrowed for the call.
[[nodiscard]] MotionLoweringOutcome lower_transition_value(TransitionProperty property, const ThemeValue& value,
                                                           std::uint32_t start, WrittenTransitions& written,
                                                           MotionLoweringProblem& problem);

// Resets the lists `property` sets to CSS's initial values (`initial`).
void reset_transition_lists(TransitionProperty property, WrittenTransitions& written) noexcept;

// One machine's curve a rule declares: `property` as ThemeRule records it
// (CSS's `translate` gives two, translate_x_property then
// translate_y_property).
struct DeclaredTransition final {
    std::string property{};
    MotionCurve curve{};
    std::uint32_t token{};
};

// The rule's transitions from `written`, one per written property in
// order, each taking the duration, timing function and delay at its
// position in their lists, repeated as CSS repeats a short list, into
// `declared`, which is cleared first. A parameter must be an animated
// number parameter of `primitive`, the rule's selected primitive (null
// when it selects none or the built-in recipe). A later transition of the
// same property replaces an earlier one, as CSS's does.
[[nodiscard]] MotionLoweringOutcome build_declared_transitions(const WrittenTransitions& written,
                                                               const PrimitiveDescriptor* primitive,
                                                               std::vector<DeclaredTransition>& declared,
                                                               MotionLoweringProblem& problem);

// -- animations (P7b) ---------------------------------------------------------------

// The animation properties: the shorthand, CSS's longhands the language
// reads, and the engine's two loop properties (PLAN.md section 1.8).
enum class AnimationProperty : std::uint8_t {
    animation,
    name,
    duration,
    timing_function,
    delay,
    iteration_count,
    direction,
    fill_mode,
    play_state,
    frame_rate,
    rest_pose,
};

// The property `name` (lower case) names, or none for another property.
[[nodiscard]] std::optional<AnimationProperty> animation_property(std::string_view name) noexcept;

// An animation's keyframes as named: `name` empty for `none`.
struct AnimationName final {
    std::string name{};
    std::uint32_t token{};
};

// How often an animation runs: once (an iteration count of 1), or as a
// loop (`infinite`), which becomes the rule's ambient effect.
enum class AnimationRuns : std::uint8_t { once, loop };

// One rule's animation lists as its declarations wrote them, as for
// WrittenTransitions: the shorthand sets every list but the engine's two,
// a longhand its own, a later declaration replaces the lists it sets, and
// a short list repeats. A list not set takes its initial value when the
// animations are built: `none`, 0s, `ease`, 0s, 1, 30 frames a second and
// a rest pose of 0s. Times are in seconds as written: a loop's period is
// not clamped to the motion ceiling. `animation-direction`,
// `animation-fill-mode` and `animation-play-state` admit only their
// initial values, so they set no list. Caller-owned and reused from rule
// to rule (clear_written_animations).
struct WrittenAnimations final {
    bool names_set{};
    std::vector<AnimationName> names{};
    bool durations_set{};
    std::vector<double> durations{};
    bool timings_set{};
    std::vector<TimingFunction> timings{};
    bool delays_set{};
    std::vector<double> delays{};
    bool runs_set{};
    std::vector<AnimationRuns> runs{};
    bool frame_rates_set{};
    std::vector<double> frame_rates{};
    bool poses_set{};
    std::vector<double> poses{};
    // Where the rule's last animation declaration starts.
    std::uint32_t token{};
};

void clear_written_animations(WrittenAnimations& written) noexcept;

// Whether the rule names any keyframes (an animation other than `none`).
[[nodiscard]] bool animations_written(const WrittenAnimations& written) noexcept;

// Lowers `value`, a value of `property` that is not a CSS-wide keyword,
// into the lists of `written` it sets, as lower_transition_value does.
[[nodiscard]] MotionLoweringOutcome lower_animation_value(AnimationProperty property, const ThemeValue& value,
                                                          std::uint32_t start, WrittenAnimations& written,
                                                          MotionLoweringProblem& problem);

// Resets the lists `property` sets to their initial values (`initial`).
void reset_animation_lists(AnimationProperty property, WrittenAnimations& written) noexcept;

// One property's keyframes (`@keyframes`): `property` is opacity_property,
// scale_property, translate_x_property, translate_y_property or, with
// `parameter`, a parameter's registered name; `frames` its values by
// offset, offsets within [0, 1], strictly increasing; `tokens` where each
// frame's value is written, in the same order.
struct KeyframeTrack final {
    std::string property{};
    bool parameter{};
    std::vector<MotionStop> frames{};
    std::vector<std::uint32_t> tokens{};
};

// One `@keyframes` rule: its name (as written; names are case-sensitive)
// and its tracks, in the order their properties first appear.
struct KeyframesDefinition final {
    std::string name{};
    std::vector<KeyframeTrack> tracks{};
    std::uint32_t token{};
};

// Sets `property`'s value at `offset` in `definition`: a later keyframe of
// the same offset replaces the value, as CSS merges keyframe blocks of one
// offset. `token` is where the value is written.
void add_keyframe(KeyframesDefinition& definition, std::string_view property, bool parameter, double offset,
                  double value, std::uint32_t token);

// What a property an animation moves once becomes: its machine's curve
// (the animation's duration, timing function and delay), its state's
// entry value (`from`), stops (the keyframes between) and target (`to`),
// either end absent where the keyframes do not write it.
struct DeclaredMove final {
    std::string property{};
    bool parameter{};
    MotionCurve curve{};
    std::optional<double> from{};
    std::vector<MotionStop> stops{};
    std::optional<double> to{};
    std::uint32_t token{};
};

// A parameter a loop moves: its keyframes (either end may be absent) and
// the loop's timing function, which eases each keyframe interval.
struct DeclaredLoop final {
    std::string parameter{};
    std::vector<MotionStop> frames{};
    TimingFunction timing{};
    std::uint32_t token{};
};

// A rule's animations, built: the moves, the loops, and the ambient effect
// the loops share.
struct DeclaredAnimations final {
    std::vector<DeclaredMove> moves{};
    std::vector<DeclaredLoop> loops{};
    stxlib::Optional<AmbientEffect> ambient{};
};

// The most `@keyframes` rules one sheet may have (a derived limit, R2-14).
inline constexpr std::size_t maximum_keyframes_rules = 64U;

// The longest loop period (a derived limit, PLAN.md P7b).
inline constexpr FrameInterval maximum_loop_period = std::chrono::seconds(60);

// The rule's animations from `written`, one per named keyframes in order,
// each taking the values at its position in the other lists, repeated as
// CSS repeats a short list, into `animations`, which is cleared first.
// `keyframes` are the sheet's definitions (the last of a name wins);
// `primitive` is the rule's selected primitive, or null. A later
// animation of a property replaces an earlier one's, as in CSS. A loop
// moves only number parameters of the primitive, and every loop of a rule
// shares one period, frame rate and rest pose.
[[nodiscard]] MotionLoweringOutcome build_declared_animations(const WrittenAnimations& written,
                                                              std::span<const KeyframesDefinition> keyframes,
                                                              const PrimitiveDescriptor* primitive,
                                                              DeclaredAnimations& animations,
                                                              MotionLoweringProblem& problem);

// The expression of time_in_state (seconds within the loop's `period`)
// that passes through `frames`, whose offsets start at 0 and end at 1,
// each interval eased by `timing`: linear, ease-in-out (smoothstep) or
// steps. Into `program`, which is cleared first. Other timing functions
// are E405; a program over the expression budget is E507. `token` is
// where a problem points.
[[nodiscard]] MotionLoweringOutcome loop_program(std::span<const MotionStop> frames, const TimingFunction& timing,
                                                 FrameInterval period, std::uint32_t token,
                                                 std::vector<ExpressionInstruction>& program,
                                                 MotionLoweringProblem& problem);

// Where one machine of the lowered sheet came from: rule `rule`'s
// transition at `machine`, either a transition declaration's (`curved`,
// which `token` names) or a static value's, an instant machine of one
// state; or an animation's (`animated`, `token` its keyframes' name), a
// machine of one state with the animation's curve.
struct MachineOrigin final {
    std::size_t rule{};
    std::size_t machine{};
    bool curved{};
    bool animated{};
    std::uint32_t token{};
};

// Groups each curved machine of `sheet` with the static values of the
// rules that refine its rule (R2-18): rules at the same level and under
// the same media condition whose selector is the same but for more
// states, setting the same property
// (an engine property's instant machine, or a number declaration of the
// parameter in a rule selecting the same primitive). Their values become
// the machine's states, ahead of its own, ordered most precedent first
// (the higher specificity, then the later rule), as the engine's machine
// takes the first state that matches; their own machines and
// declarations are removed, and a rule left declaring nothing is
// removed. An animation's machine in a refining rule joins the same way,
// its state keeping its entry value and stops, and its curve must be the
// transition's (owner decision, 2026-10-27, "strict for now"; else E405). A curved machine left with no state, other than a face
// cross-fade, moves nothing and is removed. More than
// maximum_presentation_states states is E407. `origins` lists every
// machine of `sheet` in rule order; `sheet` is changed in place, and on a
// problem may be left half grouped (the compiler then refuses it).
[[nodiscard]] MotionLoweringOutcome group_transitions(ThemeRuleSheet& sheet, std::span<const MachineOrigin> origins,
                                                      MotionLoweringProblem& problem);

} // namespace gui_forms::theme_language
