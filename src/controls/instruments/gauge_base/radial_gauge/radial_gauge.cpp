#include "gui_forms/controls/instruments/gauge_base/radial_gauge/radial_gauge.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The scale: this many dots over the sweep, from -135 to +135 degrees about
// twelve o'clock.
constexpr std::size_t scale_dots = 41U;
constexpr double sweep_start = -0.75 * std::numbers::pi;
constexpr double sweep = 1.5 * std::numbers::pi;
// A scale dot is this share of the face's side; a mark half of that.
constexpr double dot_share = 0.06;
constexpr double mark_share = 0.5;
// The value text is this share of the face's side, within bounds.
constexpr double caption_share = 0.16;
constexpr double caption_minimum = 9.0;
constexpr double caption_maximum = 28.0;
// The value dot is this many scale dots across.
constexpr double pointer_scale = 1.6;

constexpr std::array<PartField<RadialGauge::Parts>, 7> radial_gauge_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RadialGauge::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &RadialGauge::Parts::plot},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &RadialGauge::Parts::track},
    {.declaration = {.name = "trace", .role = PartRole::trace, .composed_from = {}},
     .member = &RadialGauge::Parts::trace},
    {.declaration = {.name = "graticule", .role = PartRole::graticule, .composed_from = {}},
     .member = &RadialGauge::Parts::graticule},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &RadialGauge::Parts::indicator},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &RadialGauge::Parts::caption},
}};

constexpr std::array<PartDeclaration, 7> radial_gauge_parts = part_declarations(radial_gauge_part_fields);

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

double dot_ratio(std::size_t index) noexcept {
    const double ratio = static_cast<double>(index) / static_cast<double>(scale_dots - 1U);
    return ratio;
}

double caption_font_size(double side) noexcept {
    const double size = std::clamp(side * caption_share, caption_minimum, caption_maximum);
    return size;
}

} // namespace

RadialGauge::RadialGauge(StableId stable_id) : GaugeBase(std::move(stable_id)) {}

RadialGauge::Face RadialGauge::face(Size size) const noexcept {
    Face placed;
    const double side = std::floor(std::min(size.width, size.height));
    if (!(side > 0.0)) return placed;
    placed.plot = Rect{std::floor((size.width - side) * 0.5), std::floor((size.height - side) * 0.5), side, side};
    placed.centre = Point{placed.plot.x + side * 0.5, placed.plot.y + side * 0.5};
    placed.dot = std::max(2.0, std::round(side * dot_share));
    placed.mark = std::max(1.0, std::round(placed.dot * mark_share));
    // The marks ring the outside, a mark and a half in from the face's edge;
    // the scale lies a mark and a dot further in.
    placed.mark_radius = std::max(0.0, side * 0.5 - placed.mark * 1.5);
    placed.radius = std::max(0.0, placed.mark_radius - placed.mark - placed.dot);
    return placed;
}

Rect RadialGauge::dot_at(Point centre, double radius, double ratio, double side) noexcept {
    // Clockwise from twelve o'clock, in y-down coordinates.
    const double angle = sweep_start + sweep * ratio;
    const double x = centre.x + radius * std::sin(angle);
    const double y = centre.y - radius * std::cos(angle);
    const Rect square{x - side * 0.5, y - side * 0.5, side, side};
    return square;
}

std::size_t RadialGauge::lit_dots() const noexcept {
    const double ratio = ratio_of(value.read());
    // The dot at the minimum is always lit: the reading starts there.
    const double lit = std::floor(ratio * static_cast<double>(scale_dots - 1U) + 0.5) + 1.0;
    const std::size_t count = static_cast<std::size_t>(std::clamp(lit, 1.0, static_cast<double>(scale_dots)));
    return count;
}

Rect RadialGauge::caption_bounds(const Face& placed) const {
    if (!show_value.read() || placed.plot.empty()) return Rect{};
    const std::string shown = value_text();
    const Size text = resolve_text_layout_utf8(shown, value_font(caption_font_size(placed.plot.width))).logical_size;
    const double width = std::min(std::ceil(text.width), placed.radius * 2.0);
    const double height = std::ceil(text.height);
    const Rect bounds{std::floor(placed.centre.x - width * 0.5), std::floor(placed.centre.y - height * 0.5), width,
                      height};
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
    if (placed.plot.empty() || !(placed.radius > 0.0)) return;
    const AnatomyStateSet states = anatomy_states();
    const AnatomyStateSet lit_states = reading_states();

    PartPaint plot{.role = PartRole::plot, .bounds = placed.plot, .states = states,
                   .family = ControlVisualRole::progress};
    plot.corner_radius = placed.plot.width * 0.5;
    plot.key = PartKey{"plot", {}, {}};
    paint_part(painter, *this, plot);

    const std::size_t lit = lit_dots();
    PartPaint dot{.states = states, .family = ControlVisualRole::progress};
    dot.corner_radius = placed.dot * 0.5;
    for (std::size_t index = 0U; index < scale_dots; ++index) {
        const bool is_lit = index < lit;
        dot.role = is_lit ? PartRole::trace : PartRole::track;
        dot.key = PartKey{is_lit ? std::string_view{"trace"} : std::string_view{"track"}, {}, {}};
        dot.states = is_lit ? lit_states : states;
        dot.bounds = dot_at(placed.centre, placed.radius, dot_ratio(index), placed.dot);
        paint_part(painter, *this, dot);
    }

    PartPaint mark{.role = PartRole::graticule, .states = states, .family = ControlVisualRole::progress};
    mark.corner_radius = placed.mark * 0.5;
    const std::vector<double> ratios = tick_ratios();
    for (const double ratio : ratios) {
        mark.bounds = dot_at(placed.centre, placed.mark_radius, ratio, placed.mark);
        paint_part(painter, *this, mark);
    }

    AnatomyStateSet alarm_states = states;
    alarm_states.insert(AnatomyState::invalid);
    PartPaint alarm{.role = PartRole::indicator, .states = alarm_states, .family = ControlVisualRole::progress};
    alarm.corner_radius = placed.mark;
    const std::array<std::optional<double>, 2> limits{low_alarm.read(), high_alarm.read()};
    for (const std::optional<double>& limit : limits) {
        if (!limit) continue;
        alarm.bounds = dot_at(placed.centre, placed.mark_radius, ratio_of(*limit), placed.mark * 2.0);
        paint_part(painter, *this, alarm);
    }

    const double pointer_side = placed.dot * pointer_scale;
    PartPaint pointer{.role = PartRole::indicator,
                      .bounds = dot_at(placed.centre, placed.radius, ratio_of(value.read()), pointer_side),
                      .states = lit_states,
                      .family = ControlVisualRole::progress};
    pointer.corner_radius = pointer_side * 0.5;
    pointer.key = PartKey{"indicator", {}, {}};
    paint_part(painter, *this, pointer);

    const Rect caption_box = caption_bounds(placed);
    if (!caption_box.empty()) {
        const std::string shown = value_text();
        PartPaint caption{.role = PartRole::caption, .bounds = caption_box, .states = states,
                          .family = ControlVisualRole::panel};
        caption.text = shown;
        caption.font = value_font(caption_font_size(placed.plot.width));
        caption.alignment = PartTextAlignment::center;
        paint_part(painter, *this, caption);
    }
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
    if (placed.radius > 0.0) {
        const std::size_t lit = lit_dots();
        Rect track{};
        Rect trace{};
        for (std::size_t index = 0U; index < scale_dots; ++index) {
            const Rect square = dot_at(placed.centre, placed.radius, dot_ratio(index), placed.dot);
            if (index < lit) {
                trace = joined(trace, square);
            } else {
                track = joined(track, square);
            }
        }
        record.track = PartGeometry::from_bounds(track);
        record.trace = PartGeometry::from_bounds(trace);
        Rect marks{};
        const std::vector<double> ratios = tick_ratios();
        for (const double ratio : ratios) {
            marks = joined(marks, dot_at(placed.centre, placed.mark_radius, ratio, placed.mark));
        }
        record.graticule = PartGeometry::from_bounds(marks);
        const double pointer_side = placed.dot * pointer_scale;
        record.indicator =
            PartGeometry::from_bounds(dot_at(placed.centre, placed.radius, ratio_of(value.read()), pointer_side));
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
