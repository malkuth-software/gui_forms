#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

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

    // Advances the indeterminate or marquee phase, and the effect
    // programs' time, to `now`; the animation timer calls it on each tick,
    // and tests call it to step the bar.
    void advance_animation(FrameTime now);

    // The effect program drawn over the bar (effect programs, design note
    // §7), by registered name. Empty (the default) selects the visual
    // style's own: progress_stripes for marching_stripes, progress_pulse
    // for pulse, progress_laser for laser_etch; for the other styles, the
    // effect the theme declares for the bar's fill part (its activity part
    // while marquee), if any (effect_property). A name no registered
    // progress-bar effect has, or one the load checks refuse, draws no
    // effect (effect_selection says why). The moving-stripes overlay draws
    // progress_stripes over any other effect.
    [[nodiscard]] const std::string& effect() const noexcept { return effect_; }
    void set_effect(std::string name);
    // The seed of the bar's effects. None (the default) derives it from the
    // bar's stable ID and the effect's name, so runs, tests and motion
    // checksums reproduce; an application sets one for another arrangement.
    [[nodiscard]] std::optional<std::uint64_t> effect_seed() const noexcept { return effect_seed_; }
    void set_effect_seed(std::optional<std::uint64_t> seed);
    // The input the bar grants its effect (none by default): with a grant
    // the bar is hit-tested and passes the pointer, in part-local units, to
    // an effect that declares pointer input. The bar never takes focus, so
    // a key grant delivers nothing.
    [[nodiscard]] EffectInputKind effect_input() const noexcept { return effect_input_; }
    void set_effect_input(EffectInputKind input);
    // What the bar's last effect selection came to: the name asked for,
    // who asked (the application, the visual style or the theme) and the
    // load checks' verdict.
    [[nodiscard]] const EffectSelection& effect_selection() const noexcept { return effect_selection_; }
    // The colours the bar's effects draw with (EffectFrame::colours): the
    // primary and secondary colour roles the theme declares for the effect's
    // part (effect_primary_property, effect_secondary_property), in the
    // window's colour scheme and remapped by the forced high-contrast pass;
    // the animation appearance's stripe and luminance colours where the
    // theme declares none, or where the application set its own appearance.
    // Under theme roles the appearance's colours keep their alpha and take
    // the roles' red, green and blue: the stripes and the laser's phase
    // bands the primary, the luminance band, the laser's edge and spark the
    // secondary.
    [[nodiscard]] EffectColours effect_colours() const noexcept { return effect_colours_; }
    // The selected effect's instance (empty for none): its counters and
    // state checksum, for diagnostics and tests.
    [[nodiscard]] const EffectInstance& effect_instance() const noexcept { return effect_instance_; }
    // The selected effect's outsets, so damage and replay cover what it
    // draws outside the bar (steam rising above it).
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    // The effects' ambient frame (MotionClock::enlist_control_ambient):
    // their ticks due at `now`, and their extent invalidated; false when
    // the bar was not painted since its last frame.
    bool on_ambient_frame(FrameTime now) override;
    void on_pointer(PointerEvent& event) override;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
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
    // The effect asked for, and who asked: the name borrows effect_, a
    // static name or the window theme's compiled rules, and is used before
    // any of them can change.
    struct RequestedEffect final {
        std::string_view name{};
        EffectSelectionSource source{EffectSelectionSource::none};
        // The theme's colour roles for the effect's part, resolved, and
        // whether it declares both.
        EffectColours colours{};
        bool themed_colours{};
    };
    // The effect the application, the visual style or the theme asks for,
    // in that order, or none; and the theme's colours for its part.
    [[nodiscard]] RequestedEffect requested_effect() const noexcept;
    // Attaches the effects the style and the effect name select, when they
    // changed, their time starting at `now`; detaches those no longer
    // selected.
    void sync_effects(FrameTime now);
    // Whether the effects move: some attached, animation on, motion
    // neither paused nor reduced.
    [[nodiscard]] bool effects_move() const noexcept;
    // Withdraws the effects from the window's motion clock when they stop
    // moving. While they move, a paint (`painting`) enlists them, their
    // time restarting when they start, and anything else only updates an
    // enlistment: as with the engine's parts, only a bar that is drawn asks
    // for frames.
    void update_effect_ambient(bool painting);
    // The effect and effect_seed properties' values (effect programs, design
    // note section 7): text, and a nullable unsigned integer.
    [[nodiscard]] BindingValue effect_property_value() const;
    void set_effect_property_value(const BindingValue& value);
    [[nodiscard]] BindingValue effect_seed_property_value() const;
    void set_effect_seed_property_value(const BindingValue& value);

    ProgressBarVisualStyle visual_style_{ProgressBarVisualStyle::continuous};
    ProgressBarOverlayStyle overlay_style_{ProgressBarOverlayStyle::none};
    ProgressBarAnimationAppearance animation_appearance_{};
    double stripe_width_{7.0};
    // ADR-016: a paint-gated UI timer the bar stops.
    AnimationTimer animation_timer_;
    SubscriptionToken presentation_subscription_;
    FrameInterval animation_period_{std::chrono::milliseconds(1400)};
    FrameTime last_animation_frame_{};
    double animation_phase_{};
    bool animation_enabled_{true};
    MotionPolicy motion_policy_{};
    std::string effect_{};
    std::optional<std::uint64_t> effect_seed_{};
    EffectInputKind effect_input_{EffectInputKind::none};
    // The selected effect, and the moving-stripes overlay's stripes. Each
    // owns its state; both are released with the bar.
    EffectInstance effect_instance_;
    EffectInstance overlay_instance_;
    // The main effect's last selection, kept for effect_selection(); a
    // change to a refusal is noted once (note_effect_refused).
    EffectSelection effect_selection_{};
    // The colours the effects were last given (effect_colours), and whether
    // they are the theme's roles.
    EffectColours effect_colours_{};
    bool themed_colours_{};
    // Paints of the bar, and the count its last ambient frame saw: a frame
    // finding them equal finds the bar no longer shown.
    std::uint64_t effect_paints_{};
    std::uint64_t effect_paints_seen_{~std::uint64_t{0}};
};

} // namespace gui_forms
