#pragma once
// The plot engine: what PlotView and its live frame share (IMGUI_IMPLOT_STUDY
// B.4). It lays a plot out, paints it through the plot primitives and the
// part painter, and turns pointer and key input into view, selection, pick,
// cursor and legend outcomes, over a list of series the owner rebuilds for
// each call (PlotEngineSeries borrows the owner's data for the call only).
// Private: PlotView owns one on the UI thread; PlotLiveFrame owns one on its
// sub-surface's thread. One thread per engine.

#include "gui_forms/control.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/plots/axis/axis.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/mark_painters/mark_painters.hpp"
#include "gui_forms/plots/plot_legend/plot_legend.hpp"
#include "gui_forms/plots/plot_transform/plot_transform.hpp"
#include "gui_forms/plots/plot_view_model/plot_view_model.hpp"
#include "gui_forms/plots/series_picker/series_picker.hpp"
#include "gui_forms/plots/series_view/series_view.hpp"
#include "gui_forms/plots/tick_generator/tick_generator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::detail {

// One series as the engine reads it for one call. Columns are read in
// place; borrowed views are read under a pin of each lease, taken by the
// engine for the call.
struct PlotEngineSeries final {
    PlotSeriesKind kind{PlotSeriesKind::line};
    std::string_view label{};
    bool hidden{};
    std::uint32_t x_axis{};
    std::uint32_t y_axis{};
    bool borrowed{};
    plots::ColumnSamples<double, double> columns{};
    const plots::BorrowedSeries<double>* borrowed_x{};
    const plots::BorrowedSeries<double>* borrowed_y{};
    plots::XOrder order{plots::XOrder::monotone};
    // Built for `columns` when non-null.
    const plots::EnvelopePyramid* pyramid{};
    // Changes whenever the data does (the unordered picker's grid).
    std::uint64_t revision{};
};

// One axis: the primitives' description and its title (borrowed).
struct PlotEngineAxis final {
    plots::Axis axis{};
    std::string_view label{};
};

// One annotation, anchored in the primary axes' units (text borrowed).
struct PlotEngineAnnotation final {
    std::string_view text{};
    double x{};
    double y{};
};

// What the engine draws besides the series.
struct PlotEngineOptions final {
    std::string_view title{};
    bool show_legend{true};
    bool show_grid{true};
    bool crosshair{};
};

// Where the parts lie, in the owner's local coordinates. The first axis of
// each orientation is drawn on its primary side (below, left), the second
// on the other; further axes keep their ranges but draw nothing.
struct PlotEngineLayout final {
    Rect bounds{};
    Rect title{};
    Rect plot{};
    std::array<Rect, 2> x_axes{};
    std::array<Rect, 2> y_axes{};
};

// What one input did, for the owner to raise its events and repaint.
struct PlotEngineOutcome final {
    bool handled{};
    bool view{};
    bool selection{};
    bool repaint{};
    std::optional<PlotPointPick> pick{};
    std::optional<std::size_t> legend_toggle{};
    // Pointer capture to take (a gesture began) or release (it ended).
    bool capture{};
    bool release{};
};

// The keyboard point cursor: a series' position and one of its elements.
struct PlotCursor final {
    std::size_t series{};
    std::size_t element{};
};

class PlotEngine final {
public:
    // The layout's bands, in logical units.
    static constexpr double inset = 4.0;
    static constexpr double title_height = 20.0;
    static constexpr double tick_length = 4.0;
    static constexpr double tick_label_height = 14.0;
    static constexpr double axis_title_band = 16.0;
    static constexpr double y_tick_label_width = 48.0;
    // The radius a click or the crosshair finds a point within.
    static constexpr double pick_radius = 8.0;
    // The most rows table_text writes per series.
    static constexpr std::size_t table_rows_per_series = 1000U;

    // Replaces the axes (at most maximum_axes_per_orientation each; none of
    // an orientation gives a linear [0, 1]) and resets the view to their
    // ranges; the labels are copied.
    void set_axes(std::span<const PlotEngineAxis> x_axes, std::span<const PlotEngineAxis> y_axes);
    // Lays the plot out in `bounds` (local coordinates).
    void layout(Rect bounds, bool has_title);
    [[nodiscard]] const PlotEngineLayout& placed() const noexcept { return layout_; }
    [[nodiscard]] const plots::PlotViewModel& model() const noexcept { return model_; }
    [[nodiscard]] plots::PlotViewModel& model() noexcept { return model_; }
    // Reserves the painter's and the picker's scratch for `series` at
    // `device_scale`; allocates only when they grow.
    void prepare(std::span<const PlotEngineSeries> series, double device_scale);

    // The extents fit() fits to, axis by axis (visible series only).
    [[nodiscard]] plots::PlotDataBounds data_bounds(std::span<const PlotEngineSeries> series) const;
    // Fits the view to the data; true when a range changed.
    bool fit(std::span<const PlotEngineSeries> series);
    // Moves following x axes to the newest x; true when a range changed.
    bool follow(std::span<const PlotEngineSeries> series);

    // Paints the whole plot into `owner`'s local coordinates: face, title,
    // well, grid, series, annotations, crosshair, drag box, selection,
    // cursor, axes, legend and, when `focus_visible`, the focus ring.
    void paint(Painter& painter, const Control& owner, AnatomyStateSet states, const PlotEngineOptions& options,
               std::span<const PlotEngineSeries> series, std::span<const PlotEngineAnnotation> annotations,
               double device_scale);

    // Pointer input at a local position.
    PlotEngineOutcome pointer(const PointerEvent& event, Point local, std::span<const PlotEngineSeries> series,
                              double device_scale, const PlotEngineOptions& options);
    // Key input (key down only).
    PlotEngineOutcome key(const KeyEvent& event, std::span<const PlotEngineSeries> series);

    [[nodiscard]] const std::optional<PlotCursor>& cursor() const noexcept { return cursor_; }
    // The cursor's point, when the cursor names a live element.
    [[nodiscard]] std::optional<PlotPointPick> cursor_point(std::span<const PlotEngineSeries> series) const;
    // Places the cursor (false when `cursor` names no element).
    bool set_cursor(PlotCursor cursor, std::span<const PlotEngineSeries> series);
    [[nodiscard]] PlotViewRanges ranges() const;
    [[nodiscard]] PlotQuery query() const;
    // The figure's summary for assistive technology: its kind of content,
    // series and visible ranges, and the cursor's point.
    [[nodiscard]] std::string summary(std::span<const PlotEngineSeries> series) const;
    // The data as a table, one "series<TAB>x<TAB>y" line per element, at
    // most table_rows_per_series per series; and the row count it holds.
    [[nodiscard]] std::string table_text(std::span<const PlotEngineSeries> series) const;
    [[nodiscard]] std::size_t table_row_count(std::span<const PlotEngineSeries> series) const;
    // The legend's entries as last painted, and the series each names.
    [[nodiscard]] std::span<const plots::LegendEntryLayout> legend_entries() const& noexcept {
        return std::span<const plots::LegendEntryLayout>(legend_entries_.data(), legend_count_);
    }
    std::span<const plots::LegendEntryLayout> legend_entries() const&& = delete;

    // Each series' colour: an accent of `theme`'s ramp by its position.
    [[nodiscard]] static Color series_color(const Control& owner, std::size_t position) noexcept;

private:
    // The transform for a series' axes over the plot; invalid when the
    // axes or the plot are.
    [[nodiscard]] plots::PlotTransformResult transform_for(const PlotEngineSeries& series,
                                                            double device_scale) const noexcept;
    [[nodiscard]] plots::PlotZone zone_at(Point local) const noexcept;
    void paint_axes(Painter& painter, const Control& owner, AnatomyStateSet states, double device_scale);
    void paint_legend(Painter& painter, const Control& owner, AnatomyStateSet states,
                      std::span<const PlotEngineSeries> series);
    void paint_series(Painter& painter, const Control& owner, std::size_t position, const PlotEngineSeries& series,
                      double device_scale);
    // The nearest visible element within pick_radius of `local`.
    [[nodiscard]] plots::PickResult pick(std::span<const PlotEngineSeries> series, Point local,
                                         double device_scale);

    plots::PlotViewModel model_{};
    std::array<std::string, plots::maximum_axes_per_orientation> x_labels_{};
    std::array<std::string, plots::maximum_axes_per_orientation> y_labels_{};
    PlotEngineLayout layout_{};
    plots::SeriesMarkPainter marks_{};
    plots::SeriesPicker picker_{};
    std::size_t prepared_columns_{};
    std::size_t prepared_points_{};
    // The drawn axes' ticks of the last paint: the bottom and top x axes,
    // then the left and right y axes; `ticks_valid_` says which were made.
    std::array<plots::TickSet, 4> ticks_{};
    std::array<bool, 4> ticks_valid_{};
    std::optional<PlotCursor> cursor_{};
    std::optional<Point> crosshair_{};
    // The legend as last painted: entries, and the series position of each.
    std::array<plots::LegendEntryLayout, 32> legend_entries_{};
    std::array<std::size_t, 32> legend_series_{};
    std::size_t legend_count_{};
    // A primary press that may still become a click (no drag yet).
    bool click_pending_{};
    Point click_start_{};
};

} // namespace gui_forms::detail
