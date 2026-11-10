#pragma once

#include "gui_forms/controls/instruments/gauge_base/gauge_base.hpp"

#include <cstddef>
#include <span>

namespace gui_forms {

// A dial gauge (W-5b; the instruments package): a 270-degree scale arc,
// clockwise from the lower left (the minimum) over twelve o'clock to the
// lower right (the maximum), lit from the minimum to the value, with a
// needle at the value, scale marks every tick_frequency outside the arc,
// the alarm ranges (the minimum to low_alarm, high_alarm to the maximum)
// as alarm segments on the arc, and the value as text below the centre
// while show_value is set. GaugeBase states the value, alarm and
// accessibility rules.
//
// Parts (design language: the dial roles, row 2.5, owner option (a)), all
// of the progress family over a round `plot` face: `track`, the whole
// scale, an `arc`; `alarm_arc`, the alarm ranges, arcs in the invalid
// state; `value_arc`, the reading, an arc in the reading's states
// (selected; invalid in alarm); `graticule`, the scale marks; `needle`, the
// pointer at the value; the caption is panel text. The theme sets the arcs'
// thickness and the needle's width and shape (stroke-width, needle-shape).
class RadialGauge final : public GaugeBase {
public:
    explicit RadialGauge(StableId stable_id);

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). The arcs and
    // the needle publish the square their circle is inscribed in;
    // `alarm_arc` is absent without alarm limits, `value_arc` at the
    // minimum, and `caption` while show_value is clear.
    struct Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry track;
        PartGeometry alarm_arc;
        PartGeometry value_arc;
        PartGeometry graticule;
        PartGeometry needle;
        PartGeometry caption;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The face and the circles in a control of `size`: `arc` the square the
    // scale arc is inscribed in, `needle` the needle's, and `mark_radius`
    // the circle the marks, `mark` units across, are centred on.
    struct Face final {
        Rect plot{};
        Point centre{};
        Rect arc{};
        Rect needle{};
        double mark_radius{0.0};
        double mark{0.0};
    };
    [[nodiscard]] Face face(Size size) const noexcept;
    // The square of a mark of side `side` at `ratio` along the sweep, on the
    // circle of `radius`.
    [[nodiscard]] static Rect mark_at(Point centre, double radius, double ratio, double side) noexcept;
    [[nodiscard]] Rect caption_bounds(const Face& placed) const;
};

} // namespace gui_forms
