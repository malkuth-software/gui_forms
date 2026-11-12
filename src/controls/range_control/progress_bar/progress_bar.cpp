#include "gui_forms/controls/progress_bar.hpp"

#include "../range_control_rendering.hpp"
#include "gui_forms/controls/progress_bar_effects.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

namespace {

// The ProgressBarVisualStyle enumeration, numbered as declared
// (luminance_pulse is pulse's other name). Static storage; any thread may
// read it.
std::shared_ptr<const PropertyEnumDescriptor> progress_bar_visual_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "progress_bar_visual_style",
            {{"blocks", 0}, {"continuous", 1}, {"marquee", 2}, {"pulse", 3}, {"marching_stripes", 4},
             {"laser_etch", 5}},
            false});
    return value;
}

// The marching stripes' defaults (owner rulings of 2026-11-11): the pale
// band 0.4 of the pitch, leaning 45 degrees back from the leading edge,
// travelling 0.8 pitches a second toward it.
constexpr double default_stripe_ratio = 0.4;
constexpr double default_stripe_angle = 45.0;
constexpr double default_stripe_speed = 0.8;
constexpr double stripe_degrees_to_radians = 3.14159265358979323846 / 180.0;
// The smallest automatic pitch, so a thin bar's bands stay visible.
constexpr double minimum_automatic_stripe_pitch = 8.0;
// Bands drawn beyond the fill's length at most: the two ends' partial bands
// and the slant's reach (ProgressStripes::maximum_stripes).
constexpr double stripe_margin_bands = 8.0;

// The fill's thickness across the bar and the track's length along it.
struct StripeSpan final {
    double thickness{};
    double length{};
};

StripeSpan stripe_span(const Rect& interior, bool vertical) noexcept {
    StripeSpan span{};
    span.thickness = vertical ? interior.width : interior.height;
    span.length = vertical ? interior.height : interior.width;
    return span;
}

// How far a band's far side runs ahead of its near side: tan(angle) times
// the thickness, positive when it leans back (the default).
double stripe_slant(const ControlStripePattern& pattern, double thickness) noexcept {
    const double angle = pattern.angle_degrees.value_or(default_stripe_angle);
    const double reach = std::tan(angle * stripe_degrees_to_radians) * thickness;
    const bool backward = pattern.lean.value_or(StripeSense::backward) == StripeSense::backward;
    const double slant = backward ? reach : -reach;
    return slant;
}

// The pitch: the theme's, or the fill's thickness rounded (never under
// minimum_automatic_stripe_pitch); raised where the track would need more
// than ProgressStripes::maximum_stripes bands.
double stripe_pitch(const ControlStripePattern& pattern, const StripeSpan& span, double slant) noexcept {
    const double authored = pattern.pitch.value_or(0.0);
    const double automatic = std::max(minimum_automatic_stripe_pitch, std::round(span.thickness));
    const double chosen = authored > 0.0 ? authored : automatic;
    const double bands = static_cast<double>(ProgressStripes::maximum_stripes) - stripe_margin_bands;
    const double least = (span.length + std::abs(slant)) / bands;
    const double pitch = std::max(chosen, least);
    return pitch;
}

// The stripes' view for a fill in `interior`, from the fill part's pattern
// and corner radius. `colour` is the pale band's colour where the theme
// sets none; `speed_scale` is the motion policy's.
ProgressStripeView stripe_view(const ControlStripePattern& pattern, double corner_radius, const Rect& interior,
                               const Rect& fill, bool vertical, Color colour, double speed_scale) noexcept {
    ProgressStripeView view{};
    // Inside the fill's one-unit rim, with its rounded ends.
    view.clip = Rect{fill.x + 1.0, fill.y + 1.0, std::max(0.0, fill.width - 2.0), std::max(0.0, fill.height - 2.0)};
    view.clip_radius = std::max(0.0, corner_radius - 1.0);
    const StripeSpan span = stripe_span(interior, vertical);
    const double across = vertical ? view.clip.width : view.clip.height;
    view.slant = stripe_slant(pattern, across);
    view.pitch = stripe_pitch(pattern, span, view.slant);
    view.band = view.pitch * pattern.ratio.value_or(default_stripe_ratio);
    view.forward = pattern.direction.value_or(StripeSense::forward) == StripeSense::forward;
    // No control-level right-to-left flow exists yet: a horizontal bar
    // grows left to right, a vertical one bottom to top.
    view.direction = vertical ? ProgressFillDirection::bottom_to_top : ProgressFillDirection::left_to_right;
    view.colour = pattern.colour.value_or(colour);
    view.ground = pattern.ground.value_or(Color::rgba(0, 0, 0, 0));
    view.gloss = pattern.gloss.value_or(StripeGloss::automatic) == StripeGloss::automatic;
    const double speed = pattern.speed.value_or(default_stripe_speed) * speed_scale;
    view.period_seconds = speed > 0.0 ? 1.0 / speed : 0.0;
    view.hidden = pattern.reduced_motion.value_or(StripeReducedMotion::still) == StripeReducedMotion::none;
    return view;
}

} // namespace

namespace {

std::shared_ptr<const PropertyEnumDescriptor> effect_input_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "effect_input_kind", {{"none", 0}, {"pointer", 1}, {"pointer_and_keys", 2}}, false});
    return value;
}

} // namespace

ProgressBar::ProgressBar(StableId stable_id)
    : RangeControl(std::move(stable_id)) {
    focusable = false;
}

BindingValue ProgressBar::effect_seed_property_value() const {
    const std::optional<std::uint64_t>& seed = effect_seed.read();
    if (!seed) return BindingValue{};
    return BindingValue{*seed};
}

void ProgressBar::write_effect_seed_property(const BindingValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        effect_seed = std::nullopt;
        return;
    }
    effect_seed = std::get<std::uint64_t>(value);
}

void ProgressBar::validate_animation_appearance(const ProgressBarAnimationAppearance& appearance) const {
    require_finite(appearance.pulse_extent, "progress pulse extent must be finite");
    require_finite(appearance.laser_edge_extent, "progress laser edge extent must be finite");
    require_finite(appearance.laser_phase_pitch, "progress laser phase pitch must be finite");
    if (appearance.pulse_extent < 0.1 || appearance.pulse_extent > 0.8) {
        throw std::invalid_argument("progress pulse extent must be between 0.1 and 0.8");
    }
    if (appearance.laser_edge_extent < 2.0 || appearance.laser_edge_extent > 48.0) {
        throw std::invalid_argument("progress laser edge extent must be between 2 and 48 pixels");
    }
    if (appearance.laser_phase_pitch < 3.0 || appearance.laser_phase_pitch > 64.0) {
        throw std::invalid_argument("progress laser phase pitch must be between 3 and 64 pixels");
    }
}

// A new style or overlay restarts the phase and reselects the effects.
void ProgressBar::restyle() {
    animation_phase_ = 0.0;
    sync_effects(FrameClock::now());
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::prepare_write(const decltype(visual_style)&, ProgressBarVisualStyle&) {
    require_mutable();
}

void ProgressBar::written(const decltype(visual_style)&) {
    restyle();
}

void ProgressBar::prepare_write(const decltype(overlay_style)&, ProgressBarOverlayStyle&) {
    require_mutable();
}

void ProgressBar::written(const decltype(overlay_style)&) {
    restyle();
}

void ProgressBar::prepare_write(const decltype(animation_enabled)&, bool&) {
    require_mutable();
}

void ProgressBar::written(const decltype(animation_enabled)&) {
    if (!animation_enabled.read()) {
        animation_phase_ = 0.0;
    }
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::prepare_write(const decltype(animation_period)&, FrameInterval& incoming) {
    require_mutable();
    if (incoming < std::chrono::milliseconds(100)) {
        throw std::invalid_argument("progress animation period must be at least 100 milliseconds");
    }
}

void ProgressBar::written(const decltype(animation_period)&) {
    last_animation_frame_ = FrameClock::now();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::prepare_write(const decltype(effect)&, std::string&) {
    require_mutable();
}

void ProgressBar::written(const decltype(effect)&) {
    effect_instance_.detach();
    sync_effects(FrameClock::now());
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::prepare_write(const decltype(effect_seed)&, std::optional<std::uint64_t>&) {
    require_mutable();
}

// A new seed is a new arrangement: the effects start again.
void ProgressBar::written(const decltype(effect_seed)&) {
    effect_instance_.detach();
    overlay_instance_.detach();
    sync_effects(FrameClock::now());
    invalidate(Dirty::paint);
}

void ProgressBar::prepare_write(const decltype(effect_input)&, EffectInputKind&) {
    require_mutable();
}

void ProgressBar::written(const decltype(effect_input)&) {
    effect_instance_.set_input_grant(effect_input.read());
    invalidate(Dirty::paint);
}

void ProgressBar::on_pointer(PointerEvent& event) {
    // The part is the bar's bounds: its origin is the bar's absolute origin.
    const Rect absolute = absolute_bounds();
    if (effect_instance_.note_pointer(event, Point{absolute.x, absolute.y})) invalidate(animated_extent());
}

void ProgressBar::write_view(const decltype(motion_policy)&, MotionPolicy incoming) {
    require_mutable();
    if (motion_policy_ == incoming) return;
    motion_policy_ = incoming;
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

MotionPolicy ProgressBar::read_view(const decltype(motion_policy)&) const {
    return motion_policy_;
}

MotionPolicy ProgressBar::effective_motion_policy() const noexcept {
    MotionPolicy policy = motion_policy_;
    if (window() != nullptr &&
        (*window()).presentation_settings().reduced_motion) {
        policy.reduced = true;
    }
    return policy;
}

bool ProgressBar::animated_style() const noexcept {
    return visual_style.read() == ProgressBarVisualStyle::marquee ||
           effect_instance_.entry() != nullptr ||
           overlay_instance_.entry() != nullptr;
}

namespace {

// One effect colour from the theme: the role's colour, or under the forced
// high-contrast pass the pass's colour for the role, drawn as a fill.
Color effect_colour(const EffectColourDeclaration& declared, const ControlVisualContext& context) noexcept {
    if (!context.high_contrast) return declared.colour;
    const Color remapped =
        high_contrast_colour(declared.origin, ColourPosition::fill, context.surface, context.selected);
    return remapped;
}

// `colour` with the red, green and blue of `role` and its own alpha.
Color role_tinted(Color colour, Color role) noexcept {
    const Color tinted = Color::rgba(role.red, role.green, role.blue, colour.alpha);
    return tinted;
}

} // namespace

ProgressBar::RequestedEffect ProgressBar::requested_effect() const noexcept {
    RequestedEffect requested{};
    // The theme's declarations for the part the effect draws over: the
    // fill, or the activity band while marquee (publish_part_geometry). A
    // bar in no window has none yet; its next paint asks again.
    bool theme_effect = false;
    std::string_view theme_name{};
    if (attached_window() != nullptr) {
        const bool marquee = visual_style.read() == ProgressBarVisualStyle::marquee;
        PartPaint part{};
        part.role = marquee ? PartRole::activity : PartRole::fill;
        part.bounds = local_bounds();
        part.states = anatomy_states();
        part.family = ControlVisualRole::progress;
        part.key = PartKey{.part = marquee ? std::string_view{"activity"} : std::string_view{"fill"}};
        const ThemeEngine::PartPlan plan = effective_theme().engine().plan(*this, part);
        theme_effect = plan.resolution.effect_declared;
        theme_name = plan.resolution.effect;
        if (plan.resolution.effect_primary && plan.resolution.effect_secondary) {
            const ControlVisualContext context = part_visual_context(*this, part.states);
            requested.colours.primary = effect_colour(*plan.resolution.effect_primary, context);
            requested.colours.secondary = effect_colour(*plan.resolution.effect_secondary, context);
            requested.themed_colours = true;
        }
    }
    if (!effect.read().empty()) {
        requested.name = effect.read();
        requested.source = EffectSelectionSource::application;
        return requested;
    }
    requested.source = EffectSelectionSource::style;
    if (visual_style.read() == ProgressBarVisualStyle::marching_stripes) {
        requested.name = ProgressStripes::declaration.name;
        return requested;
    }
    if (visual_style.read() == ProgressBarVisualStyle::pulse) {
        requested.name = ProgressPulse::declaration.name;
        return requested;
    }
    if (visual_style.read() == ProgressBarVisualStyle::laser_etch) {
        requested.name = ProgressLaser::declaration.name;
        return requested;
    }
    requested.source = EffectSelectionSource::none;
    if (!theme_effect) return requested;
    requested.name = theme_name;
    requested.source = EffectSelectionSource::theme;
    return requested;
}

namespace {

// The registered progress-bar effect named `name`, or null (the overlay's
// stripes, which only the overlay style asks for).
const EffectEntry* progress_bar_effect(std::string_view name) noexcept {
    const EffectEntry* const entry = built_in_effect_registry().find(name);
    if (entry == nullptr || (*entry).parent != EffectView<ProgressBar>::parent_name) {
        return nullptr;
    }
    return entry;
}

// Attaches `wanted` to `instance` unless it holds it already, or detaches
// it when nothing is wanted.
void sync_instance(EffectInstance& instance, const EffectEntry* wanted,
                   std::uint64_t seed, FrameTime now) {
    if (wanted == nullptr) {
        instance.detach();
        return;
    }
    if (instance.entry() == wanted) return;
    instance.attach(*wanted, seed, now);
}

} // namespace

void ProgressBar::sync_effects(FrameTime now) {
    const RequestedEffect requested = requested_effect();
    const Rect bounds = local_bounds();
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    const EffectEntry* selected = nullptr;
    const EffectSelectionOutcome outcome =
        check_effect_selection(built_in_effect_registry(), requested.name, requested.source,
                               EffectView<ProgressBar>::parent_name, Size{bounds.width, bounds.height}, scale, selected);
    const bool changed = outcome != effect_selection_.outcome || requested.source != effect_selection_.source ||
                         requested.name != effect_selection_.name;
    if (changed) {
        effect_selection_.name = std::string{requested.name};
        effect_selection_.source = requested.source;
        effect_selection_.outcome = outcome;
        if (outcome != EffectSelectionOutcome::selected && outcome != EffectSelectionOutcome::none) {
            note_effect_refused(effect_selection_);
        }
    }
    const bool overlay = overlay_style.read() == ProgressBarOverlayStyle::moving_stripes &&
                         visual_style.read() != ProgressBarVisualStyle::marching_stripes;
    const EffectEntry* const stripes =
        overlay ? progress_bar_effect(ProgressStripes::declaration.name) : nullptr;
    const std::string_view id = stable_id().value();
    const std::uint64_t selected_seed = selected != nullptr
        ? effect_seed.read().value_or(effect_default_seed(id, (*(*selected).declaration).name))
        : 0U;
    const std::uint64_t stripes_seed =
        effect_seed.read().value_or(effect_default_seed(id, ProgressStripes::declaration.name));
    sync_instance(effect_instance_, selected, selected_seed, now);
    sync_instance(overlay_instance_, stripes, stripes_seed, now);
    // The colours: the theme's roles, unless the application set its own
    // appearance (the application's colours come first).
    const bool own_appearance = !(animation_appearance.read() == ProgressBarAnimationAppearance{});
    themed_colours_ = requested.themed_colours && !own_appearance;
    effect_colours_.primary = themed_colours_ ? requested.colours.primary : animation_appearance.read().stripe_color;
    effect_colours_.secondary =
        themed_colours_ ? requested.colours.secondary : animation_appearance.read().luminance_color;
    effect_instance_.set_colours(effect_colours_);
    overlay_instance_.set_colours(effect_colours_);
}

Insets ProgressBar::visual_outsets() const noexcept {
    const EffectEntry* const entry = effect_instance_.entry();
    if (entry == nullptr) return Insets{};
    const Insets outsets = (*(*entry).declaration).outsets;
    return outsets;
}

bool ProgressBar::effects_move() const noexcept {
    const MotionPolicy policy = effective_motion_policy();
    const bool attached = effect_instance_.entry() != nullptr || overlay_instance_.entry() != nullptr;
    return attached && animation_enabled.read() && policy.active() && !policy.reduced;
}

void ProgressBar::update_effect_ambient(bool painting) {
    if (window() == nullptr) return;
    MotionClock& clock = (*window()).motion_clock();
    if (!effects_move()) {
        clock.withdraw_control_ambient(*this);
        return;
    }
    if (!painting && !clock.control_ambient(*this)) return;
    // At the effects' declared rate (the faster of the two, when both run).
    double rate = 0.0;
    if (effect_instance_.entry() != nullptr) {
        rate = (*(*effect_instance_.entry()).declaration).frames_per_second;
    }
    if (overlay_instance_.entry() != nullptr) {
        rate = std::max(rate, (*(*overlay_instance_.entry()).declaration).frames_per_second);
    }
    const FrameInterval interval =
        std::chrono::ceil<FrameInterval>(std::chrono::duration<double>(1.0 / rate));
    const bool started = clock.enlist_control_ambient(*this, interval);
    if (!started) return;
    // Motion starts or resumes: the effects' time restarts, with no
    // catch-up for the pause.
    const FrameTime now = clock.now();
    effect_instance_.rebase(now);
    overlay_instance_.rebase(now);
    effect_paints_seen_ = ~std::uint64_t{0};
}

bool ProgressBar::on_ambient_frame(FrameTime now) {
    if (effect_paints_ == effect_paints_seen_) return false;
    effect_paints_seen_ = effect_paints_;
    static_cast<void>(effect_instance_.advance(now));
    static_cast<void>(overlay_instance_.advance(now));
    invalidate(animated_extent());
    return true;
}

void ProgressBar::update_animation_registration() {
    update_effect_ambient(false);
    const MotionPolicy policy = effective_motion_policy();
    // The marquee runs on the bar's own timer (ADR-016); the effects on the
    // motion clock.
    if (window() == nullptr || !animation_enabled.read() || !policy.active() ||
        visual_style.read() != ProgressBarVisualStyle::marquee) {
        animation_timer_.stop();
        return;
    }
    last_animation_frame_ = FrameClock::now();
    const std::shared_ptr<ProgressBar> self =
        std::static_pointer_cast<ProgressBar>(shared_from_this());
    animation_timer_.start(*this, animation_interval(), AnimationTick{self});
}

FrameInterval ProgressBar::animation_interval() const noexcept {
    // Logical pixels the fastest moving part travels in one period: a band
    // crosses the track and its own extent; stripes advance one pitch. The
    // laser's flames flicker about 24 pixels a period whatever its pitch.
    const Rect interior = indicator_bounds();
    const bool horizontal = orientation.read() == Orientation::horizontal;
    const double track = horizontal ? local_bounds().width : local_bounds().height;
    double travel = 0.0;
    if (visual_style.read() == ProgressBarVisualStyle::marquee) {
        travel = track + std::max(18.0, track * 0.28);
    } else if (visual_style.read() == ProgressBarVisualStyle::pulse) {
        travel = interior.width + std::max(8.0, interior.width * animation_appearance.read().pulse_extent);
    } else if (visual_style.read() == ProgressBarVisualStyle::laser_etch) {
        travel = std::max(animation_appearance.read().laser_phase_pitch, 24.0);
    } else {
        // The other styles move only by their stripes (below), or by an
        // effect whose interval is clamped after.
        travel = 0.0;
    }
    const MotionPolicy policy = effective_motion_policy();
    // Reduced motion halves the travel (MotionPolicy::presentation_phase)
    // and scales the speed.
    const double presented_travel = policy.reduced ? travel * 0.5 : travel;
    const double period = std::chrono::duration<double>(animation_period.read()).count();
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    double device_speed = period > 0.0
        ? presented_travel * scale * policy.speed_scale() / period : 0.0;
    // The stripes travel their own pace: their speed in pitches a second,
    // on the motion clock.
    if (visual_style.read() == ProgressBarVisualStyle::marching_stripes ||
        overlay_style.read() == ProgressBarOverlayStyle::moving_stripes) {
        const FillStripes stripes = fill_stripes();
        const bool vertical = !horizontal;
        const Rect bounds = local_bounds();
        const Rect track_interior{2.0, 2.0, std::max(0.0, bounds.width - 4.0), std::max(0.0, bounds.height - 4.0)};
        const StripeSpan span = stripe_span(track_interior, vertical);
        const double slant = stripe_slant(stripes.pattern, span.thickness);
        const double pitch = stripe_pitch(stripes.pattern, span, slant);
        const double pitches = stripes.pattern.speed.value_or(default_stripe_speed);
        const double stripe_speed = pitch * pitches * scale * policy.speed_scale();
        device_speed = std::max(device_speed, stripe_speed);
    }
    const FrameInterval interval =
        policy.frame_interval(animation_interval_for_travel(device_speed));
    return interval;
}

AnimationTickResult ProgressBar::AnimationTick::operator()(FrameTime now) const {
    const std::shared_ptr<ProgressBar> live = bar.lock();
    const AnimationTickResult result = live
        ? (*live).on_animation_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult ProgressBar::on_animation_tick(FrameTime now) {
    const Rect before = animated_extent();
    advance_animation(now);
    const Rect after = animated_extent();
    const Rect damage = Rect::united(before, after);
    if (damage.empty()) return AnimationTickResult::unchanged;
    invalidate(damage);
    return AnimationTickResult::damaged;
}

Rect ProgressBar::animated_extent() const noexcept {
    const Rect fill = indicator_bounds();
    const Insets outsets = visual_outsets();
    if (outsets == Insets{}) return fill;
    // An effect drawing outside the bar (steam) may move anywhere in its
    // layer.
    const Rect bounds = local_bounds();
    const Rect layer{bounds.x - outsets.left, bounds.y - outsets.top,
                     bounds.width + outsets.left + outsets.right,
                     bounds.height + outsets.top + outsets.bottom};
    const Rect extent = Rect::united(fill, layer);
    return extent;
}

void ProgressBar::on_attached_to_window() {
    RangeControl::on_attached_to_window();
    if (window() != nullptr) {
        presentation_subscription_ = (*window()).presentation_changed().subscribe(
            *this,
            Delegate<const PresentationSettings&>::bind<
                ProgressBar, &ProgressBar::on_presentation_changed>(*this));
    }
    update_animation_registration();
}

void ProgressBar::on_presentation_changed(const PresentationSettings&) {
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::on_detached_from_window() noexcept {
    presentation_subscription_.disconnect();
    animation_timer_.stop();
    RangeControl::on_detached_from_window();
}

void ProgressBar::advance_animation(FrameTime now) {
    const MotionPolicy policy = effective_motion_policy();
    if (!animation_enabled.read() || !policy.active() ||
        !animated_style()) {
        return;
    }
    const double elapsed = std::max(
        0.0, std::chrono::duration<double>(now - last_animation_frame_).count());
    last_animation_frame_ = now;
    const double period =
        std::chrono::duration<double>(animation_period.read()).count();
    animation_phase_ = period <= 0.0
        ? 0.0
        : std::fmod(animation_phase_ +
                        elapsed * policy.speed_scale() / period,
                    1.0);
    if (animation_phase_ < 0.0) {
        animation_phase_ += 1.0;
    }
    static_cast<void>(effect_instance_.advance(now));
    static_cast<void>(overlay_instance_.advance(now));
}

Size ProgressBar::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred = orientation.read() == Orientation::horizontal
        ? Size{requested.width > 0.0 ? requested.width : 160.0,
               requested.height > 0.0 ? requested.height : 20.0}
        : Size{requested.width > 0.0 ? requested.width : 20.0,
               requested.height > 0.0 ? requested.height : 160.0};
    return {std::min(available.width, preferred.width),
            std::min(available.height, preferred.height)};
}

namespace {

constexpr std::array<PartField<ProgressBar::Parts>, 4> progress_bar_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &ProgressBar::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}, .states = {}},
     .member = &ProgressBar::Parts::track},
    {.declaration = {.name = "fill", .role = PartRole::fill, .composed_from = {}, .states = {}},
     .member = &ProgressBar::Parts::fill},
    {.declaration = {.name = "activity", .role = PartRole::activity, .composed_from = {}, .states = {}},
     .member = &ProgressBar::Parts::activity},
}};

constexpr std::array<StateField<ProgressBar::States>, 5> progress_bar_state_fields{{
    {.state = AnatomyState::normal, .member = &ProgressBar::States::normal},
    {.state = AnatomyState::indeterminate, .member = &ProgressBar::States::indeterminate},
    {.state = AnatomyState::enabled, .member = &ProgressBar::States::enabled},
    {.state = AnatomyState::disabled, .member = &ProgressBar::States::disabled},
    {.state = AnatomyState::reduced_motion, .member = &ProgressBar::States::reduced_motion},
}};

constexpr std::array<PartDeclaration, 4> progress_bar_parts =
    part_declarations(progress_bar_part_fields);
constexpr std::array<AnatomyState, 5> progress_bar_states =
    state_declarations(progress_bar_state_fields);

constexpr ControlAnatomy progress_bar_anatomy{
    .control_id = "progress_bar",
    .parts = progress_bar_parts,
    .states = progress_bar_states,
    .custom_states = {},
    .item_systems = {},
};

} // namespace

Rect ProgressBar::indicator_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect interior{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                        std::max(0.0, bounds.height - 4.0)};
    Rect fill = interior;
    if (visual_style.read() == ProgressBarVisualStyle::marquee) {
        const MotionPolicy policy = effective_motion_policy();
        const double presented_phase = policy.presentation_phase(animation_phase_);
        if (orientation.read() == Orientation::horizontal) {
            const double band = std::max(18.0, fill.width * 0.28);
            fill.x += (fill.width + band) * presented_phase - band;
            fill.width = band;
        } else {
            const double band = std::max(18.0, fill.height * 0.28);
            fill.y += (fill.height + band) * (1.0 - presented_phase) - band;
            fill.height = band;
        }
        fill = Rect::intersection(fill, interior);
        return fill;
    }
    const double ratio = normalized_value();
    if (orientation.read() == Orientation::horizontal) {
        fill.width *= ratio;
    } else {
        const double filled_height = fill.height * ratio;
        fill.y += fill.height - filled_height;
        fill.height = filled_height;
    }
    return fill;
}

ProgressBar::FillStripes ProgressBar::fill_stripes() const {
    FillStripes result{};
    if (attached_window() == nullptr) return result;
    AnatomyStateSet states = anatomy_states();
    states.insert(AnatomyState::selected);
    const ControlVisualContext context = part_visual_context(*this, states);
    const ResolvedControlRecipe resolved =
        effective_theme().resolve(*this, ControlVisualRole::progress, context, states, "fill");
    const ControlVisualRecipe& recipe = resolved.recipe();
    result.pattern = recipe.part_metrics.stripes;
    result.corner_radius = recipe.material.corner_radius;
    return result;
}

const ControlAnatomy& ProgressBar::anatomy() const noexcept {
    return progress_bar_anatomy;
}

void ProgressBar::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const Rect indicator = indicator_bounds();
    Parts record;
    record.region.bounds = bounds;
    record.region.present = true;
    record.track.bounds = bounds;
    record.track.present = true;
    // The fill exists for every determinate style even at zero value, so its
    // presence follows the style, not the value.
    if (visual_style.read() == ProgressBarVisualStyle::marquee) {
        record.activity.bounds = indicator;
        record.activity.present = true;
    } else {
        record.fill.bounds = indicator;
        record.fill.present = true;
    }
    write_parts(anatomy(), progress_bar_part_fields, record, geometry);
}

AnatomyStateSet ProgressBar::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (visual_style.read() == ProgressBarVisualStyle::marquee) {
        states.insert(AnatomyState::indeterminate);
    }
    const MotionPolicy policy = effective_motion_policy();
    if (policy.reduced) {
        states.insert(AnatomyState::reduced_motion);
    }
    return states;
}

ProgressBar::Parts ProgressBar::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), progress_bar_part_fields, published.parts(), record);
    return record;
}

ProgressBar::States ProgressBar::states() const {
    States record;
    read_states(progress_bar_state_fields, anatomy_states(), record);
    return record;
}

void ProgressBar::on_paint(Painter& painter, Rect) {
    // The bar is shown: a timer stopped while it was not starts again here,
    // its phase continuing from where it stopped.
    if (animation_timer_.started() && !animation_timer_.ticking()) {
        last_animation_frame_ = FrameClock::now();
    }
    animation_timer_.note_painted();
    // The track and the fill (one fill part per block in the blocks style)
    // are parts the theme's engine draws; the fill is the progress family
    // selected. The animated styles' effects are drawn over the fill.
    const Rect bounds = local_bounds();
    const AnatomyStateSet states = anatomy_states();
    PartPaint track;
    track.family = ControlVisualRole::progress;
    track.role = PartRole::track;
    track.bounds = bounds;
    track.states = states;
    track.key = PartKey{"track", {}, {}};
    paint_part(painter, *this, track);
    const ControlVisualRecipe& track_recipe = effective_theme().resolve(
        ControlVisualRole::progress, visual_context());
    // The track was painted as a part above; this is its shape, which an
    // effect's clip_part_shape follows.
    PartShape track_shape{};
    track_shape.corner_radius = track_recipe.material.corner_radius;
    if (track_recipe.material.corner_radii.has_value()) {
        const MaterialCornerRadii& radii = *track_recipe.material.corner_radii;
        track_shape.per_corner = true;
        track_shape.corners = PartCornerRadii{radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
    }
    const Rect interior{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                        std::max(0.0, bounds.height - 4.0)};
    const MotionPolicy policy = effective_motion_policy();
    const Rect fill = indicator_bounds();

    PartPaint fill_part;
    fill_part.family = ControlVisualRole::progress;
    fill_part.role = PartRole::fill;
    fill_part.states = states;
    fill_part.states.insert(AnatomyState::selected);
    // Every fill is the anatomy's `fill` part, so that a sheet's
    // `progress-bar::part(fill)` reaches it. Each block of the blocks style
    // is the same part, told apart by its position along the bar (the key's
    // item index), so that each keeps its own engine record and damage.
    if (visual_style.read() == ProgressBarVisualStyle::blocks) {
        constexpr double gap = 2.0;
        constexpr double block = 9.0;
        std::size_t block_index = 0U;
        if (orientation.read() == Orientation::horizontal) {
            for (double x = fill.x; x + block <= fill.x + fill.width; x += block + gap) {
                fill_part.bounds = {x, fill.y, block, fill.height};
                fill_part.key = PartKey{"fill", {}, block_index};
                paint_part(painter, *this, fill_part);
                block_index = block_index + 1U;
            }
        } else {
            for (double y = fill.y + fill.height - block; y >= fill.y; y -= block + gap) {
                fill_part.bounds = {fill.x, y, fill.width, block};
                fill_part.key = PartKey{"fill", {}, block_index};
                paint_part(painter, *this, fill_part);
                block_index = block_index + 1U;
            }
        }
    } else {
        fill_part.bounds = fill;
        fill_part.key = PartKey{"fill", {}, {}};
        paint_part(painter, *this, fill_part);
    }

    // The effects (design note section 8), drawn over the bar with its
    // whole bounds as their part; still while animation is off or motion
    // is reduced.
    sync_effects(FrameClock::now());
    if (effect_instance_.entry() == nullptr && overlay_instance_.entry() == nullptr) return;
    // A paint after the clock dropped the effects (the bar was hidden)
    // enlists them again.
    effect_paints_ = effect_paints_ + 1U;
    update_effect_ambient(true);
    const double speed = policy.speed_scale();
    const double period = std::chrono::duration<double>(animation_period.read()).count();
    EffectView<ProgressBar> view{};
    view.fraction = normalized_value();
    view.interior = interior;
    view.fill = fill;
    view.vertical = orientation.read() == Orientation::vertical;
    view.appearance = animation_appearance.read();
    if (themed_colours_) {
        // The theme's roles, at the appearance's own alphas.
        const Color primary = effect_colours_.primary;
        const Color secondary = effect_colours_.secondary;
        view.appearance.stripe_color = role_tinted(view.appearance.stripe_color, primary);
        view.appearance.laser_phase_color = role_tinted(view.appearance.laser_phase_color, primary);
        view.appearance.luminance_color = role_tinted(view.appearance.luminance_color, secondary);
        view.appearance.laser_edge_color = role_tinted(view.appearance.laser_edge_color, secondary);
        view.appearance.laser_spark_color = role_tinted(view.appearance.laser_spark_color, secondary);
    }
    view.stripe_width = stripe_width.read();
    view.period_seconds = speed > 0.0 ? period / speed : period;
    const FillStripes stripes = fill_stripes();
    view.stripes = stripe_view(stripes.pattern, stripes.corner_radius, interior, fill, view.vertical,
                               view.appearance.stripe_color, speed);
    const bool suppressed = window() != nullptr && (*window()).motion_clock().ambient_suppressed();
    const bool still = !animation_enabled.read() || policy.reduced || suppressed;
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    effect_instance_.paint(painter, bounds, scale, still, view, track_shape, this);
    overlay_instance_.paint(painter, bounds, scale, still, view, track_shape, this);
}

bool ProgressBar::hit_test_local(Point local_point) const {
    // Pointer input reaches the bar only while it grants it to an effect
    // that takes it; otherwise the pointer passes through, as before.
    const EffectEntry* const entry = effect_instance_.entry();
    if (effect_input.read() == EffectInputKind::none || entry == nullptr ||
        (*(*entry).declaration).input == EffectInputKind::none) {
        return false;
    }
    const bool inside = local_bounds().contains(local_point);
    return inside;
}

SemanticDescriptor ProgressBar::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::progress_bar;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    std::ostringstream value_text;
    value_text << value.read();
    descriptor.value = value_text.str();
    descriptor.numeric_value = value.read();
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    if (animation_enabled.read() && effective_motion_policy().active() &&
        animated_style()) {
        descriptor.states |= SemanticState::busy;
    }
    descriptor.exposed = true;
    return descriptor;
}

// property_codemod begin ProgressBar (tools/property_families/progress_bar.json; generated, do not edit)

namespace {

PropertyDescriptor progress_bar_visual_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "visual_style";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How the bar draws: blocks, continuous, marquee, pulse, marching stripes or laser etch.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"progress_bar_visual_style", "blocks", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = progress_bar_visual_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_bar_visual_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_bar_visual_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor progress_bar_effect_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "effect";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The registered effect program drawn over the bar; empty takes the visual style's own.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_bar_effect() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_bar_effect_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor progress_bar_effect_seed_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "effect_seed";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Appearance";
    descriptor.description = "The effect's seed; null derives it from the stable ID and the effect's name.";
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.nullable = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_bar_effect_seed() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_bar_effect_seed_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor progress_bar_effect_input_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "effect_input";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Behavior";
    descriptor.description = "The input the bar grants its effect: none, the pointer, or the pointer and keys.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"effect_input_kind", "none", 0}};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.enumeration = effect_input_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_bar_effect_input() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_bar_effect_input_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<ProgressBarVisualStyle>::type() {
    return PropertyType{BindingValueKind::enumeration, false, progress_bar_visual_style_enum()};
}

BindingValue PropertyValueTraits<ProgressBarVisualStyle>::to_binding(ProgressBarVisualStyle value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = progress_bar_visual_style_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ProgressBarVisualStyle PropertyValueTraits<ProgressBarVisualStyle>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ProgressBarVisualStyle>(choice.value);
}

PropertyType PropertyValueTraits<EffectInputKind>::type() {
    return PropertyType{BindingValueKind::enumeration, false, effect_input_enum()};
}

BindingValue PropertyValueTraits<EffectInputKind>::to_binding(EffectInputKind value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = effect_input_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

EffectInputKind PropertyValueTraits<EffectInputKind>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<EffectInputKind>(choice.value);
}

const std::array<PropertyEntry, ProgressBar::progress_bar_property_count>& ProgressBar::progress_bar_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"visual_style", &describe_progress_bar_visual_style,
         &read_cell_entry<ProgressBar, decltype(&ProgressBar::visual_style), &ProgressBar::visual_style>,
         &write_cell_entry<ProgressBar, decltype(&ProgressBar::visual_style), &ProgressBar::visual_style>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"effect", &describe_progress_bar_effect,
         &read_cell_entry<ProgressBar, decltype(&ProgressBar::effect), &ProgressBar::effect>,
         &write_cell_entry<ProgressBar, decltype(&ProgressBar::effect), &ProgressBar::effect>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"effect_seed", &describe_progress_bar_effect_seed,
         &read_member_entry<ProgressBar, decltype(&ProgressBar::effect_seed_property_value), &ProgressBar::effect_seed_property_value>,
         &write_member_entry<ProgressBar, decltype(&ProgressBar::write_effect_seed_property), &ProgressBar::write_effect_seed_property>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"effect_input", &describe_progress_bar_effect_input,
         &read_cell_entry<ProgressBar, decltype(&ProgressBar::effect_input), &ProgressBar::effect_input>,
         &write_cell_entry<ProgressBar, decltype(&ProgressBar::effect_input), &ProgressBar::effect_input>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ProgressBar's property table names a property twice");
    static const std::array<PropertyEntry, progress_bar_property_count> entries =
        join_property_tables(RangeControl::range_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ProgressBar::property_table() const noexcept {
    const std::array<PropertyEntry, progress_bar_property_count>& entries = progress_bar_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ProgressBar::prepare_write(const decltype(stripe_width)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "progress stripe width must be finite");
    if (incoming < 2.0 || incoming > 32.0) throw std::invalid_argument("progress stripe width must be between 2 and 32 pixels");
}

void ProgressBar::written(const decltype(stripe_width)&) {
    invalidate(Dirty::paint);
}

void ProgressBar::prepare_write(const decltype(animation_appearance)&, ProgressBarAnimationAppearance& incoming) {
    require_mutable();
    validate_animation_appearance(incoming);
}

void ProgressBar::written(const decltype(animation_appearance)&) {
    invalidate(Dirty::paint);
}

// property_codemod end ProgressBar

} // namespace gui_forms
