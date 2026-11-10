#include "plot_view_live.hpp"

#include "gui_forms/surface_root/surface_root.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <span>
#include <utility>

namespace gui_forms::detail {

namespace {

// The ring a live series without a channel keeps its signal's samples in.
constexpr std::size_t live_signal_capacity = 8192U;

} // namespace

void PlotLivePost::operator()() const {
    const std::shared_ptr<PlotLiveInbox> held = inbox.lock();
    if (!held || (*held).owner == nullptr) return;
    (*(*held).owner).take_live_notice(notice);
}

// -- frame -----------------------------------------------------------------------------

PlotLiveFrame::PlotLiveFrame(StableId stable_id)
    : Control(std::move(stable_id)), signal_sink_(std::make_unique<plots::SignalSink>()) {
    focusable = true;
}

PlotLiveFrame::~PlotLiveFrame() = default;

void PlotLiveFrame::take_state(PlotLiveState state) {
    std::vector<SeriesData> next(state.series.size());
    for (std::size_t index = 0U; index < state.series.size(); ++index) {
        const PlotLiveSeries& incoming = state.series[index];
        SeriesData& data = next[index];
        // A series fed by the same channel, or running the same signal,
        // keeps its ring; anything else takes the new data.
        const bool same_feed = index < state_.series.size() && index < data_.size() &&
                               ((incoming.channel && incoming.channel == state_.series[index].channel) ||
                                (!incoming.channel && incoming.signal.has_value() &&
                                 incoming.signal_source == state_.series[index].signal_source &&
                                 state_.series[index].signal.has_value()));
        if (same_feed) {
            data = std::move(data_[index]);
            continue;
        }
        if (incoming.channel) {
            data.xs = plots::SeriesColumn<double>(plots::SeriesMode::ring, (*incoming.channel).capacity(), plots::XOrder::monotone);
            data.ys = plots::SeriesColumn<double>(plots::SeriesMode::ring, (*incoming.channel).capacity(), plots::XOrder::unordered);
            continue;
        }
        if (incoming.signal.has_value()) {
            data.xs = plots::SeriesColumn<double>(plots::SeriesMode::ring, live_signal_capacity, plots::XOrder::monotone);
            data.ys = plots::SeriesColumn<double>(plots::SeriesMode::ring, live_signal_capacity, plots::XOrder::unordered);
            continue;
        }
        // Borrowed views and copied columns are read in place (gather):
        // nothing to hold here.
    }
    data_ = std::move(next);
    const bool reset = state.reset_view;
    state_ = std::move(state);
    std::vector<PlotEngineAxis> xs(state_.x_axes.size());
    std::vector<PlotEngineAxis> ys(state_.y_axes.size());
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        xs[index].axis = state_.x_axes[index].axis;
        xs[index].label = state_.x_axes[index].label;
    }
    for (std::size_t index = 0U; index < ys.size(); ++index) {
        ys[index].axis = state_.y_axes[index].axis;
        ys[index].label = state_.y_axes[index].label;
    }
    // The UI thread's ranges replace the frame's own unless the frame's
    // user moved the view since and the plot did not ask for a reset.
    if (!reset && view_moved_) {
        const std::span<const plots::Axis> current_x = engine_.model().x_axes();
        const std::span<const plots::Axis> current_y = engine_.model().y_axes();
        for (std::size_t index = 0U; index < xs.size() && index < current_x.size(); ++index) {
            if (current_x[index].kind == xs[index].axis.kind) xs[index].axis.range = current_x[index].range;
        }
        for (std::size_t index = 0U; index < ys.size() && index < current_y.size(); ++index) {
            if (current_y[index].kind == ys[index].axis.kind) ys[index].axis.range = current_y[index].range;
        }
    }
    if (reset) view_moved_ = false;
    engine_.set_axes(xs, ys);
    gather();
    const Rect arranged = committed_arranged_bounds();
    engine_.layout(Rect{0.0, 0.0, arranged.width, arranged.height}, !state_.title.empty());
    update_ambient();
    invalidate(Dirty::paint);
}

void PlotLiveFrame::gather() {
    gathered_.clear();
    annotations_.clear();
    for (std::size_t index = 0U; index < state_.series.size() && index < data_.size(); ++index) {
        const PlotLiveSeries& state = state_.series[index];
        PlotEngineSeries series{};
        series.kind = state.kind;
        series.label = state.label;
        series.hidden = state.hidden;
        series.x_axis = state.x_axis;
        series.y_axis = state.y_axis;
        series.borrowed = state.borrowed && !state.channel && !state.signal.has_value();
        series.borrowed_x = &state.borrowed_x;
        series.borrowed_y = &state.borrowed_y;
        series.order = state.channel || state.signal.has_value() ? plots::XOrder::monotone : state.order;
        const bool copied = !state.channel && !state.signal.has_value() && !state.borrowed && state.columns;
        if (copied) {
            // Borrowed from state_, which holds the columns until the next
            // take_state, and gather runs again after each.
            const PlotLiveColumns& columns = *state.columns;
            series.columns.xs.first = std::span<const double>(columns.xs.data(), columns.xs.size());
            series.columns.ys.first = std::span<const double>(columns.ys.data(), columns.ys.size());
            series.revision = columns.generation;
        } else {
            series.columns.xs = data_[index].xs.read();
            series.columns.ys = data_[index].ys.read();
            series.revision = data_[index].ys.revision();
        }
        gathered_.push_back(series);
    }
    for (std::size_t index = 0U; index < state_.annotations.size(); ++index) {
        PlotEngineAnnotation annotation{};
        annotation.text = state_.annotations[index].text;
        annotation.x = state_.annotations[index].x;
        annotation.y = state_.annotations[index].y;
        annotations_.push_back(annotation);
    }
}

double PlotLiveFrame::device_scale() const noexcept {
    const SurfaceRoot* const root = surface_root();
    const double scale = root != nullptr ? (*root).scale() : 1.0;
    return scale;
}

void PlotLiveFrame::update_ambient() {
    SurfaceRoot* const root = surface_root();
    if (root == nullptr) return;
    MotionClock& clock = (*root).motion_clock();
    bool produced = false;
    for (std::size_t index = 0U; index < state_.series.size(); ++index) {
        produced = produced || static_cast<bool>(state_.series[index].channel) || state_.series[index].signal.has_value();
    }
    if (!produced) {
        clock.withdraw_control_ambient(*this);
        return;
    }
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(std::chrono::duration<double>(1.0 / plots::signal_tick_rate));
    const bool started = clock.enlist_control_ambient(*this, interval);
    if (started) signal_clock_.rebase();
}

bool PlotLiveFrame::on_ambient_frame(FrameTime now) {
    const plots::SignalFrame frame = signal_clock_.advance(now, 16U);
    bool changed = false;
    for (std::size_t index = 0U; index < state_.series.size() && index < data_.size(); ++index) {
        const PlotLiveSeries& series = state_.series[index];
        SeriesData& data = data_[index];
        if (series.channel) {
            const std::size_t needed = (*series.channel).capacity() * 2U;
            if (channel_scratch_.size() < needed) channel_scratch_.resize(needed);
            std::array<plots::SeriesColumn<double>*, 2> columns{&data.xs, &data.ys};
            changed = (*series.channel).consume(columns, channel_scratch_) != 0U || changed;
            continue;
        }
        if (!series.signal.has_value() || frame.tick_count == 0U) continue;
        plots::SignalFrame own = frame;
        own.samples_per_tick = (*series.signal).samples_per_tick;
        (*signal_sink_).clear();
        plots::run_signal(*series.signal, own, *signal_sink_);
        if ((*signal_sink_).count == 0U) continue;
        const std::span<const double> times((*signal_sink_).times.data(), (*signal_sink_).count);
        const std::span<const double> values((*signal_sink_).values.data(), (*signal_sink_).count);
        static_cast<void>(data.xs.push_overwrite(times));
        static_cast<void>(data.ys.push_overwrite(values));
        changed = true;
    }
    frames_run_ = frames_run_ + 1U;
    if (!changed) return true;
    gather();
    static_cast<void>(engine_.follow(gathered_));
    invalidate(Dirty::paint);
    return true;
}

void PlotLiveFrame::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    engine_.layout(Rect{0.0, 0.0, final_bounds.width, final_bounds.height}, !state_.title.empty());
}

void PlotLiveFrame::on_paint(Painter& painter, Rect) {
    gather();
    PlotEngineOptions options{};
    options.title = state_.title;
    options.show_legend = state_.show_legend;
    options.show_grid = state_.show_grid;
    options.crosshair = state_.crosshair;
    engine_.paint(painter, *this, base_anatomy_states(), options, gathered_, annotations_, device_scale());
}

bool PlotLiveFrame::hit_test_local(Point local_point) const {
    const Rect arranged = committed_arranged_bounds();
    const Rect local{0.0, 0.0, arranged.width, arranged.height};
    return local.contains(local_point);
}

const ControlAnatomy& PlotLiveFrame::anatomy() const noexcept {
    return plot_view_anatomy_reference();
}

SemanticDescriptor PlotLiveFrame::semantic_descriptor() const {
    // The plot on the UI thread publishes the figure; the host does not
    // publish the frame's tree.
    SemanticDescriptor descriptor;
    descriptor.exposed = false;
    return descriptor;
}

void PlotLiveFrame::on_attached_to_window() {
    Control::on_attached_to_window();
    update_ambient();
}

void PlotLiveFrame::on_detached_from_window() noexcept {
    SurfaceRoot* const root = surface_root();
    if (root != nullptr) (*root).motion_clock().withdraw_control_ambient(*this);
    Control::on_detached_from_window();
}

// -- input and notices ---------------------------------------------------------------

void PlotLiveFrame::post(const PlotLiveNotice& notice) const {
    PlotLivePost task{};
    task.inbox = state_.inbox;
    task.notice = notice;
    static_cast<void>(state_.dispatcher.begin_invoke(task));
}

void PlotLiveFrame::report(const PlotEngineOutcome& outcome) {
    if (outcome.view) {
        view_moved_ = true;
        PlotLiveNotice notice{};
        notice.kind = PlotLiveNoticeKind::view;
        notice.ranges = engine_.ranges();
        post(notice);
    }
    if (outcome.selection) {
        PlotLiveNotice notice{};
        notice.kind = PlotLiveNoticeKind::selection;
        notice.query = engine_.query();
        post(notice);
    }
    if (outcome.pick.has_value()) {
        PlotLiveNotice notice{};
        notice.kind = PlotLiveNoticeKind::pick;
        notice.pick = *outcome.pick;
        post(notice);
    }
    if (outcome.legend_toggle.has_value()) {
        // The plot sets the item's `hidden`, which sends the new state.
        PlotLiveNotice notice{};
        notice.kind = PlotLiveNoticeKind::legend_toggle;
        notice.series = *outcome.legend_toggle;
        post(notice);
    }
}

void PlotLiveFrame::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    gather();
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    PlotEngineOptions options{};
    options.crosshair = state_.crosshair;
    const PlotEngineOutcome outcome = engine_.pointer(event, local, gathered_, device_scale(), options);
    if (outcome.capture) set_pointer_capture(true);
    if (outcome.release) set_pointer_capture(false);
    if (!outcome.handled) return;
    event.handled = true;
    if (outcome.repaint) invalidate(Dirty::paint);
    report(outcome);
}

void PlotLiveFrame::on_key(KeyEvent& event) {
    if (!eligible_for_input()) return;
    gather();
    const PlotEngineOutcome outcome = engine_.key(event, gathered_);
    if (!outcome.handled) return;
    event.handled = true;
    if (outcome.repaint) invalidate(Dirty::paint);
    report(outcome);
}

// -- content ---------------------------------------------------------------------------

Control::Ptr PlotLiveContent::build_root() {
    frame_ = std::make_shared<PlotLiveFrame>(stable_id_);
    Control::Ptr root = frame_;
    return root;
}

void PlotLiveContent::apply_update(SubSurfaceContentUpdate& update) {
    PlotLiveUpdate* const live_update = dynamic_cast<PlotLiveUpdate*>(&update);
    if (live_update == nullptr || !frame_) return;
    (*frame_).take_state(std::move((*live_update).state));
}

} // namespace gui_forms::detail
