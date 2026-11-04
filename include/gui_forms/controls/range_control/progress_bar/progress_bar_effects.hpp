#pragma once

// The progress bar's effect programs (design note §8): its stripes, pulse
// and laser, which replace the legacy painting in range_control_rendering
// (T4's thirteen calls), and two new ones, the randomly woven stripes and
// bubbles with rising steam. Each is a kind of progress bar
// (parent_type = ProgressBar) drawing over the bar's interior, its part.

#include "gui_forms/controls/range_control/progress_bar/progress_bar.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms {

class EffectRegistry;

// What a progress bar's effects may read: a snapshot taken when the frame
// runs. Geometry is in part-local logical units: the part is the whole bar,
// `interior` the track inside its frame, and `fill` the filled length (or
// the marquee's band) within that.
template <>
struct EffectView<ProgressBar> final {
    static constexpr std::string_view parent_name{"progress_bar"};

    // (value − minimum) ÷ (maximum − minimum), in [0, 1].
    double fraction{};
    Rect interior{};
    Rect fill{};
    bool vertical{};
    ProgressBarAnimationAppearance appearance{};
    double stripe_width{7.0};
    // The bar's animation period, in seconds: one cycle of an effect's phase.
    double period_seconds{1.4};
    // The phase an effect shows while still (the bar's reduced-motion pose).
    double still_phase{0.5};
};

// Marching diagonal stripes over the fill (ProgressBarVisualStyle::
// marching_stripes and the moving-stripes overlay).
class ProgressStripes final {
public:
    using parent_type = ProgressBar;
    static constexpr std::size_t maximum_stripes = 1024U;
    static constexpr EffectDeclaration declaration{
        .name = "progress_stripes",
        .engine_version = 1U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 40000U, .per_device_pixel = 6U},
        .state_budget = 16U,
    };
    void run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    double phase_{};
};

// A soft luminance band sweeping along the fill (ProgressBarVisualStyle::
// pulse).
class ProgressPulse final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "progress_pulse",
        .engine_version = 1U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 400U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    double phase_{};
};

// The laser etch: phase bands over the fill, a glowing edge, and a burning
// front with flickering lobes and short sparks (ProgressBarVisualStyle::
// laser_etch).
class ProgressLaser final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "progress_laser",
        .engine_version = 1U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 3000U, .per_device_pixel = 12U},
        .state_budget = 16U,
    };
    void run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    double phase_{};
};

// One woven stripe: warp (along the bar) or weft (across it), where it
// lies, how wide, which colour and the fraction at which it weaves in.
struct WovenStripe final {
    double offset{};
    double width{};
    double threshold{};
    std::uint32_t weft{};
    std::uint32_t shade{};

    friend constexpr bool operator==(const WovenStripe& left, const WovenStripe& right) noexcept {
        return left.offset == right.offset && left.width == right.width && left.threshold == right.threshold &&
               left.weft == right.weft && left.shade == right.shade;
    }
};

// Randomly woven stripes (owner amendment, 2026-11-02): setup chooses which
// stripes exist, where and in what order at random; run weaves each in as
// the fraction passes its threshold, and shimmers them. Setup stores the
// stripes in their drawing order (under, along, over), so run draws them in
// one pass with each stripe's weave and glint computed once. The effect
// pass bounds run at 3,772 operations (48 passes of about 78: a smoothstep
// 10, a sine 40, a fade 6, a stroke 6, the arithmetic and the pass's 2),
// under the 6,000 budget; three layer passes cost 11,851. Setup, with the
// ordering's three passes over 48, is bounded at 4,210, within its 8,000.
class ProgressWovenStripes final {
public:
    using parent_type = ProgressBar;
    static constexpr std::size_t maximum_stripes = 48U;
    static constexpr EffectDeclaration declaration{
        .name = "progress_woven_stripes",
        .engine_version = 1U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 6000U, .per_device_pixel = 8U},
        .setup_cost = 8000U,
        .setup_on_geometry = true,
        .state_budget = 1600U,
    };
    void setup(const EffectView<ProgressBar>& parent, const EffectSetup& setup);
    void run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    EffectArray<WovenStripe, maximum_stripes> stripes_{};
    double shimmer_{};
};

// One bubble rising in the fill, or one puff of steam rising above the bar.
struct RisingBody final {
    Point position{};
    double radius{};
    double speed{};
    double age{};
    double life{};

    friend constexpr bool operator==(const RisingBody& left, const RisingBody& right) noexcept {
        return left.position == right.position && left.radius == right.radius && left.speed == right.speed &&
               left.age == right.age && left.life == right.life;
    }
};

// Bubbles rising in the filled length; each that reaches the top breaks
// into a puff of steam rising into the top outset and fading. With pointer
// input granted, a press in the fill releases a bubble there.
class ProgressBubbles final {
public:
    using parent_type = ProgressBar;
    static constexpr std::size_t maximum_bubbles = 24U;
    static constexpr std::size_t maximum_puffs = 16U;
    static constexpr double steam_outset = 14.0;
    static constexpr EffectDeclaration declaration{
        .name = "progress_bubbles",
        .engine_version = 1U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 12000U, .per_device_pixel = 8U},
        .state_budget = 2048U,
        .outsets = {.left = 0.0, .top = steam_outset, .right = 0.0, .bottom = 0.0},
        .input = EffectInputKind::pointer,
    };
    void run(const EffectView<ProgressBar>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    EffectArray<RisingBody, maximum_bubbles> bubbles_{};
    EffectArray<RisingBody, maximum_puffs> puffs_{};
    // Births owed to the fill, in bubbles.
    double owed_{};
};

// Adds the progress bar's effects to `registry`.
void register_progress_bar_effects(EffectRegistry& registry);

} // namespace gui_forms
