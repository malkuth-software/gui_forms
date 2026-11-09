#include "plot_engine.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace gui_forms::detail {

namespace {

// The accents series take, by position: spread over the ramp so that
// neighbouring series differ (the ramp wraps).
constexpr std::uint8_t series_accent_first = 5U;
constexpr std::uint8_t series_accent_stride = 3U;
// A shaded band and a hidden legend swatch are drawn translucent.
constexpr std::uint8_t band_alpha = 96U;
constexpr std::uint8_t hidden_alpha = 80U;
constexpr std::uint8_t box_fill_alpha = 48U;
constexpr std::uint8_t grid_major_alpha = 96U;
constexpr std::uint8_t grid_minor_alpha = 40U;
// A bar is this fraction of the spacing of its first two points wide.
constexpr double bar_fraction = 0.8;
constexpr double marker_size = 6.0;
constexpr double cursor_marker_size = 11.0;
// The legend entries the engine lays out (the rest are left out).
constexpr std::size_t legend_capacity = 32U;

[[nodiscard]] Color with_alpha(Color color, std::uint8_t alpha) noexcept {
    Color result = color;
    result.alpha = alpha;
    return result;
}

[[nodiscard]] std::string format_value(double value) {
    std::array<char, 32> text{};
    const int written = std::snprintf(text.data(), text.size(), "%.6g", value);
    const std::size_t length = written > 0 ? static_cast<std::size_t>(written) : 0U;
    std::string result(text.data(), std::min(length, text.size() - 1U));
    return result;
}

[[nodiscard]] const char* kind_name(PlotSeriesKind kind) noexcept {
    switch (kind) {
    case PlotSeriesKind::line: return "line";
    case PlotSeriesKind::stairs: return "stairs";
    case PlotSeriesKind::scatter: return "scatter";
    case PlotSeriesKind::shaded: return "shaded";
    case PlotSeriesKind::bars: return "bars";
    case PlotSeriesKind::stems: return "stems";
    }
    return "line";
}

// Whether the kind is drawn from zero (its y extent includes 0).
[[nodiscard]] bool drawn_from_zero(PlotSeriesKind kind) noexcept {
    const bool result = kind == PlotSeriesKind::shaded || kind == PlotSeriesKind::bars ||
                        kind == PlotSeriesKind::stems;
    return result;
}

// A source's x samples with y held at `baseline`: the lower edge of a
// shaded band.
template <typename Source>
struct BaselineSamples final {
    const Source* source{};
    double baseline{};

    [[nodiscard]] std::size_t size() const noexcept { return (*source).size(); }
    [[nodiscard]] double x(std::size_t index) const noexcept { return (*source).x(index); }
    [[nodiscard]] double y(std::size_t) const noexcept { return baseline; }
};

// Runs `operation` on the series' samples: its columns in place, or its
// borrowed views under a pin of each lease (nothing when either lease was
// released). Operations are named structs with a templated call operator.
template <typename Operation>
void with_samples(const PlotEngineSeries& series, Operation& operation) {
    if (!series.borrowed) {
        operation(series.columns);
        return;
    }
    if (series.borrowed_x == nullptr || series.borrowed_y == nullptr) return;
    if (!(*series.borrowed_x).lease() || !(*series.borrowed_y).lease()) return;
    const plots::SeriesPin x_pin((*series.borrowed_x).lease());
    const plots::SeriesPin y_pin((*series.borrowed_y).lease());
    if (!x_pin.held() || !y_pin.held()) return;
    plots::ViewSamples<double, double> samples{};
    samples.xs = (*series.borrowed_x).view();
    samples.ys = (*series.borrowed_y).view();
    operation(samples);
}

// The finite extents of a series.
struct ExtentOperation final {
    plots::PlotRange x{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    plots::PlotRange y{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    template <typename Source>
    void operator()(const Source& source) noexcept {
        const std::size_t count = source.size();
        for (std::size_t index = 0U; index < count; ++index) {
            const double x_value = source.x(index);
            const double y_value = source.y(index);
            if (!std::isfinite(x_value) || !std::isfinite(y_value)) continue;
            x.minimum = std::min(x.minimum, x_value);
            x.maximum = std::max(x.maximum, x_value);
            y.minimum = std::min(y.minimum, y_value);
            y.maximum = std::max(y.maximum, y_value);
        }
    }
};

// The newest x of a series: its last finite x when monotone, else its
// largest.
struct NewestOperation final {
    plots::XOrder order{plots::XOrder::monotone};
    double newest{-std::numeric_limits<double>::infinity()};

    template <typename Source>
    void operator()(const Source& source) noexcept {
        const std::size_t count = source.size();
        if (order == plots::XOrder::monotone) {
            for (std::size_t remaining = count; remaining > 0U; --remaining) {
                const double value = source.x(remaining - 1U);
                if (std::isfinite(value)) {
                    newest = value;
                    return;
                }
            }
            return;
        }
        for (std::size_t index = 0U; index < count; ++index) {
            const double value = source.x(index);
            if (std::isfinite(value)) newest = std::max(newest, value);
        }
    }
};

struct SizeOperation final {
    std::size_t size{};

    template <typename Source>
    void operator()(const Source& source) noexcept {
        size = source.size();
    }
};

// One element's point.
struct PointOperation final {
    std::size_t element{};
    bool found{};
    double x{};
    double y{};

    template <typename Source>
    void operator()(const Source& source) noexcept {
        if (element >= source.size()) return;
        x = source.x(element);
        y = source.y(element);
        found = true;
    }
};

// Appends a series' rows to a table.
struct TableOperation final {
    std::string* text{};
    std::string_view label{};

    template <typename Source>
    void operator()(const Source& source) {
        const std::size_t count = std::min(source.size(), PlotEngine::table_rows_per_series);
        for (std::size_t index = 0U; index < count; ++index) {
            (*text).append(label);
            (*text).push_back('\t');
            (*text).append(format_value(source.x(index)));
            (*text).push_back('\t');
            (*text).append(format_value(source.y(index)));
            (*text).push_back('\n');
        }
    }
};

// Draws a series by its kind (mark_painters.hpp's table).
struct PaintOperation final {
    Painter* painter{};
    plots::SeriesMarkPainter* marks{};
    const plots::PlotTransform* transform{};
    PlotSeriesKind kind{PlotSeriesKind::line};
    plots::XOrder order{plots::XOrder::monotone};
    const plots::EnvelopePyramid* pyramid{};
    Color color{};

    template <typename Source>
    void operator()(const Source& source) {
        StrokeStyle stroke{};
        stroke.color = color;
        stroke.width = 1.5;
        stroke.join = StrokeJoin::round;
        MarkerStyle marker{};
        marker.size = marker_size;
        marker.fill = color;
        marker.edge = color;
        const bool monotone = order == plots::XOrder::monotone;
        switch (kind) {
        case PlotSeriesKind::line:
        case PlotSeriesKind::stairs: {
            if (kind == PlotSeriesKind::stairs && monotone) {
                static_cast<void>((*marks).paint_stairs(*painter, source, *transform, stroke, plots::StairsMode::post));
                return;
            }
            const plots::LineDecimation decimation = !monotone             ? plots::LineDecimation::none
                                                     : pyramid != nullptr ? plots::LineDecimation::pyramid
                                                                          : plots::LineDecimation::envelope;
            static_cast<void>((*marks).paint_line(*painter, source, order, *transform, stroke, decimation, pyramid));
            return;
        }
        case PlotSeriesKind::scatter:
            static_cast<void>((*marks).paint_scatter(*painter, source, *transform, marker, nullptr));
            return;
        case PlotSeriesKind::shaded: {
            if (monotone) {
                BaselineSamples<Source> lower{};
                lower.source = &source;
                lower.baseline = 0.0;
                static_cast<void>((*marks).paint_shaded(*painter, source, lower, *transform, with_alpha(color, band_alpha)));
            }
            const plots::LineDecimation decimation = monotone ? plots::LineDecimation::envelope : plots::LineDecimation::none;
            static_cast<void>((*marks).paint_line(*painter, source, order, *transform, stroke, decimation, nullptr));
            return;
        }
        case PlotSeriesKind::bars: {
            double width = 1.0;
            if (source.size() >= 2U) {
                const double spacing = std::fabs(source.x(1U) - source.x(0U));
                if (std::isfinite(spacing) && spacing > 0.0) width = spacing * bar_fraction;
            }
            static_cast<void>((*marks).paint_bars(*painter, source, *transform, width, 0.0, false, color));
            return;
        }
        case PlotSeriesKind::stems:
            static_cast<void>((*marks).paint_stems(*painter, source, *transform, 0.0, stroke, marker));
            return;
        }
    }
};

// The nearest element of a series to a point.
struct PickOperation final {
    plots::SeriesPicker* picker{};
    const plots::PlotTransform* transform{};
    plots::SeriesIdentity identity{};
    plots::XOrder order{plots::XOrder::monotone};
    std::uint64_t revision{};
    Point device{};
    std::size_t prepared_points{};
    plots::PickResult* result{};

    template <typename Source>
    void operator()(const Source& source) noexcept {
        if (order == plots::XOrder::monotone) {
            static_cast<void>((*picker).nearest_monotone(source, identity, *transform, device, PlotEngine::pick_radius, *result));
            return;
        }
        // An unordered series larger than the picker's reservation is not
        // picked.
        if (source.size() > prepared_points) return;
        static_cast<void>((*picker).nearest_unordered(source, identity, revision, *transform, device,
                                                      PlotEngine::pick_radius, *result));
    }
};

// Tick label extents through the painter's text metrics.
class PainterLabelMetrics final : public plots::TickLabelMetrics {
public:
    PainterLabelMetrics(Painter& painter, FontSpec font, bool horizontal) noexcept
        : painter_(painter), font_(font), horizontal_(horizontal) {}
    [[nodiscard]] double label_extent(std::string_view label) const override {
        if (!horizontal_) return PlotEngine::tick_label_height;
        const Size measured = painter_.measure_text_utf8(label, font_);
        return measured.width;
    }

private:
    Painter& painter_;
    FontSpec font_;
    bool horizontal_{};
};

[[nodiscard]] plots::Axis default_axis(bool horizontal) noexcept {
    plots::Axis axis{};
    axis.side = horizontal ? plots::AxisSide::bottom : plots::AxisSide::left;
    axis.range = plots::PlotRange{0.0, 1.0};
    return axis;
}

[[nodiscard]] PlotPointPick pick_of(std::size_t series, std::size_t element, double x, double y) noexcept {
    PlotPointPick pick{};
    pick.series = series;
    pick.element = element;
    pick.x = x;
    pick.y = y;
    return pick;
}

[[nodiscard]] plots::PlotModifiers plot_modifiers(Modifier modifiers) noexcept {
    plots::PlotModifiers result{};
    result.shift = has_modifier(modifiers, Modifier::shift);
    result.control = has_modifier(modifiers, Modifier::control);
    result.alt = has_modifier(modifiers, Modifier::alt);
    return result;
}

[[nodiscard]] PlotEngineOutcome outcome_of(plots::PlotViewChange change) noexcept {
    PlotEngineOutcome outcome{};
    outcome.handled = true;
    outcome.view = change.view;
    outcome.selection = change.selection;
    outcome.repaint = change.view || change.selection || change.transient;
    return outcome;
}

[[nodiscard]] std::size_t series_size(const PlotEngineSeries& series) {
    SizeOperation operation{};
    with_samples(series, operation);
    return operation.size;
}

} // namespace

// -- configuration -------------------------------------------------------------

void PlotEngine::set_axes(std::span<const PlotEngineAxis> x_axes, std::span<const PlotEngineAxis> y_axes) {
    std::array<plots::Axis, plots::maximum_axes_per_orientation> xs{};
    std::array<plots::Axis, plots::maximum_axes_per_orientation> ys{};
    const std::size_t x_count = std::min(x_axes.size(), plots::maximum_axes_per_orientation);
    const std::size_t y_count = std::min(y_axes.size(), plots::maximum_axes_per_orientation);
    for (std::size_t index = 0U; index < plots::maximum_axes_per_orientation; ++index) {
        x_labels_[index].clear();
        y_labels_[index].clear();
    }
    for (std::size_t index = 0U; index < x_count; ++index) {
        xs[index] = x_axes[index].axis;
        x_labels_[index] = std::string(x_axes[index].label);
    }
    for (std::size_t index = 0U; index < y_count; ++index) {
        ys[index] = y_axes[index].axis;
        y_labels_[index] = std::string(y_axes[index].label);
    }
    if (x_count == 0U) xs[0] = default_axis(true);
    if (y_count == 0U) ys[0] = default_axis(false);
    const std::size_t x_used = x_count == 0U ? 1U : x_count;
    const std::size_t y_used = y_count == 0U ? 1U : y_count;
    model_.set_axes(std::span<const plots::Axis>(xs.data(), x_used), std::span<const plots::Axis>(ys.data(), y_used));
    cursor_.reset();
}

void PlotEngine::layout(Rect bounds, bool has_title) {
    PlotEngineLayout placed{};
    placed.bounds = bounds;
    const std::size_t x_count = std::min<std::size_t>(model_.x_axes().size(), 2U);
    const std::size_t y_count = std::min<std::size_t>(model_.y_axes().size(), 2U);
    double top = bounds.y + inset;
    if (has_title) {
        placed.title = Rect{bounds.x + inset, top, std::max(0.0, bounds.width - inset * 2.0), title_height};
        top += title_height;
    }
    std::array<double, 2> x_bands{};
    std::array<double, 2> y_bands{};
    for (std::size_t index = 0U; index < x_count; ++index) {
        const double title_band = x_labels_[index].empty() ? 0.0 : axis_title_band;
        x_bands[index] = tick_length + tick_label_height + title_band;
    }
    for (std::size_t index = 0U; index < y_count; ++index) {
        const double title_band = y_labels_[index].empty() ? 0.0 : axis_title_band;
        y_bands[index] = tick_length + y_tick_label_width + title_band;
    }
    const double left = bounds.x + inset + y_bands[0];
    const double right = bounds.x + bounds.width - inset - y_bands[1];
    const double plot_top = top + x_bands[1];
    const double plot_bottom = bounds.y + bounds.height - inset - x_bands[0];
    placed.plot = Rect{left, plot_top, std::max(0.0, right - left), std::max(0.0, plot_bottom - plot_top)};
    placed.x_axes[0] = Rect{placed.plot.x, plot_bottom, placed.plot.width, x_bands[0]};
    placed.x_axes[1] = Rect{placed.plot.x, top, placed.plot.width, x_bands[1]};
    placed.y_axes[0] = Rect{bounds.x + inset, placed.plot.y, y_bands[0], placed.plot.height};
    placed.y_axes[1] = Rect{right, placed.plot.y, y_bands[1], placed.plot.height};
    layout_ = placed;
    model_.set_plot(placed.plot);
}

void PlotEngine::prepare(std::span<const PlotEngineSeries> series, double device_scale) {
    const double scale = device_scale > 0.0 && std::isfinite(device_scale) ? device_scale : 1.0;
    const std::size_t columns = static_cast<std::size_t>(std::ceil(layout_.plot.width * scale)) + 2U;
    std::size_t points = 4096U;
    for (std::size_t index = 0U; index < series.size(); ++index) {
        points = std::max(points, series_size(series[index]));
    }
    if (columns > prepared_columns_ || points > prepared_points_) {
        prepared_columns_ = std::max(columns, prepared_columns_);
        prepared_points_ = std::max(points, prepared_points_);
        marks_.reserve(prepared_columns_, prepared_points_);
        picker_.reserve(prepared_points_, 1024U);
    }
}

// -- data ------------------------------------------------------------------------

plots::PlotDataBounds PlotEngine::data_bounds(std::span<const PlotEngineSeries> series) const {
    constexpr double infinity = std::numeric_limits<double>::infinity();
    std::array<plots::PlotRange, plots::maximum_axes_per_orientation> xs{};
    std::array<plots::PlotRange, plots::maximum_axes_per_orientation> ys{};
    for (std::size_t index = 0U; index < plots::maximum_axes_per_orientation; ++index) {
        xs[index] = plots::PlotRange{infinity, -infinity};
        ys[index] = plots::PlotRange{infinity, -infinity};
    }
    const std::size_t x_count = model_.x_axes().size();
    const std::size_t y_count = model_.y_axes().size();
    for (std::size_t index = 0U; index < series.size(); ++index) {
        const PlotEngineSeries& one = series[index];
        if (one.hidden) continue;
        ExtentOperation extent{};
        with_samples(one, extent);
        if (!(extent.x.minimum <= extent.x.maximum)) continue;
        if (drawn_from_zero(one.kind)) {
            extent.y.minimum = std::min(extent.y.minimum, 0.0);
            extent.y.maximum = std::max(extent.y.maximum, 0.0);
        }
        const std::size_t x_axis = one.x_axis < x_count ? one.x_axis : 0U;
        const std::size_t y_axis = one.y_axis < y_count ? one.y_axis : 0U;
        xs[x_axis].minimum = std::min(xs[x_axis].minimum, extent.x.minimum);
        xs[x_axis].maximum = std::max(xs[x_axis].maximum, extent.x.maximum);
        ys[y_axis].minimum = std::min(ys[y_axis].minimum, extent.y.minimum);
        ys[y_axis].maximum = std::max(ys[y_axis].maximum, extent.y.maximum);
    }
    plots::PlotDataBounds bounds{};
    for (std::size_t index = 0U; index < plots::maximum_axes_per_orientation; ++index) {
        // A single value is widened to a unit range about it; none is left
        // invalid (fit leaves its axis).
        if (xs[index].minimum == xs[index].maximum) xs[index] = plots::PlotRange{xs[index].minimum - 0.5, xs[index].maximum + 0.5};
        if (ys[index].minimum == ys[index].maximum) ys[index] = plots::PlotRange{ys[index].minimum - 0.5, ys[index].maximum + 0.5};
        bounds.x[index] = xs[index];
        bounds.y[index] = ys[index];
    }
    return bounds;
}

bool PlotEngine::fit(std::span<const PlotEngineSeries> series) {
    const plots::PlotDataBounds bounds = data_bounds(series);
    const plots::PlotViewChange change = model_.fit(bounds);
    return change.view;
}

bool PlotEngine::follow(std::span<const PlotEngineSeries> series) {
    double newest = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < series.size(); ++index) {
        NewestOperation operation{};
        operation.order = series[index].order;
        with_samples(series[index], operation);
        newest = std::max(newest, operation.newest);
    }
    if (!std::isfinite(newest)) return false;
    const bool changed = model_.follow_tail(newest);
    return changed;
}

plots::PlotTransformResult PlotEngine::transform_for(const PlotEngineSeries& series, double device_scale) const noexcept {
    const std::span<const plots::Axis> xs = model_.x_axes();
    const std::span<const plots::Axis> ys = model_.y_axes();
    const std::size_t x_axis = series.x_axis < xs.size() ? series.x_axis : 0U;
    const std::size_t y_axis = series.y_axis < ys.size() ? series.y_axis : 0U;
    const plots::PlotTransformResult result =
        plots::PlotTransform::make(xs[x_axis], ys[y_axis], layout_.plot, device_scale);
    return result;
}

Color PlotEngine::series_color(const Control& owner, std::size_t position) noexcept {
    const std::size_t step = (position * series_accent_stride + series_accent_first) % 256U;
    const AccentIndex accent{static_cast<std::uint8_t>(step)};
    Color color = owner.effective_theme().accent(accent);
    color.alpha = 255U;
    return color;
}

// -- paint ---------------------------------------------------------------------------

void PlotEngine::paint(Painter& painter, const Control& owner, AnatomyStateSet states, const PlotEngineOptions& options,
                       std::span<const PlotEngineSeries> series, std::span<const PlotEngineAnnotation> annotations,
                       double device_scale) {
    const ControlVisualContext context = part_visual_context(owner, states);
    const ControlVisualRecipe& recipe = owner.effective_theme().resolve(ControlVisualRole::editor, context);
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = layout_.bounds;
    paint_part(painter, owner, part);
    if (!options.title.empty()) {
        part.role = PartRole::caption;
        part.bounds = layout_.title;
        part.text = options.title;
        part.font = owner.effective_font(FontSpec{FontRole::control, 13.0, 600, false});
        part.alignment = PartTextAlignment::center;
        paint_part(painter, owner, part);
        part.text = std::string_view{};
    }
    part.family = ControlVisualRole::editor;
    part.role = PartRole::plot;
    part.bounds = layout_.plot;
    part.alignment = PartTextAlignment::start;
    paint_part(painter, owner, part);
    if (layout_.plot.width <= 0.0 || layout_.plot.height <= 0.0) return;
    prepare(series, device_scale);

    // Ticks for the drawn axes, then the grid of the primary ones under
    // the series.
    const FontSpec label_font = owner.effective_font(FontSpec{FontRole::control, 11.0, 400, false});
    const std::span<const plots::Axis> xs = model_.x_axes();
    const std::span<const plots::Axis> ys = model_.y_axes();
    ticks_valid_ = std::array<bool, 4>{};
    for (std::size_t slot = 0U; slot < 4U; ++slot) {
        const bool horizontal = slot < 2U;
        const std::size_t index = horizontal ? slot : slot - 2U;
        if (index >= (horizontal ? xs.size() : ys.size())) continue;
        const plots::Axis& x_axis = horizontal ? xs[index] : xs[0];
        const plots::Axis& y_axis = horizontal ? ys[0] : ys[index];
        const plots::PlotTransformResult made = plots::PlotTransform::make(x_axis, y_axis, layout_.plot, device_scale);
        if (made.check != plots::PlotTransformCheck::valid) continue;
        const PainterLabelMetrics metrics(painter, label_font, horizontal);
        plots::TickRequest request{};
        request.minimum_spacing = horizontal ? 64.0 : 32.0;
        const plots::ScaleMapping& mapping = horizontal ? made.transform.x() : made.transform.y();
        const plots::TickOutcome outcome = plots::generate_ticks(mapping, request, &metrics, ticks_[slot]);
        ticks_valid_[slot] = outcome != plots::TickOutcome::too_small;
    }
    painter.save();
    painter.clip_rect(layout_.plot);
    if (options.show_grid && ticks_valid_[0] && ticks_valid_[2]) {
        const plots::PlotTransformResult primary = plots::PlotTransform::make(xs[0], ys[0], layout_.plot, device_scale);
        if (primary.check == plots::PlotTransformCheck::valid) {
            StrokeStyle major{};
            major.color = with_alpha(recipe.material.border, grid_major_alpha);
            StrokeStyle minor{};
            minor.color = with_alpha(recipe.material.border, grid_minor_alpha);
            static_cast<void>(marks_.paint_grid(painter, ticks_[0], true, primary.transform, major, minor));
            static_cast<void>(marks_.paint_grid(painter, ticks_[2], false, primary.transform, major, minor));
        }
    }
    for (std::size_t index = 0U; index < series.size(); ++index) {
        if (series[index].hidden) continue;
        paint_series(painter, owner, index, series[index], device_scale);
    }

    // Annotations, laid out clear of each other.
    const plots::PlotTransformResult primary = plots::PlotTransform::make(xs[0], ys[0], layout_.plot, device_scale);
    if (primary.check == plots::PlotTransformCheck::valid && !annotations.empty()) {
        std::array<plots::AnnotationRequest, plots::maximum_annotations> requests{};
        std::array<Rect, plots::maximum_annotations> placed{};
        const std::size_t count = std::min(annotations.size(), plots::maximum_annotations);
        for (std::size_t index = 0U; index < count; ++index) {
            const Size measured = painter.measure_text_utf8(annotations[index].text, label_font);
            requests[index].anchor = primary.transform.to_device(annotations[index].x, annotations[index].y);
            requests[index].size = Size{measured.width + 6.0, tick_label_height + 2.0};
        }
        const std::size_t laid = plots::layout_annotations(
            layout_.plot, std::span<const plots::AnnotationRequest>(requests.data(), count),
            std::span<Rect>(placed.data(), count));
        part.family = ControlVisualRole::panel;
        part.role = PartRole::annotation;
        part.font = label_font;
        part.alignment = PartTextAlignment::center;
        for (std::size_t index = 0U; index < laid; ++index) {
            part.bounds = placed[index];
            part.text = annotations[index].text;
            paint_part(painter, owner, part);
        }
        part.text = std::string_view{};
    }

    // The crosshair and its coordinates.
    if (options.crosshair && crosshair_.has_value() && primary.check == plots::PlotTransformCheck::valid) {
        const Point at = *crosshair_;
        part.family = ControlVisualRole::panel;
        part.role = PartRole::crosshair;
        part.bounds = Rect{std::floor(at.x), layout_.plot.y, 1.0, layout_.plot.height};
        paint_part(painter, owner, part);
        part.bounds = Rect{layout_.plot.x, std::floor(at.y), layout_.plot.width, 1.0};
        paint_part(painter, owner, part);
        std::string coordinates = format_value(primary.transform.x_from_device(at.x));
        coordinates += ", ";
        coordinates += format_value(primary.transform.y_from_device(at.y));
        part.role = PartRole::annotation;
        part.font = label_font;
        part.alignment = PartTextAlignment::start;
        part.bounds = Rect{layout_.plot.x + 4.0, layout_.plot.y + 2.0, layout_.plot.width - 8.0, tick_label_height};
        part.text = coordinates;
        paint_part(painter, owner, part);
        part.text = std::string_view{};
    }

    // The box being dragged and the kept selection.
    const std::optional<Rect> box = model_.drag_box();
    if (box.has_value()) {
        painter.fill_rect(*box, with_alpha(recipe.focus_ring, box_fill_alpha));
        painter.stroke_rect(*box, recipe.focus_ring, 1.0);
    }
    const std::optional<plots::PlotSelection>& selection = model_.selection();
    if (selection.has_value() && primary.check == plots::PlotTransformCheck::valid) {
        const Point low = primary.transform.to_device((*selection).x.minimum, (*selection).y.maximum);
        const Point high = primary.transform.to_device((*selection).x.maximum, (*selection).y.minimum);
        const Rect area{std::min(low.x, high.x), std::min(low.y, high.y), std::fabs(high.x - low.x), std::fabs(high.y - low.y)};
        painter.fill_rect(area, with_alpha(recipe.focus_ring, box_fill_alpha));
        painter.stroke_rect(area, recipe.focus_ring, 1.0);
    }

    // The keyboard cursor's point.
    const std::optional<PlotPointPick> at_cursor = cursor_point(series);
    if (at_cursor.has_value()) {
        const plots::PlotTransformResult made = transform_for(series[(*at_cursor).series], device_scale);
        if (made.check == plots::PlotTransformCheck::valid) {
            const std::array<Point, 1> centre{made.transform.to_device((*at_cursor).x, (*at_cursor).y)};
            MarkerStyle ring{};
            ring.shape = MarkerShape::circle;
            ring.size = cursor_marker_size;
            ring.fill = with_alpha(recipe.focus_ring, 0U);
            ring.edge = recipe.focus_ring;
            ring.edge_width = 2.0;
            painter.draw_markers(centre, ring);
        }
    }
    painter.restore();

    paint_axes(painter, owner, states, device_scale);
    if (options.show_legend) {
        paint_legend(painter, owner, states, series);
    } else {
        legend_count_ = 0U;
    }
    if (states.contains(AnatomyState::focus_visible)) {
        part.family = ControlVisualRole::editor;
        part.role = PartRole::focus;
        part.bounds = Rect{layout_.plot.x - 2.0, layout_.plot.y - 2.0, layout_.plot.width + 4.0, layout_.plot.height + 4.0};
        paint_part(painter, owner, part);
    }
}

void PlotEngine::paint_series(Painter& painter, const Control& owner, std::size_t position, const PlotEngineSeries& series,
                              double device_scale) {
    const plots::PlotTransformResult made = transform_for(series, device_scale);
    if (made.check != plots::PlotTransformCheck::valid) return;
    PaintOperation operation{};
    operation.painter = &painter;
    operation.marks = &marks_;
    operation.transform = &made.transform;
    operation.kind = series.kind;
    operation.order = series.order;
    operation.pyramid = series.borrowed ? nullptr : series.pyramid;
    operation.color = series_color(owner, position);
    with_samples(series, operation);
}

void PlotEngine::paint_axes(Painter& painter, const Control& owner, AnatomyStateSet states, double device_scale) {
    const ControlVisualContext context = part_visual_context(owner, states);
    const ControlVisualRecipe& recipe = owner.effective_theme().resolve(ControlVisualRole::editor, context);
    const FontSpec label_font = owner.effective_font(FontSpec{FontRole::control, 11.0, 400, false});
    const std::span<const plots::Axis> xs = model_.x_axes();
    const std::span<const plots::Axis> ys = model_.y_axes();
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::panel;
    part.font = label_font;
    StrokeStyle stroke{};
    stroke.color = recipe.material.border;
    for (std::size_t slot = 0U; slot < 4U; ++slot) {
        if (!ticks_valid_[slot]) continue;
        const bool horizontal = slot < 2U;
        const std::size_t index = horizontal ? slot : slot - 2U;
        const plots::AxisSide side = horizontal ? (index == 0U ? plots::AxisSide::bottom : plots::AxisSide::top)
                                                : (index == 0U ? plots::AxisSide::left : plots::AxisSide::right);
        const Rect band = horizontal ? layout_.x_axes[index] : layout_.y_axes[index];
        const plots::Axis& x_axis = horizontal ? xs[index] : xs[0];
        const plots::Axis& y_axis = horizontal ? ys[0] : ys[index];
        const plots::PlotTransformResult made = plots::PlotTransform::make(x_axis, y_axis, layout_.plot, device_scale);
        if (made.check != plots::PlotTransformCheck::valid) continue;
        // The axis line along the plot's edge.
        part.role = PartRole::axis;
        if (side == plots::AxisSide::bottom) part.bounds = Rect{layout_.plot.x, layout_.plot.y + layout_.plot.height - 1.0, layout_.plot.width, 1.0};
        if (side == plots::AxisSide::top) part.bounds = Rect{layout_.plot.x, layout_.plot.y, layout_.plot.width, 1.0};
        if (side == plots::AxisSide::left) part.bounds = Rect{layout_.plot.x, layout_.plot.y, 1.0, layout_.plot.height};
        if (side == plots::AxisSide::right) part.bounds = Rect{layout_.plot.x + layout_.plot.width - 1.0, layout_.plot.y, 1.0, layout_.plot.height};
        paint_part(painter, owner, part);
        static_cast<void>(marks_.paint_ticks(painter, ticks_[slot], side, made.transform, tick_length, stroke));
        // The labels.
        part.role = PartRole::tick_label;
        const std::span<const plots::MajorTick> majors = ticks_[slot].majors();
        for (std::size_t tick = 0U; tick < majors.size(); ++tick) {
            if (!majors[tick].labelled) continue;
            const std::string_view text = majors[tick].label.text();
            part.text = text;
            if (horizontal) {
                const double width = painter.measure_text_utf8(text, label_font).width + 2.0;
                const double y = side == plots::AxisSide::bottom ? band.y + tick_length
                                                                  : band.y + band.height - tick_length - tick_label_height;
                part.bounds = Rect{majors[tick].device - width / 2.0, y, width, tick_label_height};
                part.alignment = PartTextAlignment::center;
            } else {
                const double y = majors[tick].device - tick_label_height / 2.0;
                const double width = y_tick_label_width - 2.0;
                const double x = side == plots::AxisSide::left ? layout_.plot.x - tick_length - width
                                                               : layout_.plot.x + layout_.plot.width + tick_length;
                part.bounds = Rect{x, y, width, tick_label_height};
                part.alignment = side == plots::AxisSide::left ? PartTextAlignment::end : PartTextAlignment::start;
            }
            paint_part(painter, owner, part);
        }
        // The axis title.
        const std::string& title = horizontal ? x_labels_[index] : y_labels_[index];
        if (title.empty()) continue;
        part.role = PartRole::caption;
        part.text = title;
        part.alignment = PartTextAlignment::center;
        if (horizontal) {
            const double y = side == plots::AxisSide::bottom ? band.y + band.height - axis_title_band : band.y;
            part.bounds = Rect{band.x, y, band.width, axis_title_band};
            part.orientation = TextOrientation::horizontal;
        } else {
            const double x = side == plots::AxisSide::left ? band.x : band.x + band.width - axis_title_band;
            part.bounds = Rect{x, band.y, axis_title_band, band.height};
            part.orientation = side == plots::AxisSide::left ? TextOrientation::bottom_to_top : TextOrientation::top_to_bottom;
        }
        paint_part(painter, owner, part);
        part.orientation = TextOrientation::horizontal;
    }
}

void PlotEngine::paint_legend(Painter& painter, const Control& owner, AnatomyStateSet states,
                              std::span<const PlotEngineSeries> series) {
    const FontSpec label_font = owner.effective_font(FontSpec{FontRole::control, 11.0, 400, false});
    std::array<double, legend_capacity> widths{};
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < series.size() && count < legend_capacity; ++index) {
        if (series[index].label.empty()) continue;
        widths[count] = painter.measure_text_utf8(series[index].label, label_font).width;
        legend_series_[count] = index;
        count = count + 1U;
    }
    legend_count_ = 0U;
    if (count == 0U) return;
    const plots::LegendStyle style{};
    const plots::LegendLayout laid = plots::layout_legend(style, layout_.plot, std::span<const double>(widths.data(), count),
                                                          std::span<plots::LegendEntryLayout>(legend_entries_.data(), count));
    legend_count_ = laid.entry_count;
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::legend;
    part.bounds = laid.frame;
    paint_part(painter, owner, part);
    part.role = PartRole::legend_entry;
    part.font = label_font;
    for (std::size_t entry = 0U; entry < legend_count_; ++entry) {
        const std::size_t position = legend_series_[entry];
        const bool hidden = series[position].hidden;
        const Color color = series_color(owner, position);
        painter.fill_rect(legend_entries_[entry].swatch, hidden ? with_alpha(color, hidden_alpha) : color);
        part.bounds = legend_entries_[entry].label;
        part.text = series[position].label;
        part.tone = hidden ? PartTextTone::secondary : PartTextTone::ordinary;
        paint_part(painter, owner, part);
    }
}

// -- input ---------------------------------------------------------------------------

plots::PlotZone PlotEngine::zone_at(Point local) const noexcept {
    plots::PlotZone zone{};
    if (layout_.plot.contains(local)) {
        zone.kind = plots::PlotZoneKind::plot;
        return zone;
    }
    for (std::size_t index = 0U; index < 2U; ++index) {
        if (index < model_.x_axes().size() && layout_.x_axes[index].contains(local)) {
            zone.kind = plots::PlotZoneKind::x_axis;
            zone.axis = index;
            return zone;
        }
        if (index < model_.y_axes().size() && layout_.y_axes[index].contains(local)) {
            zone.kind = plots::PlotZoneKind::y_axis;
            zone.axis = index;
            return zone;
        }
    }
    return zone;
}

plots::PickResult PlotEngine::pick(std::span<const PlotEngineSeries> series, Point local, double device_scale) {
    plots::PickResult result{};
    for (std::size_t index = 0U; index < series.size(); ++index) {
        if (series[index].hidden) continue;
        const plots::PlotTransformResult made = transform_for(series[index], device_scale);
        if (made.check != plots::PlotTransformCheck::valid) continue;
        PickOperation operation{};
        operation.picker = &picker_;
        operation.transform = &made.transform;
        operation.identity = plots::SeriesIdentity{static_cast<std::uint32_t>(index), 0U};
        operation.order = series[index].order;
        operation.revision = series[index].revision;
        operation.device = local;
        operation.prepared_points = prepared_points_;
        operation.result = &result;
        with_samples(series[index], operation);
    }
    return result;
}

PlotEngineOutcome PlotEngine::pointer(const PointerEvent& event, Point local, std::span<const PlotEngineSeries> series,
                                      double device_scale, const PlotEngineOptions& options) {
    PlotEngineOutcome outcome{};
    const plots::PlotZone zone = zone_at(local);
    switch (event.action) {
    case PointerAction::down: {
        // A click on a legend entry toggles its series.
        const std::size_t entry = plots::legend_entry_at(legend_entries(), local);
        if (event.button == PointerButton::primary && entry < legend_count_) {
            outcome.handled = true;
            outcome.legend_toggle = legend_series_[entry];
            return outcome;
        }
        if (zone.kind == plots::PlotZoneKind::outside) return outcome;
        if (event.button == PointerButton::primary && event.click_count >= 2U) {
            const plots::PlotViewChange change = model_.fit(data_bounds(series));
            outcome = outcome_of(change);
            return outcome;
        }
        plots::PlotPointerButton button = plots::PlotPointerButton::primary;
        if (event.button == PointerButton::secondary) button = plots::PlotPointerButton::secondary;
        if (event.button == PointerButton::middle) button = plots::PlotPointerButton::middle;
        if (event.button == PointerButton::none) return outcome;
        const plots::PlotViewChange change = model_.pointer_pressed(local, button, plot_modifiers(event.modifiers), zone);
        outcome = outcome_of(change);
        outcome.capture = true;
        click_pending_ = event.button == PointerButton::primary && zone.kind == plots::PlotZoneKind::plot;
        click_start_ = local;
        return outcome;
    }
    case PointerAction::move: {
        if (click_pending_) {
            const double dx = local.x - click_start_.x;
            const double dy = local.y - click_start_.y;
            if (std::sqrt(dx * dx + dy * dy) >= plots::drag_threshold) click_pending_ = false;
        }
        const plots::PlotViewChange change = model_.pointer_moved(local);
        outcome = outcome_of(change);
        if (options.crosshair) {
            const std::optional<Point> next = zone.kind == plots::PlotZoneKind::plot ? std::optional<Point>(local) : std::nullopt;
            if (next != crosshair_) {
                crosshair_ = next;
                outcome.repaint = true;
            }
        }
        outcome.handled = outcome.handled || outcome.repaint;
        return outcome;
    }
    case PointerAction::up: {
        const plots::PlotViewChange change = model_.pointer_released(local);
        outcome = outcome_of(change);
        outcome.release = true;
        if (click_pending_) {
            click_pending_ = false;
            const plots::PickResult found = pick(series, local, device_scale);
            if (found.found) {
                const std::size_t position = found.series.position;
                cursor_ = PlotCursor{position, found.element.value};
                outcome.pick = pick_of(position, found.element.value, found.x, found.y);
                outcome.repaint = true;
            }
        }
        return outcome;
    }
    case PointerAction::wheel: {
        if (zone.kind == plots::PlotZoneKind::outside) return outcome;
        const double notches = event.wheel_unit == WheelDeltaUnit::lines ? event.wheel_delta.y : event.wheel_delta.y / 40.0;
        if (notches == 0.0 || !std::isfinite(notches)) return outcome;
        const plots::PlotViewChange change = model_.wheel(local, notches, zone);
        outcome = outcome_of(change);
        return outcome;
    }
    case PointerAction::enter:
        return outcome;
    case PointerAction::leave: {
        if (crosshair_.has_value()) {
            crosshair_.reset();
            outcome.handled = true;
            outcome.repaint = true;
        }
        return outcome;
    }
    }
    return outcome;
}

PlotEngineOutcome PlotEngine::key(const KeyEvent& event, std::span<const PlotEngineSeries> series) {
    PlotEngineOutcome outcome{};
    if (event.action != KeyAction::down) return outcome;
    const std::uint32_t code = event.physical_key;
    const bool control = has_modifier(event.modifiers, Modifier::control);
    const bool alt = has_modifier(event.modifiers, Modifier::alt);
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    const bool arrow = code == PhysicalKey::left || code == PhysicalKey::right || code == PhysicalKey::up ||
                       code == PhysicalKey::down;
    if (code == PhysicalKey::escape) {
        if (model_.gesture() != plots::PlotGesture::none) {
            outcome = outcome_of(model_.cancel_gesture());
            outcome.release = true;
            return outcome;
        }
        if (model_.selection().has_value()) {
            model_.clear_selection();
            outcome.handled = true;
            outcome.selection = true;
            outcome.repaint = true;
        }
        return outcome;
    }
    // Plain arrows move the point cursor: left and right along its series
    // (Shift: ten elements), up and down to the previous or next visible
    // series, keeping the element where that series has one.
    if (arrow && !control && !alt) {
        if (series.empty()) return outcome;
        PlotCursor next = cursor_.value_or(PlotCursor{});
        if (!cursor_.has_value()) {
            while (next.series < series.size() && (series[next.series].hidden || series_size(series[next.series]) == 0U)) {
                next.series = next.series + 1U;
            }
            if (next.series == series.size()) return outcome;
        } else if (code == PhysicalKey::left || code == PhysicalKey::right) {
            const std::size_t size = series_size(series[next.series]);
            if (size == 0U) return outcome;
            const std::size_t step = shift ? 10U : 1U;
            if (code == PhysicalKey::left) next.element = next.element > step ? next.element - step : 0U;
            if (code == PhysicalKey::right) next.element = std::min(next.element + step, size - 1U);
        } else {
            const bool forward = code == PhysicalKey::down;
            std::size_t candidate = next.series;
            for (std::size_t tried = 0U; tried < series.size(); ++tried) {
                candidate = forward ? (candidate + 1U) % series.size() : (candidate + series.size() - 1U) % series.size();
                if (!series[candidate].hidden && series_size(series[candidate]) != 0U) break;
            }
            next.series = candidate;
            const std::size_t size = series_size(series[candidate]);
            if (size == 0U) return outcome;
            next.element = std::min(next.element, size - 1U);
        }
        if (!set_cursor(next, series)) return outcome;
        outcome.handled = true;
        outcome.repaint = true;
        outcome.pick = cursor_point(series);
        return outcome;
    }
    // Alt with arrows pans, Ctrl with arrows zooms one axis, + and - zoom,
    // Home fits and End resumes following (plot_view_model.hpp).
    plots::PlotModifiers modifiers = plot_modifiers(event.modifiers);
    modifiers.alt = false;
    std::optional<plots::PlotKey> plot_key{};
    if (code == PhysicalKey::left) plot_key = plots::PlotKey::left;
    if (code == PhysicalKey::right) plot_key = plots::PlotKey::right;
    if (code == PhysicalKey::up) plot_key = plots::PlotKey::up;
    if (code == PhysicalKey::down) plot_key = plots::PlotKey::down;
    if (code == PhysicalKey::equal || code == PhysicalKey::keypad_plus) plot_key = plots::PlotKey::zoom_in;
    if (code == PhysicalKey::minus || code == PhysicalKey::keypad_minus) plot_key = plots::PlotKey::zoom_out;
    if (code == PhysicalKey::home) plot_key = plots::PlotKey::home;
    if (code == PhysicalKey::end) plot_key = plots::PlotKey::end;
    if (!plot_key.has_value()) return outcome;
    const plots::PlotViewChange change = model_.key(*plot_key, modifiers, data_bounds(series));
    outcome = outcome_of(change);
    if (*plot_key == plots::PlotKey::end && follow(series)) outcome.view = true;
    outcome.repaint = outcome.repaint || outcome.view;
    return outcome;
}

// -- cursor, ranges and text -----------------------------------------------------

std::optional<PlotPointPick> PlotEngine::cursor_point(std::span<const PlotEngineSeries> series) const {
    if (!cursor_.has_value() || (*cursor_).series >= series.size()) return std::nullopt;
    PointOperation operation{};
    operation.element = (*cursor_).element;
    with_samples(series[(*cursor_).series], operation);
    if (!operation.found) return std::nullopt;
    const PlotPointPick result = pick_of((*cursor_).series, (*cursor_).element, operation.x, operation.y);
    return result;
}

bool PlotEngine::set_cursor(PlotCursor cursor, std::span<const PlotEngineSeries> series) {
    if (cursor.series >= series.size()) return false;
    if (cursor.element >= series_size(series[cursor.series])) return false;
    cursor_ = cursor;
    return true;
}

PlotViewRanges PlotEngine::ranges() const {
    PlotViewRanges result{};
    result.x = model_.x_axes()[0].range;
    result.y = model_.y_axes()[0].range;
    return result;
}

PlotQuery PlotEngine::query() const {
    PlotQuery result{};
    const std::optional<plots::PlotSelection>& selection = model_.selection();
    if (!selection.has_value()) return result;
    result.present = true;
    result.x = (*selection).x;
    result.y = (*selection).y;
    return result;
}

std::string PlotEngine::summary(std::span<const PlotEngineSeries> series) const {
    std::string text = "Plot with ";
    text += std::to_string(series.size());
    text += " series";
    for (std::size_t index = 0U; index < series.size(); ++index) {
        text += index == 0U ? ": " : "; ";
        text += series[index].label.empty() ? std::string_view("unnamed") : series[index].label;
        text += " (";
        text += kind_name(series[index].kind);
        text += ", ";
        text += std::to_string(series_size(series[index]));
        text += " points";
        if (series[index].hidden) text += ", hidden";
        text += ")";
    }
    const PlotViewRanges shown = ranges();
    text += ". x from ";
    text += format_value(shown.x.minimum);
    text += " to ";
    text += format_value(shown.x.maximum);
    text += ", y from ";
    text += format_value(shown.y.minimum);
    text += " to ";
    text += format_value(shown.y.maximum);
    text += ".";
    const std::optional<PlotPointPick> at = cursor_point(series);
    if (at.has_value()) {
        text += " Cursor at ";
        const std::string_view label = series[(*at).series].label;
        text += label.empty() ? std::string_view("unnamed") : label;
        text += " point ";
        text += std::to_string((*at).element + 1U);
        text += ": x ";
        text += format_value((*at).x);
        text += ", y ";
        text += format_value((*at).y);
        text += ".";
    }
    return text;
}

std::string PlotEngine::table_text(std::span<const PlotEngineSeries> series) const {
    std::string text;
    for (std::size_t index = 0U; index < series.size(); ++index) {
        TableOperation operation{};
        operation.text = &text;
        operation.label = series[index].label;
        with_samples(series[index], operation);
    }
    return text;
}

std::size_t PlotEngine::table_row_count(std::span<const PlotEngineSeries> series) const {
    std::size_t rows = 0U;
    for (std::size_t index = 0U; index < series.size(); ++index) {
        rows += std::min(series_size(series[index]), table_rows_per_series);
    }
    return rows;
}

} // namespace gui_forms::detail
