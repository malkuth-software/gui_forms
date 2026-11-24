#include "touch_input.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::touch_start {

GestureClassification GestureClassification::none() noexcept {
    const GestureClassification made{};
    return made;
}

GestureClassification GestureClassification::tap(Point position) noexcept {
    const GestureClassification made{.kind = GestureKind::tap, .position = position, .velocity = {}};
    return made;
}

GestureClassification GestureClassification::pan(Point position) noexcept {
    const GestureClassification made{.kind = GestureKind::pan, .position = position, .velocity = {}};
    return made;
}

GestureClassification GestureClassification::flick(Point position, Point velocity) noexcept {
    const GestureClassification made{.kind = GestureKind::flick, .position = position, .velocity = velocity};
    return made;
}

GestureClassification GestureClassification::press_and_hold(Point position) noexcept {
    const GestureClassification made{.kind = GestureKind::press_and_hold, .position = position, .velocity = {}};
    return made;
}

GestureClassifier::GestureClassifier(TouchThresholds thresholds) noexcept : thresholds_(thresholds) {}

void GestureClassifier::press(PointerKind kind, TouchSample sample) noexcept {
    kind_ = kind;
    pressed_ = true;
    panning_ = false;
    origin_ = sample;
    last_ = sample;
    sample_count_ = 0U;
    next_sample_ = 0U;
    keep(sample);
}

PanStep GestureClassifier::move(TouchSample sample) noexcept {
    PanStep step{};
    if (!pressed_) return step;
    keep(sample);
    if (!panning_) {
        const double travel = std::hypot(sample.position.x - origin_.position.x,
                                         sample.position.y - origin_.position.y);
        if (travel <= slop()) return step;
        // Past the slop the surface catches up with the pointer, so the
        // content stays under it from here on (direct manipulation).
        panning_ = true;
        step.panning = true;
        step.phase = TouchPhase::began;
        step.delta = Point{sample.position.x - origin_.position.x, sample.position.y - origin_.position.y};
        last_ = sample;
        return step;
    }
    step.panning = true;
    step.phase = TouchPhase::changed;
    step.delta = Point{sample.position.x - last_.position.x, sample.position.y - last_.position.y};
    last_ = sample;
    return step;
}

GestureClassification GestureClassifier::release(TouchSample sample) noexcept {
    if (!pressed_) return GestureClassification::none();
    keep(sample);
    pressed_ = false;
    if (panning_) {
        panning_ = false;
        const Point velocity = estimated_velocity();
        const double speed = std::hypot(velocity.x, velocity.y);
        if (speed >= thresholds_.flick_speed) return GestureClassification::flick(sample.position, velocity);
        return GestureClassification::pan(sample.position);
    }
    // TODO(touch): a touch press held still for hold_nanoseconds (the
    // release's timestamp less the press's) is a press-and-hold: the field
    // shows the tile's tool tip and does not select. A host that reports
    // the hold before the lift (a timer from press()) raises it then.
    return GestureClassification::tap(sample.position);
}

void GestureClassifier::cancel() noexcept {
    pressed_ = false;
    panning_ = false;
    sample_count_ = 0U;
    next_sample_ = 0U;
}

bool GestureClassifier::pressed() const noexcept { return pressed_; }

bool GestureClassifier::panning() const noexcept { return panning_; }

PointerKind GestureClassifier::kind() const noexcept { return kind_; }

double GestureClassifier::slop() const noexcept {
    const double chosen = kind_ == PointerKind::touch ? thresholds_.touch_slop : thresholds_.mouse_slop;
    return chosen;
}

Point GestureClassifier::estimated_velocity() const noexcept {
    // TODO(touch): fit the kept samples (samples_, sample_count_ of them,
    // the newest before next_sample_) that lie within 100 ms of the newest.
    const Point still{};
    return still;
}

void GestureClassifier::keep(TouchSample sample) noexcept {
    samples_[next_sample_] = sample;
    next_sample_ = (next_sample_ + 1U) % sample_capacity;
    sample_count_ = std::min(sample_count_ + 1U, sample_capacity);
}

void PanInertia::start(double velocity) noexcept {
    // TODO(touch): active_ = |velocity| >= stop_speed.
    velocity_ = velocity;
    active_ = false;
}

double PanInertia::step(double seconds) noexcept {
    if (!active_ || seconds <= 0.0) return 0.0;
    // TODO(touch): the decay's distance over `seconds`:
    // velocity * tau * (1 - exp(-seconds / tau)); the velocity decays by
    // exp(-seconds / tau), and below stop_speed the inertia stops.
    return 0.0;
}

void PanInertia::stop() noexcept {
    velocity_ = 0.0;
    active_ = false;
}

bool PanInertia::active() const noexcept { return active_; }

double PanInertia::velocity() const noexcept { return velocity_; }

double settle_pan_offset(double requested, double maximum, double viewport) noexcept {
    // TODO(touch): rubber band past either end, at most viewport / 5.
    static_cast<void>(viewport);
    const double upper = std::max(0.0, maximum);
    const double settled = std::clamp(requested, 0.0, upper);
    return settled;
}

double spring_back_target(double offset, double maximum) noexcept {
    const double upper = std::max(0.0, maximum);
    const double target = std::clamp(offset, 0.0, upper);
    return target;
}

} // namespace gui_forms::touch_start
