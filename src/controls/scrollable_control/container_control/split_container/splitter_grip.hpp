#pragma once

#include "gui_forms/animation.hpp"
#include "gui_forms/controls/split_container.hpp"
#include "gui_forms/scheduler.hpp"

#include <chrono>

namespace gui_forms {

// Source-private retained control for the visible splitter seam, enlarged hit
// target, keyboard focus cue, and optional collapse tab. SplitContainer owns
// its policy; SplitterGrip owns only the seam's visual state machine.
class SplitterGrip final : public Control {
public:
    explicit SplitterGrip(StableId stable_id);

    void set_orientation(Orientation orientation);
    void set_visible_geometry(double origin, double width);
    void set_collapse_appearance(SplitFixedPanel panel, bool collapsed);
    void set_dragging(bool dragging);
    void set_actuator_pressed(bool pressed);
    void set_transition_duration(FrameInterval duration);
    [[nodiscard]] FrameInterval transition_duration() const noexcept {
        return transition_duration_;
    }
    [[nodiscard]] bool transition_active() const noexcept {
        return transition_active_;
    }
    [[nodiscard]] double transition_progress() const noexcept {
        return transition_progress_;
    }
    void reset_interaction() noexcept;
    [[nodiscard]] SplitSeamState seam_state() const noexcept;
    [[nodiscard]] Rect actuator_bounds() const noexcept;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    void on_pointer(PointerEvent& event) override;
    // The grip holds focus, so the rule action reaches it; the container
    // runs the drag.
    bool begin_keyboard_drag() override;
    void on_focus_changed(bool focused) override;
    void on_focus_cue_changed(bool visible) override;
    void on_paint(Painter& painter, Rect local_damage) override;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;

private:
    // The actuator transition timer's target. Observes the grip.
    struct TransitionTick final {
        std::weak_ptr<SplitterGrip> grip;
        AnimationTickResult operator()(FrameTime now) const;
    };

    // One transition tick: advances the actuator and damages its old and new
    // place.
    AnimationTickResult on_transition_tick(FrameTime now);
    // Advances the actuator transition to `now`.
    void advance_animation(FrameTime now);
    [[nodiscard]] bool engaged() const noexcept;
    [[nodiscard]] Rect actuator_bounds(double cross_extent,
                                       double axis_extent) const noexcept;
    [[nodiscard]] bool point_in_proximity(Point window_point) const noexcept;
    [[nodiscard]] Size target_actuator_size() const noexcept;
    void retarget_transition();
    void update_transition_registration();
    void complete_transition() noexcept;

    Orientation orientation_{Orientation::vertical};
    SplitFixedPanel collapse_panel_{SplitFixedPanel::none};
    double visible_origin_{3.0};
    double visible_width_{3.0};
    bool collapsed_{};
    bool near_{};
    bool hot_{};
    bool dragging_{};
    bool actuator_pressed_{};
    bool focused_{};
    FrameInterval transition_duration_{std::chrono::milliseconds(90)};
    AnimationTimeline transition_timeline_;
    // ADR-016: a paint-gated UI timer at the display rate.
    AnimationTimer transition_frames_;
    Size transition_from_{7.0, 28.0};
    Size transition_to_{7.0, 28.0};
    Size presented_actuator_{7.0, 28.0};
    double transition_progress_{1.0};
    bool transition_active_{};
};

} // namespace gui_forms
