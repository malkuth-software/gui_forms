#include "gui_forms/controls/radial_gauge.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The sweep, from -135 to +135 degrees about twelve o'clock.
constexpr double sweep_start = -0.75 * std::numbers::pi;
constexpr double sweep = 1.5 * std::numbers::pi;
// A scale mark is this share of the face's side.
constexpr double mark_share = 0.03;
// The needle reaches this share of the way to the arc's outer edge, short
// of the arc whatever thickness the theme gives it.
constexpr double needle_share = 0.78;
// The value text is this share of the face's side, within bounds, centred
// this share of the arc's radius below the centre.
constexpr double caption_share = 0.16;
constexpr double caption_minimum = 9.0;
constexpr double caption_maximum = 28.0;
constexpr double caption_drop = 0.45;

constexpr std::array<PartField<RadialGauge::Parts>, 8> radial_gauge_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RadialGauge::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &RadialGauge::Parts::plot},
    {.declaration = {.name = "track", .role = PartRole::arc, .composed_from = {}},
     .member = &RadialGauge::Parts::track},
    {.declaration = {.name = "alarm_arc", .role = PartRole::arc, .composed_from = {}},
     .member = &RadialGauge::Parts::alarm_arc},
    {.declaration = {.name = "value_arc", .role = PartRole::arc, .composed_from = {}},
     .member = &RadialGauge::Parts::value_arc},
    {.declaration = {.name = "graticule", .role = PartRole::graticule, .composed_from = {}},
     .member = &RadialGauge::Parts::graticule},
    {.declaration = {.name = "needle", .role = PartRole::needle, .composed_from = {}},
     .member = &RadialGauge::Parts::needle},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &RadialGauge::Parts::caption},
}};

constexpr std::array<PartDeclaration, 8> radial_gauge_parts = part_declarations(radial_gauge_part_fields);

constexpr std::array<AnatomyState, 5> radial_gauge_states{
    AnatomyState::normal, AnatomyState::selected, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::invalid,
};

constexpr ControlAnatomy radial_gauge_anatomy{
    .control_id = "radial_gauge",
    .parts = radial_gauge_parts,
    .states = radial_gauge_states,
    .custom_states = {},
};

Rect joined(Rect left, Rect right) noexcept {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const double x = std::min(left.x, right.x);
    const double y = std::min(left.y, right.y);
    const double right_edge = std::max(left.x + left.width, right.x + right.width);
    const double bottom = std::max(left.y + left.height, right.y + right.height);
    const Rect result{x, y, right_edge - x, bottom - y};
    return result;
}

double caption_font_size(double side) noexcept {
    const double size = std::clamp(side * caption_share, caption_minimum, caption_maximum);
    return size;
}

// The square of side twice `half` centred on `centre`.
Rect centred_square(Point centre, double half) noexcept {
    const Rect square{centre.x - half, centre.y - half, half * 2.0, half * 2.0};
    return square;
}

// The arc from `first` to `last` along the sweep, both ratios of the range.
PartArc arc_between(double first, double last) noexcept {
    const PartArc result{.start = sweep_start + sweep * first, .sweep = sweep * (last - first)};
    return result;
}

} // namespace

RadialGauge::RadialGauge(StableId stable_id) : GaugeBase(std::move(stable_id)) {}

RadialGauge::Face RadialGauge::face(Size size) const noexcept {
    Face placed;
    const double side = std::floor(std::min(size.width, size.height));
    if (!(side > 0.0)) return placed;
    placed.plot = Rect{std::floor((size.width - side) * 0.5), std::floor((size.height - side) * 0.5), side, side};
    placed.centre = Point{placed.plot.x + side * 0.5, placed.plot.y + side * 0.5};
    placed.mark = std::max(2.0, std::round(side * mark_share));
    // The marks ring the outside, a mark and a half in from the face's edge;
    // the arc lies inside them, a mark and a half further in.
    placed.mark_radius = std::max(0.0, side * 0.5 - placed.mark * 1.5);
    const double arc_half = std::max(0.0, placed.mark_radius - placed.mark * 1.5);
    placed.arc = centred_square(placed.centre, arc_half);
    placed.needle = centred_square(placed.centre, arc_half * needle_share);
    return placed;
}

Rect RadialGauge::mark_at(Point centre, double radius, double ratio, double side) noexcept {
    // Clockwise from twelve o'clock, in y-down coordinates.
    const double angle = sweep_start + sweep * ratio;
    const double x = centre.x + radius * std::sin(angle);
    const double y = centre.y - radius * std::cos(angle);
    const Rect square{x - side * 0.5, y - side * 0.5, side, side};
    return square;
}

Rect RadialGauge::caption_bounds(const Face& placed) const {
    if (!show_value.read() || placed.plot.empty()) return Rect{};
    const std::string shown = value_text();
    const Size text = measure_text_extent_utf8(shown, value_font(caption_font_size(placed.plot.width))).logical_size;
    const double radius = placed.arc.width * 0.5;
    const double width = std::min(std::ceil(text.width), radius * 2.0);
    const double height = std::ceil(text.height);
    const double middle = placed.centre.y + radius * caption_drop;
    const Rect bounds{std::floor(placed.centre.x - width * 0.5), std::floor(middle - height * 0.5), width, height};
    return bounds;
}

Size RadialGauge::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : 120.0;
    const double height = requested.height > 0.0 ? requested.height : 120.0;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void RadialGauge::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Face placed = face(Size{client.width, client.height});
    if (placed.plot.empty() || placed.arc.empty()) return;
    const AnatomyStateSet states = anatomy_states();
    const AnatomyStateSet lit_states = reading_states();
    const double reading = ratio_of(value.read());

    PartPaint plot{.role = PartRole::plot, .bounds = placed.plot, .states = states,
                   .family = ControlVisualRole::progress};
    plot.corner_radius = placed.plot.width * 0.5;
    plot.key = PartKey{"plot", {}, {}};
    paint_part(painter, *this, plot);

    PartPaint track{.role = PartRole::arc, .bounds = placed.arc, .states = states,
                    .family = ControlVisualRole::progress};
    track.arc = arc_between(0.0, 1.0);
    track.key = PartKey{"track", {}, {}};
    paint_part(painter, *this, track);

    // The alarm ranges lie on the track, under the reading.
    AnatomyStateSet alarm_states = states;
    alarm_states.insert(AnatomyState::invalid);
    PartPaint alarm{.role = PartRole::arc, .bounds = placed.arc, .states = alarm_states,
                    .family = ControlVisualRole::progress};
    alarm.key = PartKey{"alarm_arc", {}, {}};
    const std::optional<double> low = low_alarm.read();
    const std::optional<double> high = high_alarm.read();
    if (low && ratio_of(*low) > 0.0) {
        alarm.arc = arc_between(0.0, ratio_of(*low));
        paint_part(painter, *this, alarm);
    }
    if (high && ratio_of(*high) < 1.0) {
        alarm.arc = arc_between(ratio_of(*high), 1.0);
        paint_part(painter, *this, alarm);
    }

    if (reading > 0.0) {
        PartPaint lit{.role = PartRole::arc, .bounds = placed.arc, .states = lit_states,
                      .family = ControlVisualRole::progress};
        lit.arc = arc_between(0.0, reading);
        lit.key = PartKey{"value_arc", {}, {}};
        paint_part(painter, *this, lit);
    }

    PartPaint mark{.role = PartRole::graticule, .states = states, .family = ControlVisualRole::progress};
    mark.corner_radius = placed.mark * 0.5;
    mark.key = PartKey{"graticule", {}, {}};
    const std::vector<double> ratios = tick_ratios();
    for (const double ratio : ratios) {
        mark.bounds = mark_at(placed.centre, placed.mark_radius, ratio, placed.mark);
        paint_part(painter, *this, mark);
    }

    const Rect caption_box = caption_bounds(placed);
    if (!caption_box.empty()) {
        const std::string shown = value_text();
        PartPaint caption{.role = PartRole::caption, .bounds = caption_box, .states = states,
                          .family = ControlVisualRole::panel};
        caption.text = lend_paint_text(shown);
        caption.font = value_font(caption_font_size(placed.plot.width));
        caption.alignment = PartTextAlignment::center;
        paint_part(painter, *this, caption);
    }

    // The needle last, over the caption where they meet.
    PartPaint needle{.role = PartRole::needle, .bounds = placed.needle, .states = lit_states,
                     .family = ControlVisualRole::progress};
    needle.arc = PartArc{.start = sweep_start + sweep * reading, .sweep = 0.0};
    needle.key = PartKey{"needle", {}, {}};
    paint_part(painter, *this, needle);
}

const ControlAnatomy& RadialGauge::anatomy() const noexcept {
    return radial_gauge_anatomy;
}

void RadialGauge::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Face placed = face(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.plot = PartGeometry::from_bounds(placed.plot);
    if (!placed.arc.empty()) {
        record.track = PartGeometry::from_bounds(placed.arc);
        const std::optional<double> low = low_alarm.read();
        const std::optional<double> high = high_alarm.read();
        const bool low_shown = low && ratio_of(*low) > 0.0;
        const bool high_shown = high && ratio_of(*high) < 1.0;
        if (low_shown || high_shown) record.alarm_arc = PartGeometry::from_bounds(placed.arc);
        if (ratio_of(value.read()) > 0.0) record.value_arc = PartGeometry::from_bounds(placed.arc);
        Rect marks{};
        const std::vector<double> ratios = tick_ratios();
        for (const double ratio : ratios) {
            marks = joined(marks, mark_at(placed.centre, placed.mark_radius, ratio, placed.mark));
        }
        record.graticule = PartGeometry::from_bounds(marks);
        record.needle = PartGeometry::from_bounds(placed.needle);
    }
    record.caption = PartGeometry::from_bounds(caption_bounds(placed));
    write_parts(anatomy(), radial_gauge_part_fields, record, geometry);
}

RadialGauge::Parts RadialGauge::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), radial_gauge_part_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
