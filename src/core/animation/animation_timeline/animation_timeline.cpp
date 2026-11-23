#include "gui_forms/animation.hpp"
#include "stxlib/math.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gui_forms {
namespace {

void validate(const AnimationSpec& specification) {
    if (specification.delay < FrameInterval::zero()) {
        throw std::invalid_argument("animation delay may not be negative");
    }
    if (specification.duration <= FrameInterval::zero()) {
        throw std::invalid_argument("animation duration must be positive");
    }
    if (!specification.infinite &&
        (!std::isfinite(specification.iterations) ||
         specification.iterations <= 0.0)) {
        throw std::invalid_argument(
            "finite animation iteration count must be positive and finite");
    }
}

// `progress` played forward, or backward when `reverse`.
double played(double progress, bool reverse) noexcept {
    const double result = reverse ? 1.0 - progress : progress;
    return result;
}

double directed_progress(AnimationDirection direction,
                         std::uint64_t iteration,
                         double progress) noexcept {
    const bool odd = (iteration % 2U) != 0U;
    switch (direction) {
    case AnimationDirection::normal:
        return played(progress, false);
    case AnimationDirection::reverse:
        return played(progress, true);
    case AnimationDirection::alternate:
        return played(progress, odd);
    case AnimationDirection::alternate_reverse:
        return played(progress, !odd);
    }
    // A direction outside the enumeration plays forward.
    return played(progress, false);
}

} // namespace

double apply_easing(EasingCurve easing, double progress) noexcept {
    const double value = std::clamp(progress, 0.0, 1.0);
    switch (easing) {
    case EasingCurve::linear:
        return value;
    case EasingCurve::ease_in:
        return value * value * value;
    case EasingCurve::ease_out: {
        const double inverse = 1.0 - value;
        return 1.0 - inverse * inverse * inverse;
    }
    case EasingCurve::ease_in_out:
        return value < 0.5
            ? 4.0 * value * value * value
            : 1.0 - std::pow(-2.0 * value + 2.0, 3.0) * 0.5;
    case EasingCurve::smooth_step:
        return value * value * (3.0 - 2.0 * value);
    case EasingCurve::back_out: {
        constexpr double overshoot = 1.70158;
        const double shifted = value - 1.0;
        return 1.0 + (overshoot + 1.0) * shifted * shifted * shifted +
               overshoot * shifted * shifted;
    }
    case EasingCurve::bounce_out: {
        constexpr double divisor = 2.75;
        constexpr double scale = 7.5625;
        if (value < 1.0 / divisor) {
            return scale * value * value;
        }
        if (value < 2.0 / divisor) {
            const double shifted = value - 1.5 / divisor;
            return scale * shifted * shifted + 0.75;
        }
        if (value < 2.5 / divisor) {
            const double shifted = value - 2.25 / divisor;
            return scale * shifted * shifted + 0.9375;
        }
        const double shifted = value - 2.625 / divisor;
        return scale * shifted * shifted + 0.984375;
    }
    case EasingCurve::elastic_out: {
        if (value == 0.0 || value == 1.0) {
            return value;
        }
        // STXlib's deterministic power and sine (the same bits on
        // every host).
        const double decay = stxlib::math::exp2(-10.0 * value);
        double swing_sine = 0.0;
        double swing_cosine = 0.0;
        stxlib::math::sincos((value * 10.0 - 0.75) * (2.0 * 3.14159265358979323846 / 3.0), swing_sine, swing_cosine);
        const double eased = decay * swing_sine + 1.0;
        return eased;
    }
    }
    return value;
}

AnimationTimeline::AnimationTimeline(AnimationSpec specification)
    : specification_(specification) {
    validate(specification_);
}

void AnimationTimeline::set_specification(AnimationSpec specification) {
    validate(specification);
    specification_ = specification;
}

void AnimationTimeline::start(FrameTime start_time) noexcept {
    start_time_ = start_time;
    pause_time_ = start_time;
    running_ = true;
    paused_ = false;
}

void AnimationTimeline::pause(FrameTime pause_time) noexcept {
    if (!running_ || paused_) {
        return;
    }
    pause_time_ = std::max(pause_time, start_time_);
    paused_ = true;
}

void AnimationTimeline::resume(FrameTime resume_time) noexcept {
    if (!running_ || !paused_) {
        return;
    }
    const FrameTime effective_resume = std::max(resume_time, pause_time_);
    start_time_ += effective_resume - pause_time_;
    pause_time_ = effective_resume;
    paused_ = false;
}

void AnimationTimeline::stop() noexcept {
    running_ = false;
    paused_ = false;
}

AnimationSample AnimationTimeline::sample(FrameTime now) const noexcept {
    AnimationSample result;
    if (!running_) {
        return result;
    }
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    // A high-integrity build has no motion (cmake/HighIntegrity.cmake): a
    // running timeline is complete whatever the time. A finite one shows its
    // last iteration's end, an infinite one its start, at rest; the delay and
    // the duration are not read.
    static_cast<void>(now);
    const double last_position =
        specification_.infinite ? 0.0 : std::max(0.0, specification_.iterations - 1.0);
    const double local = specification_.infinite ? 0.0 : 1.0;
    result.iteration = static_cast<std::uint64_t>(std::floor(last_position));
    result.progress = directed_progress(specification_.direction, result.iteration, local);
    result.eased_progress = apply_easing(specification_.easing, result.progress);
    result.finished = true;
    return result;
#else
    const FrameTime sample_time = paused_ ? pause_time_ : now;
    const FrameInterval elapsed = sample_time - start_time_;
    if (elapsed < specification_.delay) {
        result.progress = directed_progress(specification_.direction, 0U, 0.0);
        result.eased_progress = apply_easing(specification_.easing, result.progress);
        result.delayed = true;
        result.active = true;
        return result;
    }

    const double duration =
        std::chrono::duration<double>(specification_.duration).count();
    const double active_elapsed =
        std::chrono::duration<double>(elapsed - specification_.delay).count();
    const double position = std::max(0.0, active_elapsed / duration);
    if (!specification_.infinite && position >= specification_.iterations) {
        const double last_position = std::max(0.0, specification_.iterations - 1.0);
        result.iteration = static_cast<std::uint64_t>(std::floor(last_position));
        result.progress = directed_progress(specification_.direction,
                                            result.iteration, 1.0);
        result.eased_progress = apply_easing(specification_.easing, result.progress);
        result.finished = true;
        return result;
    }

    result.iteration = static_cast<std::uint64_t>(std::floor(position));
    const double local = position - std::floor(position);
    result.progress = directed_progress(specification_.direction,
                                        result.iteration, local);
    result.eased_progress = apply_easing(specification_.easing, result.progress);
    result.active = true;
    return result;
#endif
}

} // namespace gui_forms
