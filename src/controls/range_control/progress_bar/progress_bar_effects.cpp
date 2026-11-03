// The progress bar's effect programs. Every function here obeys the effect
// rules (gui_forms/engine/effect/effect.hpp): bounded loops only, no
// recursion, allocation, globals, clocks or host maths; drawing through the
// canvas alone. The stripes, pulse and laser reproduce the legacy painting
// of range_control_rendering.cpp, with their phase as state.

#include "gui_forms/controls/range_control/progress_bar/progress_bar_effects.hpp"

// Built without the gui_forms plugin's effect pass (GUI_FORMS_EFFECT_VERIFY
// OFF, or a toolchain that cannot load the plugin), CMake defines this, and
// the effects registered here run labelled unverified.
#if defined(GUI_FORMS_EFFECTS_UNVERIFIED)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma message("effects progress_stripes, progress_pulse, progress_laser, progress_woven_stripes and progress_bubbles are unverified: their bounds are not checked")
#else
#warning "effects progress_stripes, progress_pulse, progress_laser, progress_woven_stripes and progress_bubbles are unverified: their bounds are not checked"
#endif
#endif

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gui_forms {
namespace {

constexpr double tau = 6.28318530717958647692;
constexpr double tick_seconds = 1.0 / static_cast<double>(effect_ticks_per_second);

// The phase after `ticks` ticks of a cycle `period` seconds long.
double advanced_phase(double phase, std::uint32_t ticks, double period) noexcept {
    const double cycles = period > 0.0 ? static_cast<double>(ticks) * tick_seconds / period : 0.0;
    const double advanced = effect_fraction(phase + cycles);
    return advanced;
}

Color with_alpha(Color color, std::uint32_t alpha) noexcept {
    const Color result = Color::rgba(color.red, color.green, color.blue, static_cast<std::uint8_t>(alpha));
    return result;
}

} // namespace

void ProgressStripes::run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    if (!frame.still) phase_ = advanced_phase(phase_, frame.ticks, parent.period_seconds);
    const Rect fill = parent.fill;
    if (fill.empty() || !(parent.stripe_width > 0.0)) return;
    const double phase = frame.still ? 0.0 : phase_;
    const double pitch = parent.stripe_width * 2.0;
    const double travel = phase * pitch;
    const double begin = fill.x - fill.height - pitch + travel;
    const double end = fill.x + fill.width + fill.height + pitch;
    const std::size_t count = static_cast<std::size_t>((end - begin) / pitch) + 1U;
    canvas.save();
    canvas.clip_rect(fill);
    for (const std::size_t index : bounded_range<maximum_stripes>(count)) {
        const double x = begin + static_cast<double>(index) * pitch;
        canvas.stroke_line({x, fill.y + fill.height}, {x + fill.height, fill.y}, parent.appearance.stripe_color,
                           parent.stripe_width);
    }
    canvas.restore();
}

void ProgressPulse::run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    if (!frame.still) phase_ = advanced_phase(phase_, frame.ticks, parent.period_seconds);
    const Rect fill = parent.fill;
    if (fill.empty()) return;
    const double phase = frame.still ? parent.still_phase : phase_;
    const double extent = fill.width * parent.appearance.pulse_extent;
    const double band = extent > 8.0 ? extent : 8.0;
    const Rect pulse{fill.x - band + (fill.width + band) * phase, fill.y, band, fill.height};
    const Color color = parent.appearance.luminance_color;
    const Color third = with_alpha(color, color.alpha / 3U);
    EffectGradient gradient{};
    gradient.stops[0] = GradientStop{0.0, with_alpha(color, 0U)};
    gradient.stops[1] = GradientStop{0.22, third};
    gradient.stops[2] = GradientStop{0.5, color};
    gradient.stops[3] = GradientStop{0.78, third};
    gradient.stops[4] = GradientStop{1.0, with_alpha(color, 0U)};
    gradient.count = 5U;
    canvas.save();
    canvas.clip_rect(fill);
    canvas.fill_linear_gradient(pulse, {pulse.x, pulse.y}, {pulse.x + pulse.width, pulse.y}, gradient, false);
    canvas.restore();
}

void ProgressLaser::run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    if (!frame.still) phase_ = advanced_phase(phase_, frame.ticks, parent.period_seconds);
    const Rect fill = parent.fill;
    if (fill.empty()) return;
    const ProgressBarAnimationAppearance& appearance = parent.appearance;
    const bool reduced = frame.still;
    const double phase = frame.still ? parent.still_phase : phase_;

    // Bands moving across the fill.
    const double pitch = appearance.laser_phase_pitch;
    const double offset = phase * pitch;
    const Color phase_color = appearance.laser_phase_color;
    EffectGradient bands{};
    bands.stops[0] = GradientStop{0.0, with_alpha(phase_color, 0U)};
    bands.stops[1] = GradientStop{0.24, phase_color};
    bands.stops[2] = GradientStop{0.5, Color::rgba(255, 255, 255, static_cast<std::uint8_t>(phase_color.alpha / 2U))};
    bands.stops[3] = GradientStop{0.76, phase_color};
    bands.stops[4] = GradientStop{1.0, with_alpha(phase_color, 0U)};
    bands.count = 5U;
    canvas.save();
    canvas.clip_rect(fill);
    canvas.fill_linear_gradient(fill, {fill.x, fill.y - offset}, {fill.x, fill.y - offset + pitch}, bands, true);
    canvas.restore();

    // The glowing edge behind the front.
    const double length = parent.vertical ? fill.height : fill.width;
    const double edge_extent = appearance.laser_edge_extent < length ? appearance.laser_edge_extent : length;
    if (!(edge_extent > 0.0)) return;
    const Color edge = appearance.laser_edge_color;
    const Color spark = appearance.laser_spark_color;
    EffectGradient edge_stops{};
    edge_stops.stops[0] = GradientStop{0.0, with_alpha(edge, 0U)};
    edge_stops.stops[1] = GradientStop{0.58, with_alpha(edge, edge.alpha / 2U)};
    edge_stops.stops[2] = GradientStop{0.86, edge};
    edge_stops.stops[3] = GradientStop{1.0, spark};
    edge_stops.count = 4U;
    if (!parent.vertical) {
        const Rect edge_rect{fill.x + fill.width - edge_extent, fill.y, edge_extent, fill.height};
        canvas.fill_linear_gradient(edge_rect, {edge_rect.x, edge_rect.y},
                                    {edge_rect.x + edge_rect.width, edge_rect.y}, edge_stops, false);
    } else {
        const Rect edge_rect{fill.x, fill.y, fill.width, edge_extent};
        canvas.fill_linear_gradient(edge_rect, {edge_rect.x, edge_rect.y + edge_rect.height},
                                    {edge_rect.x, edge_rect.y}, edge_stops, false);
    }

    // The front: a compact white-hot burn, a soft corona and a bright core,
    // kept inside the track.
    canvas.save();
    canvas.clip_rect(parent.interior);
    EffectGradient corona{};
    corona.stops[0] = GradientStop{0.0, Color::rgba(255, 255, 255, 255)};
    corona.stops[1] = GradientStop{0.2, spark};
    corona.stops[2] = GradientStop{0.56, with_alpha(edge, edge.alpha * 2U / 3U)};
    corona.stops[3] = GradientStop{1.0, with_alpha(edge, 0U)};
    corona.count = 4U;
    const Color core = Color::rgba(255, 255, 255, 252);
    Point center{};
    Size radii{};
    if (!parent.vertical) {
        center = Point{fill.x + fill.width, fill.y + fill.height * 0.5};
        radii = Size{effect_clamp(edge_extent * 0.72, 4.0, 10.0),
                     fill.height * 0.68 > 3.0 ? fill.height * 0.68 : 3.0};
    } else {
        center = Point{fill.x + fill.width * 0.5, fill.y};
        radii = Size{fill.width * 0.68 > 3.0 ? fill.width * 0.68 : 3.0,
                     effect_clamp(edge_extent * 0.72, 4.0, 10.0)};
    }
    canvas.fill_radial_gradient({center.x - radii.width, center.y - radii.height, radii.width * 2.0,
                                 radii.height * 2.0},
                                center, radii, corona);
    if (!parent.vertical) {
        canvas.stroke_line({center.x, fill.y + 1.0}, {center.x, fill.y + fill.height - 1.0}, core, 1.8);
    } else {
        canvas.stroke_line({fill.x + 1.0, center.y}, {fill.x + fill.width - 1.0, center.y}, core, 1.8);
    }

    // Small overlapping hot lobes flicker at the cut.
    const std::size_t flame_count = reduced ? 2U : 4U;
    for (const std::size_t index : bounded_range<4U>(flame_count)) {
        const double lane = (static_cast<double>(index) + 0.5) / static_cast<double>(flame_count);
        const double wave = effect_sine(tau * phase * 1.7 + static_cast<double>(index) * 2.11);
        const double heat = 0.5 + 0.5 * wave;
        Point lobe{};
        Size lobe_radii{};
        if (!parent.vertical) {
            lobe = Point{fill.x + fill.width + 0.6 + heat * 1.4, fill.y + fill.height * lane + wave * 1.1};
            lobe_radii = Size{2.4 + heat * 2.2, 1.8 + (1.0 - heat) * 2.0};
        } else {
            lobe = Point{fill.x + fill.width * lane + wave * 1.1, fill.y - 0.6 - heat * 1.4};
            lobe_radii = Size{1.8 + (1.0 - heat) * 2.0, 2.4 + heat * 2.2};
        }
        canvas.fill_radial_gradient({lobe.x - lobe_radii.width, lobe.y - lobe_radii.height, lobe_radii.width * 2.0,
                                     lobe_radii.height * 2.0},
                                    lobe, lobe_radii, corona);
    }

    // Short ember sparks.
    const std::size_t spark_count = reduced ? 3U : 7U;
    const double spark_base = reduced ? 0.9 : 1.2;
    const double spark_reach = reduced ? 0.65 : 1.35;
    const double spark_width_base = reduced ? 0.75 : 0.8;
    const double spark_width_step = reduced ? 0.0 : 0.28;
    for (const std::size_t index : bounded_range<7U>(spark_count)) {
        const double lane = (static_cast<double>(index) + 0.5) / static_cast<double>(spark_count);
        const double wave = effect_sine(tau * phase + static_cast<double>(index) * 1.73);
        const double spark_length = spark_base + (wave + 1.0) * spark_reach;
        const double spark_width = spark_width_base + spark_width_step * static_cast<double>(index % 3U);
        if (!parent.vertical) {
            const double y = fill.y + fill.height * lane;
            const double edge_x = fill.x + fill.width;
            canvas.stroke_line({edge_x - 0.35, y}, {edge_x + spark_length, y + wave * 0.8}, spark, spark_width);
        } else {
            const double x = fill.x + fill.width * lane;
            const double edge_y = fill.y;
            canvas.stroke_line({x, edge_y + 0.35}, {x + wave * 0.8, edge_y - spark_length}, spark, spark_width);
        }
    }
    canvas.restore();
}

void ProgressWovenStripes::setup(const EffectView<ProgressBar>& parent, const EffectSetup& setup) {
    EffectRandom random(setup.seed, EffectRandom::setup_stream);
    const double length = parent.vertical ? setup.part.height : setup.part.width;
    const double pitch = (parent.stripe_width > 2.0 ? parent.stripe_width : 2.0) * 1.5;
    const double wanted = length / pitch;
    const double clamped = effect_clamp(wanted, 4.0, static_cast<double>(maximum_stripes));
    const std::uint32_t count = static_cast<std::uint32_t>(clamped);
    // Which stripes exist and where.
    for (const std::size_t index : bounded_range<maximum_stripes>(count)) {
        WovenStripe& stripe = stripes_.items[index];
        stripe.offset = random.next_unit();
        stripe.width = parent.stripe_width * random.next_between(0.35, 0.8);
        stripe.weft = random.next_below(2U);
        stripe.shade = random.next_below(3U);
    }
    stripes_.count = count;
    // The order they weave in: a bounded Fisher–Yates shuffle.
    std::array<std::uint32_t, maximum_stripes> order{};
    for (const std::size_t index : bounded_range<maximum_stripes>(count)) {
        order[index] = static_cast<std::uint32_t>(index);
    }
    for (const std::size_t index : bounded_range<maximum_stripes>(count)) {
        const std::uint32_t remaining = count - static_cast<std::uint32_t>(index);
        const std::size_t chosen = index + random.next_below(remaining);
        const std::uint32_t held = order[index];
        order[index] = order[chosen];
        order[chosen] = held;
    }
    // Thresholds in that order, spread over the bar's first 95 %.
    for (const std::size_t rank : bounded_range<maximum_stripes>(count)) {
        const double threshold = (static_cast<double>(rank) + 0.5) / static_cast<double>(count) * 0.95;
        stripes_.items[order[rank]].threshold = threshold;
    }
    // The drawing order, kept as the stripes' order so that run draws them
    // in one pass: alternate weft stripes (odd places) under the warp, the
    // warp, then the other weft stripes over it.
    std::array<WovenStripe, maximum_stripes> layered{};
    std::uint32_t placed = 0U;
    for (std::uint32_t layer = 0U; layer < 3U; ++layer) {
        for (const std::size_t index : bounded_range<maximum_stripes>(count)) {
            const WovenStripe& stripe = stripes_.items[index];
            const std::uint32_t under_or_over = index % 2U == 1U ? 0U : 2U;
            const std::uint32_t stripe_layer = stripe.weft == 0U ? 1U : under_or_over;
            if (stripe_layer != layer) continue;
            layered[placed] = stripe;
            placed = placed + 1U;
        }
    }
    stripes_.items = layered;
}

void ProgressWovenStripes::run(const EffectView<ProgressBar>& parent, const EffectFrame& frame,
                               EffectCanvas& canvas) {
    if (!frame.still) shimmer_ = advanced_phase(shimmer_, frame.ticks, parent.period_seconds * 2.0);
    if (parent.fill.empty()) return;
    const Rect fill = parent.fill;
    const std::array<Color, 3> shades{parent.appearance.stripe_color, parent.appearance.luminance_color,
                                      parent.appearance.laser_phase_color};
    canvas.save();
    canvas.clip_rect(fill);
    // One pass in the drawing order setup chose (under, along, over), each
    // stripe's weave and glint computed once.
    for (const std::size_t index : bounded_range<maximum_stripes>(stripes_.count)) {
        const WovenStripe& stripe = stripes_.items[index];
        // Woven in over the 5 % of progress before its threshold.
        const double woven = effect_smoothstep(stripe.threshold - 0.05, stripe.threshold, parent.fraction);
        if (!(woven > 0.0)) continue;
        const double glint = 0.85 + 0.15 * effect_sine(tau * (shimmer_ + stripe.offset));
        const Color color = effect_faded(shades[stripe.shade % 3U], woven * glint);
        // Warp runs along the bar, weft across it.
        const bool along = (stripe.weft == 0U) != parent.vertical;
        if (along) {
            const double y = fill.y + stripe.offset * fill.height;
            canvas.stroke_line({fill.x, y}, {fill.x + fill.width, y}, color, stripe.width);
        } else {
            const double x = fill.x + stripe.offset * fill.width;
            canvas.stroke_line({x, fill.y}, {x, fill.y + fill.height}, color, stripe.width);
        }
    }
    canvas.restore();
}

void ProgressBubbles::run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    const Rect fill = parent.fill;
    // A press in the fill releases a bubble under the pointer.
    for (const std::size_t index : bounded_range<maximum_effect_input_events>(frame.input_count)) {
        const EffectInputEvent& event = frame.input[index];
        const bool pressed = event.kind == EffectInputEventKind::pointer_pressed;
        if (!pressed || !fill.contains(event.position) || bubbles_.count == maximum_bubbles) continue;
        RisingBody& released = bubbles_.items[bubbles_.count];
        released.radius = 1.6;
        released.position = event.position;
        released.speed = 10.0;
        released.age = 0.0;
        released.life = 0.0;
        bubbles_.count = bubbles_.count + 1U;
    }
    const std::uint32_t ticks = frame.still ? 0U : frame.ticks;
    for (const std::size_t tick : bounded_range<maximum_effect_ticks_per_frame>(ticks)) {
        EffectRandom random(frame.seed, frame.first_tick + tick);
        // Births: more as the bar fills, at most one a tick.
        owed_ = effect_clamp(owed_ + (1.5 + 6.0 * parent.fraction) * tick_seconds, 0.0, 2.0);
        if (owed_ >= 1.0 && !fill.empty() && bubbles_.count < maximum_bubbles) {
            RisingBody& born = bubbles_.items[bubbles_.count];
            born.radius = random.next_between(0.8, 2.2);
            born.position = Point{fill.x + random.next_unit() * fill.width, fill.y + fill.height - born.radius};
            born.speed = random.next_between(6.0, 16.0);
            born.age = 0.0;
            born.life = 0.0;
            bubbles_.count = bubbles_.count + 1U;
            owed_ = owed_ - 1.0;
        }
        // Rise; a bubble reaching the top of the fill breaks into steam, and
        // one the fill has shrunk away from is dropped.
        std::uint32_t kept = 0U;
        for (const std::size_t index : bounded_range<maximum_bubbles>(bubbles_.count)) {
            RisingBody bubble = bubbles_.items[index];
            bubble.age = bubble.age + tick_seconds;
            const double sway = effect_sine(bubble.age * 5.0 + bubble.speed) * 0.25;
            bubble.position = Point{bubble.position.x + sway, bubble.position.y - bubble.speed * tick_seconds};
            const bool outside = bubble.position.x > fill.x + fill.width || fill.empty();
            const bool broke = bubble.position.y - bubble.radius <= fill.y;
            if (broke && !outside && puffs_.count < maximum_puffs) {
                RisingBody& puff = puffs_.items[puffs_.count];
                puff.position = Point{bubble.position.x, fill.y};
                puff.radius = bubble.radius * 1.6;
                puff.speed = random.next_between(5.0, 10.0);
                puff.age = 0.0;
                puff.life = random.next_between(0.8, 1.4);
                puffs_.count = puffs_.count + 1U;
            }
            if (broke || outside) continue;
            bubbles_.items[kept] = bubble;
            kept = kept + 1U;
        }
        bubbles_.count = kept;
        // Steam rises, spreads and fades.
        std::uint32_t kept_puffs = 0U;
        for (const std::size_t index : bounded_range<maximum_puffs>(puffs_.count)) {
            RisingBody puff = puffs_.items[index];
            puff.age = puff.age + tick_seconds;
            puff.position = Point{puff.position.x, puff.position.y - puff.speed * tick_seconds};
            if (puff.age >= puff.life) continue;
            puffs_.items[kept_puffs] = puff;
            kept_puffs = kept_puffs + 1U;
        }
        puffs_.count = kept_puffs;
    }

    const Color rim = parent.appearance.stripe_color;
    const Color shine = parent.appearance.luminance_color;
    if (!fill.empty()) {
        canvas.save();
        canvas.clip_rect(fill);
        for (const std::size_t index : bounded_range<maximum_bubbles>(bubbles_.count)) {
            const RisingBody& bubble = bubbles_.items[index];
            canvas.stroke_circle(bubble.position, bubble.radius, rim, 0.8);
            const Point highlight{bubble.position.x - bubble.radius * 0.35, bubble.position.y - bubble.radius * 0.35};
            canvas.soft_dot(highlight, bubble.radius * 0.5, shine);
        }
        canvas.restore();
    }
    // Steam is drawn unclipped by the fill: it rises into the top outset.
    const Color steam = Color::rgba(255, 255, 255, 84);
    for (const std::size_t index : bounded_range<maximum_puffs>(puffs_.count)) {
        const RisingBody& puff = puffs_.items[index];
        const double left = puff.life > 0.0 ? 1.0 - puff.age / puff.life : 0.0;
        canvas.soft_dot(puff.position, puff.radius * (1.0 + puff.age * 1.5), effect_faded(steam, left));
    }
}

void register_progress_bar_effects(EffectRegistry& registry) {
    constexpr std::array<EffectEntry, 5> entries{
        make_effect_entry<ProgressStripes>(),
        make_effect_entry<ProgressPulse>(),
        make_effect_entry<ProgressLaser>(),
        make_effect_entry<ProgressWovenStripes>(),
        make_effect_entry<ProgressBubbles>(),
    };
    for (const EffectEntry& entry : entries) {
        static_cast<void>(registry.add(entry));
    }
}

} // namespace gui_forms
