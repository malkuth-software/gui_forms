#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler.hpp"

#include <cstdint>

namespace gui_forms {

enum class EasingCurve : std::uint8_t {
    linear,
    ease_in,
    ease_out,
    ease_in_out,
    smooth_step,
    back_out,
    bounce_out,
    elastic_out,
};

enum class AnimationDirection : std::uint8_t {
    normal,
    reverse,
    alternate,
    alternate_reverse,
};

struct GUI_FORMS_API AnimationSpec final {
    FrameInterval delay{};
    FrameInterval duration{std::chrono::milliseconds(300)};
    double iterations{1.0};
    EasingCurve easing{EasingCurve::linear};
    AnimationDirection direction{AnimationDirection::normal};
    bool infinite{};
};

struct GUI_FORMS_API AnimationSample final {
    double progress{};
    double eased_progress{};
    std::uint64_t iteration{};
    bool delayed{};
    bool active{};
    bool finished{};
};

// Application motion policy is deliberately orthogonal. Disabling an
// animation source, pausing playback, and requesting a reduced-motion
// presentation are distinct facts; collapsing them into one enum makes
// compound transitions order-dependent and can require multiple toggles to
// recover. Controls use active() to own a frame lease. Reduced motion remains
// active, but exposes a calmer cadence, an accumulated-phase speed scale, and
// limited visual excursion. Controls consume the helpers appropriate to their
// timeline model. Only the explicit playback gates are quiescent.
struct GUI_FORMS_API MotionPolicy final {
    bool enabled{true};
    bool paused{};
    bool reduced{};

    [[nodiscard]] constexpr bool active() const noexcept {
        return enabled && !paused;
    }
    [[nodiscard]] constexpr bool quiescent() const noexcept {
        return !active();
    }
    [[nodiscard]] constexpr FrameInterval frame_interval(
        FrameInterval full_motion_interval) const noexcept {
        constexpr FrameInterval reduced_interval =
            std::chrono::milliseconds(100);
        return reduced && full_motion_interval < reduced_interval
            ? reduced_interval : full_motion_interval;
    }
    [[nodiscard]] constexpr double speed_scale() const noexcept {
        return reduced ? 0.35 : 1.0;
    }
    [[nodiscard]] constexpr double presentation_phase(double phase) const noexcept {
        // Keep reduced motion centered and visibly alive while limiting travel
        // to half the full-motion excursion.
        return reduced ? 0.5 + (phase - 0.5) * 0.5 : phase;
    }

    friend constexpr bool operator==(const MotionPolicy& left,
                                     const MotionPolicy& right) noexcept {
        return left.enabled == right.enabled && left.paused == right.paused &&
               left.reduced == right.reduced;
    }
};

[[nodiscard]] GUI_FORMS_API double apply_easing(EasingCurve easing, double progress) noexcept;

class GUI_FORMS_API AnimationTimeline final {
public:
    AnimationTimeline() = default;
    explicit AnimationTimeline(AnimationSpec specification);

    void set_specification(AnimationSpec specification);
    [[nodiscard]] const AnimationSpec& specification() const noexcept {
        return specification_;
    }
    void start(FrameTime start_time) noexcept;
    // Pause retains the exact sampled timeline position. Resume rebases the
    // origin by the suspended duration, so the first resumed sample advances
    // from that position instead of jumping or catching up.
    void pause(FrameTime pause_time) noexcept;
    void resume(FrameTime resume_time) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] AnimationSample sample(FrameTime now) const noexcept;

private:
    AnimationSpec specification_{};
    FrameTime start_time_{};
    FrameTime pause_time_{};
    bool running_{};
    bool paused_{};
};

} // namespace gui_forms
