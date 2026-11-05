#include "gui_forms/controls/instruments/sweep_scope/sweep_scope.hpp"

#include "gui_forms/controls/instruments/sweep_scope/sweep_scope_effects.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The scope's layout, in logical units: the caption's band across the top,
// the plot below it, both inset from the scope's edges.
constexpr double scope_inset = 8.0;
constexpr double caption_top = 2.0;
constexpr double caption_height = 18.0;
constexpr double plot_top = caption_top + caption_height + 2.0;
constexpr double plot_bottom_inset = 8.0;
constexpr double graticule_thickness = 1.0;

// The sweep period's admitted range.
constexpr FrameInterval shortest_sweep_period{std::chrono::milliseconds(100)};
constexpr FrameInterval longest_sweep_period{std::chrono::seconds(60)};

// The accents the trace and the sweep take from the theme's ramp: teal
// rising, amber falling (the Gallery exhibit's cyan and amber).
constexpr AccentIndex rising_accent{5U};
constexpr AccentIndex falling_accent{0U};
// The ramp's tints are translucent washes over a material; the trace is
// drawn opaque and the sweep at a little over half.
constexpr std::uint8_t trace_alpha = 255U;
constexpr std::uint8_t sweep_alpha = 150U;

Color with_alpha(Color color, std::uint8_t alpha) noexcept {
    Color result = color;
    result.alpha = alpha;
    return result;
}

constexpr std::array<PartField<SweepScope::Parts>, 6> sweep_scope_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &SweepScope::Parts::region},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &SweepScope::Parts::caption},
    {.declaration = {.name = "plot", .role = PartRole::well, .composed_from = {}},
     .member = &SweepScope::Parts::plot},
    {.declaration = {.name = "graticule", .role = PartRole::divider, .composed_from = {}},
     .member = &SweepScope::Parts::graticule},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &SweepScope::Parts::indicator},
    {.declaration = {.name = "activity", .role = PartRole::activity, .composed_from = {}},
     .member = &SweepScope::Parts::activity},
}};

constexpr std::array<StateField<SweepScope::States>, 4> sweep_scope_state_fields{{
    {.state = AnatomyState::normal, .member = &SweepScope::States::normal},
    {.state = AnatomyState::enabled, .member = &SweepScope::States::enabled},
    {.state = AnatomyState::disabled, .member = &SweepScope::States::disabled},
    {.state = AnatomyState::reduced_motion, .member = &SweepScope::States::reduced_motion},
}};

constexpr std::array<PartDeclaration, 6> sweep_scope_parts = part_declarations(sweep_scope_part_fields);
constexpr std::array<AnatomyState, 4> sweep_scope_states = state_declarations(sweep_scope_state_fields);

constexpr ControlAnatomy sweep_scope_anatomy{
    .control_id = "sweep_scope",
    .parts = sweep_scope_parts,
    .states = sweep_scope_states,
    .custom_states = {},
};

// The registered scope_sweep effect.
const EffectEntry* scope_sweep_entry() noexcept {
    const EffectEntry* const entry = built_in_effect_registry().find(ScopeSweep::declaration.name);
    return entry;
}

} // namespace

SweepScope::SweepScope(StableId stable_id)
    : RangeControl(std::move(stable_id)) {
    set_focusable(false);

    PropertyDescriptor caption;
    caption.name = "caption";
    caption.kind = BindingValueKind::text;
    caption.category = "Appearance";
    caption.description = "The words drawn before the level's percentage.";
    caption.default_value = BindingValue{std::string{"LEVEL"}};
    caption.invalidation_effects = Dirty::paint | Dirty::semantics;
    caption.bindable = false;
    define_bindable_property({
        std::move(caption),
        detail::BoundMemberFunction<BindingValue (SweepScope::*)() const>(*this,
                                                                          &SweepScope::caption_property_value),
        detail::BoundMemberFunction<void (SweepScope::*)(const BindingValue&)>(
            *this, &SweepScope::set_caption_property_value),
        {}, {}, {}, {}});

    PropertyDescriptor period;
    period.name = "sweep_period";
    period.kind = BindingValueKind::number;
    period.category = "Behavior";
    period.description = "Seconds the sweep takes to cross the trace once, from 0.1 to 60.";
    period.default_value = BindingValue{1.234568};
    period.invalidation_effects = Dirty::paint;
    period.bindable = false;
    define_bindable_property({
        std::move(period),
        detail::BoundMemberFunction<BindingValue (SweepScope::*)() const>(
            *this, &SweepScope::sweep_period_property_value),
        detail::BoundMemberFunction<void (SweepScope::*)(const BindingValue&)>(
            *this, &SweepScope::set_sweep_period_property_value),
        {}, {}, {}, {}});
}

// -- properties ----------------------------------------------------------------

void SweepScope::set_caption(std::string caption) {
    require_mutable();
    if (caption == caption_) return;
    caption_ = std::move(caption);
    invalidate(Dirty::paint | Dirty::semantics);
}

void SweepScope::set_sweep_period(FrameInterval period) {
    require_mutable();
    if (period < shortest_sweep_period || period > longest_sweep_period) {
        throw std::out_of_range("SweepScope sweep period must be from 0.1 to 60 seconds");
    }
    if (period == sweep_period_) return;
    sweep_period_ = period;
    invalidate(plot_bounds());
}

BindingValue SweepScope::caption_property_value() const {
    return BindingValue{caption_};
}

void SweepScope::set_caption_property_value(const BindingValue& value) {
    set_caption(std::get<std::string>(value));
}

BindingValue SweepScope::sweep_period_property_value() const {
    const double seconds = std::chrono::duration<double>(sweep_period_).count();
    return BindingValue{seconds};
}

void SweepScope::set_sweep_period_property_value(const BindingValue& value) {
    const double seconds = std::get<double>(value);
    if (!std::isfinite(seconds)) {
        throw std::out_of_range("SweepScope sweep period must be from 0.1 to 60 seconds");
    }
    const FrameInterval period = std::chrono::round<FrameInterval>(std::chrono::duration<double>(seconds));
    set_sweep_period(period);
}

// -- geometry ------------------------------------------------------------------

Size SweepScope::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred{requested.width > 0.0 ? requested.width : 220.0,
                         requested.height > 0.0 ? requested.height : 120.0};
    return {std::min(available.width, preferred.width), std::min(available.height, preferred.height)};
}

Rect SweepScope::plot_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect plot{scope_inset, plot_top, std::max(0.0, bounds.width - scope_inset * 2.0),
                    std::max(0.0, bounds.height - plot_top - plot_bottom_inset)};
    return plot;
}

double SweepScope::graticule_y(int line) const noexcept {
    const Rect plot = plot_bounds();
    const double y = plot.y + plot.height * static_cast<double>(line) / static_cast<double>(graticule_lines + 1);
    return y;
}

std::string SweepScope::caption_text() const {
    char percent[32]{};
    std::snprintf(percent, sizeof(percent), " %.0f%%", normalized_value() * 100.0);
    std::string text = caption_;
    text += percent;
    return text;
}

// -- anatomy -------------------------------------------------------------------

const ControlAnatomy& SweepScope::anatomy() const noexcept {
    return sweep_scope_anatomy;
}

void SweepScope::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const Rect plot = plot_bounds();
    Parts record;
    record.region.bounds = bounds;
    record.region.present = true;
    record.caption.bounds = Rect{scope_inset, caption_top, plot.width, caption_height};
    record.caption.present = true;
    record.plot.bounds = plot;
    record.plot.present = true;
    // The graticule's extent: its first line to its last.
    const double first = graticule_y(1);
    const double last = graticule_y(graticule_lines);
    record.graticule.bounds = Rect{plot.x, first, plot.width, last - first + graticule_thickness};
    record.graticule.present = true;
    // The trace and the sweep are the effect's, drawn anywhere in the plot.
    record.indicator.bounds = plot;
    record.indicator.present = true;
    record.activity.bounds = plot;
    record.activity.present = true;
    write_parts(anatomy(), sweep_scope_part_fields, record, geometry);
}

AnatomyStateSet SweepScope::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (window() != nullptr && (*window()).presentation_settings().reduced_motion) {
        states.insert(AnatomyState::reduced_motion);
    }
    return states;
}

SweepScope::Parts SweepScope::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), sweep_scope_part_fields, published.parts(), record);
    return record;
}

SweepScope::States SweepScope::states() const {
    States record;
    read_states(sweep_scope_state_fields, anatomy_states(), record);
    return record;
}

// -- motion --------------------------------------------------------------------

bool SweepScope::sweep_moves() const noexcept {
    if (window() == nullptr || effect_instance_.entry() == nullptr) return false;
    const PresentationSettings& settings = (*window()).presentation_settings();
    const bool moves = !settings.reduced_motion && settings.ambient_motion;
    return moves;
}

void SweepScope::update_sweep_ambient(bool painting) {
    if (window() == nullptr) return;
    MotionClock& clock = (*window()).motion_clock();
    if (!sweep_moves()) {
        clock.withdraw_control_ambient(*this);
        return;
    }
    if (!painting && !clock.control_ambient(*this)) return;
    const double rate = (*(*effect_instance_.entry()).declaration).frames_per_second;
    const FrameInterval interval =
        std::chrono::ceil<FrameInterval>(std::chrono::duration<double>(1.0 / rate));
    const bool started = clock.enlist_control_ambient(*this, interval);
    if (!started) return;
    // Motion starts or resumes: the sweep's time restarts, with no catch-up
    // for the pause.
    effect_instance_.rebase(clock.now());
    effect_paints_seen_ = ~std::uint64_t{0};
}

bool SweepScope::on_ambient_frame(FrameTime now) {
    if (effect_paints_ == effect_paints_seen_) return false;
    effect_paints_seen_ = effect_paints_;
    static_cast<void>(effect_instance_.advance(now));
    invalidate(plot_bounds());
    return true;
}

void SweepScope::on_attached_to_window() {
    RangeControl::on_attached_to_window();
    if (window() != nullptr) {
        presentation_subscription_ = (*window()).presentation_changed().subscribe(
            *this,
            Delegate<const PresentationSettings&>::bind<SweepScope, &SweepScope::on_presentation_changed>(*this));
    }
    update_sweep_ambient(false);
}

void SweepScope::on_presentation_changed(const PresentationSettings&) {
    update_sweep_ambient(false);
    invalidate(Dirty::paint | Dirty::semantics);
}

void SweepScope::on_detached_from_window() noexcept {
    presentation_subscription_.disconnect();
    if (window() != nullptr) (*window()).motion_clock().withdraw_control_ambient(*this);
    RangeControl::on_detached_from_window();
}

// -- paint ---------------------------------------------------------------------

void SweepScope::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const Rect plot = plot_bounds();
    const AnatomyStateSet states = anatomy_states();
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = bounds;
    paint_part(painter, *this, part);
    part.family = ControlVisualRole::editor;
    part.role = PartRole::well;
    part.bounds = plot;
    paint_part(painter, *this, part);
    part.role = PartRole::divider;
    for (int line = 1; line <= graticule_lines; ++line) {
        part.bounds = Rect{plot.x, graticule_y(line), plot.width, graticule_thickness};
        paint_part(painter, *this, part);
    }
    const std::string caption = caption_text();
    part.family = ControlVisualRole::panel;
    part.role = PartRole::caption;
    part.bounds = Rect{scope_inset, caption_top, plot.width, caption_height};
    part.text = caption;
    part.font = effective_font(FontSpec{FontRole::monospace, 12.0, 600, false});
    paint_part(painter, *this, part);

    // The trace and the sweep (effect programs, design note section 8),
    // drawn in the plot; still while motion is reduced or off.
    const EffectEntry* const entry = scope_sweep_entry();
    if (entry == nullptr) return;
    if (effect_instance_.entry() != entry) {
        effect_instance_.attach(*entry, effect_default_seed(stable_id().value(), ScopeSweep::declaration.name),
                                FrameClock::now());
    }
    // A paint after the clock dropped the sweep (the scope was hidden)
    // enlists it again.
    effect_paints_ = effect_paints_ + 1U;
    update_sweep_ambient(true);
    const Theme& theme = effective_theme();
    EffectView<SweepScope> view{};
    view.fraction = normalized_value();
    view.plot = Size{plot.width, plot.height};
    view.rising = with_alpha(theme.accent(rising_accent), trace_alpha);
    view.falling = with_alpha(theme.accent(falling_accent), trace_alpha);
    view.sweep = with_alpha(theme.accent(rising_accent), sweep_alpha);
    view.period_seconds = std::chrono::duration<double>(sweep_period_).count();
    const bool suppressed = window() != nullptr && (*window()).motion_clock().ambient_suppressed();
    const bool still = !sweep_moves() || suppressed;
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    effect_instance_.paint(painter, plot, scale, still, view);
}

// -- semantics and input -------------------------------------------------------

bool SweepScope::hit_test_local(Point) const {
    // The scope is read-only: the pointer passes through it.
    return false;
}

SemanticDescriptor SweepScope::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::progress_bar;
    descriptor.name = accessible_name.type().empty() ? caption_ : accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.value = caption_text();
    descriptor.numeric_value = value();
    descriptor.minimum_value = minimum();
    descriptor.maximum_value = maximum();
    descriptor.exposed = true;
    return descriptor;
}

} // namespace gui_forms
