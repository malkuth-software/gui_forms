#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/progress_ring_capabilities.hpp"
#include "gui_forms/controls/progress_ring_values.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms {

// A circular progress indicator (W-4): a ring of forty dots. Determinate,
// the dots from twelve o'clock clockwise are lit in proportion to value
// over maximum (clamped to the ring). Indeterminate, a run of ten lit dots
// turns once every 1.2 seconds instead. The motion is an ambient control on
// the window's motion clock (MotionClock::enlist_control_ambient), only
// while indeterminate and drawn; with ambient motion suppressed (reduced
// motion, ambient motion off, an occluded window) the run stands still at
// twelve o'clock. A determinate ring is off the clock. It takes no input
// and no focus.
//
// Accessibility: a progress bar; determinate, its value, minimum (0) and
// maximum, with the value as text; indeterminate, no value and the busy
// state. Named by its accessible name.
//
// Theme: its dots are the progress family's `track` and, lit, its `fill`
// (determinate) or `activity` (indeterminate) part with the selected state
// (src/controls/progress_ring/ring_dots.hpp), as ProgressBar's track and
// fill, so the floor sheet's :family(progress) blocks style it. The custom
// state `indeterminate` selects the turning ring.
//
// Continuous arc (claude/geometry-1; owner decision 3 of the 2026-11-16
// capability review): with ring_style `arc`, or `theme` (the default) under
// a theme whose rules say `ring-style: arc` for the control
// (RecipeField::ring_style), the ring is the progress family's `arc_track`
// part, a full circle, under its `value_arc` part with the selected state,
// both of the arc role (the engine strokes them as a trimmed circle, so the
// core cuts the arc for every painter). Determinate, the value arc runs
// clockwise from twelve o'clock over fraction() of the turn (exactly, not in
// dots); indeterminate, an arc of run_count / dot_count of the turn turns
// with the same steps and clock as the dots. Every built-in theme draws
// dots, so the default look is unchanged.
class GUI_FORMS_API ProgressRing final : public Control, public ProgressRingCapabilities<ProgressRing> {
public:
    explicit ProgressRing(StableId stable_id);

    static constexpr std::size_t dot_count = 40U;
    // The turning run while indeterminate.
    static constexpr std::size_t run_count = 10U;
    static constexpr FrameInterval turn_period = std::chrono::milliseconds(1200);

    // value over maximum, clamped to [0, 1].
    [[nodiscard]] double fraction() const noexcept;
    // The dots a determinate ring lights: fraction() of dot_count, rounded
    // to the nearest dot (a positive fraction lights at least one).
    [[nodiscard]] std::size_t lit_dots() const noexcept;
    // The first dot of the turning run (0 while still or determinate).
    [[nodiscard]] std::size_t phase() const noexcept { return phase_; }
    // Whether the run is turning: on the window's motion clock.
    [[nodiscard]] bool moving() const noexcept;
    // Whether the ring is drawn as a continuous arc: ring_style `arc`, or
    // `theme` with the theme's ring-style `arc` for this control.
    [[nodiscard]] bool draws_arc() const;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). Drawn as
    // dots, `track` is the ring's square; `fill` the same square while
    // determinate with a positive fraction, `activity` while
    // indeterminate. Drawn as an arc, `arc_track` is the ring's square and
    // `value_arc` the same square while determinate with a positive
    // fraction or indeterminate; the dot parts are then absent.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry track;
        PartGeometry fill;
        PartGeometry activity;
        PartGeometry arc_track;
        PartGeometry value_arc;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;
    // One ambient frame: the ring moves to the step `now` gives.
    bool on_ambient_frame(FrameTime now) override;

private:
    // Whether the ring turns now: indeterminate, in a window whose ambient motion is
    // not suppressed (reduced motion, ambient motion off, occluded).
    [[nodiscard]] bool wants_motion() const noexcept;
    // Enlists the control on the window's motion clock, or withdraws it,
    // for wants_motion(); `painting` is true from on_paint, the only place
    // that enlists it (a control not drawn is not kept on the clock). A
    // withdrawn ring returns to phase 0.
    void update_motion(bool painting);
    // The square the ring is drawn in: the client area's centred square.
    [[nodiscard]] Rect ring_square() const noexcept;
    // Paints the continuous arc (draws_arc) in `square`.
    void paint_arc_ring(Painter& painter, Rect square) const;

    // The clock time phase 0 began at, the paints counted, and the count
    // the last ambient frame saw (a frame with no paint since is dropped).
    FrameTime motion_start_{};
    std::uint64_t paints_{};
    std::uint64_t paints_seen_{};
    std::size_t phase_{};
    // property_codemod begin ProgressRing (tools/property_families/progress_ring.json; generated, do not edit)
public:
    // ProgressRing's property table: Control's entries, then its own.
    static constexpr std::size_t progress_ring_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, progress_ring_property_count>& progress_ring_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ProgressRingValueCapability<ProgressRing>;
    friend struct ProgressRingMaximumCapability<ProgressRing>;
    friend struct ProgressRingIndeterminateCapability<ProgressRing>;
    friend struct ProgressRingStyleCapability<ProgressRing>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(value)& cell, double& incoming);
    void written(const decltype(value)& cell);
    void prepare_write(const decltype(maximum)& cell, double& incoming);
    void written(const decltype(maximum)& cell);
    void prepare_write(const decltype(indeterminate)& cell, bool& incoming);
    void written(const decltype(indeterminate)& cell);
    void prepare_write(const decltype(ring_style)& cell, ProgressRingStyle& incoming);
    void written(const decltype(ring_style)& cell);
    // property_codemod end ProgressRing
};

} // namespace gui_forms
