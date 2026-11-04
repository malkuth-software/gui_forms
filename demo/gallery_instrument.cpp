#include "gallery_instrument.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace gui_forms::gallery {
namespace {

constexpr Color instrument_face = Color::rgba(13, 24, 36);
constexpr Color instrument_rule = Color::rgba(47, 70, 101);
constexpr Color deep_shadow = Color::rgba(76, 94, 111);
constexpr Color cyan = Color::rgba(89, 216, 230);
constexpr Color amber = Color::rgba(255, 210, 122);
constexpr Color sweep_ink = Color::rgba(128, 223, 239, 150);

// The sweep crosses the trace once in this period (it moved 0.027 of the
// trace per 30 Hz frame before it followed time).
constexpr FrameInterval instrument_sweep_period = std::chrono::microseconds(1'234'568);
// The instrument's authored tick rate, 30 Hz.
constexpr FrameInterval instrument_sweep_interval = std::chrono::nanoseconds(33'333'333);

} // namespace

Instrument::Instrument(StableId stable_id) : UserControl(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
}

void Instrument::set_value(double value) {
    require_mutable();
    const double bounded = std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    if (bounded == value_) return;
    value_ = bounded;
    invalidate(Dirty::paint | Dirty::semantics);
}

void Instrument::on_paint(Painter& painter, Rect) {
    sweep_timer_.note_painted();
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    painter.fill_rect(bounds, instrument_face);
    painter.stroke_rect({0.5, 0.5, bounds.width - 1.0, bounds.height - 1.0}, deep_shadow, 1.0);
    for (int line = 1; line < 5; ++line) {
        const double y = bounds.height * static_cast<double>(line) / 5.0;
        painter.draw_line({8.0, y}, {bounds.width - 8.0, y}, instrument_rule, 1.0);
    }
    const double marker_x = 10.0 + value_ * std::max(0.0, bounds.width - 20.0);
    painter.draw_line({10.0, bounds.height - 14.0}, {marker_x, 18.0}, cyan, 2.0);
    painter.draw_line({marker_x, 18.0}, {bounds.width - 10.0, bounds.height - 25.0}, amber, 2.0);
    const Rect sweep = sweep_bounds(sweep_phase_);
    const double sweep_x = sweep.x + 1.0;
    painter.draw_line({sweep_x, 25.0}, {sweep_x, bounds.height - 8.0}, sweep_ink, 1.0);
    char percent[32]{};
    std::snprintf(percent, sizeof(percent), "LEVEL %3.0f%%", value_ * 100.0);
    painter.draw_text_utf8({12.0, 17.0}, percent, {FontRole::monospace, 12.0, 600, false}, cyan);
}

bool Instrument::hit_test_local(Point) const {
    return false;
}

void Instrument::on_attached_to_window() {
    UserControl::on_attached_to_window();
    sweep_last_tick_ = FrameClock::now();
    const std::shared_ptr<Instrument> self = std::static_pointer_cast<Instrument>(shared_from_this());
    sweep_timer_.start(*this, instrument_sweep_interval, SweepTick{self});
}

void Instrument::on_detached_from_window() noexcept {
    sweep_timer_.stop();
    UserControl::on_detached_from_window();
}

AnimationTickResult Instrument::SweepTick::operator()(FrameTime now) const {
    const std::shared_ptr<Instrument> live = instrument.lock();
    const AnimationTickResult result = live ? (*live).on_sweep_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult Instrument::on_sweep_tick(FrameTime now) {
    // A timer that waited for a paint resumes from where it stopped.
    const FrameInterval elapsed = std::min(now - sweep_last_tick_, instrument_sweep_interval);
    sweep_last_tick_ = now;
    const double advance = std::chrono::duration<double>(std::max(elapsed, FrameInterval{})).count() /
                           std::chrono::duration<double>(instrument_sweep_period).count();
    const Rect before = sweep_bounds(sweep_phase_);
    sweep_phase_ = std::fmod(sweep_phase_ + advance, 1.0);
    const Rect after = sweep_bounds(sweep_phase_);
    invalidate(Rect::united(before, after));
    return AnimationTickResult::damaged;
}

Rect Instrument::sweep_bounds(double phase) const noexcept {
    // The sweep is a 1-pixel line from y 25 to 8 above the bottom; its
    // bounds take one pixel either side of its centre.
    const Rect bounds = committed_arranged_bounds();
    const double x = 10.0 + phase * std::max(0.0, bounds.width - 20.0);
    const Rect sweep{x - 1.0, 24.0, 2.0, std::max(0.0, bounds.height - 31.0)};
    return sweep;
}

} // namespace gui_forms::gallery
