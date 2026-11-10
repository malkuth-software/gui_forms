#pragma once

#include "gui_forms/controls/instruments/gauge_base/gauge_base.hpp"

#include <span>

namespace gui_forms {

// A bar gauge (W-5b; the instruments package): the value filling a straight
// scale from the minimum, along RangeControl's orientation (left to right,
// or bottom to top), with scale marks every tick_frequency beside it, the
// alarm ranges (the minimum to low_alarm, high_alarm to the maximum) as
// alarm segments of the field, and the value as text at its end (to the
// right of a horizontal bar, below a vertical one) while show_value is set.
// GaugeBase states the value, alarm and accessibility rules.
//
// Theme: `plot` is the bar's field (drawn as a well), `alarm_zone` the
// alarm segments, the straight counterpart of RadialGauge's alarm arc
// (a track in the invalid state, over the field and under the reading),
// `trace` the filled reading in the selected state (invalid while in
// alarm), `graticule` the band of scale marks and `caption` the value text,
// all of the progress family but the caption (panel).
class LinearGauge final : public GaugeBase {
public:
    explicit LinearGauge(StableId stable_id);

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `alarm_zone`
    // spans the alarm segments and is absent without alarm limits inside
    // the range; `caption` is absent while show_value is clear.
    struct Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry alarm_zone;
        PartGeometry trace;
        PartGeometry graticule;
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
    // The rectangle of a scale mark across the tick band at `ratio`.
    [[nodiscard]] Rect mark_at(const Placement& placed, double ratio) const noexcept;
    // The part of the field from `first` to `last`, ratios of the range.
    [[nodiscard]] static Rect zone_between(const Placement& placed, double first, double last) noexcept;
    [[nodiscard]] double caption_size() const noexcept;
};

} // namespace gui_forms
