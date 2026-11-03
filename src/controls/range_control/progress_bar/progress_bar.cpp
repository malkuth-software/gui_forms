#include "gui_forms/controls/range_control/progress_bar/progress_bar.hpp"

#include "../range_control_rendering.hpp"
#include "gui_forms/controls/range_control/progress_bar/progress_bar_effects.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

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
    set_focusable(false);

    // The effect properties (effect programs, design note section 7).
    PropertyDescriptor effect;
    effect.name = "effect";
    effect.kind = BindingValueKind::text;
    effect.category = "Appearance";
    effect.description = "The registered effect program drawn over the bar; empty takes the visual style's own.";
    effect.default_value = BindingValue{std::string{}};
    effect.invalidation_effects = Dirty::paint | Dirty::semantics;
    effect.bindable = false;
    define_bindable_property({
        std::move(effect),
        detail::BoundMemberFunction<BindingValue (ProgressBar::*)() const>(*this,
                                                                           &ProgressBar::effect_property_value),
        detail::BoundMemberFunction<void (ProgressBar::*)(const BindingValue&)>(
            *this, &ProgressBar::set_effect_property_value),
        {}, {}, {}, {}});

    PropertyDescriptor effect_seed;
    effect_seed.name = "effect_seed";
    effect_seed.kind = BindingValueKind::unsigned_integer;
    effect_seed.category = "Appearance";
    effect_seed.description = "The effect's seed; null derives it from the stable ID and the effect's name.";
    effect_seed.invalidation_effects = Dirty::paint;
    effect_seed.bindable = false;
    effect_seed.nullable = true;
    define_bindable_property({
        std::move(effect_seed),
        detail::BoundMemberFunction<BindingValue (ProgressBar::*)() const>(
            *this, &ProgressBar::effect_seed_property_value),
        detail::BoundMemberFunction<void (ProgressBar::*)(const BindingValue&)>(
            *this, &ProgressBar::set_effect_seed_property_value),
        {}, {}, {}, {}});

    PropertyDescriptor effect_input;
    effect_input.name = "effect_input";
    effect_input.kind = BindingValueKind::enumeration;
    effect_input.category = "Behavior";
    effect_input.description = "The input the bar grants its effect: none, the pointer, or the pointer and keys.";
    effect_input.default_value = BindingValue{PropertyEnumValue{"effect_input_kind", "none", 0}};
    effect_input.invalidation_effects = Dirty::paint;
    effect_input.bindable = false;
    effect_input.enumeration = effect_input_enum();
    define_bindable_property({
        std::move(effect_input),
        detail::EnumerationPropertyGetter<ProgressBar, EffectInputKind>(*this, &ProgressBar::effect_input,
                                                                         effect_input_enum()),
        detail::EnumerationPropertySetter<ProgressBar, EffectInputKind>(*this, &ProgressBar::set_effect_input),
        {}, {}, {}});
}

BindingValue ProgressBar::effect_property_value() const {
    return BindingValue{effect_};
}

void ProgressBar::set_effect_property_value(const BindingValue& value) {
    set_effect(std::get<std::string>(value));
}

BindingValue ProgressBar::effect_seed_property_value() const {
    if (!effect_seed_) return BindingValue{};
    return BindingValue{*effect_seed_};
}

void ProgressBar::set_effect_seed_property_value(const BindingValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        set_effect_seed(std::nullopt);
        return;
    }
    set_effect_seed(std::get<std::uint64_t>(value));
}

void ProgressBar::set_effect_input(EffectInputKind input) {
    require_mutable();
    if (input == effect_input_) return;
    effect_input_ = input;
    invalidate(Dirty::paint);
}

void ProgressBar::on_pointer(PointerEvent& event) {
    if (effect_input_ == EffectInputKind::none || effect_instance_.entry() == nullptr) return;
    EffectInputEvent input{};
    if (event.action == PointerAction::move) {
        input.kind = EffectInputEventKind::pointer_moved;
    } else if (event.action == PointerAction::down) {
        input.kind = EffectInputEventKind::pointer_pressed;
    } else if (event.action == PointerAction::up) {
        input.kind = EffectInputEventKind::pointer_released;
    } else {
        return;
    }
    // The part is the bar's bounds: part-local is the bar's local space.
    const Rect absolute = absolute_bounds();
    input.position = Point{event.position.x - absolute.x, event.position.y - absolute.y};
    if (effect_instance_.queue_input(input, effect_input_)) invalidate(animated_extent());
}

void ProgressBar::set_visual_style(ProgressBarVisualStyle style_value) {
    require_mutable();
    if (visual_style_ == style_value) {
        return;
    }
    visual_style_ = style_value;
    animation_phase_ = 0.0;
    sync_effects(FrameClock::now());
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::set_overlay_style(ProgressBarOverlayStyle style_value) {
    require_mutable();
    if (overlay_style_ == style_value) return;
    overlay_style_ = style_value;
    animation_phase_ = 0.0;
    sync_effects(FrameClock::now());
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::set_stripe_width(double width) {
    require_mutable();
    require_finite(width, "progress stripe width must be finite");
    if (width < 2.0 || width > 32.0) {
        throw std::invalid_argument(
            "progress stripe width must be between 2 and 32 pixels");
    }
    if (stripe_width_ == width) return;
    stripe_width_ = width;
    invalidate(Dirty::paint);
}

void ProgressBar::set_animation_appearance(
    ProgressBarAnimationAppearance appearance) {
    require_mutable();
    require_finite(appearance.pulse_extent,
                   "progress pulse extent must be finite");
    require_finite(appearance.laser_edge_extent,
                   "progress laser edge extent must be finite");
    require_finite(appearance.laser_phase_pitch,
                   "progress laser phase pitch must be finite");
    if (appearance.pulse_extent < 0.1 || appearance.pulse_extent > 0.8) {
        throw std::invalid_argument(
            "progress pulse extent must be between 0.1 and 0.8");
    }
    if (appearance.laser_edge_extent < 2.0 ||
        appearance.laser_edge_extent > 48.0) {
        throw std::invalid_argument(
            "progress laser edge extent must be between 2 and 48 pixels");
    }
    if (appearance.laser_phase_pitch < 3.0 ||
        appearance.laser_phase_pitch > 64.0) {
        throw std::invalid_argument(
            "progress laser phase pitch must be between 3 and 64 pixels");
    }
    if (animation_appearance_ == appearance) return;
    animation_appearance_ = appearance;
    invalidate(Dirty::paint);
}

void ProgressBar::set_animation_enabled(bool enabled_value) {
    require_mutable();
    if (animation_enabled_ == enabled_value) {
        return;
    }
    animation_enabled_ = enabled_value;
    if (!animation_enabled_) {
        animation_phase_ = 0.0;
    }
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::set_animation_paused(bool paused) {
    MotionPolicy policy = motion_policy_;
    policy.paused = paused;
    set_motion_policy(policy);
}

void ProgressBar::set_reduced_motion(bool reduced) {
    MotionPolicy policy = motion_policy_;
    policy.reduced = reduced;
    set_motion_policy(policy);
}

void ProgressBar::set_motion_policy(bool paused, bool reduced) {
    MotionPolicy policy = motion_policy_;
    policy.paused = paused;
    policy.reduced = reduced;
    set_motion_policy(policy);
}

void ProgressBar::set_motion_policy(MotionPolicy policy) {
    require_mutable();
    if (motion_policy_ == policy) return;
    motion_policy_ = policy;
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

MotionPolicy ProgressBar::effective_motion_policy() const noexcept {
    MotionPolicy policy = motion_policy_;
    if (window() != nullptr &&
        (*window()).presentation_settings().reduced_motion) {
        policy.reduced = true;
    }
    return policy;
}

void ProgressBar::set_animation_period(FrameInterval period) {
    require_mutable();
    if (period < std::chrono::milliseconds(100)) {
        throw std::invalid_argument(
            "progress animation period must be at least 100 milliseconds");
    }
    if (animation_period_ == period) {
        return;
    }
    animation_period_ = period;
    last_animation_frame_ = FrameClock::now();
    invalidate(Dirty::paint | Dirty::semantics);
}

bool ProgressBar::animated_style() const noexcept {
    return visual_style_ == ProgressBarVisualStyle::marquee ||
           effect_instance_.entry() != nullptr ||
           overlay_instance_.entry() != nullptr;
}

std::string_view ProgressBar::resolved_effect() const noexcept {
    if (!effect_.empty()) return effect_;
    if (visual_style_ == ProgressBarVisualStyle::marching_stripes) {
        return ProgressStripes::declaration.name;
    }
    if (visual_style_ == ProgressBarVisualStyle::pulse) {
        return ProgressPulse::declaration.name;
    }
    if (visual_style_ == ProgressBarVisualStyle::laser_etch) {
        return ProgressLaser::declaration.name;
    }
    return {};
}

namespace {

// The registered progress-bar effect named `name`, or null.
const EffectEntry* progress_bar_effect(std::string_view name) noexcept {
    if (name.empty()) return nullptr;
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
    const EffectEntry* const effect = progress_bar_effect(resolved_effect());
    const bool overlay = overlay_style_ == ProgressBarOverlayStyle::moving_stripes &&
                         visual_style_ != ProgressBarVisualStyle::marching_stripes;
    const EffectEntry* const stripes =
        overlay ? progress_bar_effect(ProgressStripes::declaration.name) : nullptr;
    const std::string_view id = stable_id().value();
    const std::uint64_t effect_seed = effect != nullptr
        ? effect_seed_.value_or(effect_default_seed(id, (*(*effect).declaration).name))
        : 0U;
    const std::uint64_t stripes_seed =
        effect_seed_.value_or(effect_default_seed(id, ProgressStripes::declaration.name));
    sync_instance(effect_instance_, effect, effect_seed, now);
    sync_instance(overlay_instance_, stripes, stripes_seed, now);
}

void ProgressBar::set_effect(std::string name) {
    require_mutable();
    if (name == effect_) return;
    effect_ = std::move(name);
    effect_instance_.detach();
    sync_effects(FrameClock::now());
    update_animation_registration();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressBar::set_effect_seed(std::optional<std::uint64_t> seed) {
    require_mutable();
    if (seed == effect_seed_) return;
    effect_seed_ = seed;
    // A new seed is a new arrangement: the effects start again.
    effect_instance_.detach();
    overlay_instance_.detach();
    sync_effects(FrameClock::now());
    invalidate(Dirty::paint);
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
    return attached && animation_enabled_ && policy.active() && !policy.reduced;
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
    if (window() == nullptr || !animation_enabled_ || !policy.active() ||
        visual_style_ != ProgressBarVisualStyle::marquee) {
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
    const bool horizontal = orientation() == Orientation::horizontal;
    const double track = horizontal ? local_bounds().width : local_bounds().height;
    double travel = 0.0;
    if (visual_style_ == ProgressBarVisualStyle::marquee) {
        travel = track + std::max(18.0, track * 0.28);
    } else if (visual_style_ == ProgressBarVisualStyle::pulse) {
        travel = interior.width + std::max(8.0, interior.width * animation_appearance_.pulse_extent);
    } else if (visual_style_ == ProgressBarVisualStyle::laser_etch) {
        travel = std::max(animation_appearance_.laser_phase_pitch, 24.0);
    } else {
        // The other styles move only by their stripes (below), or by an
        // effect whose interval is clamped after.
        travel = 0.0;
    }
    if (visual_style_ == ProgressBarVisualStyle::marching_stripes ||
        overlay_style_ == ProgressBarOverlayStyle::moving_stripes) {
        travel = std::max(travel, stripe_width_ * 2.0);
    }
    const MotionPolicy policy = effective_motion_policy();
    // Reduced motion halves the travel (MotionPolicy::presentation_phase)
    // and scales the speed.
    const double presented_travel = policy.reduced ? travel * 0.5 : travel;
    const double period = std::chrono::duration<double>(animation_period_).count();
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    const double device_speed = period > 0.0
        ? presented_travel * scale * policy.speed_scale() / period : 0.0;
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
    if (!animation_enabled_ || !policy.active() ||
        !animated_style()) {
        return;
    }
    const double elapsed = std::max(
        0.0, std::chrono::duration<double>(now - last_animation_frame_).count());
    last_animation_frame_ = now;
    const double period =
        std::chrono::duration<double>(animation_period_).count();
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
    const Size preferred = orientation() == Orientation::horizontal
        ? Size{requested.width > 0.0 ? requested.width : 160.0,
               requested.height > 0.0 ? requested.height : 20.0}
        : Size{requested.width > 0.0 ? requested.width : 20.0,
               requested.height > 0.0 ? requested.height : 160.0};
    return {std::min(available.width, preferred.width),
            std::min(available.height, preferred.height)};
}

namespace {

constexpr std::array<PartField<ProgressBar::Parts>, 4> progress_bar_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ProgressBar::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &ProgressBar::Parts::track},
    {.declaration = {.name = "fill", .role = PartRole::fill, .composed_from = {}},
     .member = &ProgressBar::Parts::fill},
    {.declaration = {.name = "activity", .role = PartRole::activity, .composed_from = {}},
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
};

} // namespace

Rect ProgressBar::indicator_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect interior{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                        std::max(0.0, bounds.height - 4.0)};
    Rect fill = interior;
    if (visual_style_ == ProgressBarVisualStyle::marquee) {
        const MotionPolicy policy = effective_motion_policy();
        const double presented_phase = policy.presentation_phase(animation_phase_);
        if (orientation() == Orientation::horizontal) {
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
    if (orientation() == Orientation::horizontal) {
        fill.width *= ratio;
    } else {
        const double filled_height = fill.height * ratio;
        fill.y += fill.height - filled_height;
        fill.height = filled_height;
    }
    return fill;
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
    if (visual_style_ == ProgressBarVisualStyle::marquee) {
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
    if (visual_style_ == ProgressBarVisualStyle::marquee) {
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
    const Rect bounds = local_bounds();
    const ControlVisualRecipe& track_recipe = effective_theme().resolve(
        ControlVisualRole::progress, visual_context());
    paint_surface_material(painter, bounds, track_recipe.material);
    const Rect interior{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                        std::max(0.0, bounds.height - 4.0)};
    const MotionPolicy policy = effective_motion_policy();
    const Rect fill = indicator_bounds();

    const ControlVisualRecipe& fill_recipe = effective_theme().resolve(
        ControlVisualRole::progress,
        visual_context(false, false, true, false));
    if (visual_style_ == ProgressBarVisualStyle::blocks) {
        constexpr double gap = 2.0;
        constexpr double block = 9.0;
        if (orientation() == Orientation::horizontal) {
            for (double x = fill.x; x + block <= fill.x + fill.width; x += block + gap) {
                paint_surface_material(
                    painter, {x, fill.y, block, fill.height},
                    fill_recipe.material);
            }
        } else {
            for (double y = fill.y + fill.height - block; y >= fill.y; y -= block + gap) {
                paint_surface_material(
                    painter, {fill.x, y, fill.width, block},
                    fill_recipe.material);
            }
        }
    } else {
        paint_surface_material(painter, fill, fill_recipe.material);
    }
    const bool own_effect = visual_style_ == ProgressBarVisualStyle::pulse ||
                            visual_style_ == ProgressBarVisualStyle::marching_stripes ||
                            visual_style_ == ProgressBarVisualStyle::laser_etch;
    if (!own_effect && fill.height > 3.0 && fill.width > 0.0) {
        painter.fill_rect({fill.x, fill.y, fill.width, 3.0},
                          fill_recipe.muted_text);
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
    const double period = std::chrono::duration<double>(animation_period_).count();
    EffectView<ProgressBar> view{};
    view.fraction = normalized_value();
    view.interior = interior;
    view.fill = fill;
    view.vertical = orientation() == Orientation::vertical;
    view.appearance = animation_appearance_;
    view.stripe_width = stripe_width_;
    view.period_seconds = speed > 0.0 ? period / speed : period;
    const bool suppressed = window() != nullptr && (*window()).motion_clock().ambient_suppressed();
    const bool still = !animation_enabled_ || policy.reduced || suppressed;
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    effect_instance_.paint(painter, bounds, scale, still, view);
    overlay_instance_.paint(painter, bounds, scale, still, view);
}

bool ProgressBar::hit_test_local(Point local_point) const {
    // Pointer input reaches the bar only while it grants it to an effect
    // that takes it; otherwise the pointer passes through, as before.
    const EffectEntry* const entry = effect_instance_.entry();
    if (effect_input_ == EffectInputKind::none || entry == nullptr ||
        (*(*entry).declaration).input == EffectInputKind::none) {
        return false;
    }
    const bool inside = local_bounds().contains(local_point);
    return inside;
}

SemanticDescriptor ProgressBar::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::progress_bar;
    descriptor.name = accessible_name();
    descriptor.description = accessible_description();
    std::ostringstream value_text;
    value_text << value();
    descriptor.value = value_text.str();
    descriptor.numeric_value = value();
    descriptor.minimum_value = minimum();
    descriptor.maximum_value = maximum();
    if (animation_enabled_ && effective_motion_policy().active() &&
        animated_style()) {
        descriptor.states |= SemanticState::busy;
    }
    descriptor.exposed = true;
    return descriptor;
}

} // namespace gui_forms
