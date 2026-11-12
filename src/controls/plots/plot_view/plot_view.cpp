#include "gui_forms/controls/plot_view.hpp"

#include "plot_engine.hpp"
#include "plot_view_live.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/controls/plot_annotation_item.hpp"
#include "gui_forms/controls/plot_axis_item.hpp"
#include "gui_forms/controls/plot_series_item.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace gui_forms {

namespace {

// The rate a non-live plot runs its signal programs and channels at.
constexpr double plot_frame_rate = plots::signal_tick_rate;
// The size a plot asks for when its bounds request none.
constexpr double preferred_width = 360.0;
constexpr double preferred_height = 240.0;
// Mixes one value into a signature (FNV-1a's prime).
constexpr std::uint64_t signature_prime = 1099511628211ULL;

[[nodiscard]] std::uint64_t mix(std::uint64_t signature, std::uint64_t value) noexcept {
    const std::uint64_t result = (signature ^ value) * signature_prime;
    return result;
}

constexpr std::array<PartField<PlotView::Parts>, 8> plot_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &PlotView::Parts::region},
    {.declaration = {.name = "title", .role = PartRole::caption, .composed_from = {}},
     .member = &PlotView::Parts::title},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &PlotView::Parts::plot},
    {.declaration = {.name = "x_axis", .role = PartRole::axis, .composed_from = {}},
     .member = &PlotView::Parts::x_axis},
    {.declaration = {.name = "y_axis", .role = PartRole::axis, .composed_from = {}},
     .member = &PlotView::Parts::y_axis},
    {.declaration = {.name = "legend", .role = PartRole::legend, .composed_from = {}},
     .member = &PlotView::Parts::legend},
    {.declaration = {.name = "crosshair", .role = PartRole::crosshair, .composed_from = {}},
     .member = &PlotView::Parts::crosshair},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &PlotView::Parts::focus},
}};

constexpr std::array<StateField<PlotView::States>, 5> plot_view_state_fields{{
    {.state = AnatomyState::normal, .member = &PlotView::States::normal},
    {.state = AnatomyState::enabled, .member = &PlotView::States::enabled},
    {.state = AnatomyState::disabled, .member = &PlotView::States::disabled},
    {.state = AnatomyState::focused, .member = &PlotView::States::focused},
    {.state = AnatomyState::focus_visible, .member = &PlotView::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 8> plot_view_parts = part_declarations(plot_view_part_fields);
constexpr std::array<AnatomyState, 5> plot_view_states = state_declarations(plot_view_state_fields);

constexpr ControlAnatomy plot_view_anatomy{
    .control_id = "plot_view",
    .parts = plot_view_parts,
    .states = plot_view_states,
    .custom_states = {},
};

// The events PlotView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 4> plot_view_interactions{{
    {.name = "view_changed", .payload = "PlotViewRanges", .mutable_payload = false},
    {.name = "selection_changed", .payload = "PlotQuery", .mutable_payload = false},
    {.name = "point_picked", .payload = "PlotPointPick", .mutable_payload = false},
    {.name = "legend_toggled", .payload = "std::size_t", .mutable_payload = false},
}};

[[nodiscard]] plots::AxisSide axis_side(bool horizontal, std::size_t position) noexcept {
    if (horizontal) return position == 0U ? plots::AxisSide::bottom : plots::AxisSide::top;
    return position == 0U ? plots::AxisSide::left : plots::AxisSide::right;
}

} // namespace

const ControlAnatomy& detail::plot_view_anatomy_reference() noexcept {
    return plot_view_anatomy;
}

PlotView::PlotView(StableId stable_id)
    : Control(std::move(stable_id)), engine_(std::make_unique<detail::PlotEngine>()),
      signal_sink_(std::make_unique<plots::SignalSink>()) {
    focusable = true;
    (*engine_).set_axes(std::span<const detail::PlotEngineAxis>{}, std::span<const detail::PlotEngineAxis>{});
}

PlotView::~PlotView() {
    // Notices the live frame posted after this point find no plot.
    if (live_inbox_) (*live_inbox_).owner = nullptr;
}

// -- items ---------------------------------------------------------------------------

std::vector<PlotSeriesItem*> PlotView::series_items() const {
    std::vector<PlotSeriesItem*> items;
    const std::span<const Control::Ptr> all = children();
    for (std::size_t index = 0U; index < all.size(); ++index) {
        PlotSeriesItem* const item = dynamic_cast<PlotSeriesItem*>(all[index].get());
        if (item != nullptr) items.push_back(item);
    }
    return items;
}

void PlotView::collect_series(std::vector<detail::PlotEngineSeries>& series,
                              std::vector<detail::PlotEngineAnnotation>* annotations,
                              std::vector<PlotSeriesItem*>* items) const {
    series.clear();
    if (annotations != nullptr) (*annotations).clear();
    if (items != nullptr) (*items).clear();
    const std::span<const Control::Ptr> all = children();
    for (std::size_t index = 0U; index < all.size(); ++index) {
        Control* const child = all[index].get();
        PlotSeriesItem* const item = dynamic_cast<PlotSeriesItem*>(child);
        if (item != nullptr) {
            detail::PlotEngineSeries one{};
            one.kind = (*item).kind.read();
            one.label = (*item).label.read();
            one.hidden = (*item).hidden.read();
            one.x_axis = (*item).x_axis.read();
            one.y_axis = (*item).y_axis.read();
            one.borrowed = (*item).data_source() == PlotSeriesItem::DataSource::borrowed;
            one.columns = (*item).column_samples();
            one.borrowed_x = &(*item).borrowed_x();
            one.borrowed_y = &(*item).borrowed_y();
            one.order = (*item).x_order();
            one.pyramid = (*item).pyramid();
            one.revision = (*item).revision();
            series.push_back(one);
            if (items != nullptr) (*items).push_back(item);
            continue;
        }
        const PlotAnnotationItem* const annotation = dynamic_cast<const PlotAnnotationItem*>(child);
        if (annotation != nullptr && annotations != nullptr) {
            detail::PlotEngineAnnotation entry{};
            entry.text = (*annotation).text.read();
            entry.x = (*annotation).x.read();
            entry.y = (*annotation).y.read();
            (*annotations).push_back(entry);
        }
    }
}

void PlotView::gather_series() {
    collect_series(gathered_, &annotations_, &series_items_);
}

void PlotView::take_axes(bool force) {
    std::array<detail::PlotEngineAxis, plots::maximum_axes_per_orientation> xs{};
    std::array<detail::PlotEngineAxis, plots::maximum_axes_per_orientation> ys{};
    std::size_t x_count = 0U;
    std::size_t y_count = 0U;
    std::uint64_t signature = 0U;
    const std::span<const Control::Ptr> all = children();
    for (std::size_t index = 0U; index < all.size(); ++index) {
        const PlotAxisItem* const item = dynamic_cast<const PlotAxisItem*>(all[index].get());
        if (item == nullptr) continue;
        signature = mix(signature, reinterpret_cast<std::uintptr_t>(item));
        signature = mix(signature, (*item).revision());
        const bool horizontal = (*item).direction.read() == PlotAxisDirection::horizontal;
        std::size_t& count = horizontal ? x_count : y_count;
        if (count == plots::maximum_axes_per_orientation) continue;
        detail::PlotEngineAxis& axis = horizontal ? xs[count] : ys[count];
        axis.axis = (*item).axis_description(axis_side(horizontal, count));
        axis.label = (*item).label.read();
        count = count + 1U;
    }
    if (!force && axes_taken_ && signature == axes_signature_) return;
    // After a gesture moved the view, a changed item keeps the moved range
    // of an axis of the same kind; reset_view forces the items' ranges.
    if (!force && view_moved_) {
        const std::span<const plots::Axis> current_x = (*engine_).model().x_axes();
        const std::span<const plots::Axis> current_y = (*engine_).model().y_axes();
        for (std::size_t index = 0U; index < x_count && index < current_x.size(); ++index) {
            if (current_x[index].kind == xs[index].axis.kind) xs[index].axis.range = current_x[index].range;
        }
        for (std::size_t index = 0U; index < y_count && index < current_y.size(); ++index) {
            if (current_y[index].kind == ys[index].axis.kind) ys[index].axis.range = current_y[index].range;
        }
    }
    (*engine_).set_axes(std::span<const detail::PlotEngineAxis>(xs.data(), x_count),
                        std::span<const detail::PlotEngineAxis>(ys.data(), y_count));
    axes_signature_ = signature;
    axes_taken_ = true;
    // The new axes fit again on the next data check.
    series_signature_ = 0U;
}

void PlotView::fit_and_follow() {
    std::uint64_t signature = mix(0U, gathered_.size());
    for (std::size_t index = 0U; index < gathered_.size(); ++index) {
        signature = mix(signature, gathered_[index].revision);
        signature = mix(signature, gathered_[index].hidden ? 1U : 0U);
    }
    const bool data_changed = signature != series_signature_;
    series_signature_ = signature;
    bool auto_fit = false;
    bool follows = false;
    const std::span<const plots::Axis> xs = (*engine_).model().x_axes();
    const std::span<const plots::Axis> ys = (*engine_).model().y_axes();
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        auto_fit = auto_fit || xs[index].fit != plots::AxisFit::none;
        follows = follows || xs[index].follow.tail;
    }
    for (std::size_t index = 0U; index < ys.size(); ++index) {
        auto_fit = auto_fit || ys[index].fit != plots::AxisFit::none;
    }
    if (data_changed && auto_fit && !view_moved_) static_cast<void>((*engine_).fit(gathered_));
    if (follows) static_cast<void>((*engine_).follow(gathered_));
}

// -- view ----------------------------------------------------------------------------

plots::PlotRange PlotView::x_range(std::size_t index) const {
    const std::span<const plots::Axis> axes = (*engine_).model().x_axes();
    return index < axes.size() ? axes[index].range : plots::PlotRange{};
}

plots::PlotRange PlotView::y_range(std::size_t index) const {
    const std::span<const plots::Axis> axes = (*engine_).model().y_axes();
    return index < axes.size() ? axes[index].range : plots::PlotRange{};
}

bool PlotView::set_x_range(std::size_t index, plots::PlotRange range) {
    require_mutable();
    if (!(*engine_).model().set_x_range(index, range)) return false;
    view_moved_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    if (index == 0U) raise_outcome(true, false, std::nullopt, std::nullopt);
    if (live_host_) send_live_update(false);
    return true;
}

bool PlotView::set_y_range(std::size_t index, plots::PlotRange range) {
    require_mutable();
    if (!(*engine_).model().set_y_range(index, range)) return false;
    view_moved_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    if (index == 0U) raise_outcome(true, false, std::nullopt, std::nullopt);
    if (live_host_) send_live_update(false);
    return true;
}

void PlotView::fit_view() {
    require_mutable();
    gather_series();
    take_axes(false);
    const bool moved = (*engine_).fit(gathered_);
    view_moved_ = false;
    if (!moved) return;
    invalidate(Dirty::paint | Dirty::semantics);
    raise_outcome(true, false, std::nullopt, std::nullopt);
    if (live_host_) send_live_update(false);
}

void PlotView::reset_view() {
    require_mutable();
    view_moved_ = false;
    gather_series();
    take_axes(true);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    if (live_host_) send_live_update(true);
}

void PlotView::toggle_series(std::size_t position) {
    require_mutable();
    const std::vector<PlotSeriesItem*> items = series_items();
    if (position >= items.size()) return;
    PlotSeriesItem& item = *items[position];
    item.hidden = !item.hidden.read();
    legend_toggled_.emit(position);
}

PlotQuery PlotView::query() const {
    return (*engine_).query();
}

void PlotView::clear_query() {
    require_mutable();
    if (!(*engine_).model().selection().has_value()) return;
    (*engine_).model().clear_selection();
    invalidate(Dirty::paint);
    raise_outcome(false, true, std::nullopt, std::nullopt);
}

std::optional<PlotPointPick> PlotView::cursor() const {
    if (live_host_) return live_cursor_;
    std::vector<detail::PlotEngineSeries> series;
    collect_series(series, nullptr, nullptr);
    return (*engine_).cursor_point(series);
}

bool PlotView::set_cursor(std::size_t series, std::size_t element) {
    require_mutable();
    gather_series();
    if (!(*engine_).set_cursor(detail::PlotCursor{series, element}, gathered_)) return false;
    invalidate(Dirty::paint | Dirty::semantics);
    raise_outcome(false, false, (*engine_).cursor_point(gathered_), std::nullopt);
    return true;
}

std::string PlotView::summary() const {
    std::vector<detail::PlotEngineSeries> series;
    collect_series(series, nullptr, nullptr);
    return (*engine_).summary(series);
}

std::string PlotView::table_text() const {
    std::vector<detail::PlotEngineSeries> series;
    collect_series(series, nullptr, nullptr);
    return (*engine_).table_text(series);
}

std::size_t PlotView::table_row_count() const {
    std::vector<detail::PlotEngineSeries> series;
    collect_series(series, nullptr, nullptr);
    return (*engine_).table_row_count(series);
}

Rect PlotView::plot_bounds() const {
    return (*engine_).placed().plot;
}

const plots::PlotViewModel& PlotView::view_model() const {
    return (*engine_).model();
}

double PlotView::device_scale() const noexcept {
    const SurfaceRoot* const root = surface_root();
    const double scale = root != nullptr ? (*root).scale() : 1.0;
    return scale;
}

void PlotView::raise_outcome(bool view, bool selection, const std::optional<PlotPointPick>& pick,
                             const std::optional<std::size_t>& legend_toggle) {
    if (view) {
        const PlotViewRanges ranges = (*engine_).ranges();
        view_changed_.emit(ranges);
    }
    if (selection) {
        const PlotQuery shown = (*engine_).query();
        selection_changed_.emit(shown);
    }
    if (pick.has_value()) point_picked_.emit(*pick);
    if (legend_toggle.has_value()) toggle_series(*legend_toggle);
}

// -- layout and paint ----------------------------------------------------------------

Size PlotView::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred{requested.width > 0.0 ? requested.width : preferred_width,
                         requested.height > 0.0 ? requested.height : preferred_height};
    return {std::min(available.width, preferred.width), std::min(available.height, preferred.height)};
}

void PlotView::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    gather_series();
    take_axes(false);
    const Rect local{0.0, 0.0, final_bounds.width, final_bounds.height};
    (*engine_).layout(local, !title.read().empty());
    fit_and_follow();
    // Items take no space; the live host covers the plot.
    const std::span<const Control::Ptr> all = children();
    for (std::size_t index = 0U; index < all.size(); ++index) {
        const bool host = live_host_ && all[index] == live_host_;
        set_child_layout(all[index], host ? local : Rect{});
    }
}

void PlotView::on_paint(Painter& painter, Rect) {
    gather_series();
    take_axes(false);
    if (live_host_) {
        // The frame draws the plot; it is sent the plot's state with each
        // change that repaints this control.
        send_live_update(false);
        return;
    }
    fit_and_follow();
    detail::PlotEngineOptions options{};
    options.title = title.read();
    options.show_legend = show_legend.read();
    options.show_grid = show_grid.read();
    options.crosshair = crosshair.read();
    (*engine_).paint(painter, *this, anatomy_states(), options, gathered_, annotations_, device_scale());
    paints_ = paints_ + 1U;
    update_ambient();
}

bool PlotView::hit_test_local(Point local_point) const {
    const Rect arranged = committed_arranged_bounds();
    const Rect local{0.0, 0.0, arranged.width, arranged.height};
    return local.contains(local_point);
}

// -- motion --------------------------------------------------------------------------

void PlotView::update_ambient() {
    SurfaceRoot* const root = surface_root();
    if (root == nullptr) return;
    MotionClock& clock = (*root).motion_clock();
    bool produced = false;
    for (std::size_t index = 0U; index < series_items_.size(); ++index) {
        const PlotSeriesItem& item = *series_items_[index];
        produced = produced || item.signal().has_value() || static_cast<bool>(item.channel());
    }
    if (!produced || live_host_) {
        clock.withdraw_control_ambient(*this);
        return;
    }
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(std::chrono::duration<double>(1.0 / plot_frame_rate));
    const bool started = clock.enlist_control_ambient(*this, interval);
    // A start or a resume continues the signals without catching up.
    if (started) signal_clock_.rebase();
}

bool PlotView::on_ambient_frame(FrameTime now) {
    if (live_host_ || paints_ == paints_seen_) return false;
    paints_seen_ = paints_;
    // The items may have changed since the paint.
    gather_series();
    const plots::SignalFrame frame = signal_clock_.advance(now, 16U);
    for (std::size_t index = 0U; index < series_items_.size(); ++index) {
        PlotSeriesItem& item = *series_items_[index];
        if (item.signal().has_value() && frame.tick_count != 0U) {
            plots::SignalFrame own = frame;
            own.samples_per_tick = (*item.signal()).samples_per_tick;
            static_cast<void>(item.run_signal_frame(own, *signal_sink_));
        }
        if (item.channel()) {
            const std::size_t needed = (*item.channel()).capacity() * 2U;
            if (channel_scratch_.size() < needed) channel_scratch_.resize(needed);
            static_cast<void>(item.consume_channel(channel_scratch_));
        }
    }
    frames_run_ = frames_run_ + 1U;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void PlotView::on_attached_to_window() {
    Control::on_attached_to_window();
    update_live_host();
}

void PlotView::on_detached_from_window() noexcept {
    SurfaceRoot* const root = surface_root();
    if (root != nullptr) (*root).motion_clock().withdraw_control_ambient(*this);
    Control::on_detached_from_window();
}

// -- input ---------------------------------------------------------------------------

void PlotView::on_pointer(PointerEvent& event) {
    if (live_host_ || !eligible_for_input()) return;
    gather_series();
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    if (event.action == PointerAction::down && surface_root() != nullptr) {
        static_cast<void>((*surface_root()).request_focus(shared_from_this()));
    }
    detail::PlotEngineOptions options{};
    options.crosshair = crosshair.read();
    const detail::PlotEngineOutcome outcome = (*engine_).pointer(event, local, gathered_, device_scale(), options);
    if (outcome.capture) set_pointer_capture(true);
    if (outcome.release) set_pointer_capture(false);
    if (!outcome.handled) return;
    event.handled = true;
    if (outcome.view) view_moved_ = true;
    if (outcome.repaint) invalidate(Dirty::paint | Dirty::semantics);
    raise_outcome(outcome.view, outcome.selection, outcome.pick, outcome.legend_toggle);
}

void PlotView::on_key(KeyEvent& event) {
    if (live_host_ || !eligible_for_input()) return;
    gather_series();
    const detail::PlotEngineOutcome outcome = (*engine_).key(event, gathered_);
    if (!outcome.handled) return;
    event.handled = true;
    if (outcome.view) view_moved_ = true;
    if (outcome.repaint) invalidate(Dirty::paint | Dirty::semantics);
    raise_outcome(outcome.view, outcome.selection, outcome.pick, outcome.legend_toggle);
}

void PlotView::on_focus_changed(bool focused) {
    Control::on_focus_changed(focused);
    invalidate(Dirty::paint);
}

// -- live ----------------------------------------------------------------------------

void PlotView::update_live_host() {
    const bool wanted = live.read();
    if (wanted && !live_host_) {
        const std::string prefix(stable_id().value());
        live_host_ = make_control<SubSurfaceHost>(StableId(prefix + ".live"));
        // The plot publishes its figure node; the frame's tree is not
        // published beside it.
        (*live_host_).set_publishes_semantics(false);
        if (!live_inbox_) {
            live_inbox_ = std::make_shared<detail::PlotLiveInbox>();
            (*live_inbox_).owner = this;
        }
        add_child(live_host_);
        (*live_host_).set_content(std::make_unique<detail::PlotLiveContent>(stable_id()), true);
        focusable = false;
        if (surface_root() != nullptr) (*surface_root()).motion_clock().withdraw_control_ambient(*this);
        send_live_update(true);
    } else if (!wanted && live_host_) {
        const Control::Ptr removed = remove_child((*live_host_).runtime_id());
        static_cast<void>(removed);
        live_host_.reset();
        live_cursor_.reset();
        live_columns_.clear();
        focusable = true;
        // Channels are consumed on this thread again from the next frame.
        update_ambient();
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void PlotView::send_live_update(bool reset_view) {
    if (!live_host_) return;
    gather_series();
    detail::PlotLiveState state{};
    state.title = title.read();
    state.show_legend = show_legend.read();
    state.show_grid = show_grid.read();
    state.crosshair = crosshair.read();
    state.reset_view = reset_view || !view_moved_;
    const std::span<const plots::Axis> xs = (*engine_).model().x_axes();
    const std::span<const plots::Axis> ys = (*engine_).model().y_axes();
    std::size_t x_seen = 0U;
    std::size_t y_seen = 0U;
    const std::span<const Control::Ptr> all = children();
    std::vector<const PlotAxisItem*> axis_items;
    for (std::size_t index = 0U; index < all.size(); ++index) {
        const PlotAxisItem* const item = dynamic_cast<const PlotAxisItem*>(all[index].get());
        if (item != nullptr) axis_items.push_back(item);
    }
    for (std::size_t index = 0U; index < axis_items.size(); ++index) {
        const bool horizontal = (*axis_items[index]).direction.read() == PlotAxisDirection::horizontal;
        std::size_t& seen = horizontal ? x_seen : y_seen;
        const std::span<const plots::Axis> current = horizontal ? xs : ys;
        if (seen >= current.size()) continue;
        detail::PlotLiveAxis axis{};
        axis.axis = current[seen];
        axis.label = (*axis_items[index]).label.read();
        if (horizontal) state.x_axes.push_back(std::move(axis));
        if (!horizontal) state.y_axes.push_back(std::move(axis));
        seen = seen + 1U;
    }
    for (std::size_t index = 0U; index < series_items_.size(); ++index) {
        const PlotSeriesItem& item = *series_items_[index];
        detail::PlotLiveSeries series{};
        series.kind = item.kind.read();
        series.label = item.label.read();
        series.hidden = item.hidden.read();
        series.x_axis = item.x_axis.read();
        series.y_axis = item.y_axis.read();
        series.order = item.x_order();
        series.channel = item.channel();
        series.signal = item.signal();
        series.signal_source = item.source.read();
        series.borrowed = item.data_source() == PlotSeriesItem::DataSource::borrowed;
        if (series.borrowed) {
            series.borrowed_x = item.borrowed_x();
            series.borrowed_y = item.borrowed_y();
        } else if (!series.channel && !series.signal.has_value()) {
            series.columns = live_columns(index, item);
        }
        state.series.push_back(std::move(series));
    }
    for (std::size_t index = 0U; index < annotations_.size(); ++index) {
        detail::PlotLiveAnnotation annotation{};
        annotation.text = std::string(annotations_[index].text);
        annotation.x = annotations_[index].x;
        annotation.y = annotations_[index].y;
        state.annotations.push_back(std::move(annotation));
    }
    if (surface_root() != nullptr) state.dispatcher = (*surface_root()).dispatcher();
    state.inbox = live_inbox_;
    static_cast<void>((*live_host_).redraw_all(std::make_unique<detail::PlotLiveUpdate>(std::move(state))));
}

std::shared_ptr<const detail::PlotLiveColumns> PlotView::live_columns(std::size_t position,
                                                                     const PlotSeriesItem& item) {
    if (live_columns_.size() <= position) live_columns_.resize(position + 1U);
    LiveColumnsEntry& entry = live_columns_[position];
    const bool unchanged = entry.columns && entry.item == item.runtime_id() && entry.data_revision == item.data_revision();
    if (unchanged) return entry.columns;
    // The points changed (or another item took this position): one copy,
    // made aside so a throw leaves the entry as it was.
    const plots::ColumnSamples<double, double> samples = item.column_samples();
    const std::size_t count = samples.size();
    std::shared_ptr<detail::PlotLiveColumns> made = std::make_shared<detail::PlotLiveColumns>();
    (*made).xs.resize(count);
    (*made).ys.resize(count);
    for (std::size_t element = 0U; element < count; ++element) {
        (*made).xs[element] = samples.x(element);
        (*made).ys[element] = samples.y(element);
    }
    live_generation_ = live_generation_ + 1U;
    (*made).generation = live_generation_;
    entry.item = item.runtime_id();
    entry.data_revision = item.data_revision();
    entry.columns = std::move(made);
    live_column_copies_ = live_column_copies_ + 1U;
    return entry.columns;
}

void PlotView::take_live_notice(const detail::PlotLiveNotice& notice) {
    switch (notice.kind) {
    case detail::PlotLiveNoticeKind::view:
        live_ranges_ = notice.ranges;
        view_moved_ = true;
        static_cast<void>((*engine_).model().set_x_range(
            0U, plots::PlotRange{notice.ranges.x_minimum, notice.ranges.x_maximum}));
        static_cast<void>((*engine_).model().set_y_range(
            0U, plots::PlotRange{notice.ranges.y_minimum, notice.ranges.y_maximum}));
        invalidate(Dirty::semantics);
        view_changed_.emit(notice.ranges);
        return;
    case detail::PlotLiveNoticeKind::selection:
        selection_changed_.emit(notice.query);
        return;
    case detail::PlotLiveNoticeKind::pick:
        live_cursor_ = notice.pick;
        invalidate(Dirty::semantics);
        point_picked_.emit(notice.pick);
        return;
    case detail::PlotLiveNoticeKind::legend_toggle:
        toggle_series(notice.series);
        return;
    default:
        // An enumerator outside the declared set (a corrupt value) does nothing.
        break;
    }
}

// -- anatomy, semantics and rules -------------------------------------------------

const ControlAnatomy& PlotView::anatomy() const noexcept {
    return plot_view_anatomy;
}

AnatomyStateSet PlotView::anatomy_states() const {
    return base_anatomy_states();
}

void PlotView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const detail::PlotEngineLayout& placed = (*engine_).placed();
    Parts record;
    record.region.bounds = placed.bounds;
    record.region.present = true;
    record.title.bounds = placed.title;
    record.title.present = !title.read().empty();
    record.plot.bounds = placed.plot;
    record.plot.present = true;
    record.x_axis.bounds = placed.x_axes[0];
    record.x_axis.present = true;
    record.y_axis.bounds = placed.y_axes[0];
    record.y_axis.present = true;
    const std::span<const plots::LegendEntryLayout> entries = (*engine_).legend_entries();
    if (!entries.empty() && show_legend.read()) {
        Rect frame = entries[0].bounds;
        for (std::size_t index = 1U; index < entries.size(); ++index) frame = Rect::united(frame, entries[index].bounds);
        record.legend.bounds = frame;
        record.legend.present = true;
    }
    record.crosshair.bounds = placed.plot;
    record.crosshair.present = crosshair.read();
    record.focus.bounds = Rect{placed.plot.x - 2.0, placed.plot.y - 2.0, placed.plot.width + 4.0, placed.plot.height + 4.0};
    record.focus.present = true;
    write_parts(anatomy(), plot_view_part_fields, record, geometry);
}

PlotView::Parts PlotView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), plot_view_part_fields, published.parts(), record);
    return record;
}

PlotView::States PlotView::states() const {
    States record;
    read_states(plot_view_state_fields, anatomy_states(), record);
    return record;
}

SemanticDescriptor PlotView::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::figure;
    descriptor.name = accessible_name.read().empty() ? title.read() : accessible_name.read();
    descriptor.description = accessible_description.read().empty() ? summary() : accessible_description.read();
    const std::optional<PlotPointPick> at = cursor();
    if (at.has_value()) {
        std::array<char, 64> text{};
        const int written = std::snprintf(text.data(), text.size(), "%.6g, %.6g", (*at).x, (*at).y);
        if (written > 0) descriptor.value = std::string(text.data());
    }
    descriptor.include_descendants = false;
    descriptor.exposed = true;
    return descriptor;
}

void PlotView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), plot_view_interactions.begin(), plot_view_interactions.end());
}

// -- properties ----------------------------------------------------------------------

void PlotView::prepare_write(const decltype(title)&, std::string&) {
    require_mutable();
}

void PlotView::written(const decltype(title)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void PlotView::prepare_write(const decltype(live)&, bool&) {
    require_mutable();
}

void PlotView::written(const decltype(live)&) {
    update_live_host();
}

void PlotView::prepare_write(const decltype(show_legend)&, bool&) {
    require_mutable();
}

void PlotView::written(const decltype(show_legend)&) {
    invalidate(Dirty::paint);
}

void PlotView::prepare_write(const decltype(show_grid)&, bool&) {
    require_mutable();
}

void PlotView::written(const decltype(show_grid)&) {
    invalidate(Dirty::paint);
}

void PlotView::prepare_write(const decltype(crosshair)&, bool&) {
    require_mutable();
}

void PlotView::written(const decltype(crosshair)&) {
    invalidate(Dirty::paint);
}

// property_codemod begin PlotView (tools/property_families/plot_view.json; generated, do not edit)

namespace {

PropertyDescriptor plot_view_title_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "title";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The words drawn above the plot.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_view_title() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_view_title_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_view_live_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "live";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the plot draws on its own sub-surface.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_view_live() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_view_live_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_view_show_legend_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_legend";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the legend is drawn.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_view_show_legend() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_view_show_legend_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_view_show_grid_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_grid";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the grid is drawn.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_view_show_grid() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_view_show_grid_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_view_crosshair_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "crosshair";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether a crosshair follows the pointer.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_view_crosshair() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_view_crosshair_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, PlotView::plot_view_property_count>& PlotView::plot_view_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 5U> own{{
        {"title", &describe_plot_view_title,
         &read_cell_entry<PlotView, decltype(&PlotView::title), &PlotView::title>,
         &write_cell_entry<PlotView, decltype(&PlotView::title), &PlotView::title>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"live", &describe_plot_view_live,
         &read_cell_entry<PlotView, decltype(&PlotView::live), &PlotView::live>,
         &write_cell_entry<PlotView, decltype(&PlotView::live), &PlotView::live>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_legend", &describe_plot_view_show_legend,
         &read_cell_entry<PlotView, decltype(&PlotView::show_legend), &PlotView::show_legend>,
         &write_cell_entry<PlotView, decltype(&PlotView::show_legend), &PlotView::show_legend>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_grid", &describe_plot_view_show_grid,
         &read_cell_entry<PlotView, decltype(&PlotView::show_grid), &PlotView::show_grid>,
         &write_cell_entry<PlotView, decltype(&PlotView::show_grid), &PlotView::show_grid>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"crosshair", &describe_plot_view_crosshair,
         &read_cell_entry<PlotView, decltype(&PlotView::crosshair), &PlotView::crosshair>,
         &write_cell_entry<PlotView, decltype(&PlotView::crosshair), &PlotView::crosshair>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PlotView's property table names a property twice");
    static const std::array<PropertyEntry, plot_view_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PlotView::property_table() const noexcept {
    const std::array<PropertyEntry, plot_view_property_count>& entries = plot_view_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end PlotView

} // namespace gui_forms
