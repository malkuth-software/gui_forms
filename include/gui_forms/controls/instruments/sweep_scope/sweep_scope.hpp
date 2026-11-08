#pragma once

// SweepScope: the instruments package's level scope (owner, 2026-11-03:
// gauges, knob, seven-segment readout and sparkline live in an "instruments"
// package next to the charts). A read-only range whose level is drawn as a
// trace on a graticule, with a sweep line crossing the trace at the
// instrument's 30 Hz. It is the registered control the Gallery's
// custom-control exhibit (gallery::Instrument) is modelled on; the sweep and
// the trace are an effect program, `scope_sweep` (sweep_scope_effects.hpp),
// bounded and verified by the gui_forms plugin's effect pass.

#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/controls/instruments/sweep_scope/sweep_scope_capabilities/sweep_scope_capabilities.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>

namespace gui_forms {

struct PresentationSettings;

class SweepScope : public RangeControl, public SweepScopeCapabilities<SweepScope> {
public:
    explicit SweepScope(StableId stable_id);

    // The sweep's effect instance: its counters and state checksum, for
    // diagnostics and tests.
    [[nodiscard]] const EffectInstance& effect_instance() const noexcept { return effect_instance_; }
    // The sweep's ambient frame (MotionClock::enlist_control_ambient): its
    // ticks due at `now`, and the plot invalidated; false when the scope was
    // not painted since its last frame.
    bool on_ambient_frame(FrameTime now) override;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    // Stage T1 anatomy, as dot members (see Control::Parts). The plot is the
    // well the trace is drawn in; the graticule its ruled lines; the
    // indicator the level's trace; the activity the band the sweep line
    // crosses (the plot: its phase is the effect's state).
    struct Parts final {
        PartGeometry region;
        PartGeometry caption;
        PartGeometry plot;
        PartGeometry graticule;
        PartGeometry indicator;
        PartGeometry activity;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool reduced_motion{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

    // The plot's bounds in the scope's local coordinates; the trace and the
    // sweep are drawn inside it, and a sweep frame damages it.
    [[nodiscard]] Rect plot_bounds() const noexcept;

    // The number of graticule lines across the plot.
    static constexpr int graticule_lines = 4;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The sweep_period property's table value, in seconds.
    [[nodiscard]] BindingValue sweep_period_property_value() const;
    void write_sweep_period_property(const BindingValue& value);
    void on_presentation_changed(const PresentationSettings& settings);
    // Whether the sweep moves: attached to a window, motion neither reduced
    // nor switched off.
    [[nodiscard]] bool sweep_moves() const noexcept;
    // Withdraws the sweep from the window's motion clock when it stops
    // moving; while it moves, a paint (`painting`) enlists it, its time
    // restarting, and anything else only updates an enlistment.
    void update_sweep_ambient(bool painting);
    // The caption's text: the caption and the level's percentage.
    [[nodiscard]] std::string caption_text() const;
    // The y of graticule line `line` (1 to graticule_lines) in local
    // coordinates.
    [[nodiscard]] double graticule_y(int line) const noexcept;

    SubscriptionToken presentation_subscription_;
    // The sweep; owns its state and is released with the scope.
    EffectInstance effect_instance_;
    // Paints of the scope, and the count its last ambient frame saw: a frame
    // finding them equal finds the scope no longer shown.
    std::uint64_t effect_paints_{};
    std::uint64_t effect_paints_seen_{~std::uint64_t{0}};
    // property_codemod begin SweepScope (tools/property_families/sweep_scope.json; generated, do not edit)
public:
    // SweepScope's property table: RangeControl's entries, then its own.
    static constexpr std::size_t sweep_scope_property_count = RangeControl::range_control_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, sweep_scope_property_count>& sweep_scope_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct SweepScopeCaptionCapability<SweepScope>;
    friend struct SweepScopePeriodCapability<SweepScope>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(caption)& cell, std::string& incoming);
    void written(const decltype(caption)& cell);
    void prepare_write(const decltype(sweep_period)& cell, FrameInterval& incoming);
    void written(const decltype(sweep_period)& cell);
    // property_codemod end SweepScope
};

} // namespace gui_forms
