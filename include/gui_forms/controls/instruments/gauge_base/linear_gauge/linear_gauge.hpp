#pragma once

#include "gui_forms/controls/instruments/gauge_base/gauge_base.hpp"

#include <span>

namespace gui_forms {

// A bar gauge (W-5b; the instruments package): the value filling a straight
// scale from the minimum, along RangeControl's orientation (left to right,
// or bottom to top), with scale marks every tick_frequency beside it, the
// alarm limits marked across it, and the value as text at its end (to the
// right of a horizontal bar, below a vertical one) while show_value is set.
// GaugeBase states the value, alarm and accessibility rules.
//
// Theme: `plot` is the bar's field (drawn as a well), `trace` the filled
// reading in the selected state (invalid while in alarm), `graticule` the
// band of scale marks, `indicator` the alarm marks and `caption` the value
// text, all of the progress family but the caption (panel).
class LinearGauge final : public GaugeBase {
public:
    explicit LinearGauge(StableId stable_id);

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `indicator`
    // spans the alarm marks and is absent without alarm limits; `caption`
    // is absent while show_value is clear.
    struct Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry trace;
        PartGeometry graticule;
        PartGeometry indicator;
        PartGeometry caption;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Where each part lies in a control of `size`; `ticks` is the band the
    // marks are drawn in, `trace` the filled part of `plot`.
    struct Placement final {
        Rect plot{};
        Rect trace{};
        Rect ticks{};
        Rect caption{};
        bool horizontal{true};
    };
    [[nodiscard]] Placement placement(Size size) const;
    // The rectangle of a mark across the bar at `ratio`: a tick in the band
    // (`across_band`) or an alarm mark over the field.
    [[nodiscard]] Rect mark_at(const Placement& placed, double ratio, bool across_band) const noexcept;
    [[nodiscard]] double caption_size() const noexcept;
};

} // namespace gui_forms
