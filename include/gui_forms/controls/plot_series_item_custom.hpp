#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/colormap/colormap.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/mark_painters/mark_painters.hpp"
#include "gui_forms/plots/plot_transform/plot_transform.hpp"
#include "gui_forms/plots/series_picker/series_picker.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <stxlib/optional.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

// The data rectangle a custom series covers, in its axes' units; either
// range invalid (PlotRange::valid) when it covers nothing.
struct GUI_FORMS_API PlotCustomExtent final {
    plots::PlotRange x{1.0, 0.0};
    plots::PlotRange y{1.0, 0.0};

    friend bool operator==(const PlotCustomExtent& left, const PlotCustomExtent& right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

// A plot kind PlotView does not have, drawn inside PlotView's frame (the
// plot demonstrator's gap P1): installed on a PlotSeriesItem with
// set_custom, it gets the plot's margins, axes, ticks, grid, view model
// (pan, zoom, fit), legend entry, hiding and theme colour, and draws only
// its marks. While installed, the item's points and `kind` are not drawn.
//
// The plot calls it on its UI thread, during its layout, paint and input:
// extent when it fits, mark_capacity before it paints, paint when the
// series is visible, pick on a click or crosshair move. A live plot (`live`)
// draws no custom series: its frame runs on another thread. After its
// content changes, the owner calls PlotSeriesItem::custom_changed so that
// the plot fits and repaints.
class GUI_FORMS_API PlotCustomSeries {
public:
    virtual ~PlotCustomSeries() = default;

    // The data it covers, for fitting the axes.
    [[nodiscard]] virtual PlotCustomExtent extent() const = 0;
    // The most points, rectangles or colours one paint writes through
    // `marks` (the plot reserves them before painting, so the paint does
    // not allocate). The default is none beyond the plot's own.
    [[nodiscard]] virtual std::size_t mark_capacity() const noexcept;
    // Draws the marks through `painter` under the plot's clip, mapping data
    // through `transform`. `color` is the series' theme colour
    // (plot_series_color); `marks` is the plot's reused mark painter.
    virtual plots::MarkStatistics paint(Painter& painter, const plots::PlotTransform& transform,
                                        plots::SeriesMarkPainter& marks, Color color) = 0;
    // The nearest element to `device` (the plot's local coordinates), or
    // found false. The plot keeps a result only within its pick radius and
    // sets its series identity itself. The default finds nothing.
    [[nodiscard]] virtual plots::PickResult pick(Point device, const plots::PlotTransform& transform) const;
};

// A heatmap (P2): `values` row-major, rows × columns, row 0 at the top of
// the data rectangle [area_x] × [area_y], coloured through a built-in
// colormap over `domain` (the finite values' extent unless set). Its
// extent is the area; a pick names the cell under the pointer (element
// row * columns + column, y its value).
class GUI_FORMS_API PlotHeatmapSeries final : public PlotCustomSeries {
public:
    // Copies `values`. Throws std::invalid_argument, changing nothing,
    // unless rows × columns equals values.size() (without overflow) and
    // both ranges are valid. An empty grid draws nothing.
    void set_grid(std::span<const double> values, std::size_t rows, std::size_t columns, plots::PlotRange area_x,
                  plots::PlotRange area_y);
    void set_colormap(plots::BuiltInColormap colormap) noexcept { colormap_ = colormap; }
    [[nodiscard]] plots::BuiltInColormap colormap() const noexcept { return colormap_; }
    // A fixed colour domain (valid), or none to follow the values.
    void set_domain(stxlib::Optional<plots::PlotRange> domain);
    [[nodiscard]] plots::PlotRange domain() const noexcept;
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    [[nodiscard]] PlotCustomExtent extent() const override;
    [[nodiscard]] std::size_t mark_capacity() const noexcept override;
    plots::MarkStatistics paint(Painter& painter, const plots::PlotTransform& transform,
                                plots::SeriesMarkPainter& marks, Color color) override;
    [[nodiscard]] plots::PickResult pick(Point device, const plots::PlotTransform& transform) const override;

private:
    std::vector<double> values_{};
    std::size_t rows_{};
    std::size_t columns_{};
    plots::PlotRange area_x_{0.0, 1.0};
    plots::PlotRange area_y_{0.0, 1.0};
    plots::PlotRange value_extent_{0.0, 1.0};
    stxlib::Optional<plots::PlotRange> domain_{};
    plots::BuiltInColormap colormap_{plots::BuiltInColormap::gui_forms_default};
};

// A histogram (P2): `samples` counted into `bins` fixed bins over their
// finite extent (or a set range), drawn as bars in the series colour. Its
// extent is the bins' range by [0, the largest count]; a pick names the
// bin under the pointer (element its index, y its count).
class GUI_FORMS_API PlotHistogramSeries final : public PlotCustomSeries {
public:
    // Copies `samples` and counts them. Throws std::invalid_argument,
    // changing nothing, for bins of 0 or above maximum_bins.
    void set_samples(std::span<const double> samples, std::size_t bins);
    // A fixed bin range (valid), or none for the samples' finite extent;
    // the samples are counted again.
    void set_range(stxlib::Optional<plots::PlotRange> range);
    static constexpr std::size_t maximum_bins = 65536U;
    [[nodiscard]] std::span<const std::uint64_t> counts() const& noexcept { return counts_; }
    std::span<const std::uint64_t> counts() const&& = delete;
    [[nodiscard]] const plots::BinSpec& bins() const noexcept { return spec_; }
    [[nodiscard]] const plots::BinTally& tally() const noexcept { return tally_; }

    [[nodiscard]] PlotCustomExtent extent() const override;
    [[nodiscard]] std::size_t mark_capacity() const noexcept override { return counts_.size(); }
    plots::MarkStatistics paint(Painter& painter, const plots::PlotTransform& transform,
                                plots::SeriesMarkPainter& marks, Color color) override;
    [[nodiscard]] plots::PickResult pick(Point device, const plots::PlotTransform& transform) const override;

private:
    // Counts samples_ into counts_ over the current range.
    void rebin();

    std::vector<double> samples_{};
    std::vector<std::uint64_t> counts_{};
    stxlib::Optional<plots::PlotRange> range_{};
    plots::BinSpec spec_{};
    plots::BinTally tally_{};
};

} // namespace gui_forms
