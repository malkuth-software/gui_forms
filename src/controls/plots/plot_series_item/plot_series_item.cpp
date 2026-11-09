#include "gui_forms/controls/plots/plot_series_item/plot_series_item.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// The PlotSeriesKind enumeration, numbered as declared. Static storage; any
// thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> plot_series_kind_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "plot_series_kind",
            {{"line", 0}, {"stairs", 1}, {"scatter", 2}, {"shaded", 3}, {"bars", 4}, {"stems", 5}},
            false});
    return value;
}

// Whether `xs` never decreases, gaps aside.
bool never_decreases(std::span<const double> xs) noexcept {
    double previous = -std::numeric_limits<double>::infinity();
    for (const double x : xs) {
        if (std::isnan(x)) continue;
        if (x < previous) return false;
        previous = x;
    }
    return true;
}

void require_equal_lengths(std::span<const double> xs, std::span<const double> ys) {
    if (xs.size() != ys.size()) throw std::invalid_argument("PlotSeriesItem x and y counts differ");
}

} // namespace

PlotSeriesItem::PlotSeriesItem(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = false;
    tab_stop = false;
}

// -- data --------------------------------------------------------------------------

void PlotSeriesItem::set_points(std::span<const double> xs, std::span<const double> ys) {
    require_mutable();
    require_equal_lengths(xs, ys);
    const plots::XOrder order = never_decreases(xs) ? plots::XOrder::monotone : plots::XOrder::unordered;
    // Made aside, so a throw (an allocation) leaves the series as it was.
    plots::SeriesColumn<double> new_xs(plots::SeriesMode::growable, xs.size(), order);
    plots::SeriesColumn<double> new_ys(plots::SeriesMode::growable, ys.size(), plots::XOrder::unordered);
    static_cast<void>(new_xs.assign(xs));
    static_cast<void>(new_ys.assign(ys));
    xs_ = std::move(new_xs);
    ys_ = std::move(new_ys);
    order_ = order;
    data_source_ = DataSource::columns;
    borrowed_x_ = plots::BorrowedSeries<double>{};
    borrowed_y_ = plots::BorrowedSeries<double>{};
    update_pyramid(0U);
    notify_plot();
}

void PlotSeriesItem::append_points(std::span<const double> xs, std::span<const double> ys) {
    require_mutable();
    require_equal_lengths(xs, ys);
    if (data_source_ != DataSource::columns || xs_.mode() != plots::SeriesMode::growable) {
        throw std::logic_error("PlotSeriesItem append_points needs copied-in points");
    }
    const std::size_t previous = xs_.length();
    const std::size_t needed = previous + xs.size();
    if (needed > xs_.capacity()) {
        // Grows geometrically, as std::vector does.
        const std::size_t grown = std::max(needed, xs_.capacity() * 2U);
        static_cast<void>(xs_.reserve(grown));
        static_cast<void>(ys_.reserve(grown));
    }
    const plots::SeriesEditOutcome x_outcome = xs_.append(xs);
    if (x_outcome == plots::SeriesEditOutcome::refused_order) {
        throw std::invalid_argument("PlotSeriesItem append_points would put a monotone series out of order");
    }
    static_cast<void>(ys_.append(ys));
    update_pyramid(previous);
    notify_plot();
}

void PlotSeriesItem::set_values(std::span<const double> ys) {
    require_mutable();
    // The implied x, written out once: the columns are what the plot reads.
    std::vector<double> implied(ys.size());
    for (std::size_t index = 0U; index < ys.size(); ++index) implied[index] = static_cast<double>(index);
    plots::SeriesColumn<double> new_xs(plots::SeriesMode::growable, ys.size(), plots::XOrder::monotone);
    plots::SeriesColumn<double> new_ys(plots::SeriesMode::growable, ys.size(), plots::XOrder::unordered);
    static_cast<void>(new_xs.assign(implied));
    static_cast<void>(new_ys.assign(ys));
    xs_ = std::move(new_xs);
    ys_ = std::move(new_ys);
    order_ = plots::XOrder::monotone;
    data_source_ = DataSource::columns;
    borrowed_x_ = plots::BorrowedSeries<double>{};
    borrowed_y_ = plots::BorrowedSeries<double>{};
    update_pyramid(0U);
    notify_plot();
}

void PlotSeriesItem::make_ring(std::size_t capacity) {
    plots::SeriesColumn<double> new_xs(plots::SeriesMode::ring, capacity, plots::XOrder::monotone);
    plots::SeriesColumn<double> new_ys(plots::SeriesMode::ring, capacity, plots::XOrder::unordered);
    xs_ = std::move(new_xs);
    ys_ = std::move(new_ys);
    order_ = plots::XOrder::monotone;
    data_source_ = DataSource::columns;
    borrowed_x_ = plots::BorrowedSeries<double>{};
    borrowed_y_ = plots::BorrowedSeries<double>{};
    pyramid_valid_ = false;
}

void PlotSeriesItem::use_ring(std::size_t capacity) {
    require_mutable();
    if (capacity < 2U) throw std::invalid_argument("PlotSeriesItem ring capacity must be at least 2");
    make_ring(capacity);
    notify_plot();
}

void PlotSeriesItem::push_points(std::span<const double> xs, std::span<const double> ys) {
    require_mutable();
    require_equal_lengths(xs, ys);
    if (data_source_ != DataSource::columns || xs_.mode() != plots::SeriesMode::ring) {
        throw std::logic_error("PlotSeriesItem push_points needs a ring (use_ring)");
    }
    const plots::SeriesEditOutcome outcome = xs_.push_overwrite(xs);
    if (outcome == plots::SeriesEditOutcome::refused_order) {
        throw std::invalid_argument("PlotSeriesItem push_points would go back in x");
    }
    static_cast<void>(ys_.push_overwrite(ys));
    notify_plot();
}

void PlotSeriesItem::borrow_points(plots::BorrowedSeries<double> xs, plots::BorrowedSeries<double> ys,
                                   plots::XOrder order) {
    require_mutable();
    if (xs.check() != plots::SeriesViewCheck::valid || ys.check() != plots::SeriesViewCheck::valid) {
        throw std::invalid_argument("PlotSeriesItem borrowed views must be valid");
    }
    borrowed_x_ = std::move(xs);
    borrowed_y_ = std::move(ys);
    order_ = order;
    data_source_ = DataSource::borrowed;
    pyramid_valid_ = false;
    notify_plot();
}

std::shared_ptr<plots::SeriesRingChannel> PlotSeriesItem::live_channel(std::size_t capacity) {
    require_mutable();
    if (!channel_) {
        channel_ = std::make_shared<plots::SeriesRingChannel>(capacity, 2U);
        make_ring((*channel_).capacity());
        notify_plot();
    }
    return channel_;
}

void PlotSeriesItem::clear_points() {
    require_mutable();
    xs_.clear();
    ys_.clear();
    borrowed_x_ = plots::BorrowedSeries<double>{};
    borrowed_y_ = plots::BorrowedSeries<double>{};
    data_source_ = DataSource::columns;
    pyramid_valid_ = false;
    notify_plot();
}

plots::ColumnSamples<double, double> PlotSeriesItem::column_samples() const noexcept {
    plots::ColumnSamples<double, double> samples{};
    samples.xs = xs_.read();
    samples.ys = ys_.read();
    return samples;
}

const plots::EnvelopePyramid* PlotSeriesItem::pyramid() const noexcept {
    const bool matches = pyramid_valid_ && data_source_ == DataSource::columns && pyramid_.size() == ys_.length();
    return matches ? &pyramid_ : nullptr;
}

void PlotSeriesItem::update_pyramid(std::size_t previous) {
    const bool wanted = order_ == plots::XOrder::monotone && xs_.mode() == plots::SeriesMode::growable &&
                        ys_.length() >= pyramid_threshold && ys_.gap_count() == 0U && xs_.gap_count() == 0U;
    if (!wanted) {
        pyramid_valid_ = false;
        return;
    }
    if (pyramid_.capacity() < ys_.capacity()) {
        pyramid_.reserve(ys_.capacity());
        previous = 0U;
    }
    const plots::ColumnSamples<double, double> samples = column_samples();
    const plots::PyramidOutcome outcome =
        pyramid_valid_ && previous != 0U ? pyramid_.extend(samples, previous) : pyramid_.build(samples);
    pyramid_valid_ = outcome == plots::PyramidOutcome::built;
}

bool PlotSeriesItem::run_signal_frame(const plots::SignalFrame& frame, plots::SignalSink& sink) {
    if (!signal_) return false;
    if (xs_.mode() != plots::SeriesMode::ring) make_ring(signal_ring_capacity);
    sink.clear();
    plots::run_signal(*signal_, frame, sink);
    if (sink.count == 0U) return false;
    const std::span<const double> times(sink.times.data(), sink.count);
    const std::span<const double> values(sink.values.data(), sink.count);
    static_cast<void>(xs_.push_overwrite(times));
    static_cast<void>(ys_.push_overwrite(values));
    revision_ = revision_ + 1U;
    return true;
}

std::size_t PlotSeriesItem::consume_channel(std::span<double> scratch) {
    if (!channel_) return 0U;
    if (xs_.mode() != plots::SeriesMode::ring) make_ring((*channel_).capacity());
    std::array<plots::SeriesColumn<double>*, 2> columns{&xs_, &ys_};
    const std::size_t taken = (*channel_).consume(columns, scratch);
    if (taken != 0U) revision_ = revision_ + 1U;
    return taken;
}

// -- control -------------------------------------------------------------------------

Size PlotSeriesItem::measure(Size) {
    return Size{0.0, 0.0};
}

bool PlotSeriesItem::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor PlotSeriesItem::semantic_descriptor() const {
    // The plot publishes its series in its own description and table.
    SemanticDescriptor descriptor;
    descriptor.exposed = false;
    return descriptor;
}

void PlotSeriesItem::notify_plot() {
    revision_ = revision_ + 1U;
    const Control::Ptr owner = parent();
    if (owner) (*owner).invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

// -- property hooks --------------------------------------------------------------------

void PlotSeriesItem::prepare_write(const decltype(kind)&, PlotSeriesKind&) {
    require_mutable();
}

void PlotSeriesItem::written(const decltype(kind)&) {
    notify_plot();
}

void PlotSeriesItem::prepare_write(const decltype(label)&, std::string&) {
    require_mutable();
}

void PlotSeriesItem::written(const decltype(label)&) {
    notify_plot();
}

// A declaration is parsed before it is stored: one that does not parse is
// refused whole.
void PlotSeriesItem::prepare_write(const decltype(source)&, std::string& incoming) {
    require_mutable();
    if (incoming.empty()) return;
    plots::SignalProgram program{};
    const plots::SignalParseOutcome outcome = plots::parse_signal_source(incoming, program);
    if (outcome != plots::SignalParseOutcome::parsed) {
        throw std::invalid_argument("PlotSeriesItem source is not a signal program declaration");
    }
}

void PlotSeriesItem::written(const decltype(source)&) {
    const std::string& text = source.read();
    if (text.empty()) {
        signal_.reset();
    } else {
        plots::SignalProgram program{};
        static_cast<void>(plots::parse_signal_source(text, program));
        signal_ = program;
        make_ring(signal_ring_capacity);
    }
    notify_plot();
}

void PlotSeriesItem::prepare_write(const decltype(hidden)&, bool&) {
    require_mutable();
}

void PlotSeriesItem::written(const decltype(hidden)&) {
    notify_plot();
}

void PlotSeriesItem::prepare_write(const decltype(x_axis)&, std::uint32_t&) {
    require_mutable();
}

void PlotSeriesItem::written(const decltype(x_axis)&) {
    notify_plot();
}

void PlotSeriesItem::prepare_write(const decltype(y_axis)&, std::uint32_t&) {
    require_mutable();
}

void PlotSeriesItem::written(const decltype(y_axis)&) {
    notify_plot();
}

// property_codemod begin PlotSeriesItem (tools/property_families/plot_series_item.json; generated, do not edit)

namespace {

PropertyDescriptor plot_series_item_kind_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "kind";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "What the series draws.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"plot_series_kind", "line", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = plot_series_kind_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_series_item_kind() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_series_item_kind_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_series_item_label_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "label";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The series' name in the legend.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_series_item_label() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_series_item_label_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_series_item_source_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "source";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Data";
    descriptor.description = "A signal program declaration, or empty.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_series_item_source() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_series_item_source_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_series_item_hidden_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "hidden";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the series is hidden.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_series_item_hidden() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_series_item_hidden_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_series_item_x_axis_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "x_axis";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Layout";
    descriptor.description = "The horizontal axis's position.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_series_item_x_axis() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_series_item_x_axis_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_series_item_y_axis_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "y_axis";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Layout";
    descriptor.description = "The vertical axis's position.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_series_item_y_axis() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_series_item_y_axis_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<PlotSeriesKind>::type() {
    return PropertyType{BindingValueKind::enumeration, false, plot_series_kind_enum()};
}

BindingValue PropertyValueTraits<PlotSeriesKind>::to_binding(PlotSeriesKind value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = plot_series_kind_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

PlotSeriesKind PropertyValueTraits<PlotSeriesKind>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<PlotSeriesKind>(choice.value);
}

const std::array<PropertyEntry, PlotSeriesItem::plot_series_item_property_count>& PlotSeriesItem::plot_series_item_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 6U> own{{
        {"kind", &describe_plot_series_item_kind,
         &read_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::kind), &PlotSeriesItem::kind>,
         &write_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::kind), &PlotSeriesItem::kind>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"label", &describe_plot_series_item_label,
         &read_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::label), &PlotSeriesItem::label>,
         &write_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::label), &PlotSeriesItem::label>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"source", &describe_plot_series_item_source,
         &read_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::source), &PlotSeriesItem::source>,
         &write_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::source), &PlotSeriesItem::source>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"hidden", &describe_plot_series_item_hidden,
         &read_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::hidden), &PlotSeriesItem::hidden>,
         &write_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::hidden), &PlotSeriesItem::hidden>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"x_axis", &describe_plot_series_item_x_axis,
         &read_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::x_axis), &PlotSeriesItem::x_axis>,
         &write_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::x_axis), &PlotSeriesItem::x_axis>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"y_axis", &describe_plot_series_item_y_axis,
         &read_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::y_axis), &PlotSeriesItem::y_axis>,
         &write_cell_entry<PlotSeriesItem, decltype(&PlotSeriesItem::y_axis), &PlotSeriesItem::y_axis>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PlotSeriesItem's property table names a property twice");
    static const std::array<PropertyEntry, plot_series_item_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PlotSeriesItem::property_table() const noexcept {
    const std::array<PropertyEntry, plot_series_item_property_count>& entries = plot_series_item_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end PlotSeriesItem

} // namespace gui_forms
