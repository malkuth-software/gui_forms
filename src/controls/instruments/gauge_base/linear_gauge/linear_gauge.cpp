#include "gui_forms/controls/instruments/gauge_base/linear_gauge/linear_gauge.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// Metrics in logical units: the bar's thickness, the tick band's depth and
// its gap from the bar, a mark's width, the gap before the value text and
// the text's size.
constexpr double bar_thickness = 12.0;
constexpr double tick_depth = 4.0;
constexpr double tick_gap = 2.0;
constexpr double mark_width = 1.0;
constexpr double alarm_width = 2.0;
constexpr double caption_gap = 6.0;
constexpr double caption_font_size = 11.0;

constexpr std::array<PartField<LinearGauge::Parts>, 6> linear_gauge_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &LinearGauge::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &LinearGauge::Parts::plot},
    {.declaration = {.name = "trace", .role = PartRole::trace, .composed_from = {}},
     .member = &LinearGauge::Parts::trace},
    {.declaration = {.name = "graticule", .role = PartRole::graticule, .composed_from = {}},
     .member = &LinearGauge::Parts::graticule},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &LinearGauge::Parts::indicator},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &LinearGauge::Parts::caption},
}};

constexpr std::array<PartDeclaration, 6> linear_gauge_parts = part_declarations(linear_gauge_part_fields);

constexpr std::array<AnatomyState, 5> linear_gauge_states{
    AnatomyState::normal, AnatomyState::selected, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::invalid,
};

constexpr ControlAnatomy linear_gauge_anatomy{
    .control_id = "linear_gauge",
    .parts = linear_gauge_parts,
    .states = linear_gauge_states,
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

} // namespace

LinearGauge::LinearGauge(StableId stable_id) : GaugeBase(std::move(stable_id)) {}

double LinearGauge::caption_size() const noexcept {
    const double size = caption_font_size;
    return size;
}

LinearGauge::Placement LinearGauge::placement(Size size) const {
    Placement placed;
    placed.horizontal = orientation.read() == Orientation::horizontal;
    Size text{};
    if (show_value.read()) {
        const std::string shown = value_text();
        text = resolve_text_layout_utf8(shown, value_font(caption_size())).logical_size;
        text = Size{std::ceil(text.width), std::ceil(text.height)};
    }
    const double band = tick_ratios().empty() ? 0.0 : tick_gap + tick_depth;
    if (placed.horizontal) {
        const double caption_width = text.width > 0.0 ? text.width + caption_gap : 0.0;
        const double length = std::max(0.0, size.width - caption_width);
        const double thickness = std::min(bar_thickness, std::max(0.0, size.height - band));
        const double top = std::floor(std::max(0.0, (size.height - thickness - band) * 0.5));
        placed.plot = Rect{0.0, top, length, thickness};
        const double filled = std::round(length * ratio_of(value.read()));
        placed.trace = Rect{0.0, top, filled, thickness};
        if (band > 0.0) placed.ticks = Rect{0.0, top + thickness + tick_gap, length, tick_depth};
        if (text.width > 0.0) {
            placed.caption = Rect{length + caption_gap, std::floor((size.height - text.height) * 0.5), text.width,
                                  text.height};
        }
        return placed;
    }
    const double caption_height = text.height > 0.0 ? text.height + caption_gap : 0.0;
    const double length = std::max(0.0, size.height - caption_height);
    const double thickness = std::min(bar_thickness, std::max(0.0, size.width - band));
    const double left = std::floor(std::max(0.0, (size.width - thickness - band) * 0.5));
    placed.plot = Rect{left, 0.0, thickness, length};
    const double filled = std::round(length * ratio_of(value.read()));
    placed.trace = Rect{left, length - filled, thickness, filled};
    if (band > 0.0) placed.ticks = Rect{left + thickness + tick_gap, 0.0, tick_depth, length};
    if (text.height > 0.0) {
        placed.caption = Rect{std::floor((size.width - text.width) * 0.5), length + caption_gap, text.width,
                              text.height};
    }
    return placed;
}

Rect LinearGauge::mark_at(const Placement& placed, double ratio, bool across_band) const noexcept {
    const Rect along = across_band ? placed.ticks : placed.plot;
    const double width = across_band ? mark_width : alarm_width;
    // An alarm mark reaches a unit past the field on both sides.
    const double reach = across_band ? 0.0 : 1.0;
    if (placed.horizontal) {
        const double x = std::clamp(along.x + along.width * ratio - width * 0.5, along.x,
                                    along.x + std::max(0.0, along.width - width));
        const Rect mark{x, along.y - reach, width, along.height + reach * 2.0};
        return mark;
    }
    const double y = std::clamp(along.y + along.height * (1.0 - ratio) - width * 0.5, along.y,
                                along.y + std::max(0.0, along.height - width));
    const Rect mark{along.x - reach, y, along.width + reach * 2.0, width};
    return mark;
}

Size LinearGauge::measure(Size available) {
    const Rect requested = requested_bounds();
    const bool horizontal = orientation.read() == Orientation::horizontal;
    const Size natural = horizontal ? Size{200.0, 28.0} : Size{48.0, 160.0};
    const double width = requested.width > 0.0 ? requested.width : natural.width;
    const double height = requested.height > 0.0 ? requested.height : natural.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void LinearGauge::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    PartPaint plot{.role = PartRole::plot, .bounds = placed.plot, .states = states,
                   .family = ControlVisualRole::progress};
    plot.key = PartKey{"plot", {}, {}};
    paint_part(painter, *this, plot);
    PartPaint trace{.role = PartRole::trace, .bounds = placed.trace, .states = reading_states(),
                    .family = ControlVisualRole::progress};
    trace.key = PartKey{"trace", {}, {}};
    paint_part(painter, *this, trace);

    PartPaint tick{.role = PartRole::graticule, .states = states, .family = ControlVisualRole::progress};
    const std::vector<double> ratios = tick_ratios();
    for (const double ratio : ratios) {
        tick.bounds = mark_at(placed, ratio, true);
        paint_part(painter, *this, tick);
    }

    AnatomyStateSet alarm_states = states;
    alarm_states.insert(AnatomyState::invalid);
    PartPaint alarm{.role = PartRole::indicator, .states = alarm_states, .family = ControlVisualRole::progress};
    const std::array<std::optional<double>, 2> limits{low_alarm.read(), high_alarm.read()};
    for (const std::optional<double>& limit : limits) {
        if (!limit) continue;
        alarm.bounds = mark_at(placed, ratio_of(*limit), false);
        paint_part(painter, *this, alarm);
    }

    if (!placed.caption.empty()) {
        const std::string shown = value_text();
        PartPaint caption{.role = PartRole::caption, .bounds = placed.caption, .states = states,
                          .family = ControlVisualRole::panel};
        caption.text = shown;
        caption.font = value_font(caption_size());
        caption.alignment = PartTextAlignment::center;
        paint_part(painter, *this, caption);
    }
}

const ControlAnatomy& LinearGauge::anatomy() const noexcept {
    return linear_gauge_anatomy;
}

void LinearGauge::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.plot = PartGeometry::from_bounds(placed.plot);
    record.trace = PartGeometry::from_bounds(placed.trace);
    record.graticule = PartGeometry::from_bounds(placed.ticks);
    Rect alarms{};
    const std::array<std::optional<double>, 2> limits{low_alarm.read(), high_alarm.read()};
    for (const std::optional<double>& limit : limits) {
        if (limit) alarms = joined(alarms, mark_at(placed, ratio_of(*limit), false));
    }
    record.indicator = PartGeometry::from_bounds(alarms);
    record.caption = PartGeometry::from_bounds(placed.caption);
    write_parts(anatomy(), linear_gauge_part_fields, record, geometry);
}

LinearGauge::Parts LinearGauge::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), linear_gauge_part_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
