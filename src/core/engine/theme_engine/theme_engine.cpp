#include "gui_forms/engine/theme_engine/theme_engine.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window/window.hpp"
#include "../../theme/part_painting/built_in_recipe.hpp"
#include "../../theme/theme/forced_high_contrast.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <span>

// The values a part is drawn with (expressions, transitions, inputs) must
// have the same bits on every host: no fused multiply-add here either.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms {
namespace {

bool animated(const PrimitiveDescriptor& descriptor) noexcept {
    for (const PrimitiveParameterDeclaration& parameter : descriptor.parameters) {
        if (parameter.animated) return true;
    }
    return false;
}

// One side of a raster: the logical length times the scale, rounded up;
// zero when it does not fit maximum_part_raster_side.
std::uint32_t device_side(double logical, double scale) noexcept {
    const double device = std::ceil(logical * scale);
    if (!(device > 0.0) || device > static_cast<double>(maximum_part_raster_side)) return 0U;
    const std::uint32_t side = static_cast<std::uint32_t>(device);
    return side;
}

// The device extent of a raster covering a part of `logical` size grown by
// `outsets` (a primitive's), at `scale`: zero sides when it does not fit.
DeviceExtent raster_extent(Size logical, Insets outsets, double scale) noexcept {
    const double width = logical.width + outsets.left + outsets.right;
    const double height = logical.height + outsets.top + outsets.bottom;
    const DeviceExtent extent{device_side(width, scale), device_side(height, scale)};
    return extent;
}

// A part's extent where it was recorded: its bounds moved by the painter's
// origin and grown by its primitive's outsets.
Rect part_extent(Rect bounds, Point origin, Insets outsets) noexcept {
    const double width = bounds.width + outsets.left + outsets.right;
    const double height = bounds.height + outsets.top + outsets.bottom;
    const Rect extent{bounds.x + origin.x - outsets.left, bounds.y + origin.y - outsets.top, width, height};
    return extent;
}

// An input that is not published reads as NaN, so an expression reading it
// gives its fallback (E6).
constexpr double not_published = std::numeric_limits<double>::quiet_NaN();

// Where a part's inputs come from (E6).
struct PartInputSources final {
    const Window* window{};
    // The part's record, or null for a part painted without a key, which
    // reads only the two scales: it is drawn statically.
    const EnginePartRecord* record{};
    // The pointer in window coordinates, or none outside the window.
    std::optional<Point> pointer{};
    // The part's top-left corner in window coordinates.
    Point part_origin{};
    AnatomyStateSet states{};
    // time_in_state as an ambient effect gives it (E7), in place of the
    // time since the record's state entry.
    std::optional<double> time_in_state{};
};

std::size_t input_slot(EngineInput input) noexcept {
    const std::size_t slot = engine_input_position(input).value;
    return slot;
}

// Fills `inputs`, by expression input position, with every input published
// for the part, and NaN elsewhere. The published values are looked up only
// where `read` names them. `inputs` holds maximum_expression_inputs values.
void gather_inputs(const CompiledThemeRules& rules, std::uint64_t read, const PartInputSources& sources,
                   std::span<double> inputs) {
    std::fill(inputs.begin(), inputs.end(), not_published);
    const Window& window = *sources.window;
    inputs[input_slot(EngineInput::display_scale)] = window.scale();
    inputs[input_slot(EngineInput::text_scale)] = window.presentation_settings().text_scale;
    if (sources.record == nullptr) return;
    const EnginePartRecord& record = *sources.record;
    const FrameTime now = window.motion_clock().now();
    const double in_state = std::chrono::duration<double>(now - record.state_entered).count();
    inputs[input_slot(EngineInput::time_in_state)] = sources.time_in_state.value_or(in_state);
    // The pointer only while the part is hot (GIVEN B1-1), in part-local
    // logical units (B2-1).
    if (sources.states.contains(AnatomyState::hot) && sources.pointer) {
        const Point pointer = *sources.pointer;
        inputs[input_slot(EngineInput::pointer_x)] = pointer.x - sources.part_origin.x;
        inputs[input_slot(EngineInput::pointer_y)] = pointer.y - sources.part_origin.y;
    }
    const std::vector<std::string>& names = rules.published_values();
    for (std::size_t value = 0; value < names.size(); ++value) {
        const ExpressionInputIndex position = published_value_position(value);
        if ((read & expression_input_bit(position)) == 0U) continue;
        const std::optional<double> published = window.published_value(names[value]);
        if (published) inputs[position.value] = *published;
    }
}

// Gives each parameter whose winning declaration is an expression the
// expression's value, clamped to the parameter's range. Every expression
// reads the cascade's values of the parameters, before any expression.
void evaluate_expressions(const CompiledThemeRules::Resolution& resolution, std::span<const double> inputs,
                          AnatomyStateSet states, PrimitiveParameterBlock& parameters) noexcept {
    bool any = false;
    std::array<double, maximum_primitive_parameters> numbers{};
    for (std::size_t index = 0; index < parameters.count; ++index) {
        numbers[index] = parameters.values[index].number;
        if (resolution.expressions[index] != nullptr) any = true;
    }
    if (!any) return;
    const PrimitiveDescriptor& descriptor = (*resolution.primitive).descriptor();
    ExpressionEnvironment environment{};
    environment.inputs = inputs;
    environment.states = states;
    environment.parameters = std::span<const double>(numbers.data(), parameters.count);
    for (std::size_t index = 0; index < parameters.count; ++index) {
        const EngineExpression* const expression = resolution.expressions[index];
        if (expression == nullptr) continue;
        const ExpressionResult result = (*expression).evaluate(environment);
        const PrimitiveParameterDeclaration& declared = descriptor.parameters[index];
        const double raised = std::max(result.value, declared.minimum);
        parameters.values[index].number = std::min(raised, declared.maximum);
    }
}

// -- transitions (E7) ------------------------------------------------------------

// Where a part's transitions run: its window, its record and the record's
// handle (none for a part painted without a key, which is drawn
// statically), the part's control as the owner of its motion values, and
// its published states.
struct PartTransitionSources final {
    Window* window{};
    EnginePartRecord* record{};
    PartRecordHandle handle{};
    std::shared_ptr<Control> owner{};
    AnatomyStateSet states{};
    // The serial of the compiled rules the transitions belong to.
    std::uint64_t rules_serial{};
    // The part's em, where a transition's value is in em (T3 P8c); else 0.
    double em{};
};

// Whether a resolved transition's value is in em (P8c).
bool transitions_read_em(const CompiledThemeRules::Resolution& resolution) noexcept {
    for (std::size_t index = 0; index < resolution.transition_count; ++index) {
        if ((*resolution.transitions[index]).reads_em) return true;
    }
    return false;
}

using CompiledTransition = CompiledThemeRules::CompiledTransition;

// The machine state the part's states select: the first whose `when`
// states they all include, or none.
std::optional<std::size_t> machine_state(const CompiledTransition& transition, AnatomyStateSet states) noexcept {
    for (std::size_t index = 0; index < transition.states.size(); ++index) {
        const std::uint64_t needed = transition.states[index].when.standard_bits();
        if ((states.standard_bits() & needed) == needed) return index;
    }
    return std::nullopt;
}

// A property's range: [0, 1] for opacity and face, the presentational
// scale's for scale, ±maximum_presentational_translate for a translation,
// the parameter's declared range otherwise.
struct PropertyRange final {
    double minimum{};
    double maximum{1.0};
};

// Whether `property` is one of the presentational translations (E9.6).
bool translates(PresentationProperty property) noexcept {
    const bool translation =
        property == PresentationProperty::translate_x || property == PresentationProperty::translate_y;
    return translation;
}

PropertyRange property_range(const CompiledTransition& transition, const PrimitiveDescriptor* descriptor) noexcept {
    PropertyRange range{};
    if (transition.property == PresentationProperty::scale) {
        range.minimum = minimum_presentational_scale;
        range.maximum = maximum_presentational_scale;
        return range;
    }
    if (translates(transition.property)) {
        range.minimum = -maximum_presentational_translate;
        range.maximum = maximum_presentational_translate;
        return range;
    }
    if (transition.property == PresentationProperty::opacity || transition.property == PresentationProperty::face ||
        descriptor == nullptr) {
        return range;
    }
    const PrimitiveParameterDeclaration& declared = (*descriptor).parameters[transition.parameter];
    range.minimum = declared.minimum;
    range.maximum = declared.maximum;
    return range;
}

// The value a property rests at when the part's states match no machine
// state: its cascade value, 0 for a translation, or 1 for opacity and
// scale.
double rest_value(const CompiledTransition& transition, const PrimitiveParameterBlock& parameters) noexcept {
    if (translates(transition.property)) return 0.0;
    if (transition.property != PresentationProperty::parameter) return 1.0;
    const double value = parameters.values[transition.parameter].number;
    return value;
}

// The engine properties a part is shown with this paint, and what its
// transitions did: whether its scale is still moving, and by parameter
// position the bits of the parameters it transitions and of those still
// moving. `translation` is the move its machines want, in logical units,
// before the grant clamps it.
struct PresentedProperties final {
    double opacity{1.0};
    double scale{1.0};
    bool scale_moving{false};
    Point translation{};
    // The part's face cross-fade (E7r form a), when it has one and a
    // record: its value, 1 when settled. The paint starts it.
    MotionValue* face{};
    std::uint32_t transitioned_parameters{};
    std::uint32_t moving_parameters{};
};

std::uint32_t parameter_bit(std::size_t parameter) noexcept {
    const std::uint32_t bit = std::uint32_t{1} << parameter;
    return bit;
}

// Shows a transition's value: the part's opacity, scale or translation,
// or its parameter, clamped to the property's range.
void show_value(const CompiledTransition& transition, double value, PropertyRange range,
                PrimitiveParameterBlock& parameters, PresentedProperties& presented) noexcept {
    const double raised = std::max(value, range.minimum);
    const double shown = std::min(raised, range.maximum);
    if (transition.property == PresentationProperty::opacity) {
        presented.opacity = shown;
        return;
    }
    if (transition.property == PresentationProperty::scale) {
        presented.scale = shown;
        return;
    }
    if (transition.property == PresentationProperty::translate_x) {
        presented.translation.x = shown;
        return;
    }
    if (transition.property == PresentationProperty::translate_y) {
        presented.translation.y = shown;
        return;
    }
    parameters.values[transition.parameter].number = shown;
}

// The logical extent the settled condition measures a property's value
// against (MotionValue::set_extent): one unit of the value spans 255
// device pixels over the property's range, so that a value settles within
// half an 8-bit step of its range. A translation is a distance in logical
// units, so one unit spans one logical unit: it settles within half a
// device pixel.
double settle_extent(const CompiledTransition& transition, PropertyRange range, double scale) noexcept {
    if (translates(transition.property)) return 1.0;
    const double span = range.maximum - range.minimum;
    const double extent = 255.0 / (span * scale);
    return extent;
}

// Runs the part's presentational state machines for one paint and shows
// their values: a parameter's in `parameters`, opacity and scale returned. A
// part with a record moves each property on its motion value, retargeting
// from the value shown; one without stands each property at its target.
// Parameter transitions run only while a primitive draws the part
// (`descriptor` set). A record's values for properties no longer
// transitioned are released.
PresentedProperties run_transitions(const CompiledThemeRules::Resolution& resolution,
                                    const PartTransitionSources& sources, const PrimitiveDescriptor* descriptor,
                                    PrimitiveParameterBlock& parameters) {
    PresentedProperties presented{};
    EnginePartRecord* const record = sources.owner ? sources.record : nullptr;
    PartMotion* motion = nullptr;
    if (record != nullptr && resolution.transition_count > 0U) {
        if (!(*record).motion) (*record).motion = std::make_unique<PartMotion>(*sources.window, sources.handle);
        motion = (*record).motion.get();
    }
    std::array<bool, maximum_part_transitions> used{};
    for (std::size_t index = 0; index < resolution.transition_count; ++index) {
        const CompiledTransition& transition = *resolution.transitions[index];
        if (transition.property == PresentationProperty::parameter && descriptor == nullptr) continue;
        // Without a record a face has nothing to fade from.
        if (transition.property == PresentationProperty::face && motion == nullptr) continue;
        const PropertyRange range = property_range(transition, descriptor);
        const std::optional<std::size_t> state = machine_state(transition, sources.states);
        // A value in em is taken at the part's em (P8c).
        double target = rest_value(transition, parameters);
        if (state) {
            const CompiledThemeRules::CompiledPresentationState& held = transition.states[*state];
            target = held.target + sources.em * held.target_em;
        }
        if (motion == nullptr) {
            show_value(transition, target, range, parameters, presented);
            continue;
        }
        // The channel already running this property, else the first free one.
        std::size_t chosen = maximum_part_transitions;
        for (std::size_t channel = 0; channel < maximum_part_transitions; ++channel) {
            const PartTransitionChannel& running = (*motion).channels[channel];
            if (!running.value || used[channel]) continue;
            if (running.property == transition.property && running.parameter == transition.parameter) {
                chosen = channel;
            }
        }
        for (std::size_t channel = 0; chosen == maximum_part_transitions && channel < maximum_part_transitions;
             ++channel) {
            if (!(*motion).channels[channel].value && !used[channel]) chosen = channel;
        }
        if (chosen == maximum_part_transitions) continue;
        used[chosen] = true;
        PartTransitionChannel& channel = (*motion).channels[chosen];
        if (transition.property == PresentationProperty::face) {
            // Settled at 1 until the paint finds a new face.
            if (!channel.value) {
                channel.property = transition.property;
                channel.parameter = 0U;
                channel.state.reset();
                channel.value.emplace(*sources.owner, transition.curve, 1.0);
                MotionValue& made = *channel.value;
                static_cast<void>(made.set_extent(settle_extent(transition, range, (*sources.window).scale())));
                made.set_listener(MotionListener::bind<PartMotion, &PartMotion::on_step>(*motion));
            } else {
                (*channel.value).set_curve(transition.curve);
            }
            presented.face = &*channel.value;
            continue;
        }
        std::optional<double> entry{};
        std::array<MotionStop, maximum_motion_stops> taken{};
        std::span<const MotionStop> stops{};
        if (state) {
            const CompiledThemeRules::CompiledPresentationState& held = transition.states[*state];
            if (held.enter_from) entry = *held.enter_from + sources.em * held.enter_from_em;
            const std::size_t count = std::min(held.stops.size(), maximum_motion_stops);
            for (std::size_t stop = 0; stop < count; ++stop) {
                taken[stop] = held.stops[stop];
                taken[stop].value = held.stops[stop].value + sources.em * held.stops[stop].value_em;
                taken[stop].value_em = 0.0;
            }
            stops = std::span<const MotionStop>(taken.data(), count);
        }
        // Whether the move into the state begins this paint, so that its
        // stops (E9.6) play: on appearing from an entry value, or on a
        // change of state.
        bool entering = false;
        if (!channel.value) {
            // The part appears in its state: at the state's entry value if
            // it has one, else at its target.
            channel.property = transition.property;
            channel.parameter = transition.parameter;
            channel.rules_serial = sources.rules_serial;
            channel.machine = transition.machine;
            channel.state = state;
            const double initial = entry ? *entry : target;
            channel.value.emplace(*sources.owner, transition.curve, initial);
            MotionValue& made = *channel.value;
            static_cast<void>(made.set_extent(settle_extent(transition, range, (*sources.window).scale())));
            made.set_listener(MotionListener::bind<PartMotion, &PartMotion::on_step>(*motion));
            entering = entry.has_value();
        } else {
            (*channel.value).set_curve(transition.curve);
            // Another machine of the same rules (another rule's) is another
            // state, even at the same position.
            const bool other_machine =
                channel.rules_serial == sources.rules_serial && channel.machine != transition.machine;
            if (channel.state != state || other_machine) {
                channel.state = state;
                if (entry) (*channel.value).jump_to(*entry);
                entering = true;
            }
            channel.rules_serial = sources.rules_serial;
            channel.machine = transition.machine;
        }
        MotionValue& value = *channel.value;
        if (entering && !stops.empty()) {
            static_cast<void>(value.animate_through(stops, target));
        } else {
            static_cast<void>(value.animate_to(target));
        }
        show_value(transition, value.current(), range, parameters, presented);
        if (transition.property == PresentationProperty::scale && !value.settled()) presented.scale_moving = true;
        if (transition.property == PresentationProperty::parameter) {
            const std::uint32_t bit = parameter_bit(transition.parameter);
            presented.transitioned_parameters = presented.transitioned_parameters | bit;
            if (!value.settled()) presented.moving_parameters = presented.moving_parameters | bit;
        }
    }
    if (motion != nullptr) {
        for (std::size_t channel = 0; channel < maximum_part_transitions; ++channel) {
            if (used[channel]) continue;
            (*motion).channels[channel].value.reset();
            (*motion).channels[channel].state.reset();
        }
        (*record).motion_unsettled = (*motion).moving();
    } else if (record != nullptr && (*record).motion) {
        (*record).motion.reset();
        (*record).motion_unsettled = false;
    }
    return presented;
}

// -- face cross-fade (E7r) --------------------------------------------------------

// Whether two faces may cross-fade: the same extent at the same scale.
bool fades_between(const PartRaster& from, const PartRaster& to) noexcept {
    const DeviceExtent first = from.extent();
    const DeviceExtent second = to.extent();
    const bool same = first.width == second.width && first.height == second.height &&
                      from.scale().value == to.scale().value;
    return same;
}

// Notes the face a part shows on this paint in `states`, starts its
// cross-fade when its states changed and with them its face, and returns
// the face it fades from (null when none fades). A part without a face
// transition holds no faces.
std::shared_ptr<const PartRaster> note_face(EnginePartRecord& part, MotionValue* face,
                                            const std::shared_ptr<const PartRaster>& raster, AnatomyStateSet states) {
    if (face == nullptr) {
        part.shown_face.reset();
        part.fading_face.reset();
        return nullptr;
    }
    MotionValue& value = *face;
    const bool discrete = part.face_states != states;
    if (discrete && part.shown_face && part.shown_face != raster) {
        if (fades_between(*part.shown_face, *raster)) {
            part.fading_face = part.shown_face;
            value.jump_to(0.0);
            static_cast<void>(value.animate_to(1.0));
            part.motion_unsettled = true;
        } else {
            part.fading_face.reset();
            value.jump_to(1.0);
        }
    }
    part.shown_face = raster;
    part.face_states = states;
    if (value.settled()) part.fading_face.reset();
    std::shared_ptr<const PartRaster> from = part.fading_face;
    return from;
}

// -- keyframes (E7r) ------------------------------------------------------------

// Keyframe level `level` of a parameter's declared range, its ends exact.
double keyframe_value(const PrimitiveParameterDeclaration& declared, std::size_t level) noexcept {
    if (level + 1U >= animated_keyframe_levels) return declared.maximum;
    const double span = declared.maximum - declared.minimum;
    const double steps = static_cast<double>(animated_keyframe_levels - 1U);
    const double value = declared.minimum + span * static_cast<double>(level) / steps;
    return value;
}

// The keyframe level nearest `value`.
std::size_t nearest_keyframe(const PrimitiveParameterDeclaration& declared, double value) noexcept {
    const double span = declared.maximum - declared.minimum;
    if (!(span > 0.0)) return 0U;
    const double steps = static_cast<double>(animated_keyframe_levels - 1U);
    const double position = std::clamp((value - declared.minimum) / span * steps, 0.0, steps);
    const std::size_t level = static_cast<std::size_t>(std::round(position));
    return level;
}

// The pointer inputs: an expression reading them moves its parameter as
// the pointer moves.
constexpr std::uint64_t pointer_inputs() noexcept {
    const std::uint64_t across = expression_input_bit(engine_input_position(EngineInput::pointer_x));
    const std::uint64_t down = expression_input_bit(engine_input_position(EngineInput::pointer_y));
    return across | down;
}

// By parameter position, the animated parameters an expression drives
// from the pointer (owner D, 2026-10-22: they step through the keyframes).
std::uint32_t pointer_driven(const CompiledThemeRules::Resolution& resolution,
                             const PrimitiveDescriptor& descriptor) noexcept {
    std::uint32_t driven = 0U;
    for (std::size_t index = 0; index < resolution.parameters.count; ++index) {
        const EngineExpression* const expression = resolution.expressions[index];
        if (!descriptor.parameters[index].animated || expression == nullptr) continue;
        if (((*expression).dependencies().inputs & pointer_inputs()) == 0U) continue;
        driven = driven | parameter_bit(index);
    }
    return driven;
}

// Steps each animated parameter in `stepped` (bits by parameter position:
// those whose transition runs on this paint, and those the pointer
// drives) to its nearest keyframe level, and returns whether every animated
// parameter now stands at a level (false for a primitive with none), so
// that its raster may be a shared keyframe. A parameter an expression
// drives with the time in state needs no step: an ambient effect's time
// steps through its loop's frames, and nothing else repaints a part as
// time passes.
bool step_to_keyframes(const PrimitiveDescriptor& descriptor, std::uint32_t stepped,
                       PrimitiveParameterBlock& parameters) noexcept {
    bool any = false;
    bool all_at_levels = true;
    for (std::size_t index = 0; index < parameters.count; ++index) {
        const PrimitiveParameterDeclaration& declared = descriptor.parameters[index];
        if (!declared.animated) continue;
        any = true;
        const bool moving = (stepped & parameter_bit(index)) != 0U;
        double& value = parameters.values[index].number;
        const double level = keyframe_value(declared, nearest_keyframe(declared, value));
        if (moving) value = level;
        if (value != level) all_at_levels = false;
    }
    const bool shared = any && all_at_levels;
    return shared;
}

// Whether the part's transitions can draw it into its granted overflow:
// its scale or a translation.
bool draws_in_overflow(const CompiledThemeRules::Resolution& resolution) noexcept {
    for (std::size_t index = 0; index < resolution.transition_count; ++index) {
        const PresentationProperty property = (*resolution.transitions[index]).property;
        if (property == PresentationProperty::scale || translates(property)) return true;
    }
    return false;
}

// The largest scale at which `bounds`, grown about its centre, stays within
// the overflow its control grants: no more than 1 when none is granted.
double granted_scale(Rect bounds, Insets overflow) noexcept {
    double limit = maximum_presentational_scale;
    if (bounds.width > 0.0) {
        const double across = 1.0 + 2.0 * std::min(overflow.left, overflow.right) / bounds.width;
        limit = std::min(limit, across);
    }
    if (bounds.height > 0.0) {
        const double down = 1.0 + 2.0 * std::min(overflow.top, overflow.bottom) / bounds.height;
        limit = std::min(limit, down);
    }
    const double granted = std::max(1.0, limit);
    return granted;
}

// `bounds` less `insets`, each axis shrinking to the centre line of
// `bounds` where the insets leave no room on it.
Rect slot_within(Rect bounds, Insets insets) noexcept {
    Rect slot{bounds.x + insets.left, bounds.y + insets.top, bounds.width - insets.left - insets.right,
              bounds.height - insets.top - insets.bottom};
    if (!(slot.width > 0.0)) {
        slot.x = bounds.x + bounds.width * 0.5;
        slot.width = 0.0;
    }
    if (!(slot.height > 0.0)) {
        slot.y = bounds.y + bounds.height * 0.5;
        slot.height = 0.0;
    }
    return slot;
}

// `bounds` at `scale` about its centre.
Rect scaled_about_centre(Rect bounds, double scale) noexcept {
    const double width = bounds.width * scale;
    const double height = bounds.height * scale;
    const Rect scaled{bounds.x + (bounds.width - width) * 0.5, bounds.y + (bounds.height - height) * 0.5, width,
                      height};
    return scaled;
}

// The part as shown at its presentational scale: its bounds grown or
// shrunk about their centre, the growth clamped to the granted overflow.
PartPaint scaled_part(const PartPaint& part, double scale) noexcept {
    PartPaint shown = part;
    const double granted = std::min(scale, granted_scale(part.bounds, part.granted_overflow));
    if (granted != 1.0) shown.bounds = scaled_about_centre(part.bounds, granted);
    return shown;
}

// The presentational translation (E9.6) the part is drawn with: `wanted`,
// clamped on each axis to the overflow its control grants beyond what its
// bounds, at `shown_scale` about their centre, take. The part as drawn so
// stays within its extent; a side granted no overflow, or one the scale
// takes, admits no move toward it.
Point granted_translation(const PartPaint& part, double shown_scale, Point wanted) noexcept {
    const double grown_across = (shown_scale - 1.0) * part.bounds.width * 0.5;
    const double grown_down = (shown_scale - 1.0) * part.bounds.height * 0.5;
    const double left = std::max(0.0, part.granted_overflow.left - grown_across);
    const double right = std::max(0.0, part.granted_overflow.right - grown_across);
    const double up = std::max(0.0, part.granted_overflow.top - grown_down);
    const double down = std::max(0.0, part.granted_overflow.bottom - grown_down);
    const Point granted{std::clamp(wanted.x, -left, right), std::clamp(wanted.y, -up, down)};
    return granted;
}

// `bounds` moved by `offset`.
Rect moved_by(Rect bounds, Point offset) noexcept {
    const Rect moved{bounds.x + offset.x, bounds.y + offset.y, bounds.width, bounds.height};
    return moved;
}

// Whether the part's primitive stands behind the overlay seam (E9-A) and
// follows the pointer: not under reduced motion, where its overlay rests.
bool follows_pointer(const CompiledThemeRules::Resolution& resolution, const PartPaint& part,
                     bool reduced_motion) noexcept {
    if (reduced_motion) return false;
    const Size size{part.bounds.width, part.bounds.height};
    const bool follows = (*resolution.primitive).pointer_overlay(resolution.parameters, size).has_value();
    return follows;
}

// Where an overlay is composited: over the part as shown (`face`, its
// bounds at the presentational scale `granted`, 1 when unscaled), its
// raster stretched by `granted` while the scale moves, centred at the
// part-local `pointer_x` (NaN while the pointer is not over the part).
struct OverlayPlacement final {
    Rect face{};
    double granted{1.0};
    bool stretched{};
    double pointer_x{};
    bool reduced_motion{};
};

// Composites a primitive's overlay raster (E9-A) over the part as shown:
// its centre across at the pointer, or at its rest across the shown face
// with no pointer or under reduced motion; its top at the face's top;
// clipped to the face's rounded shape (the part's own corner radius, else
// its family recipe's, scaled with the face while it stretches), so that
// it never draws outside the part's extent.
void paint_pointer_overlay(Painter& painter, const Control& control, const PartPaint& part,
                           const PointerOverlay& overlay, const OverlayPlacement& placement,
                           const std::shared_ptr<const PartRaster>& raster, double opacity) {
    const Rect& face = placement.face;
    double centre = face.x + overlay.rest * face.width;
    if (!placement.reduced_motion && std::isfinite(placement.pointer_x)) centre = part.bounds.x + placement.pointer_x;
    const double width = overlay.size.width * placement.granted;
    const ControlVisualContext context = part_visual_context(control, part.states);
    const ControlVisualRecipe& recipe = control.effective_theme().resolve(part.family, context);
    double radius = recipe.material.corner_radius;
    if (part.corner_radius) radius = std::max(0.0, *part.corner_radius);
    if (placement.stretched) radius = radius * placement.granted;
    painter.save();
    painter.clip_rounded_rect(face, radius);
    if (placement.stretched) {
        const Rect destination{centre - width * 0.5, face.y, width, face.height};
        painter.draw_part_raster_scaled(raster, destination, opacity);
    } else {
        painter.draw_part_raster(raster, Point{centre - width * 0.5, face.y}, opacity);
    }
    painter.restore();
}

// How far past its bounds the built-in recipe may draw a part that is not
// a shadow: its one-pixel contact shadow, default ring and focus outsets.
constexpr double built_in_group_margin = 4.0;

// Paints a part with the built-in recipe at `opacity`: not at all at 0, and
// between 0 and 1 as one paint group, so that the recipe's overlapping
// commands composite as one image (owner, E7-1). The group covers the
// part's bounds and what the recipe draws around them: the overlay
// shadow's outsets for a shadow part, built_in_group_margin otherwise.
// -- authored materials (T3 EC1) -----------------------------------------------------

// Whether `assembled` was assembled from `floor` and the winning fields of
// `resolution` in the compiled rules numbered `rules_serial`.
bool same_assembly(const AssembledPartMaterial& assembled, std::uint64_t rules_serial, const SurfaceMaterial& floor,
                   Size box, MaterialAssemblyContext context,
                   const CompiledThemeRules::Resolution& resolution) noexcept {
    if (assembled.rules_serial != rules_serial || assembled.inline_serial != resolution.inline_serial) return false;
    if (assembled.user_serial != resolution.user_serial) return false;
    if (assembled.fields != resolution.materials) return false;
    if (!(assembled.box == box) || assembled.em != context.em) return false;
    if (!(assembled.current_colour == context.current_colour)) return false;
    const bool same_floor = assembled.floor == floor;
    return same_floor;
}

// The material a part with authored material fields draws: its record's
// assembly, made again only when the assembly's identity changed, or, for
// a part without a record, `unkept`, assembled for this paint. Each
// assembly is counted. The result is borrowed from `kept` or `unkept`.
const SurfaceMaterial& authored_material(EnginePartRecord* kept, std::uint64_t rules_serial,
                                         const SurfaceMaterial& floor, Rect box, MaterialAssemblyContext context,
                                         const CompiledThemeRules::Resolution& resolution,
                                         std::optional<SurfaceMaterial>& unkept, Metrics& metrics) {
    // The box keys the assembly only where a field reads it, so that a
    // resize assembles nothing new otherwise; so does the em (P8c).
    const Size size{box.width, box.height};
    const Size keyed = material_fields_read_box(resolution.materials) ? size : Size{};
    if (kept == nullptr) {
        unkept.emplace();
        assemble_material(floor, resolution.materials, size, context, *unkept);
        metrics.record_engine_material_assembly();
        return *unkept;
    }
    std::unique_ptr<AssembledPartMaterial>& held = (*kept).assembled_material;
    if (held != nullptr && same_assembly(*held, rules_serial, floor, keyed, context, resolution)) {
        return (*held).material;
    }
    if (held == nullptr) held = std::make_unique<AssembledPartMaterial>();
    (*held).rules_serial = rules_serial;
    (*held).inline_serial = resolution.inline_serial;
    (*held).user_serial = resolution.user_serial;
    (*held).fields = resolution.materials;
    (*held).box = keyed;
    (*held).em = context.em;
    (*held).current_colour = context.current_colour;
    (*held).floor = floor;
    assemble_material(floor, resolution.materials, size, context, (*held).material);
    metrics.record_engine_material_assembly();
    return (*held).material;
}

void paint_built_in(Painter& painter, const Control& control, const PartPaint& part, double opacity) {
    if (opacity <= 0.0) return;
    if (opacity >= 1.0) {
        paint_part_with_built_in_recipe(painter, control, part);
        return;
    }
    Insets reach{built_in_group_margin, built_in_group_margin, built_in_group_margin, built_in_group_margin};
    if (part.role == PartRole::shadow) reach = overlay_shadow_outsets(control);
    PaintGroup group{};
    group.bounds = Rect{part.bounds.x - reach.left, part.bounds.y - reach.top,
                        part.bounds.width + reach.left + reach.right, part.bounds.height + reach.top + reach.bottom};
    group.opacity = opacity;
    painter.begin_group(group);
    paint_part_with_built_in_recipe(painter, control, part);
    painter.end_group();
}

} // namespace

MediaFacts window_media_facts(const Window& window) noexcept {
    const PresentationSettings& settings = window.presentation_settings();
    MediaFacts facts{};
    facts.dark_scheme = settings.colour_scheme == ColourScheme::dark;
    facts.forced_colours = settings.high_contrast;
    facts.reduced_motion = settings.reduced_motion;
    facts.resolution = window.scale();
    return facts;
}

TextRole resolved_text_role(const Control& control, FontDeclaration font) {
    // Each step reads one control's own rules; the tree is finite, so the
    // walk ends at the root.
    Control::Ptr parent = control.parent();
    while (font.inherit) {
        if (parent == nullptr) return TextRole::ui;
        const Control& above = *parent;
        CompiledThemeRules::PartFacts facts{};
        facts.control_type = above.anatomy().control_id;
        facts.stable_id = above.stable_id().value();
        facts.classes = above.classes();
        facts.inline_rules = above.inline_style_rules();
        facts.states = above.anatomy_states();
        const Window* const window = above.attached_window();
        if (window != nullptr) facts.media = window_media_facts(*window);
        if (window != nullptr) facts.user_rules = (*window).user_theme_rules();
        font = above.effective_theme().engine().rules().resolve(facts).font;
        parent = above.parent();
    }
    return font.role;
}

double text_role_em(const Control& control, TextRole role) noexcept {
    const ThemeTypographyTokens& tokens = control.effective_theme().structure().typography;
    double size = tokens.control.size;
    switch (role) {
    case TextRole::ui: size = tokens.control.size; break;
    case TextRole::field: size = tokens.field.size; break;
    case TextRole::caption: size = tokens.caption.size; break;
    case TextRole::heading: size = tokens.heading.size; break;
    case TextRole::title: size = tokens.title.size; break;
    case TextRole::monospace: size = tokens.monospace.size; break;
    case TextRole::count:
    default: break; // Not a role: the ui role's size.
    }
    const double em = size * control.effective_text_scale();
    return em;
}

ThemeEngine::ThemeEngine() : ThemeEngine(ThemeRuleSheet{}) {}

ThemeEngine::ThemeEngine(const ThemeRuleSheet& sheet) : ThemeEngine(sheet, ThemeRuleSet::program) {}

namespace {
// `sheet` compiled as `set`.
CompiledThemeRules compiled_as(const ThemeRuleSheet& sheet, ThemeRuleSet set, const PrimitiveRegistry& registry) {
    if (set == ThemeRuleSet::user) return CompiledThemeRules::compile_user(sheet, registry);
    return CompiledThemeRules::compile_with_built_in(sheet, registry);
}
} // namespace

ThemeEngine::ThemeEngine(const ThemeRuleSheet& sheet, ThemeRuleSet set)
    : registry_(&builtin_primitive_registry()), rules_(compiled_as(sheet, set, *registry_)) {}

ThemeEngine::PartPlan ThemeEngine::plan(const Control& control, const PartPaint& part) const noexcept {
    PartPlan result{};
    if (rules_.empty()) return result;
    const Window* const window = control.attached_window();
    if (window == nullptr || !part_bounds_paintable(part.bounds)) {
        result.drawing = PartDrawing::not_paintable;
        return result;
    }
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = control.anatomy().control_id;
    facts.stable_id = control.stable_id().value();
    facts.classes = control.classes();
    facts.inline_rules = control.inline_style_rules();
    if (part.key) facts.part = (*part.key).part;
    facts.role = part.role;
    facts.states = part.states;
    facts.media = window_media_facts(*window);
    facts.user_rules = (*window).user_theme_rules();
    // Forced high contrast's pass (E1): only an important inline style's
    // drawing stands over it.
    const ControlVisualContext context = part_visual_context(control, part.states);
    facts.forced_high_contrast = context.high_contrast;
    result.resolution = rules_.resolve(facts);
    if (result.resolution.outcome == CompiledThemeRules::Outcome::role_mismatch) {
        result.drawing = PartDrawing::role_mismatch;
        return result;
    }
    if (result.resolution.outcome != CompiledThemeRules::Outcome::primitive) {
        if (context.high_contrast) result.drawing = PartDrawing::forced_high_contrast;
        return result;
    }
    if (result.resolution.parameters_read_em) {
        // A number parameter in em, at the part's em within its range (P8c).
        const double em = text_role_em(control, resolved_text_role(control, result.resolution.font));
        const PrimitiveDescriptor& described = (*result.resolution.primitive).descriptor();
        for (std::size_t index = 0; index < result.resolution.parameters.count; ++index) {
            const double coefficient = result.resolution.parameter_em[index];
            if (coefficient == 0.0 || index >= described.parameters.size()) continue;
            const PrimitiveParameterDeclaration& declared = described.parameters[index];
            PrimitiveParameterValue& value = result.resolution.parameters.values[index];
            const double taken = value.number + em * coefficient;
            value.number = std::min(std::max(taken, declared.minimum), declared.maximum);
        }
    }
    if (context.high_contrast) {
        // The pass's colours stand over every declaration's: each colour
        // parameter takes the high-contrast colour of its origin, drawn as
        // a fill.
        const PrimitiveDescriptor& described = (*result.resolution.primitive).descriptor();
        for (std::size_t index = 0; index < result.resolution.parameters.count; ++index) {
            if (index >= described.parameters.size()) break;
            if (described.parameters[index].kind != PrimitiveParameterKind::colour) continue;
            result.resolution.parameters.values[index].colour =
                high_contrast_colour(result.resolution.parameter_origins[index], ColourPosition::fill,
                                     context.surface, context.selected);
        }
    }
    const double scale = (*window).scale();
    result.scale = DeviceScale{scale};
    const PrimitiveDescriptor& descriptor = (*result.resolution.primitive).descriptor();
    const Size logical{part.bounds.width, part.bounds.height};
    result.extent = raster_extent(logical, descriptor.outsets, scale);
    if (result.extent.width == 0U || result.extent.height == 0U) {
        result.drawing = PartDrawing::not_paintable;
        return result;
    }
    if (animated(descriptor)) {
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(result.extent.width) * static_cast<std::uint64_t>(result.extent.height);
        const std::uint64_t operations =
            pixels * static_cast<std::uint64_t>(descriptor.cost.per_device_pixel) + descriptor.cost.fixed;
        if (operations > maximum_animated_operations_per_frame) {
            result.drawing = PartDrawing::over_frame_budget;
            return result;
        }
    }
    result.drawing = PartDrawing::primitive;
    return result;
}

PartHitRegion ThemeEngine::hit_region(const Control& control, const PartPaint& part) const noexcept {
    PartHitRegion region{};
    region.bounds = part.bounds;
    const PartPlan decided = plan(control, part);
    if (decided.drawing == PartDrawing::primitive) {
        region.shape = (*decided.resolution.primitive).shape(decided.resolution.parameters);
    }
    return region;
}

Rect ThemeEngine::text_slot(const Control& control, const PartPaint& part) const noexcept {
    const PartPlan decided = plan(control, part);
    if (decided.drawing != PartDrawing::primitive) return part.bounds;
    const Window* const window = control.attached_window();
    const double text_scale = (*window).presentation_settings().text_scale;
    const Size logical{part.bounds.width, part.bounds.height};
    const Insets insets = (*decided.resolution.primitive)
                              .content_insets(decided.resolution.parameters, logical, decided.scale, text_scale);
    const Rect slot = slot_within(part.bounds, insets);
    return slot;
}

void ThemeEngine::paint(Painter& painter, const Control& control, const PartPaint& part) const {
    const PartPlan decided = plan(control, part);
    const PartBlend blend = decided.resolution.blend;
    // Forced high contrast draws no blend, whatever draws the part.
    const bool high_contrast = blend != PartBlend::normal && part_visual_context(control, part.states).high_contrast;
    const bool blends = blend != PartBlend::normal && !high_contrast;
    if (!blends) {
        paint_planned(painter, control, part, decided);
        return;
    }
    // The part's extent, as its record notes it: its bounds grown by its
    // primitive's outsets and its granted overflow.
    Insets grown = part.granted_overflow;
    if (decided.drawing == PartDrawing::primitive) {
        const Insets outsets = (*decided.resolution.primitive).descriptor().outsets;
        grown.left = grown.left + outsets.left;
        grown.top = grown.top + outsets.top;
        grown.right = grown.right + outsets.right;
        grown.bottom = grown.bottom + outsets.bottom;
    }
    PaintGroup group{};
    group.bounds = Rect{part.bounds.x - grown.left, part.bounds.y - grown.top,
                        part.bounds.width + grown.left + grown.right, part.bounds.height + grown.top + grown.bottom};
    group.composite = blend == PartBlend::multiply ? PaintGroupComposite::multiply : PaintGroupComposite::screen;
    painter.begin_group(group);
    paint_planned(painter, control, part, decided);
    painter.end_group();
}

void ThemeEngine::paint_planned(Painter& painter, const Control& control, const PartPaint& part,
                                const PartPlan& decided) const {
    Window* const window = control.attached_window();
    // A program drawing both a theme and an inline style (E1, W401).
    const bool themed = rules_.rule_count() > rules_.built_in_rule_count();
    if (window != nullptr && themed && control.inline_style_rules() != nullptr) {
        (*window).note_inline_with_theme(control);
    }
    std::optional<PartRecordHandle> record{};
    if (part.key && part_bounds_paintable(part.bounds) && window != nullptr) {
        record = (*window).note_engine_part(control, *part.key, part.states);
    }
    const CompiledThemeRules::Resolution& resolution = decided.resolution;
    const bool primitive_draws = decided.drawing == PartDrawing::primitive && window != nullptr;
    // Where the part lands, and what it reads: the inputs are delivered and
    // damaged only for a part with a record (E6).
    Window::EnginePaintPlace place{};
    if (record || primitive_draws) place = (*window).engine_paint_place(control, painter);
    const bool reduced_motion = window != nullptr && (*window).presentation_settings().reduced_motion;
    Rect extent{};
    // Glass (E9-B) samples the window's backplane, the backplane plane
    // alone; a part recorded in the backplane plane would sample itself, so
    // it samples nothing.
    const bool samples_backplane =
        primitive_draws && (*resolution.primitive).backplane_margin(resolution.parameters, decided.scale) &&
        (*place.painted_by).paint_plane() != PaintPlane::backplane;
    if (record) {
        Insets outsets{};
        std::uint64_t inputs_read = 0U;
        if (primitive_draws) {
            outsets = (*resolution.primitive).descriptor().outsets;
            inputs_read = resolution.inputs_read;
            // An overlay that follows the pointer (E9-A) is repainted when
            // the pointer moves within the part, though its raster never
            // reads it.
            if (follows_pointer(resolution, part, reduced_motion)) inputs_read = inputs_read | pointer_inputs();
        }
        // A part that scales or translates may draw anywhere in its granted
        // overflow.
        if (draws_in_overflow(resolution)) {
            outsets.left = outsets.left + part.granted_overflow.left;
            outsets.top = outsets.top + part.granted_overflow.top;
            outsets.right = outsets.right + part.granted_overflow.right;
            outsets.bottom = outsets.bottom + part.granted_overflow.bottom;
        }
        extent = part_extent(part.bounds, place.origin, outsets);
        (*window).note_engine_paint(*record, inputs_read, *place.painted_by, extent);
        (*window).note_engine_backplane_reader(*record, samples_backplane);
    }
    // The part's record, for its transitions and its ambient effect (E7).
    EnginePartRecord* const kept = record ? (*window).engine_part_record(*record) : nullptr;
    PartTransitionSources transitions{};
    transitions.window = window;
    transitions.record = kept;
    if (record) transitions.handle = *record;
    if (kept != nullptr) transitions.owner = (*window).find(control.stable_id().value());
    transitions.states = part.states;
    transitions.rules_serial = rules_.serial();
    if (transitions_read_em(resolution)) {
        transitions.em = text_role_em(control, resolved_text_role(control, resolution.font));
    }
    if (!primitive_draws) {
        if (kept != nullptr) stop_ambient(*window, *kept, *record);
        PrimitiveParameterBlock unused{};
        const PresentedProperties presented = run_transitions(resolution, transitions, nullptr, unused);
        PartPaint shown = scaled_part(part, presented.scale);
        const double shown_scale = std::min(presented.scale, granted_scale(part.bounds, part.granted_overflow));
        shown.bounds = moved_by(shown.bounds, granted_translation(part, shown_scale, presented.translation));
        paint_built_in(painter, control, shown, presented.opacity);
        return;
    }
    PartRasterRequest request{};
    request.primitive = resolution.primitive;
    request.parameters = resolution.parameters;
    request.target.logical_size = Size{part.bounds.width, part.bounds.height};
    request.target.scale = decided.scale;
    request.target.states = part.states;
    request.extent = decided.extent;
    // The ambient effect's time, then the inputs, the expressions that read
    // them, and the transitions over both.
    std::optional<double> ambient_time{};
    if (resolution.ambient != nullptr) {
        const double window_area = (*window).client_size().width * (*window).client_size().height;
        const bool everyday = !control.on_ceremonial_surface();
        const bool area_ok = !everyday || extent.area() <= maximum_ambient_window_fraction * window_area;
        ambient_time = ambient_seconds(*window, kept, record, *resolution.ambient, area_ok);
    } else if (kept != nullptr) {
        stop_ambient(*window, *kept, *record);
    }
    std::array<double, maximum_expression_inputs> inputs{};
    PartInputSources sources{};
    sources.window = window;
    sources.part_origin = Point{place.window_origin.x + part.bounds.x, place.window_origin.y + part.bounds.y};
    sources.states = part.states;
    sources.pointer = (*window).engine_pointer();
    sources.record = kept;
    sources.time_in_state = ambient_time;
    gather_inputs(rules_, resolution.inputs_read, sources, inputs);
    for (std::size_t index = 0; index < engine_input_count; ++index) request.target.inputs[index] = inputs[index];
    evaluate_expressions(resolution, inputs, part.states, request.parameters);
    const PrimitiveDescriptor& descriptor = (*request.primitive).descriptor();
    const PresentedProperties presented = run_transitions(resolution, transitions, &descriptor, request.parameters);
    // E7r: nothing bakes per frame. Moving animated parameters step through
    // shared keyframes, and an ambient effect's loop frames are shared.
    // Owner C and D (2026-10-22): a running transition, or the pointer,
    // steps its parameter to the nearest of the keyframes.
    const std::uint32_t driven = pointer_driven(resolution, descriptor);
    const bool at_keyframes =
        step_to_keyframes(descriptor, presented.moving_parameters | driven, request.parameters);
    // The parameters whose keyframes are worth baking ahead: those a
    // transition or the pointer can move.
    const std::uint32_t movable = presented.transitioned_parameters | driven;
    // While its scale moves, the part's raster at its own size is shown
    // stretched (form b), so it is shared too: baked when the move starts.
    request.shared = kept != nullptr && (at_keyframes || ambient_time.has_value() || presented.scale_moving);
    const double opacity = presented.opacity;
    // A part at opacity 0, or one its primitive draws as nothing (a flat
    // bulge), composites nothing and needs no raster: it only prepares the
    // keyframes of a transition starting from it (a rise from the plane).
    const bool invisible = opacity <= 0.0 || (*request.primitive).blank(request.parameters);
    // A scaled part at rest is drawn at its scaled size (D5-1), so that its
    // edges stay sharp; one whose scaled raster would not fit, or would
    // break the animated frame budget, is drawn at its own size. While its
    // scale moves, its raster at its own size is stretched over the scaled
    // bounds (E7r form b), so that the move bakes nothing.
    PartPaint shown = scaled_part(part, presented.scale);
    const bool stretched = shown.bounds != part.bounds && presented.scale_moving;
    // A primitive behind the overlay seam (E9-A, the sheen) bakes its own
    // size, a band of the part's height, and is placed when composited. It
    // follows the part's presentational scale as a face does: at rest it
    // bakes at the scaled size, and while the scale moves its raster at the
    // part's own size is stretched by the same factor (E7r form b).
    const double granted = std::min(presented.scale, granted_scale(part.bounds, part.granted_overflow));
    const std::optional<PointerOverlay> placed =
        (*request.primitive).pointer_overlay(request.parameters, Size{part.bounds.width, part.bounds.height});
    if (placed) {
        const double baked_scale = stretched || shown.bounds == part.bounds ? 1.0 : granted;
        const Size baked{(*placed).size.width * baked_scale, (*placed).size.height * baked_scale};
        request.target.logical_size = baked;
        request.extent = raster_extent(baked, Insets{}, decided.scale.value);
        if (request.extent.width == 0U || request.extent.height == 0U) return;
    }
    if (!placed && shown.bounds != part.bounds && !stretched) {
        const double scale = decided.scale.value;
        const Size shown_size{shown.bounds.width, shown.bounds.height};
        const DeviceExtent scaled = raster_extent(shown_size, descriptor.outsets, scale);
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(scaled.width) * static_cast<std::uint64_t>(scaled.height);
        const std::uint64_t operations =
            pixels * static_cast<std::uint64_t>(descriptor.cost.per_device_pixel) + descriptor.cost.fixed;
        const bool within = scaled.width > 0U && scaled.height > 0U &&
                            (!animated(descriptor) || operations <= maximum_animated_operations_per_frame);
        if (within) {
            request.target.logical_size = Size{shown.bounds.width, shown.bounds.height};
            request.extent = scaled;
        } else {
            shown = part;
        }
    }
    // The part as drawn moves by its translation (E9.6), within the overflow
    // its scale as shown leaves; its raster is composited at the offset, so
    // that the move bakes nothing. Its hit region and text slot stay put.
    const bool at_own_size = shown.bounds == part.bounds;
    const double shown_scale = at_own_size ? 1.0 : granted;
    const Point moved = granted_translation(part, shown_scale, presented.translation);
    shown.bounds = moved_by(shown.bounds, moved);
    // A primitive that draws a theme material borrows the material of its
    // recipe role for the part's state (E8-1), with the material fields the
    // rules set in place (EC1), the part's own corner radius and the
    // window's images, for this paint only.
    std::optional<SurfaceMaterial> unkept_material{};
    std::optional<SurfaceMaterial> authored_ground{};
    const std::optional<ControlVisualRole> material_role =
        (*request.primitive).theme_material(request.parameters, part.family);
    if (material_role) {
        const ControlVisualContext context = part_visual_context(control, part.states);
        const ControlVisualRecipe& recipe = control.effective_theme().resolve(*material_role, context);
        request.target.material = &recipe.material;
        if (resolution.materials_authored && context.high_contrast) {
            // Forced high contrast's material fields, for this paint only.
            detail::ForcedMaterialFields forced{};
            detail::forced_material_fields(resolution.materials, context.surface, context.selected, forced);
            MaterialAssemblyContext relative{};
            if (material_fields_read_em(forced.fields)) {
                relative.em = text_role_em(control, resolved_text_role(control, resolution.font));
            }
            unkept_material.emplace();
            assemble_material(recipe.material, forced.fields, Size{part.bounds.width, part.bounds.height}, relative,
                              *unkept_material);
            (*window).metrics().record_engine_material_assembly();
            request.target.material = &*unkept_material;
        } else if (resolution.materials_authored) {
            // The part's em and text colour, where a field reads them (P8c).
            MaterialAssemblyContext relative{};
            if (material_fields_read_em(resolution.materials)) {
                relative.em = text_role_em(control, resolved_text_role(control, resolution.font));
            }
            if (material_fields_read_current_colour(resolution.materials)) {
                const Theme& theme = control.effective_theme();
                const ResolvedControlRecipe text = theme.resolve(control, part.family, context, part.states);
                relative.current_colour = text.recipe().text;
            }
            request.target.material = &authored_material(kept, rules_.serial(), recipe.material, part.bounds,
                                                         relative, resolution, unkept_material,
                                                         (*window).metrics());
        }
        // An authored background (owner decision T4-1) stands as the
        // material's only fill, under an important rule's primitive and
        // never in high contrast.
        const bool important = resolution.level >= CascadeLevel::important_user_theme;
        if (authored_background_applies(part) && !important && !context.high_contrast) {
            authored_ground.emplace(*request.target.material);
            (*authored_ground).fills.assign(1U, MaterialFillLayer::solid(*part.authored_background));
            request.target.material = &*authored_ground;
        }
        request.target.corner_radius = part.corner_radius;
        request.target.images = &(*window).image_resources();
    }
    // The overlay shadow (E9, GIVEN E1-1) borrows the theme's layers for the
    // part's presentation, as a material of one transparent fill and those
    // shadows at the part's corner radius (its own, else its family
    // recipe's), for this paint only. A theme that casts none draws nothing.
    SurfaceMaterial overlay{};
    if ((*request.primitive).lends_overlay_shadow()) {
        const ControlVisualContext context = part_visual_context(control, part.states);
        const Theme& theme = control.effective_theme();
        const std::vector<MaterialShadow>& layers = theme.overlay_shadow(context.high_contrast);
        if (layers.empty()) return;
        const ControlVisualRecipe& recipe = theme.resolve(part.family, context);
        overlay.fills.assign(1U, MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
        overlay.shadows = layers;
        overlay.corner_radius = recipe.material.corner_radius;
        if (part.corner_radius) overlay.corner_radius = std::max(0.0, *part.corner_radius);
        request.target.material = &overlay;
        request.target.images = &(*window).image_resources();
    }
    // Glass borrows the backplane image under its raster, grown by the
    // margin its blur reads, for this paint only. Its raster depends on
    // where it lies, so it is never shared as a keyframe.
    Window::EngineBackplane backplane{};
    const std::optional<std::uint32_t> margin =
        samples_backplane ? (*request.primitive).backplane_margin(request.parameters, decided.scale) : std::nullopt;
    if (margin) {
        const Point raster_origin{place.window_origin.x + shown.bounds.x - descriptor.outsets.left,
                                  place.window_origin.y + shown.bounds.y - descriptor.outsets.top};
        backplane = (*window).engine_backplane(raster_origin, request.extent, decided.scale, *margin);
        request.target.backplane = backplane.image.get();
        request.target.backplane_margin = *margin;
        request.target.backplane_generation = backplane.generation;
        request.target.backplane_origin = backplane.origin;
        request.target.reduced_transparency = (*window).presentation_settings().reduced_transparency;
        request.target.corner_radius = part.corner_radius;
        request.shared = false;
    }
    // A primitive that refuses the part leaves it to the built-in recipe;
    // a keyed part's refusal is counted when it begins (E8c).
    const bool accepted = (*request.primitive).accepts(request.parameters, request.target);
    // Owner A (2026-10-22): a part that can move its parameters has their
    // keyframes queued for idle frames when it first paints and whenever
    // its states change; one it reaches first is baked on demand.
    const bool requeue = kept != nullptr && (!(*kept).keyframe_states || *(*kept).keyframe_states != part.states);
    if (accepted && movable != 0U && requeue && !margin) {
        (*kept).keyframe_states = part.states;
        queue_keyframes(*window, request, movable);
    }
    if (invisible) return;
    const bool newly_refused = kept == nullptr || !(*kept).primitive_refused;
    if (kept != nullptr) (*kept).primitive_refused = !accepted;
    if (!accepted) {
        if (newly_refused) (*window).metrics().record_engine_primitive_refusal();
        paint_built_in(painter, control, shown, opacity);
        return;
    }
    std::shared_ptr<const PartRaster> raster{};
    if (record) {
        const PartRasterResult drawn = (*window).engine_part_raster(*record, request);
        raster = drawn.raster;
    } else {
        // A part with no record: a raster of its own, for this paint only
        // (assembled from the window's slice references when it slices).
        const PartRasterResult drawn = (*window).engine_unkeyed_raster(request);
        raster = drawn.raster;
    }
    if (!raster) {
        paint_built_in(painter, control, shown, opacity);
        return;
    }
    if (placed) {
        const double pointer_x = inputs[input_slot(EngineInput::pointer_x)];
        OverlayPlacement placement{};
        placement.face = shown.bounds;
        placement.granted = shown_scale;
        placement.stretched = stretched;
        placement.pointer_x = pointer_x;
        placement.reduced_motion = reduced_motion;
        paint_pointer_overlay(painter, control, part, *placed, placement, raster, opacity);
        return;
    }
    // A face that changed from one discrete state to another cross-fades
    // (form a): the old face at 1 - t and the new one added at t, in one
    // isolated group at the part's opacity. The two faces have the same
    // extent (fades_between), so while the scale moves (form b) both are
    // stretched over the same rectangle and the fade composes with the pop.
    std::shared_ptr<const PartRaster> fading_from{};
    if (kept != nullptr) fading_from = note_face(*kept, presented.face, raster, part.states);
    // The raster covers the part grown by its primitive's outsets; stretched,
    // that rectangle is scaled about the part's centre as its bounds are.
    const Insets outsets = descriptor.outsets;
    Rect stretched_to{};
    if (stretched) {
        const Rect covered{part.bounds.x - outsets.left, part.bounds.y - outsets.top,
                           part.bounds.width + outsets.left + outsets.right,
                           part.bounds.height + outsets.top + outsets.bottom};
        const double granted = std::min(presented.scale, granted_scale(part.bounds, part.granted_overflow));
        const Point centre{part.bounds.x + part.bounds.width * 0.5, part.bounds.y + part.bounds.height * 0.5};
        stretched_to = Rect{centre.x + (covered.x - centre.x) * granted + moved.x,
                            centre.y + (covered.y - centre.y) * granted + moved.y, covered.width * granted,
                            covered.height * granted};
    }
    if (fading_from) {
        const double fade = std::clamp((*presented.face).current(), 0.0, 1.0);
        const Point origin{shown.bounds.x - outsets.left, shown.bounds.y - outsets.top};
        const DeviceExtent covered = (*raster).extent();
        const double device_scale = (*raster).scale().value;
        Rect bounds{origin.x, origin.y, static_cast<double>(covered.width) / device_scale,
                    static_cast<double>(covered.height) / device_scale};
        if (stretched) bounds = stretched_to;
        PaintGroup whole{};
        whole.bounds = bounds;
        whole.opacity = opacity;
        whole.isolated = true;
        PaintGroup added{};
        added.bounds = bounds;
        added.opacity = fade;
        added.composite = PaintGroupComposite::plus;
        painter.begin_group(whole);
        if (stretched) {
            painter.draw_part_raster_scaled(fading_from, stretched_to, 1.0 - fade);
        } else {
            painter.draw_part_raster(fading_from, origin, 1.0 - fade);
        }
        painter.begin_group(added);
        if (stretched) {
            painter.draw_part_raster_scaled(raster, stretched_to, 1.0);
        } else {
            painter.draw_part_raster(raster, origin, 1.0);
        }
        painter.end_group();
        painter.end_group();
        return;
    }
    if (stretched) {
        painter.draw_part_raster_scaled(raster, stretched_to, opacity);
        return;
    }
    painter.draw_part_raster(raster, Point{shown.bounds.x - outsets.left, shown.bounds.y - outsets.top}, opacity);
}

// Queues every keyframe level of each animated parameter in `movable`,
// nearest the value `request` shows first, with the part's other
// parameters as `request` has them. Blank keyframes (a flat bulge) need
// none.
void ThemeEngine::queue_keyframes(Window& window, const PartRasterRequest& request, std::uint32_t movable) {
    const EnginePrimitive& primitive = *request.primitive;
    const PrimitiveDescriptor& descriptor = primitive.descriptor();
    for (std::size_t index = 0; index < request.parameters.count; ++index) {
        const PrimitiveParameterDeclaration& declared = descriptor.parameters[index];
        if (!declared.animated || (movable & parameter_bit(index)) == 0U) continue;
        const std::size_t shown = nearest_keyframe(declared, request.parameters.values[index].number);
        PartRasterRequest keyframe = request;
        keyframe.shared = true;
        // Outward from the level shown: shown, shown + 1, shown - 1, ...
        for (std::size_t distance = 0U; distance < animated_keyframe_levels; ++distance) {
            for (std::size_t side = 0U; side < 2U; ++side) {
                if (distance == 0U && side == 1U) continue;
                if (side == 1U && distance > shown) continue;
                const std::size_t level = side == 0U ? shown + distance : shown - distance;
                if (level >= animated_keyframe_levels) continue;
                keyframe.parameters.values[index].number = keyframe_value(declared, level);
                if (primitive.blank(keyframe.parameters)) continue;
                window.engine_queue_keyframe(keyframe);
            }
        }
    }
}

double ThemeEngine::ambient_seconds(Window& window, EnginePartRecord* record,
                                    const std::optional<PartRecordHandle>& handle, const AmbientEffect& ambient,
                                    bool area_ok) {
    const double pose = std::chrono::duration<double>(ambient.pose).count();
    // A part without a record is drawn statically, at its pose.
    if (record == nullptr || !handle) return pose;
    EnginePartRecord& part = *record;
    part.ambient = true;
    if (!area_ok && !part.ambient_refused) window.metrics().record_engine_ambient_refusal();
    part.ambient_refused = !area_ok;
    if (!area_ok || window.motion_clock().ambient_suppressed()) {
        window.stop_engine_ambient(*handle);
        return pose;
    }
    const std::chrono::duration<double> frame(1.0 / ambient.frames_per_second);
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(frame);
    window.run_engine_ambient(*handle, interval);
    // The time in the part's states, wrapped to the period, so the effect
    // cycles, and stepped to the start of its loop frame (E7r), so that each
    // frame's raster is baked once and shown again every period; exact in
    // the clock's ticks.
    const FrameInterval elapsed = window.motion_clock().now() - part.state_entered;
    const FrameInterval forward = elapsed < FrameInterval::zero() ? FrameInterval::zero() : elapsed;
    const FrameInterval within = forward % ambient.period;
    const double frames_in_period = std::ceil(std::chrono::duration<double>(ambient.period).count() *
                                              ambient.frames_per_second);
    const double frames = std::clamp(frames_in_period, 1.0, static_cast<double>(maximum_ambient_loop_frames));
    const FrameInterval::rep frame_ticks = ambient.period.count() / static_cast<FrameInterval::rep>(frames);
    FrameInterval stepped = within;
    if (frame_ticks > 0) stepped = FrameInterval(within.count() / frame_ticks * frame_ticks);
    const double seconds = std::chrono::duration<double>(stepped).count();
    return seconds;
}

void ThemeEngine::stop_ambient(Window& window, EnginePartRecord& record, PartRecordHandle handle) noexcept {
    if (!record.ambient) return;
    record.ambient = false;
    record.ambient_refused = false;
    window.stop_engine_ambient(handle);
}

} // namespace gui_forms
