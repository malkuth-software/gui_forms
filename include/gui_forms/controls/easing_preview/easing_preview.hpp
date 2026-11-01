#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/animation.hpp"
#include "gui_forms/basic_controls.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

struct PresentationSettings;

struct EasingPreviewTrack final {
    EasingCurve curve{EasingCurve::linear};
    std::string label;
    Color color{};

    friend bool operator==(const EasingPreviewTrack& left,
                           const EasingPreviewTrack& right) noexcept(noexcept(
        left.curve == right.curve && left.label == right.label &&
        left.color == right.color)) {
        return left.curve == right.curve && left.label == right.label &&
               left.color == right.color;
    }
};

// A retained animation/easing conformance surface.
class EasingPreview final : public Control {
public:
    explicit EasingPreview(StableId stable_id);

    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    void set_title(std::string title);
    [[nodiscard]] const BasicControlStyle& style() const noexcept {
        return style_;
    }
    void set_style(BasicControlStyle style);
    [[nodiscard]] const AnimationSpec& specification() const noexcept {
        return timeline_.specification();
    }
    void set_specification(AnimationSpec specification);
    [[nodiscard]] const std::vector<EasingPreviewTrack>& tracks() const noexcept {
        return tracks_;
    }
    void set_tracks(std::vector<EasingPreviewTrack> tracks);
    [[nodiscard]] MotionPolicy motion_policy() const noexcept {
        return motion_policy_;
    }
    [[nodiscard]] MotionPolicy effective_motion_policy() const noexcept;
    void set_motion_policy(MotionPolicy policy);
    [[nodiscard]] double marker_size() const noexcept { return marker_size_; }
    void set_marker_size(double size);
    [[nodiscard]] double phase() const noexcept { return phase_; }

    // Samples the timeline at `now` into the phase; the frame timer calls it
    // on each tick, and tests call it to step the preview without a clock.
    void advance_animation(FrameTime now);
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The animation timer's named target. Observes the preview; a destroyed
    // one is skipped.
    struct FrameTick final {
        std::weak_ptr<EasingPreview> preview;
        AnimationTickResult operator()(FrameTime now) const;
    };

    void register_frames();
    // One timer tick: samples the timeline and damages each marker's old and
    // new place and the readout.
    AnimationTickResult on_frame_tick(FrameTime now);
    // The places that move with the phase, used by on_paint and the tick's
    // damage: track `index`'s marker at `presented_phase`, and the readout.
    [[nodiscard]] Rect marker_bounds(std::size_t index, double presented_phase) const noexcept;
    [[nodiscard]] Rect readout_bounds() const noexcept;
    // The vertical origin of track `index`'s row.
    [[nodiscard]] double track_row_top(std::size_t index) const noexcept;
    [[nodiscard]] double track_left() const noexcept;
    [[nodiscard]] double track_width() const noexcept;
    void on_presentation_changed(const PresentationSettings& settings);
    [[nodiscard]] std::string motion_readout(double presented_phase) const;

    // ADR-016: a paint-gated UI timer the preview stops.
    AnimationTimer frames_;
    SubscriptionToken presentation_subscription_;
    AnimationTimeline timeline_;
    std::vector<EasingPreviewTrack> tracks_;
    std::string title_{"Animation timeline and easing"};
    BasicControlStyle style_;
    MotionPolicy motion_policy_{};
    double marker_size_{14.0};
    double phase_{};
    bool timeline_started_{};
};

} // namespace gui_forms
