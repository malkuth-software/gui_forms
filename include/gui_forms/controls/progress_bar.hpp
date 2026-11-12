#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/controls/range_control.hpp"
#include "gui_forms/controls/progress_bar_values.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/controls/progress_bar_capabilities.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace gui_forms {

struct PresentationSettings;

class ProgressBar : public RangeControl, public ProgressBarCapabilities<ProgressBar> {
public:
    explicit ProgressBar(StableId stable_id);

    // motion_policy (a capability view) combined with the window's
    // reduced-motion setting: what the bar's animation obeys.
    [[nodiscard]] MotionPolicy effective_motion_policy() const noexcept;
    [[nodiscard]] double animation_phase() const noexcept {
        return animation_phase_;
    }

    // Advances the indeterminate or marquee phase, and the effect
    // programs' time, to `now`; the animation timer calls it on each tick,
    // and tests call it to step the bar.
    void advance_animation(FrameTime now);

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
    // The fill part's stripe properties (the barber pole's theme knobs) and
    // the fill's corner radius, from the theme's rules for the part; unset
    // and 0 for a bar in no window.
    struct FillStripes final {
        ControlStripePattern pattern{};
        double corner_radius{};
    };
    [[nodiscard]] FillStripes fill_stripes() const;
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
    // The effect_seed property's table value: a nullable unsigned integer.
    [[nodiscard]] BindingValue effect_seed_property_value() const;
    void write_effect_seed_property(const BindingValue& value);
    // The animation appearance's checks (std::invalid_argument).
    void validate_animation_appearance(const ProgressBarAnimationAppearance& appearance) const;
    // What a visual style or overlay change does after the store.
    void restyle();

    // ADR-016: a paint-gated UI timer the bar stops.
    AnimationTimer animation_timer_;
    SubscriptionToken presentation_subscription_;
    FrameTime last_animation_frame_{};
    double animation_phase_{};
    MotionPolicy motion_policy_{};
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
    // property_codemod begin ProgressBar (tools/property_families/progress_bar.json; generated, do not edit)
public:
    // ProgressBar's property table: RangeControl's entries, then its own.
    static constexpr std::size_t progress_bar_property_count = RangeControl::range_control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, progress_bar_property_count>& progress_bar_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ProgressBarVisualStyleChoiceCapability<ProgressBar>;
    friend struct ProgressBarOverlayStyleChoiceCapability<ProgressBar>;
    friend struct ProgressBarStripeWidthCapability<ProgressBar>;
    friend struct ProgressBarAppearanceCapability<ProgressBar>;
    friend struct ProgressBarAnimationEnabledCapability<ProgressBar>;
    friend struct ProgressBarAnimationPeriodCapability<ProgressBar>;
    friend struct ProgressBarEffectCapability<ProgressBar>;
    friend struct ProgressBarEffectSeedCapability<ProgressBar>;
    friend struct ProgressBarEffectInputCapability<ProgressBar>;
    friend struct ProgressBarMotionPolicyCapability<ProgressBar>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(visual_style)& cell, ProgressBarVisualStyle& incoming);
    void written(const decltype(visual_style)& cell);
    void prepare_write(const decltype(overlay_style)& cell, ProgressBarOverlayStyle& incoming);
    void written(const decltype(overlay_style)& cell);
    void prepare_write(const decltype(stripe_width)& cell, double& incoming);
    void written(const decltype(stripe_width)& cell);
    void prepare_write(const decltype(animation_appearance)& cell, ProgressBarAnimationAppearance& incoming);
    void written(const decltype(animation_appearance)& cell);
    void prepare_write(const decltype(animation_enabled)& cell, bool& incoming);
    void written(const decltype(animation_enabled)& cell);
    void prepare_write(const decltype(animation_period)& cell, FrameInterval& incoming);
    void written(const decltype(animation_period)& cell);
    void prepare_write(const decltype(effect)& cell, std::string& incoming);
    void written(const decltype(effect)& cell);
    void prepare_write(const decltype(effect_seed)& cell, std::optional<std::uint64_t>& incoming);
    void written(const decltype(effect_seed)& cell);
    void prepare_write(const decltype(effect_input)& cell, EffectInputKind& incoming);
    void written(const decltype(effect_input)& cell);
    void write_view(const decltype(motion_policy)& cell, MotionPolicy incoming);
    [[nodiscard]] MotionPolicy read_view(const decltype(motion_policy)& cell) const;
    // property_codemod end ProgressBar
};

} // namespace gui_forms
