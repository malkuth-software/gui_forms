#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/controls/range_control/range_control.hpp"

#include <cstdint>
#include <span>

namespace gui_forms {

struct PresentationSettings;

enum class ProgressBarVisualStyle : std::uint8_t {
    blocks,
    continuous,
    marquee,
    pulse,
    luminance_pulse = pulse,
    marching_stripes,
    laser_etch,
};

enum class ProgressBarOverlayStyle : std::uint8_t {
    none,
    moving_stripes,
    marching_stripes = moving_stripes,
};

struct ProgressBarAnimationAppearance final {
    Color luminance_color{Color::rgba(255, 255, 255, 92)};
    Color stripe_color{Color::rgba(224, 241, 252, 116)};
    Color laser_phase_color{Color::rgba(130, 222, 247, 108)};
    Color laser_edge_color{Color::rgba(196, 246, 255, 226)};
    Color laser_spark_color{Color::rgba(238, 253, 255, 236)};
    double pulse_extent{0.32};
    double laser_edge_extent{13.0};
    double laser_phase_pitch{10.0};

    friend constexpr bool operator==(
        const ProgressBarAnimationAppearance& left,
        const ProgressBarAnimationAppearance& right) noexcept {
        return left.luminance_color == right.luminance_color &&
               left.stripe_color == right.stripe_color &&
               left.laser_phase_color == right.laser_phase_color &&
               left.laser_edge_color == right.laser_edge_color &&
               left.laser_spark_color == right.laser_spark_color &&
               left.pulse_extent == right.pulse_extent &&
               left.laser_edge_extent == right.laser_edge_extent &&
               left.laser_phase_pitch == right.laser_phase_pitch;
    }
};

class ProgressBar : public RangeControl {
public:
    explicit ProgressBar(StableId stable_id);

    [[nodiscard]] ProgressBarVisualStyle visual_style() const noexcept {
        return visual_style_;
    }
    void set_visual_style(ProgressBarVisualStyle style);
    [[nodiscard]] ProgressBarOverlayStyle overlay_style() const noexcept {
        return overlay_style_;
    }
    void set_overlay_style(ProgressBarOverlayStyle style);
    [[nodiscard]] double stripe_width() const noexcept { return stripe_width_; }
    void set_stripe_width(double width);
    [[nodiscard]] const ProgressBarAnimationAppearance& animation_appearance()
        const noexcept { return animation_appearance_; }
    void set_animation_appearance(ProgressBarAnimationAppearance appearance);
    [[nodiscard]] bool animation_enabled() const noexcept {
        return animation_enabled_;
    }
    void set_animation_enabled(bool enabled);
    [[nodiscard]] bool animation_paused() const noexcept {
        return motion_policy_.paused;
    }
    void set_animation_paused(bool paused);
    [[nodiscard]] bool reduced_motion() const noexcept {
        return motion_policy_.reduced;
    }
    void set_reduced_motion(bool reduced);
    void set_motion_policy(bool paused, bool reduced_motion);
    void set_motion_policy(MotionPolicy policy);
    [[nodiscard]] MotionPolicy motion_policy() const noexcept {
        return motion_policy_;
    }
    [[nodiscard]] MotionPolicy effective_motion_policy() const noexcept;
    [[nodiscard]] FrameInterval animation_period() const noexcept {
        return animation_period_;
    }
    void set_animation_period(FrameInterval period);
    [[nodiscard]] double animation_phase() const noexcept {
        return animation_phase_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_frame(FrameTime now) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry track;
        PartGeometry fill;
        PartGeometry activity;
    };
    struct States final {
        bool normal{};
        bool indeterminate{};
        bool enabled{};
        bool disabled{};
        bool reduced_motion{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The animation timer's named target. Observes the bar; a destroyed bar
    // is skipped.
    struct AnimationTick final {
        std::weak_ptr<ProgressBar> bar;
        AnimationTickResult operator()(FrameTime now) const;
    };

    void update_animation_registration();
    // One animation timer tick: advances the phase and damages what moved.
    AnimationTickResult on_animation_tick(FrameTime now);
    // The part of the bar that the animation phase changes: the activity
    // band (marquee), the pulse band (luminance pulse), or the fill.
    [[nodiscard]] Rect animated_extent() const noexcept;
    // The tick interval the bar's motion needs: about one device pixel of
    // its fastest moving part per tick, never faster than the display.
    [[nodiscard]] FrameInterval animation_interval() const noexcept;
    void on_presentation_changed(const PresentationSettings& settings);
    [[nodiscard]] bool animated_style() const noexcept;
    // The fill (determinate styles) or the moving activity band (marquee).
    [[nodiscard]] Rect indicator_bounds() const noexcept;

    ProgressBarVisualStyle visual_style_{ProgressBarVisualStyle::continuous};
    ProgressBarOverlayStyle overlay_style_{ProgressBarOverlayStyle::none};
    ProgressBarAnimationAppearance animation_appearance_{};
    double stripe_width_{7.0};
    // ADR-016: a paint-gated UI timer the bar stops, not an active surface.
    AnimationTimer animation_timer_;
    SubscriptionToken presentation_subscription_;
    FrameInterval animation_period_{std::chrono::milliseconds(1400)};
    FrameTime last_animation_frame_{};
    double animation_phase_{};
    bool animation_enabled_{true};
    MotionPolicy motion_policy_{};
};

} // namespace gui_forms
