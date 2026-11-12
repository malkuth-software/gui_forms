#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/plots/series_ring_channel/series_ring_channel.hpp"
#include "gui_forms/plots/series_view/series_view.hpp"
#include "gui_forms/plots/signal_program/signal_program.hpp"
#include "gui_forms/controls/plot_series_item_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace gui_forms {

// One series of a PlotView (IMGUI_IMPLOT_STUDY B.4), declared as a child of
// the plot: what it draws, its legend label, the axes it is drawn against,
// and where its data comes from. Data arrives in one of four ways:
//
//   copied in   set_points, append_points, set_values (owner ruling 9a):
//               the item owns growable columns; a monotone, gap-free series
//               keeps an envelope pyramid, extended on append.
//   a ring      use_ring, then push_points: the newest `capacity` points.
//   borrowed    borrow_points: two leased views of the caller's arrays,
//               read under a pin each paint; the caller releases the lease
//               before changing or freeing them (plots::SeriesLease).
//   produced    `source`, a signal program the plot runs on its motion
//               clock (a live plot: on its sub-surface's clock), or
//               live_channel, a ring channel a producer thread writes.
//
// Each is a whole replacement: the last call chooses. The item draws
// nothing, takes no input, no focus and no space, and publishes no semantic
// node; the plot reads it, and its legend entry toggles `hidden`. All of it
// on the plot's UI thread, except the channel's write lease.
class PlotSeriesItem final : public Control, public PlotSeriesItemCapabilities<PlotSeriesItem> {
public:
    explicit PlotSeriesItem(StableId stable_id);

    // Copies `xs` and `ys` (equal lengths; std::invalid_argument
    // otherwise) into owned columns, monotone when xs never decreases
    // (gaps aside). Allocates; nothing changes on a throw.
    void set_points(std::span<const double> xs, std::span<const double> ys);
    // Appends to the owned columns (after set_points or set_values; a
    // monotone series stays monotone or the append throws
    // std::invalid_argument, changing nothing). Grows as std::vector does.
    void append_points(std::span<const double> xs, std::span<const double> ys);
    // Copies `ys` with x = 0, 1, 2, ...
    void set_values(std::span<const double> ys);
    // Replaces the data with an empty ring of `capacity` points (at least
    // 2; std::invalid_argument otherwise), monotone in x.
    void use_ring(std::size_t capacity);
    // Pushes points into the ring (use_ring first; std::logic_error
    // otherwise), dropping the oldest past its capacity. A point going back
    // in x throws std::invalid_argument, changing nothing.
    void push_points(std::span<const double> xs, std::span<const double> ys);
    // Reads `xs` and `ys` in place under their leases from now on.
    // Invalid views (BorrowedSeries::check) throw std::invalid_argument.
    void borrow_points(plots::BorrowedSeries<double> xs, plots::BorrowedSeries<double> ys, plots::XOrder order);
    // The channel a producer thread writes (x, y) samples into (two
    // streams), made with `capacity` samples on the first call and kept;
    // the series becomes a ring of the channel's capacity, fed each frame.
    [[nodiscard]] std::shared_ptr<plots::SeriesRingChannel> live_channel(std::size_t capacity);
    // Empties the series (copied-in columns stay reserved).
    void clear_points();

    // What the plot reads.
    enum class DataSource : std::uint8_t { columns, borrowed };
    [[nodiscard]] DataSource data_source() const noexcept { return data_source_; }
    [[nodiscard]] plots::ColumnSamples<double, double> column_samples() const noexcept;
    [[nodiscard]] const plots::BorrowedSeries<double>& borrowed_x() const noexcept { return borrowed_x_; }
    [[nodiscard]] const plots::BorrowedSeries<double>& borrowed_y() const noexcept { return borrowed_y_; }
    [[nodiscard]] plots::XOrder x_order() const noexcept { return order_; }
    // The pyramid when it matches the columns (a growable, monotone,
    // gap-free series of at least pyramid_threshold points), else null.
    [[nodiscard]] const plots::EnvelopePyramid* pyramid() const noexcept;
    // The parsed `source`, when one is set.
    [[nodiscard]] const std::optional<plots::SignalProgram>& signal() const noexcept { return signal_; }
    [[nodiscard]] const std::shared_ptr<plots::SeriesRingChannel>& channel() const noexcept { return channel_; }
    // Bumped by every data change and property write.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Bumped by every change to the points alone (set, append, push, borrow,
    // clear, a new ring, a signal or channel frame), not by other property
    // writes: a live plot copies a series again only when it moves.
    [[nodiscard]] std::uint64_t data_revision() const noexcept { return data_revision_; }
    // Runs the signal program for `frame` into the ring (the plot's
    // ambient frame; `sink` is the plot's reused scratch). False when the
    // item has no signal.
    bool run_signal_frame(const plots::SignalFrame& frame, plots::SignalSink& sink);
    // Moves the channel's published samples into the ring (`scratch` holds
    // capacity * 2 doubles). The count taken; 0 without a channel.
    std::size_t consume_channel(std::span<double> scratch);

    // Series shorter than this keep no pyramid: the linear envelope is
    // cheaper than keeping one.
    static constexpr std::size_t pyramid_threshold = 65536U;
    // The ring a signal program fills.
    static constexpr std::size_t signal_ring_capacity = 8192U;

    [[nodiscard]] Size measure(Size available) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

private:
    // Bumps the revision and invalidates the owning plot.
    void notify_plot();
    // Bumps the data revision, then notify_plot().
    void notify_data_changed();
    // Rebuilds or extends the pyramid after the columns changed (from
    // `previous` elements).
    void update_pyramid(std::size_t previous);
    // Replaces the columns with rings of `capacity`.
    void make_ring(std::size_t capacity);

    DataSource data_source_{DataSource::columns};
    plots::XOrder order_{plots::XOrder::monotone};
    plots::SeriesColumn<double> xs_{};
    plots::SeriesColumn<double> ys_{};
    plots::BorrowedSeries<double> borrowed_x_{};
    plots::BorrowedSeries<double> borrowed_y_{};
    plots::EnvelopePyramid pyramid_{};
    bool pyramid_valid_{};
    std::optional<plots::SignalProgram> signal_{};
    std::shared_ptr<plots::SeriesRingChannel> channel_{};
    std::uint64_t revision_{};
    std::uint64_t data_revision_{};
    // property_codemod begin PlotSeriesItem (tools/property_families/plot_series_item.json; generated, do not edit)
public:
    // PlotSeriesItem's property table: Control's entries, then its own.
    static constexpr std::size_t plot_series_item_property_count = Control::control_property_count + 6U;
    [[nodiscard]] static const std::array<PropertyEntry, plot_series_item_property_count>& plot_series_item_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct PlotSeriesItemKindCapability<PlotSeriesItem>;
    friend struct PlotSeriesItemLabelCapability<PlotSeriesItem>;
    friend struct PlotSeriesItemSourceCapability<PlotSeriesItem>;
    friend struct PlotSeriesItemHiddenCapability<PlotSeriesItem>;
    friend struct PlotSeriesItemXAxisCapability<PlotSeriesItem>;
    friend struct PlotSeriesItemYAxisCapability<PlotSeriesItem>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(kind)& cell, PlotSeriesKind& incoming);
    void written(const decltype(kind)& cell);
    void prepare_write(const decltype(label)& cell, std::string& incoming);
    void written(const decltype(label)& cell);
    void prepare_write(const decltype(source)& cell, std::string& incoming);
    void written(const decltype(source)& cell);
    void prepare_write(const decltype(hidden)& cell, bool& incoming);
    void written(const decltype(hidden)& cell);
    void prepare_write(const decltype(x_axis)& cell, std::uint32_t& incoming);
    void written(const decltype(x_axis)& cell);
    void prepare_write(const decltype(y_axis)& cell, std::uint32_t& incoming);
    void written(const decltype(y_axis)& cell);
    // property_codemod end PlotSeriesItem
};

} // namespace gui_forms
