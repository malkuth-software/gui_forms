#include "gui_forms/controls/instruments/sparkline/sparkline.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/plot_transform/plot_transform.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

constexpr double sparkline_inset = 2.0;
constexpr double newest_marker_size = 4.0;
// The line's accent and the band's translucency.
constexpr AccentIndex line_accent{5U};
constexpr std::uint8_t band_alpha = 72U;
// The admitted capacities.
constexpr std::uint32_t smallest_capacity = 2U;
constexpr std::uint32_t largest_capacity = 100000U;

constexpr std::array<PartField<Sparkline::Parts>, 4> sparkline_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Sparkline::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &Sparkline::Parts::plot},
    {.declaration = {.name = "series", .role = PartRole::series, .composed_from = {}},
     .member = &Sparkline::Parts::series},
    {.declaration = {.name = "marker", .role = PartRole::marker, .composed_from = {}},
     .member = &Sparkline::Parts::marker},
}};

constexpr std::array<StateField<Sparkline::States>, 3> sparkline_state_fields{{
    {.state = AnatomyState::normal, .member = &Sparkline::States::normal},
    {.state = AnatomyState::enabled, .member = &Sparkline::States::enabled},
    {.state = AnatomyState::disabled, .member = &Sparkline::States::disabled},
}};

constexpr std::array<PartDeclaration, 4> sparkline_parts = part_declarations(sparkline_part_fields);
constexpr std::array<AnatomyState, 3> sparkline_states = state_declarations(sparkline_state_fields);

constexpr ControlAnatomy sparkline_anatomy{
    .control_id = "sparkline",
    .parts = sparkline_parts,
    .states = sparkline_states,
    .custom_states = {},
};

[[nodiscard]] bool has_infinity(std::span<const double> values) noexcept {
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (std::isinf(values[index])) return true;
    }
    return false;
}

// The values with x placed so that the newest lies at capacity - 1.
[[nodiscard]] plots::ImplicitXSamples<double> placed_samples(const plots::SeriesColumn<double>& column) noexcept {
    plots::ImplicitXSamples<double> samples{};
    samples.ys = column.read();
    samples.x_start = static_cast<double>(column.capacity() - column.length());
    samples.x_step = 1.0;
    return samples;
}

// The source's y values held at `baseline`: the band's lower edge.
struct SparklineBaseline final {
    const plots::ImplicitXSamples<double>* source{};
    double baseline{};

    [[nodiscard]] std::size_t size() const noexcept { return (*source).size(); }
    [[nodiscard]] double x(std::size_t index) const noexcept { return (*source).x(index); }
    [[nodiscard]] double y(std::size_t) const noexcept { return baseline; }
};

} // namespace

Sparkline::Sparkline(StableId stable_id)
    : Control(std::move(stable_id)),
      values_(plots::SeriesMode::ring, 120U, plots::XOrder::unordered) {
    focusable = false;
}

// -- values ----------------------------------------------------------------------------

void Sparkline::push(double value) {
    const std::array<double, 1> one{value};
    push_values(one);
}

void Sparkline::push_values(std::span<const double> values) {
    require_mutable();
    if (has_infinity(values)) throw std::invalid_argument("Sparkline values must be finite or NaN");
    if (values.empty()) return;
    static_cast<void>(values_.push_overwrite(values));
    invalidate(Dirty::paint | Dirty::semantics);
}

void Sparkline::clear_values() {
    require_mutable();
    values_.clear();
    invalidate(Dirty::paint | Dirty::semantics);
}

std::optional<double> Sparkline::latest() const noexcept {
    const plots::SeriesSpans<double> live = values_.read();
    for (std::size_t remaining = live.size(); remaining > 0U; --remaining) {
        const double value = live.at(plots::ElementIndex{remaining - 1U});
        if (std::isfinite(value)) return value;
    }
    return std::nullopt;
}

plots::PlotRange Sparkline::extent() const noexcept {
    const plots::SeriesSpans<double> live = values_.read();
    plots::PlotRange range{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (std::size_t index = 0U; index < live.size(); ++index) {
        const double value = live.at(plots::ElementIndex{index});
        if (!std::isfinite(value)) continue;
        range.minimum = std::min(range.minimum, value);
        range.maximum = std::max(range.maximum, value);
    }
    return range;
}

void Sparkline::resize_ring(std::size_t capacity) {
    const plots::SeriesSpans<double> live = values_.read();
    const std::size_t kept = std::min(live.size(), capacity);
    std::vector<double> newest(kept);
    for (std::size_t index = 0U; index < kept; ++index) {
        newest[index] = live.at(plots::ElementIndex{live.size() - kept + index});
    }
    plots::SeriesColumn<double> next(plots::SeriesMode::ring, capacity, plots::XOrder::unordered);
    static_cast<void>(next.push_overwrite(newest));
    values_ = std::move(next);
}

// -- geometry and paint ----------------------------------------------------------------

Size Sparkline::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred{requested.width > 0.0 ? requested.width : 80.0,
                         requested.height > 0.0 ? requested.height : 20.0};
    return {std::min(available.width, preferred.width), std::min(available.height, preferred.height)};
}

Rect Sparkline::plot_bounds() const noexcept {
    const Rect arranged = committed_arranged_bounds();
    const Rect plot{sparkline_inset, sparkline_inset, std::max(0.0, arranged.width - sparkline_inset * 2.0),
                    std::max(0.0, arranged.height - sparkline_inset * 2.0)};
    return plot;
}

void Sparkline::on_paint(Painter& painter, Rect) {
    const Rect plot = plot_bounds();
    const AnatomyStateSet states = base_anatomy_states();
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::editor;
    part.role = PartRole::plot;
    part.bounds = plot;
    paint_part(painter, *this, part);
    const plots::PlotRange range = extent();
    if (values_.length() == 0U || !(range.minimum <= range.maximum) || plot.width <= 0.0 || plot.height <= 0.0) return;

    // The axes: x over the ring's slots, y over the values (a flat line
    // sits in the middle).
    plots::Axis x_axis{};
    x_axis.range = plots::PlotRange{0.0, static_cast<double>(values_.capacity() - 1U)};
    plots::Axis y_axis{};
    y_axis.side = plots::AxisSide::left;
    y_axis.range = range.minimum < range.maximum ? range : plots::PlotRange{range.minimum - 1.0, range.maximum + 1.0};
    const SurfaceRoot* const root = attached_surface_root();
    const double scale = root != nullptr ? (*root).scale() : 1.0;
    const plots::PlotTransformResult made = plots::PlotTransform::make(x_axis, y_axis, plot, scale);
    if (made.check != plots::PlotTransformCheck::valid) return;
    const std::size_t columns = made.transform.column_count() + 2U;
    if (columns > reserved_columns_) {
        reserved_columns_ = columns;
        marks_.reserve(reserved_columns_, 0U);
    }
    Color color = effective_theme().accent(line_accent);
    color.alpha = 255U;
    const plots::ImplicitXSamples<double> samples = placed_samples(values_);
    painter.save();
    painter.clip_rect(plot);
    if (fill.read()) {
        SparklineBaseline lower{};
        lower.source = &samples;
        lower.baseline = y_axis.range.minimum;
        Color band = color;
        band.alpha = band_alpha;
        static_cast<void>(marks_.paint_shaded(painter, samples, lower, made.transform, band));
    }
    StrokeStyle stroke{};
    stroke.color = color;
    stroke.width = 1.0;
    stroke.join = StrokeJoin::round;
    static_cast<void>(marks_.paint_line(painter, samples, plots::XOrder::monotone, made.transform, stroke,
                                        plots::LineDecimation::envelope, nullptr));
    const std::optional<double> newest = latest();
    if (newest.has_value()) {
        const std::array<Point, 1> centre{made.transform.to_device(x_axis.range.maximum, *newest)};
        MarkerStyle marker{};
        marker.size = newest_marker_size;
        marker.fill = color;
        marker.edge = color;
        painter.draw_markers(centre, marker);
    }
    painter.restore();
}

bool Sparkline::hit_test_local(Point) const {
    // Read-only: the pointer passes through.
    return false;
}

SemanticDescriptor Sparkline::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::figure;
    descriptor.name = accessible_name.read();
    const std::optional<double> newest = latest();
    const plots::PlotRange range = extent();
    std::array<char, 96> text{};
    if (newest.has_value()) {
        const int written = std::snprintf(text.data(), text.size(), "%.6g", *newest);
        if (written > 0) descriptor.value = std::string(text.data());
        const int described = std::snprintf(text.data(), text.size(), "Trend of %zu values from %.6g to %.6g",
                                            values_.length(), range.minimum, range.maximum);
        if (described > 0) descriptor.description = std::string(text.data());
    }
    if (!accessible_description.read().empty()) descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

// -- anatomy -----------------------------------------------------------------------------

const ControlAnatomy& Sparkline::anatomy() const noexcept {
    return sparkline_anatomy;
}

void Sparkline::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect arranged = committed_arranged_bounds();
    const Rect plot = plot_bounds();
    Parts record;
    record.region.bounds = Rect{0.0, 0.0, arranged.width, arranged.height};
    record.region.present = true;
    record.plot.bounds = plot;
    record.plot.present = true;
    record.series.bounds = plot;
    record.series.present = values_.length() != 0U;
    // The newest value's dot sits on the plot's right edge.
    record.marker.bounds = Rect{plot.x + plot.width - newest_marker_size / 2.0, plot.y, newest_marker_size, plot.height};
    record.marker.present = latest().has_value();
    write_parts(anatomy(), sparkline_part_fields, record, geometry);
}

Sparkline::Parts Sparkline::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), sparkline_part_fields, published.parts(), record);
    return record;
}

Sparkline::States Sparkline::states() const {
    States record;
    read_states(sparkline_state_fields, base_anatomy_states(), record);
    return record;
}

// -- properties ----------------------------------------------------------------------------

void Sparkline::prepare_write(const decltype(capacity)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming < smallest_capacity || incoming > largest_capacity) {
        throw std::invalid_argument("Sparkline capacity must be from 2 to 100000");
    }
}

void Sparkline::written(const decltype(capacity)&) {
    resize_ring(capacity.read());
    invalidate(Dirty::paint | Dirty::semantics);
}

void Sparkline::prepare_write(const decltype(fill)&, bool&) {
    require_mutable();
}

void Sparkline::written(const decltype(fill)&) {
    invalidate(Dirty::paint);
}

// property_codemod begin Sparkline (tools/property_families/sparkline.json; generated, do not edit)

namespace {

PropertyDescriptor sparkline_capacity_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "capacity";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Behavior";
    descriptor.description = "How many values the line keeps.";
    descriptor.default_value = BindingValue{std::uint64_t{120U}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_sparkline_capacity() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        sparkline_capacity_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor sparkline_fill_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "fill";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the band under the line is shaded.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_sparkline_fill() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        sparkline_fill_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, Sparkline::sparkline_property_count>& Sparkline::sparkline_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 2U> own{{
        {"capacity", &describe_sparkline_capacity,
         &read_cell_entry<Sparkline, decltype(&Sparkline::capacity), &Sparkline::capacity>,
         &write_cell_entry<Sparkline, decltype(&Sparkline::capacity), &Sparkline::capacity>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"fill", &describe_sparkline_fill,
         &read_cell_entry<Sparkline, decltype(&Sparkline::fill), &Sparkline::fill>,
         &write_cell_entry<Sparkline, decltype(&Sparkline::fill), &Sparkline::fill>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Sparkline's property table names a property twice");
    static const std::array<PropertyEntry, sparkline_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Sparkline::property_table() const noexcept {
    const std::array<PropertyEntry, sparkline_property_count>& entries = sparkline_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end Sparkline

} // namespace gui_forms
