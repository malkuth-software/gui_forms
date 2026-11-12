#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/plots/plot_view_model/plot_view_model.hpp"
#include "gui_forms/plots/signal_program/signal_program.hpp"
#include "gui_forms/controls/plot_view_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

class PlotAnnotationItem;
class PlotAxisItem;
class PlotSeriesItem;
class SubSurfaceHost;

namespace detail {
class PlotEngine;
// Complete only in plot_view.cpp: the vectors of them below have no
// default member initializer, so libstdc++ instantiates their constructor and
// destructor only there (PlotView's constructor and destructor).
struct PlotEngineSeries;
struct PlotEngineAnnotation;
struct PlotLiveInbox;
struct PlotLiveNotice;
struct PlotLiveColumns;
} // namespace detail

// PlotView: the plots package's retained, declarative plot (IMGUI_IMPLOT_STUDY
// B.4; owner rulings 1a to 14a of 2026-11-08). A plot is declared, in a
// form or in C++, as a PlotView with item children: PlotAxisItem (its axes),
// PlotSeriesItem (what it draws) and PlotAnnotationItem (labels at data
// points). Other children are ignored. The plot reads its items on each
// layout and paint; an item's change tells the plot, which lays out and
// repaints, so a form's rules reach a plot through its items' properties
// and the plot's own.
//
// Interaction (plot_view_model.hpp's table): left drag pans, the wheel
// zooms about the pointer, right drag zooms to a box, Ctrl + right drag
// keeps a query rectangle, a double click fits; on an axis each acts on
// that axis only. A click on a point picks it and a click on a legend
// entry hides or shows its series. Keyboard: the arrows move the point
// cursor (left and right along its series, Shift ten points; up and down
// between series), Alt with arrows pans, Ctrl with arrows zooms one axis,
// + and - zoom, Home fits, End resumes following, Escape cancels a gesture
// or clears the query.
//
// Events, raised on the UI thread after the change they report and
// declared for rules (append_interactions): view_changed once per change of
// the first axes' ranges by a gesture, a key, set_x_range, set_y_range or
// fit_view (automatic fitting and following, which run inside layout and
// paint, raise nothing);
// selection_changed when the query rectangle is made or cleared;
// point_picked when a click or the cursor reaches a point; legend_toggled
// (the series' position) after a legend click changed its `hidden`.
//
// Accessibility: one figure node (SemanticRole::figure), named by the
// accessible name or the title, whose description is the summary (series,
// kinds, point counts, visible ranges) and whose value is the cursor's
// point; table_text offers the data as rows.
//
// Live (`live`, owner ruling 4a): the plot draws on its own sub-surface, a
// SubSurfaceHost child covering it, paced by the sub-surface's
// LiveSurfacePacer and running its signal programs on that clock. Each
// series' ring channel (PlotSeriesItem::live_channel) is consumed there,
// its producer thread writing without touching the UI thread; copied
// columns travel as a copy, and borrowed views are read there under their
// leases. The live plot's gestures and cursor run on its thread and come
// back as the same events, raised by tasks posted to the UI thread. Off,
// signal programs and channels are run and consumed on the window's
// motion clock, 60 times a second while the plot has either.
//
// One UI thread; the live frame's state belongs to its sub-surface thread.
class GUI_FORMS_API PlotView final : public Control, public PlotViewCapabilities<PlotView> {
public:
    explicit PlotView(StableId stable_id);
    ~PlotView() override;
    PlotView(const PlotView&) = delete;
    PlotView& operator=(const PlotView&) = delete;

    [[nodiscard]] Event<const PlotViewRanges&>& view_changed() noexcept { return view_changed_; }
    [[nodiscard]] Event<const PlotQuery&>& selection_changed() noexcept { return selection_changed_; }
    [[nodiscard]] Event<const PlotPointPick&>& point_picked() noexcept { return point_picked_; }
    [[nodiscard]] Event<std::size_t>& legend_toggled() noexcept { return legend_toggled_; }

    // The item children, in child order, as the plot read them last.
    [[nodiscard]] std::vector<PlotSeriesItem*> series_items() const;
    // The view: the visible range of axis `index` of each orientation (the
    // items' ranges until a gesture, a fit or following moves them).
    [[nodiscard]] plots::PlotRange x_range(std::size_t index) const;
    [[nodiscard]] plots::PlotRange y_range(std::size_t index) const;
    // Sets a range (refused, false, when the axis does not exist or the
    // range is inadmissible); raises view_changed for the first axes.
    bool set_x_range(std::size_t index, plots::PlotRange range);
    bool set_y_range(std::size_t index, plots::PlotRange range);
    // Fits the view to the visible series' data; raises view_changed when
    // it moved.
    void fit_view();
    // Takes the axis items' ranges again.
    void reset_view();
    // Hides or shows series `position` (its item's `hidden`) and raises
    // legend_toggled, as a click on its legend entry does.
    void toggle_series(std::size_t position);
    // The query rectangle, if one is kept.
    [[nodiscard]] PlotQuery query() const;
    void clear_query();

    // The keyboard point cursor's point, if it is on one.
    [[nodiscard]] std::optional<PlotPointPick> cursor() const;
    // Places the cursor on element `element` of series `series` (false
    // when there is none) and raises point_picked.
    bool set_cursor(std::size_t series, std::size_t element);

    // The figure's summary (its semantic description) and its data as
    // tab-separated rows (series, x, y), at most 1000 per series.
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] std::string table_text() const;
    [[nodiscard]] std::size_t table_row_count() const;

    // The plot rectangle, in local coordinates.
    [[nodiscard]] Rect plot_bounds() const;
    // The view and gesture model (diagnostics and tests; live plots keep
    // theirs on the sub-surface).
    [[nodiscard]] const plots::PlotViewModel& view_model() const;
    // The live sub-surface's host while `live`; null otherwise.
    [[nodiscard]] SubSurfaceHost* live_host() const noexcept { return live_host_.get(); }
    // Copies of copied-in series made for the live frame: one each time a
    // series' points change while live, none for a repaint that leaves them.
    [[nodiscard]] std::uint64_t live_column_copies() const noexcept { return live_column_copies_; }
    // Ambient frames run (signal programs and channels while not live).
    [[nodiscard]] std::uint64_t frames_run() const noexcept { return frames_run_; }

    // Runs due signal ticks and consumes channels; true when the plot was
    // painted since the last frame (it invalidated itself again).
    bool on_ambient_frame(FrameTime now) override;
    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry title;
        PartGeometry plot;
        PartGeometry x_axis;
        PartGeometry y_axis;
        PartGeometry legend;
        PartGeometry crosshair;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

    // A live plot's frame reports a change; raised here on the UI thread
    // (detail::PlotLiveNotice). Not for applications.
    void take_live_notice(const detail::PlotLiveNotice& notice);

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;

private:
    // The series (and annotations and series items, when asked) as the
    // engine reads them, from the current children: they borrow the items'
    // data, valid until an item's data changes, so every call collects
    // afresh.
    void collect_series(std::vector<detail::PlotEngineSeries>& series,
                        std::vector<detail::PlotEngineAnnotation>* annotations,
                        std::vector<PlotSeriesItem*>* items) const;
    // collect_series into the members, for the call in progress.
    void gather_series();
    // Takes the axis items' ranges when any of them changed since the last
    // time, or `force`; then fits auto-fitting axes when the data changed
    // and follows the tail.
    void take_axes(bool force);
    void fit_and_follow();
    // Enlists the plot on the motion clock while it has signals or
    // channels, is attached and not live; withdraws it otherwise.
    void update_ambient();
    // Raises the events an engine outcome calls for.
    void raise_outcome(bool view, bool selection, const std::optional<PlotPointPick>& pick,
                       const std::optional<std::size_t>& legend_toggle);
    // Makes or removes the live sub-surface, and sends it the plot's state.
    void update_live_host();
    void send_live_update(bool reset_view);
    // The shared points of the copied series `item` at `position` for the
    // live frame: the entry's when the item and its data revision are
    // unchanged, else a new copy (live_columns_).
    std::shared_ptr<const detail::PlotLiveColumns> live_columns(std::size_t position, const PlotSeriesItem& item);
    [[nodiscard]] double device_scale() const noexcept;

    std::unique_ptr<detail::PlotEngine> engine_;
    // The items and what the engine reads of them, gathered for each call
    // (gather_series); the engine's series borrow the items' data and
    // labels until the next gather.
    std::vector<PlotSeriesItem*> series_items_{};
    std::vector<detail::PlotEngineSeries> gathered_;
    std::vector<detail::PlotEngineAnnotation> annotations_;
    // The axis items' revisions and the series' data revisions when last
    // taken (sums with the counts mixed in).
    std::uint64_t axes_signature_{};
    std::uint64_t series_signature_{};
    bool axes_taken_{};
    // A gesture, a key or a range set moved the view since the axes were
    // last taken: axis item changes then keep the moved ranges and auto
    // fitting stops (reset_view clears it).
    bool view_moved_{};
    plots::SignalClock signal_clock_{};
    std::unique_ptr<plots::SignalSink> signal_sink_;
    std::vector<double> channel_scratch_{};
    std::uint64_t paints_{};
    std::uint64_t paints_seen_{~std::uint64_t{0}};
    std::uint64_t frames_run_{};
    std::shared_ptr<SubSurfaceHost> live_host_{};
    std::shared_ptr<detail::PlotLiveInbox> live_inbox_{};
    // The points last sent to the live frame for each copied series, by
    // series position: copied again only when the series' item or its data
    // revision changes (send_live_update), and shared with the frame
    // otherwise. Cleared when live mode ends.
    struct LiveColumnsEntry final {
        RuntimeId item{};
        std::uint64_t data_revision{};
        std::shared_ptr<const detail::PlotLiveColumns> columns{};
    };
    std::vector<LiveColumnsEntry> live_columns_{};
    std::uint64_t live_generation_{};
    // Copies of a series' points made for the live frame (diagnostics and
    // tests: unchanged repaints make none).
    std::uint64_t live_column_copies_{};
    // The live frame's last reported view and cursor, for the summary.
    PlotViewRanges live_ranges_{};
    std::optional<PlotPointPick> live_cursor_{};
    Event<const PlotViewRanges&> view_changed_{};
    Event<const PlotQuery&> selection_changed_{};
    Event<const PlotPointPick&> point_picked_{};
    Event<std::size_t> legend_toggled_{};
    // property_codemod begin PlotView (tools/property_families/plot_view.json; generated, do not edit)
public:
    // PlotView's property table: Control's entries, then its own.
    static constexpr std::size_t plot_view_property_count = Control::control_property_count + 5U;
    [[nodiscard]] static const std::array<PropertyEntry, plot_view_property_count>& plot_view_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct PlotViewTitleCapability<PlotView>;
    friend struct PlotViewLiveCapability<PlotView>;
    friend struct PlotViewShowLegendCapability<PlotView>;
    friend struct PlotViewShowGridCapability<PlotView>;
    friend struct PlotViewCrosshairCapability<PlotView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(live)& cell, bool& incoming);
    void written(const decltype(live)& cell);
    void prepare_write(const decltype(show_legend)& cell, bool& incoming);
    void written(const decltype(show_legend)& cell);
    void prepare_write(const decltype(show_grid)& cell, bool& incoming);
    void written(const decltype(show_grid)& cell);
    void prepare_write(const decltype(crosshair)& cell, bool& incoming);
    void written(const decltype(crosshair)& cell);
    // property_codemod end PlotView
};

} // namespace gui_forms
