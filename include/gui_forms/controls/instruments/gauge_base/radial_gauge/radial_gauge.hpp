#pragma once

#include "gui_forms/controls/instruments/gauge_base/gauge_base.hpp"

#include <cstddef>
#include <span>

namespace gui_forms {

// A dial gauge (W-5b; the instruments package): the value lighting a
// 270-degree scale of dots, clockwise from the lower left (the minimum) over
// twelve o'clock to the lower right (the maximum), with an indicator dot at
// the value, scale marks every tick_frequency outside the scale, the alarm
// limits marked in the warning colours, and the value as text in the centre
// while show_value is set. GaugeBase states the value, alarm and
// accessibility rules.
//
// The design language has no arc or needle vocabulary yet (an owner
// decision), so the scale is drawn as dots, as ProgressRing's is: lit dots
// are `trace` (selected; invalid in alarm), unlit dots `track`, scale marks
// `graticule`, the value dot and alarm marks `indicator`, over a round
// `plot` face, all of the progress family; the caption is panel text.
class RadialGauge final : public GaugeBase {
public:
    explicit RadialGauge(StableId stable_id);

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `trace` spans
    // the lit dots and `indicator` the value dot; `caption` is absent while
    // show_value is clear.
    struct Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry track;
        PartGeometry trace;
        PartGeometry graticule;
        PartGeometry indicator;
        PartGeometry caption;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The face and the circles the scale and the marks lie on, in a control
    // of `size`. `radius` is the scale dots' centre circle, `mark_radius`
    // the marks'.
    struct Face final {
        Rect plot{};
        Point centre{};
        double radius{0.0};
        double mark_radius{0.0};
        double dot{0.0};
        double mark{0.0};
    };
    [[nodiscard]] Face face(Size size) const noexcept;
    // The square of a dot of side `side` at `ratio` along the sweep, on the
    // circle of `radius`.
    [[nodiscard]] static Rect dot_at(Point centre, double radius, double ratio, double side) noexcept;
    // How many scale dots are lit for the value.
    [[nodiscard]] std::size_t lit_dots() const noexcept;
    [[nodiscard]] Rect caption_bounds(const Face& placed) const;
};

} // namespace gui_forms
