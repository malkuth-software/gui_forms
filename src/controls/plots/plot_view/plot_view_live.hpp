#pragma once
// A live PlotView's sub-surface (owner ruling 4a): the content its
// SubSurfaceHost runs, the frame control that content builds on the
// sub-surface's thread, the update that carries the plot's state there with
// each content epoch, and the notices that carry the frame's gestures back.
// Private: applications reach a live plot only through PlotView, so the
// frame is not a registry type (it borrows PlotView's anatomy ID).

#include "plot_engine.hpp"

#include "gui_forms/controls/plots/plot_view/plot_view.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/plots/series_ring_channel/series_ring_channel.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::detail {

// PlotView's anatomy, which the frame reports so that the theme's rules for
// the plot's parts select the frame's (plot_view.cpp).
[[nodiscard]] const ControlAnatomy& plot_view_anatomy_reference() noexcept;

// The plot a live frame's notices go to. Made, read and cleared on the UI
// thread only: the frame holds a weak reference, which it copies into each
// posted notice; PlotView clears `owner` when it is destroyed, so a notice
// arriving later is dropped.
struct PlotLiveInbox final {
    PlotView* owner{};
};

enum class PlotLiveNoticeKind : std::uint8_t { view, selection, pick, legend_toggle };

// One change the frame reports.
struct PlotLiveNotice final {
    PlotLiveNoticeKind kind{PlotLiveNoticeKind::view};
    PlotViewRanges ranges{};
    PlotQuery query{};
    PlotPointPick pick{};
    std::size_t series{};
};

// The task a frame posts to the UI thread (WindowDispatcher::begin_invoke):
// hands its notice to the plot when the plot still exists.
struct PlotLivePost final {
    std::weak_ptr<PlotLiveInbox> inbox{};
    PlotLiveNotice notice{};

    void operator()() const;
};

struct PlotLiveAxis final {
    plots::Axis axis{};
    std::string label{};
};

struct PlotLiveAnnotation final {
    std::string text{};
    double x{};
    double y{};
};

// A copied series' points for the live frame, made on the UI thread when
// the series' data changes and never written after. The plot keeps it for
// its next updates and the frame reads it in place, so a repaint with
// unchanged data sends a shared pointer, not a copy; an update coalesced
// away costs nothing, since every update names the whole set. Freed by the
// last holder, on either thread (immutable, so shared without a lock).
struct PlotLiveColumns final {
    std::vector<double> xs{};
    std::vector<double> ys{};
    // Unique within the plot that made it: the engine's revision for these
    // points (its picker caches by revision).
    std::uint64_t generation{};
};

// One series' state as the UI thread sent it. Exactly one data source is
// used: a channel, else a signal program, else borrowed views, else the
// copied columns.
struct PlotLiveSeries final {
    PlotSeriesKind kind{PlotSeriesKind::line};
    std::string label{};
    bool hidden{};
    std::uint32_t x_axis{};
    std::uint32_t y_axis{};
    plots::XOrder order{plots::XOrder::monotone};
    std::shared_ptr<plots::SeriesRingChannel> channel{};
    std::optional<plots::SignalProgram> signal{};
    // The signal's declaration: a series keeps its ring while it is equal.
    std::string signal_source{};
    bool borrowed{};
    plots::BorrowedSeries<double> borrowed_x{};
    plots::BorrowedSeries<double> borrowed_y{};
    std::shared_ptr<const PlotLiveColumns> columns{};
};

// Everything the frame shows, made on the UI thread and moved to the
// sub-surface's thread with a content epoch.
struct PlotLiveState final {
    std::string title{};
    bool show_legend{true};
    bool show_grid{true};
    bool crosshair{};
    std::vector<PlotLiveAxis> x_axes{};
    std::vector<PlotLiveAxis> y_axes{};
    // Take the axes' ranges (the axes changed, or reset_view).
    bool reset_view{};
    std::vector<PlotLiveSeries> series{};
    std::vector<PlotLiveAnnotation> annotations{};
    WindowDispatcher dispatcher{};
    std::weak_ptr<PlotLiveInbox> inbox{};
};

class PlotLiveUpdate final : public SubSurfaceContentUpdate {
public:
    explicit PlotLiveUpdate(PlotLiveState state) : state(std::move(state)) {}
    PlotLiveState state;
};

// The live sub-surface's root: a whole plot on its own thread. It owns its
// engine, its series' rings and columns, and its signal clock; it runs the
// signals and consumes the channels on its surface's motion clock (paced by
// the sub-surface's LiveSurfacePacer), takes the host's forwarded input,
// and posts what changed to the UI thread. Sub-surface thread.
class PlotLiveFrame final : public Control {
public:
    explicit PlotLiveFrame(StableId stable_id);
    ~PlotLiveFrame() override;
    PlotLiveFrame(const PlotLiveFrame&) = delete;
    PlotLiveFrame& operator=(const PlotLiveFrame&) = delete;

    // Shows `state` from the next frame.
    void take_state(PlotLiveState state);
    [[nodiscard]] std::uint64_t frames_run() const noexcept { return frames_run_; }

    bool on_ambient_frame(FrameTime now) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;

private:
    // One series' ring on this thread (channels and signals; copied
    // series read their PlotLiveColumns in place).
    struct SeriesData final {
        plots::SeriesColumn<double> xs{};
        plots::SeriesColumn<double> ys{};
    };
    void gather();
    void post(const PlotLiveNotice& notice) const;
    void report(const PlotEngineOutcome& outcome);
    void update_ambient();
    [[nodiscard]] double device_scale() const noexcept;

    PlotEngine engine_{};
    PlotLiveState state_{};
    std::vector<SeriesData> data_{};
    std::vector<PlotEngineSeries> gathered_{};
    std::vector<PlotEngineAnnotation> annotations_{};
    plots::SignalClock signal_clock_{};
    std::unique_ptr<plots::SignalSink> signal_sink_;
    std::vector<double> channel_scratch_{};
    std::uint64_t frames_run_{};
    bool view_moved_{};
};

// The content a live plot's host runs: builds a PlotLiveFrame under the
// plot's stable ID and hands it each PlotLiveUpdate.
class PlotLiveContent final : public SubSurfaceContent {
public:
    explicit PlotLiveContent(StableId stable_id) : stable_id_(std::move(stable_id)) {}
    [[nodiscard]] Control::Ptr build_root() override;
    void apply_update(SubSurfaceContentUpdate& update) override;

private:
    StableId stable_id_;
    std::shared_ptr<PlotLiveFrame> frame_{};
};

} // namespace gui_forms::detail
