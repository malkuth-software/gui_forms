#pragma once

// The Gallery's custom instrument (the "instrument" page): a trace over a
// level in 0..1 with a sweep line that crosses it on a paint-gated 30 Hz UI
// timer (ADR-016). An application type of the Gallery's form document
// (FD6.2, provisionally; owner decision pending on whether it becomes a
// registered instruments control): `gallery-instrument` derives from
// `user-control`.

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <memory>

namespace gui_forms::gallery {

class Instrument final : public UserControl {
public:
    explicit Instrument(StableId stable_id);

    // The level the trace shows, 0..1 (clamped).
    [[nodiscard]] double value() const noexcept { return value_; }
    void set_value(double value);

    void on_paint(Painter& painter, Rect local_damage) override;
    // The instrument is a display: it takes no pointer input.
    [[nodiscard]] bool hit_test_local(Point local_point) const override;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;

private:
    // The sweep timer's target. Observes the instrument; a destroyed one is
    // skipped.
    struct SweepTick final {
        std::weak_ptr<Instrument> instrument;
        AnimationTickResult operator()(FrameTime now) const;
    };
    // One sweep tick: advances the sweep by the time since the last one and
    // damages the sweep line's old and new place.
    AnimationTickResult on_sweep_tick(FrameTime now);
    // The sweep line's bounds at `phase` (0 to 1 across the trace).
    [[nodiscard]] Rect sweep_bounds(double phase) const noexcept;

    double value_{};
    // The phase advances with time, not with paints.
    AnimationTimer sweep_timer_;
    FrameTime sweep_last_tick_{};
    double sweep_phase_{};
};

} // namespace gui_forms::gallery
