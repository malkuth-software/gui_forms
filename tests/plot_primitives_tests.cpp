// The plots package's public primitives (IMGUI_IMPLOT_STUDY B.3 and B.7),
// headless and numeric, so every host gives the same answers: series
// columns and their edits, leases and the ring channel, scales against
// their stated bounds, tick golden tables (linear, log, symlog, time across
// months and leap years, categories), axes and the transform, the
// envelope against a brute-force oracle, the pyramid against a full
// rebuild, occupancy, binning edges, the picker against a brute-force
// oracle, colormaps, the view and gesture model, legend and annotation
// layout, and signal programs (checksums per tick, frame grouping, the form
// grammar). Each mark painter's display trace is compared exactly.
#include "gui_forms/plots.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::plots;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

// The distance in ulps between two finite doubles of one sign.
std::uint64_t ulp_distance(double left, double right) {
    if (left == right) return 0U;
    std::int64_t left_bits = 0;
    std::int64_t right_bits = 0;
    std::memcpy(&left_bits, &left, sizeof(double));
    std::memcpy(&right_bits, &right, sizeof(double));
    const std::int64_t difference = left_bits > right_bits ? left_bits - right_bits : right_bits - left_bits;
    return static_cast<std::uint64_t>(difference);
}

// A deterministic pseudo-random walk (EffectRandom's hash, keyed by index).
std::vector<double> random_walk(std::size_t count, std::uint64_t seed) {
    std::vector<double> values(count);
    double level = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t bits = EffectRandom::hash(seed, 1U, index) >> 11U;
        const double unit = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
        level = level + (unit - 0.5);
        values[index] = level;
    }
    return values;
}

std::vector<double> ramp(std::size_t count, double start, double step) {
    std::vector<double> values(count);
    for (std::size_t index = 0U; index < count; ++index) values[index] = start + step * static_cast<double>(index);
    return values;
}

ColumnSamples<double, double> samples_of(const SeriesColumn<double>& xs, const SeriesColumn<double>& ys) {
    ColumnSamples<double, double> samples{};
    samples.xs = xs.read();
    samples.ys = ys.read();
    return samples;
}

Axis linear_axis(double minimum, double maximum, AxisSide side) {
    Axis axis{};
    axis.range = PlotRange{minimum, maximum};
    axis.side = side;
    return axis;
}

PlotTransform transform_over(PlotRange x, PlotRange y, Rect plot, double scale) {
    const PlotTransformResult made =
        PlotTransform::make(linear_axis(x.minimum, x.maximum, AxisSide::bottom),
                            linear_axis(y.minimum, y.maximum, AxisSide::left), plot, scale);
    require(made.check == PlotTransformCheck::valid, "the test transform must be valid");
    return made.transform;
}

// -- series columns ----------------------------------------------------------------

void test_growable_column_edits() {
    SeriesColumn<double> column(SeriesMode::growable, 8U, XOrder::unordered);
    require(column.capacity() == 8U && column.length() == 0U, "a column states its capacity and length");
    const std::array<double, 3> first{1.0, 2.0, 3.0};
    require(column.append(first) == SeriesEditOutcome::applied && column.length() == 3U, "append adds");
    require(column.dirty() == (SeriesDirtyRange{ElementSequence{0U}, ElementSequence{3U}}), "append marks its range");
    const std::uint64_t revision = column.revision();
    const std::array<double, 6> too_many{};
    require(column.append(too_many) == SeriesEditOutcome::refused_capacity && column.length() == 3U &&
                column.revision() == revision,
            "an append past capacity is refused whole");
    const std::array<double, 2> patch{9.0, not_a_number};
    static_cast<void>(column.take_dirty());
    require(column.write_range(ElementIndex{1U}, patch) == SeriesEditOutcome::applied, "write_range overwrites");
    require(column.read().at(ElementIndex{1U}) == 9.0 && std::isnan(column.read().at(ElementIndex{2U})) &&
                column.gap_count() == 1U,
            "write_range stores and counts gaps");
    require(column.dirty() == (SeriesDirtyRange{ElementSequence{1U}, ElementSequence{3U}}), "write_range marks");
    require(column.write_range(ElementIndex{2U}, patch) == SeriesEditOutcome::refused_range,
            "write_range past the length is refused");
    require(column.reserve(2U) == SeriesEditOutcome::refused_capacity, "reserve below the length is refused");
    require(column.reserve(16U) == SeriesEditOutcome::applied && column.capacity() == 16U &&
                column.read().at(ElementIndex{0U}) == 1.0,
            "reserve keeps the elements");
    const std::array<double, 2> replacement{5.0, 6.0};
    require(column.assign(replacement) == SeriesEditOutcome::applied && column.length() == 2U &&
                column.gap_count() == 0U && column.dropped() == 0U,
            "assign replaces; a growable column's sequence is its index");
    require(column.push_overwrite(replacement) == SeriesEditOutcome::refused_mode, "push_overwrite is a ring's");
}

void test_monotone_column_refuses_disorder() {
    SeriesColumn<double> column(SeriesMode::growable, 16U, XOrder::monotone);
    const std::array<double, 4> rising{0.0, 1.0, not_a_number, 2.0};
    require(column.assign(rising) == SeriesEditOutcome::applied, "rising values with a gap are monotone");
    const std::array<double, 1> back{1.5};
    require(column.append(back) == SeriesEditOutcome::refused_order && column.length() == 4U,
            "an append going back is refused");
    const std::array<double, 1> fits{0.5};
    require(column.write_range(ElementIndex{1U}, fits) == SeriesEditOutcome::applied,
            "a write between its neighbours keeps order");
    const std::array<double, 1> breaks{3.0};
    require(column.write_range(ElementIndex{1U}, breaks) == SeriesEditOutcome::refused_order,
            "a write above the next value is refused");
    SeriesColumn<std::int32_t> integers(SeriesMode::growable, 4U, XOrder::monotone);
    const std::array<std::int32_t, 3> counts{1, 2, 2};
    require(integers.assign(counts) == SeriesEditOutcome::applied && integers.gap_count() == 0U,
            "integer columns never hold gaps");
}

void test_ring_column() {
    SeriesColumn<float> ring(SeriesMode::ring, 4U, XOrder::monotone);
    const std::array<float, 3> first{1.0F, 2.0F, 3.0F};
    require(ring.push_overwrite(first) == SeriesEditOutcome::applied && ring.length() == 3U, "a ring takes a push");
    const std::array<float, 3> second{4.0F, 5.0F, 6.0F};
    require(ring.push_overwrite(second) == SeriesEditOutcome::applied && ring.length() == 4U && ring.dropped() == 2U,
            "a full ring drops its oldest");
    const SeriesSpans<float> spans = ring.read();
    require(spans.size() == 4U && !spans.second.empty(), "a wrapped ring reads as two spans");
    require(spans.at(ElementIndex{0U}) == 3.0F && spans.at(ElementIndex{3U}) == 6.0F, "the ring reads oldest first");
    require(ring.dirty().end == ElementSequence{6U}, "dirty ranges are sequence numbers");
    const std::array<float, 6> huge{7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F};
    require(ring.push_overwrite(huge) == SeriesEditOutcome::applied && ring.read().at(ElementIndex{0U}) == 9.0F &&
                ring.written() == 12U,
            "a push larger than the ring keeps its newest");
    const std::array<float, 1> back{1.0F};
    require(ring.push_overwrite(back) == SeriesEditOutcome::refused_order, "a monotone ring never goes back");
    require(ring.append(back) == SeriesEditOutcome::refused_mode, "a full ring refuses append");
}

void test_lease_and_borrowed_views() {
    struct Record final {
        double time{};
        float level{};
        std::int32_t flags{};
    };
    std::array<Record, 3> records{{{0.0, 1.5F, 0}, {1.0, 2.5F, 0}, {2.0, 3.5F, 0}}};
    SeriesView<float> view{};
    view.data = reinterpret_cast<const std::byte*>(records.data());
    view.count = records.size();
    view.stride_bytes = sizeof(Record);
    view.offset_bytes = offsetof(Record, level);
    require(validate_series_view(view) == SeriesViewCheck::valid, "a struct-of-fields view validates");
    require(series_view_at(view, ElementIndex{2U}) == 3.5F, "a view reads its strided element");
    SeriesView<double> times{};
    times.data = view.data;
    times.count = records.size();
    times.stride_bytes = sizeof(Record);
    times.offset_bytes = offsetof(Record, time);
    ViewSamples<double, float> paired{};
    paired.xs = times;
    paired.ys = view;
    require(paired.size() == 3U && paired.x(1U) == 1.0 && paired.y(1U) == 2.5, "two views read as samples");
    SeriesView<double> narrow{};
    narrow.data = view.data;
    narrow.count = 2U;
    narrow.stride_bytes = 4U;
    require(validate_series_view(narrow) == SeriesViewCheck::stride_too_small, "a stride under the element refuses");
    SeriesLease lease;
    const BorrowedSeries<float> borrowed(view, lease);
    {
        const SeriesPin pin((borrowed.lease()));
        require(pin.held(), "a held lease pins");
    }
    require(borrowed.check() == SeriesViewCheck::valid && !(*borrowed.lease()).released(), "the lease is held");
    // Every pin above has ended, so release returns at once.
    lease.release();
    const SeriesPin late((borrowed.lease()));
    require(!late.held() && (*borrowed.lease()).refused_pins() == 1U, "a pin after release is refused and counted");
}

void test_ring_channel() {
    SeriesRingChannel channel(8U, 2U);
    std::unique_ptr<SeriesWriteLease> lease = channel.acquire_write_lease();
    require(lease != nullptr && channel.acquire_write_lease() == nullptr, "a channel has one producer");
    SeriesColumn<double> xs(SeriesMode::ring, 8U, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::ring, 8U, XOrder::unordered);
    std::array<SeriesColumn<double>*, 2> columns{&xs, &ys};
    std::vector<double> scratch(16U);
    const std::array<double, 6> block{0.0, 10.0, 1.0, 11.0, 2.0, 12.0};
    (*lease).write(block);
    require(channel.consume(columns, scratch) == 3U && xs.length() == 3U && ys.read().at(ElementIndex{2U}) == 12.0,
            "consume moves published samples into the columns");
    require(channel.consume(columns, scratch) == 0U && channel.statistics().published == 3U &&
                channel.statistics().consumed == 3U,
            "nothing new is nothing taken");
    // Twelve samples into an eight-sample channel: four are lost.
    std::vector<double> many;
    for (int sample = 3; sample < 15; ++sample) {
        many.push_back(static_cast<double>(sample));
        many.push_back(static_cast<double>(sample) + 10.0);
    }
    (*lease).write(many);
    const std::size_t taken = channel.consume(columns, scratch);
    require(taken == 8U && channel.statistics().overrun == 4U, "samples the producer lapped are counted as overrun");
    require(xs.read().at(ElementIndex{7U}) == 14.0, "the newest samples arrive");
    lease.reset();
    require(channel.acquire_write_lease() != nullptr, "an ended lease frees the channel");
}

// -- scales ------------------------------------------------------------------------

void test_scale_round_trips() {
    Scale scale{};
    scale.domain = PlotRange{-3.5, 1234.25};
    scale.device_start = 10.0;
    scale.device_end = 810.0;
    const ScaleMappingResult linear = ScaleMapping::make(scale);
    require(linear.check == ScaleCheck::valid, "a linear scale prepares");
    std::uint64_t worst = 0U;
    for (int step = 0; step <= 1000; ++step) {
        const double value = scale.domain.minimum + scale.domain.extent() * static_cast<double>(step) / 1000.0;
        const double back = linear.mapping.inverse(linear.mapping.forward(value));
        const double tolerance = std::fabs(value) < 1.0 ? 1e-12 : 0.0;
        if (std::fabs(back - value) > tolerance) worst = std::max(worst, ulp_distance(back, value));
    }
    require(worst <= 8U, "a linear round trip stays within a few ulp");
    require(linear.mapping.forward(scale.domain.minimum) == 10.0 && linear.mapping.forward(scale.domain.maximum) == 810.0,
            "the domain's ends map to the device's ends");
    Scale log_scale = scale;
    log_scale.kind = ScaleKind::log10;
    log_scale.domain = PlotRange{1e-3, 1e6};
    const ScaleMappingResult log = ScaleMapping::make(log_scale);
    require(log.check == ScaleCheck::valid, "a positive log scale prepares");
    double worst_relative = 0.0;
    for (int step = 0; step <= 900; ++step) {
        const double value = engine_exp10(-3.0 + static_cast<double>(step) / 100.0);
        const double back = log.mapping.inverse(log.mapping.forward(value));
        worst_relative = std::max(worst_relative, std::fabs(back - value) / value);
    }
    require(worst_relative < 1e-12, "a log round trip holds engine_math's bound");
    require(log.mapping.transform(1000.0) == 3.0 && log.mapping.transform(1e-3) == -3.0,
            "exact powers of ten map to exact decades");
    require(std::isnan(log.mapping.forward(0.0)) && std::isnan(log.mapping.forward(-5.0)),
            "a log scale culls values at or below zero");
    const std::array<double, 3> values{1.0, -1.0, 10.0};
    std::array<double, 3> devices{};
    require(log.mapping.forward_span(values, devices) == 1U, "the span form counts culled values");
    Scale symlog = scale;
    symlog.kind = ScaleKind::symlog;
    symlog.domain = PlotRange{-1000.0, 1000.0};
    symlog.symlog_width = 1.0;
    const ScaleMappingResult made_symlog = ScaleMapping::make(symlog);
    require(made_symlog.check == ScaleCheck::valid, "a symlog scale prepares");
    require(made_symlog.mapping.forward(0.0) == 410.0, "symlog maps zero to the middle of a symmetric range");
    const double back = made_symlog.mapping.inverse(made_symlog.mapping.forward(-250.0));
    require(std::fabs(back + 250.0) < 1e-9, "symlog round-trips");
}

void test_scale_refusals() {
    Scale scale{};
    scale.domain = PlotRange{1.0, 1.0};
    require(ScaleMapping::make(scale).check == ScaleCheck::domain_invalid, "an empty domain is refused");
    scale.domain = PlotRange{0.0, 10.0};
    scale.kind = ScaleKind::log10;
    require(ScaleMapping::make(scale).check == ScaleCheck::domain_not_positive, "a log domain reaching 0 is refused");
    scale.kind = ScaleKind::linear;
    scale.device_end = scale.device_start;
    require(ScaleMapping::make(scale).check == ScaleCheck::device_invalid, "an empty device range is refused");
    scale.device_end = 1.0;
    scale.kind = ScaleKind::symlog;
    scale.symlog_width = 0.0;
    require(ScaleMapping::make(scale).check == ScaleCheck::symlog_width_invalid, "a zero symlog width is refused");
}

// -- ticks -------------------------------------------------------------------------

std::string tick_line(const TickSet& ticks) {
    std::string text;
    for (const MajorTick& tick : ticks.majors()) {
        if (!text.empty()) text += ' ';
        text += tick.labelled ? std::string(tick.label.text()) : std::string("_");
    }
    return text;
}

ScaleMapping horizontal(ScaleKind kind, double minimum, double maximum, double width) {
    Scale scale{};
    scale.kind = kind;
    scale.domain = PlotRange{minimum, maximum};
    scale.device_start = 0.0;
    scale.device_end = width;
    const ScaleMappingResult made = ScaleMapping::make(scale);
    require(made.check == ScaleCheck::valid, "the tick test scale must be valid");
    return made.mapping;
}

void expect_ticks(ScaleKind kind, double minimum, double maximum, double width, const char* expected,
                  const char* message) {
    TickSet ticks{};
    TickRequest request{};
    request.minimum_spacing = 50.0;
    const EstimatedLabelMetrics metrics(7.0, 0.0);
    static_cast<void>(generate_ticks(horizontal(kind, minimum, maximum, width), request, &metrics, ticks));
    const std::string line = tick_line(ticks);
    if (line != expected) {
        std::cerr << message << ": got \"" << line << "\", expected \"" << expected << "\"\n";
    }
    require(line == expected, message);
}

void test_tick_golden_tables() {
    require(nice_step(0.37) == 0.5 && nice_step(3.0) == 5.0 && nice_step(7.0) == 10.0 && nice_step(1.0) == 1.0,
            "nice steps are 1, 2 and 5 times powers of ten");
    expect_ticks(ScaleKind::linear, 0.0, 10.0, 500.0, "0 1 2 3 4 5 6 7 8 9 10", "a unit range ticks every 1");
    expect_ticks(ScaleKind::linear, -1.0, 1.0, 400.0, "-1.0 -0.5 0.0 0.5 1.0",
                 "a zero-crossing range ticks through zero exactly");
    expect_ticks(ScaleKind::linear, 0.5e-9, 5.5e-9, 260.0, "1E-09 2E-09 3E-09 4E-09 5E-09",
                 "tiny steps use exponents");
    // Step 5E+08 at 55 units: "1.5E+09" needs 57, so every second is labelled.
    expect_ticks(ScaleKind::linear, 0.0, 3e9, 330.0, "0 _ 1E+09 _ 2E+09 _ 3E+09",
                 "huge steps use exponents and thin by the widest label");
    expect_ticks(ScaleKind::linear, 99.95, 100.55, 330.0, "100.0 100.1 100.2 100.3 100.4 100.5",
                 "a narrow range far from zero keeps its digits");
    expect_ticks(ScaleKind::log10, 1.0, 1e6, 600.0, "1 10 100 1000 10000 1E5 1E6",
                 "log ticks decades, as numbers within four decades of one");
    expect_ticks(ScaleKind::log10, 1e-3, 1e3, 120.0, "0.001 1 1000", "crowded decades take a stride of three");
    expect_ticks(ScaleKind::category, -0.5, 2.5, 300.0, "0 1 2", "categories without names are numbered");
    TickSet named{};
    TickRequest request{};
    const std::array<std::string_view, 3> names{"low", "mid", "high"};
    request.categories = names;
    static_cast<void>(generate_ticks(horizontal(ScaleKind::category, -0.5, 2.5, 300.0), request, nullptr, named));
    require(tick_line(named) == "low mid high", "categories are labelled by name");
    TickSet empty{};
    require(generate_ticks(horizontal(ScaleKind::linear, 0.0, 1.0, 0.5), request, nullptr, empty) ==
                TickOutcome::too_small && empty.major_count == 0U,
            "a sub-unit extent has no ticks");
}

void test_labels_never_overlap() {
    // Wide labels on a narrow axis: only every m-th is labelled, and the
    // labelled ones keep their measured widths apart.
    TickSet ticks{};
    TickRequest request{};
    request.minimum_spacing = 20.0;
    request.label_gap = 6.0;
    const EstimatedLabelMetrics metrics(9.0, 0.0);
    static_cast<void>(generate_ticks(horizontal(ScaleKind::linear, 0.0, 100000.0, 400.0), request, &metrics, ticks));
    double last_end = -1e9;
    for (const MajorTick& tick : ticks.majors()) {
        if (!tick.labelled) continue;
        const double width = metrics.label_extent(tick.label.text());
        require(tick.device - width * 0.5 >= last_end + request.label_gap - 1e-9, "labelled ticks never overlap");
        last_end = tick.device + width * 0.5;
    }
    require(ticks.label_every > 1U, "the axis was thinned");
}

void test_civil_time() {
    require(civil_from_seconds(0) == (CivilTime{1970, 1, 1, 0, 0, 0}), "the epoch is 1970-01-01");
    // 2024-02-29T12:34:56Z, a leap day.
    const CivilTime leap{2024, 2, 29, 12, 34, 56};
    const std::int64_t seconds = seconds_from_civil(leap);
    require(seconds == 1709210096, "a leap day's seconds are exact");
    require(civil_from_seconds(seconds) == leap, "civil time round-trips");
    require(civil_from_seconds(-1) == (CivilTime{1969, 12, 31, 23, 59, 59}), "times before the epoch work");
    require(civil_from_seconds(seconds_from_civil(CivilTime{2100, 3, 1, 0, 0, 0}) - 86400) ==
                (CivilTime{2100, 2, 28, 0, 0, 0}),
            "2100 is not a leap year");
}

void test_time_ticks() {
    // 2023-11-15 to 2024-04-15: monthly ticks across a year end and a leap
    // February, in UTC.
    const double start = static_cast<double>(seconds_from_civil(CivilTime{2023, 11, 15, 0, 0, 0}));
    const double end = static_cast<double>(seconds_from_civil(CivilTime{2024, 4, 15, 0, 0, 0}));
    expect_ticks(ScaleKind::time, start, end, 600.0, "2023-12 2024-01 2024-02 2024-03 2024-04",
                 "time ticks step by calendar months");
    const double day = static_cast<double>(seconds_from_civil(CivilTime{2024, 2, 29, 0, 0, 0}));
    expect_ticks(ScaleKind::time, day, day + 6.0 * 3600.0, 450.0, "00:00 01:00 02:00 03:00 04:00 05:00 06:00",
                 "hour ticks label hours and minutes");
    expect_ticks(ScaleKind::time, day, day + 30.0, 450.0,
                 "00:00:00 00:00:05 00:00:10 00:00:15 00:00:20 00:00:25 00:00:30", "second ticks label seconds");
}

// -- transform -------------------------------------------------------------------------

void test_axes_and_transform() {
    Axis y = linear_axis(0.0, 10.0, AxisSide::left);
    const Scale y_scale = axis_scale(y, 0.0, 100.0);
    require(y_scale.device_start == 100.0 && y_scale.device_end == 0.0, "a y axis grows upwards");
    y.inverted = true;
    require(axis_scale(y, 0.0, 100.0).device_start == 0.0, "an inverted y axis grows downwards");
    Axis log_axis = linear_axis(0.0, 10.0, AxisSide::bottom);
    log_axis.kind = ScaleKind::log10;
    require(!admissible_range(log_axis, log_axis.range), "a log axis refuses a range reaching zero");
    const PlotTransform transform = transform_over(PlotRange{0.0, 10.0}, PlotRange{0.0, 1.0}, Rect{20.0, 10.0, 200.0, 100.0}, 2.0);
    require(transform.to_device(5.0, 0.5) == (Point{120.0, 60.0}), "the transform maps data to the plot");
    require(transform.column_count() == 400U && transform.column_of(20.4).value == 0 &&
                transform.column_of(20.5).value == 1,
            "device columns count device pixels");
    require(transform.snap_line(50.2, 0.5) == 50.25 && transform.snap_line(50.2, 1.0) == 50.0,
            "odd device widths snap to centres, even to boundaries");
    AxisLink link(PlotRange{0.0, 1.0});
    require(link.set_range(PlotRange{0.0, 2.0}) && link.revision() == 1U && !link.set_range(PlotRange{0.0, 2.0}),
            "a link records changes once");
}

// -- decimation -------------------------------------------------------------------------

// The oracle: the envelope computed the obvious way, per column, from
// every element.
std::vector<Point> brute_envelope(const std::vector<double>& xs, const std::vector<double>& ys,
                                  const PlotTransform& transform) {
    std::vector<Point> out;
    std::size_t index = 0U;
    while (index < xs.size()) {
        const std::int64_t column = transform.column_of(transform.x().forward(xs[index])).value;
        std::size_t end = index;
        while (end < xs.size() && transform.column_of(transform.x().forward(xs[end])).value == column) end = end + 1U;
        std::size_t minimum = index;
        std::size_t maximum = index;
        for (std::size_t probe = index; probe < end; ++probe) {
            if (ys[probe] < ys[minimum]) minimum = probe;
            if (ys[probe] > ys[maximum]) maximum = probe;
        }
        std::vector<std::size_t> chosen{index, std::min(minimum, maximum), std::max(minimum, maximum), end - 1U};
        std::size_t previous = static_cast<std::size_t>(-1);
        for (const std::size_t pick : chosen) {
            if (pick == previous) continue;
            previous = pick;
            out.push_back(transform.to_device(xs[pick], ys[pick]));
        }
        index = end;
    }
    return out;
}

void test_envelope_matches_oracle() {
    const std::size_t count = 20000U;
    SeriesColumn<double> xs(SeriesMode::growable, count, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, count, XOrder::unordered);
    const std::vector<double> x_values = ramp(count, 0.0, 0.001);
    const std::vector<double> y_values = random_walk(count, 42U);
    require(xs.assign(x_values) == SeriesEditOutcome::applied && ys.assign(y_values) == SeriesEditOutcome::applied,
            "the test series load");
    const PlotTransform transform = transform_over(PlotRange{0.0, 20.0}, PlotRange{-60.0, 60.0}, Rect{0.0, 0.0, 300.0, 200.0}, 1.0);
    const ColumnSamples<double, double> samples = samples_of(xs, ys);
    std::vector<Point> out(envelope_capacity(transform.column_count()));
    const ElementRange all{0U, count};
    const DecimationStatistics statistics = decimate_envelope(samples, all, transform, out);
    const std::vector<Point> oracle = brute_envelope(x_values, y_values, transform);
    require(!statistics.truncated && statistics.points_out == oracle.size(), "the envelope keeps the oracle's count");
    for (std::size_t index = 0U; index < oracle.size(); ++index) {
        require(out[index] == oracle[index], "the envelope equals the oracle point for point");
    }
    require(statistics.points_out <= 4U * transform.column_count() + 4U, "at most four points per column");
    const ElementRange visible = visible_range(samples, 5.0, 6.0);
    require(samples.x(visible.first) < 5.0 && samples.x(visible.end - 1U) > 6.0, "culling keeps one beyond each end");
}

void test_envelope_gaps() {
    SeriesColumn<double> xs(SeriesMode::growable, 8U, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, 8U, XOrder::unordered);
    const std::array<double, 6> x_values{0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    const std::array<double, 6> y_values{0.0, 1.0, not_a_number, not_a_number, 2.0, 3.0};
    static_cast<void>(xs.assign(x_values));
    static_cast<void>(ys.assign(y_values));
    const PlotTransform transform = transform_over(PlotRange{0.0, 5.0}, PlotRange{0.0, 3.0}, Rect{0.0, 0.0, 50.0, 30.0}, 1.0);
    std::array<Point, 32> out{};
    const DecimationStatistics statistics =
        decimate_envelope(samples_of(xs, ys), ElementRange{0U, 6U}, transform, std::span<Point>(out));
    require(statistics.runs == 2U && statistics.points_out == 5U, "two runs, one gap point between them");
    require(std::isnan(out[2].x) && out[3] == transform.to_device(4.0, 2.0), "the gap is one NaN point");
}

void test_pyramid_equals_rebuild_and_envelope() {
    const std::size_t count = 100003U;
    SeriesColumn<double> xs(SeriesMode::growable, count, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, count, XOrder::unordered);
    const std::vector<double> x_values = ramp(count, 0.0, 1.0);
    const std::vector<double> y_values = random_walk(count, 7U);
    // Grown in blocks, extending the pyramid each time.
    EnvelopePyramid grown;
    grown.reserve(count);
    std::size_t loaded = 0U;
    const std::array<std::size_t, 5> blocks{1U, 6U, 993U, 50000U, 49003U};
    for (const std::size_t block : blocks) {
        const std::span<const double> x_block(x_values.data() + loaded, block);
        const std::span<const double> y_block(y_values.data() + loaded, block);
        require(xs.append(x_block) == SeriesEditOutcome::applied && ys.append(y_block) == SeriesEditOutcome::applied,
                "blocks append");
        require(grown.extend(samples_of(xs, ys), loaded) == PyramidOutcome::built, "the pyramid extends");
        loaded = loaded + block;
    }
    EnvelopePyramid rebuilt;
    rebuilt.reserve(count);
    require(rebuilt.build(samples_of(xs, ys)) == PyramidOutcome::built, "the pyramid builds");
    require(grown == rebuilt, "incremental extension equals a full rebuild");
    require(rebuilt.level_count() >= 10U, "a large series has many levels");
    const PlotTransform transform =
        transform_over(PlotRange{1000.0, 90000.0}, PlotRange{-400.0, 400.0}, Rect{0.0, 0.0, 1920.0, 400.0}, 1.0);
    const ColumnSamples<double, double> samples = samples_of(xs, ys);
    const ElementRange range = visible_range(samples, 1000.0, 90000.0);
    std::vector<Point> linear(envelope_capacity(transform.column_count()));
    std::vector<Point> levelled(envelope_capacity(transform.column_count()));
    const DecimationStatistics a = decimate_envelope(samples, range, transform, linear);
    const DecimationStatistics b = rebuilt.envelope(samples, range, transform, levelled);
    require(a.points_out == b.points_out, "the pyramid's envelope has the linear envelope's count");
    for (std::size_t index = 0U; index < a.points_out; ++index) {
        require(linear[index] == levelled[index], "the pyramid's envelope equals the linear one");
    }
    SeriesColumn<double> gappy(SeriesMode::growable, 8U, XOrder::unordered);
    const std::array<double, 8> with_gap{0.0, 1.0, not_a_number, 3.0, 4.0, 5.0, 6.0, 7.0};
    static_cast<void>(gappy.assign(with_gap));
    ImplicitXSamples<double> implicit{};
    implicit.ys = gappy.read();
    EnvelopePyramid declined;
    declined.reserve(8U);
    require(declined.build(implicit) == PyramidOutcome::declined_gaps && declined.size() == 0U,
            "the pyramid declines gaps");
}

void test_occupancy_and_binning() {
    SeriesColumn<double> xs(SeriesMode::growable, 6U, XOrder::unordered);
    SeriesColumn<double> ys(SeriesMode::growable, 6U, XOrder::unordered);
    const std::array<double, 6> x_values{0.1, 0.2, 5.5, 0.15, 9.9, -1.0};
    const std::array<double, 6> y_values{0.1, 0.3, 5.5, 0.12, 9.9, 0.0};
    static_cast<void>(xs.assign(x_values));
    static_cast<void>(ys.assign(y_values));
    const PlotTransform transform = transform_over(PlotRange{0.0, 10.0}, PlotRange{0.0, 10.0}, Rect{0.0, 0.0, 10.0, 10.0}, 1.0);
    OccupancyGrid grid;
    grid.reserve(10U, 10U);
    std::array<Point, 8> out{};
    const DecimationStatistics thinned = grid.thin(samples_of(xs, ys), transform, std::span<Point>(out));
    // (0.1, 0.1), (0.2, 0.3) and (0.15, 0.12) share pixel (0, 9); (-1, 0) is
    // outside.
    require(thinned.points_out == 3U && out[0] == (Point{0.5, 9.5}), "one marker per occupied pixel, at its centre");
    std::array<std::uint64_t, 4> counts{};
    SeriesColumn<double> values(SeriesMode::growable, 8U, XOrder::unordered);
    const std::array<double, 8> data{0.0, 0.25, 0.5, 0.75, 1.0, -0.1, 1.1, not_a_number};
    static_cast<void>(values.assign(data));
    const BinTally tally = bin_values(values.read(), BinSpec{0.0, 1.0, 4U}, counts);
    require(counts == (std::array<std::uint64_t, 4>{1U, 1U, 1U, 2U}), "bins are right-open, the last closed");
    require(tally.binned == 5U && tally.below == 1U && tally.above == 1U && tally.gaps == 1U,
            "out-of-range values and gaps are counted apart");
    require(bin_values(values.read(), BinSpec{1.0, 1.0, 4U}, counts).refused, "an empty bin range is refused");
    std::array<std::uint64_t, 4> grid_counts{};
    const BinTally two = bin_points(samples_of(xs, ys), BinSpec{0.0, 10.0, 2U}, BinSpec{0.0, 10.0, 2U}, grid_counts);
    require(two.binned == 5U && grid_counts[0] == 3U && grid_counts[3] == 2U, "two-dimensional bins are row-major");
}

// -- picker --------------------------------------------------------------------------------

void test_picker_matches_oracle() {
    const std::size_t count = 5000U;
    SeriesColumn<double> xs(SeriesMode::growable, count, XOrder::unordered);
    SeriesColumn<double> ys(SeriesMode::growable, count, XOrder::unordered);
    std::vector<double> x_values(count);
    std::vector<double> y_values(count);
    for (std::size_t index = 0U; index < count; ++index) {
        x_values[index] = static_cast<double>(EffectRandom::hash(3U, 0U, index) % 10000U) / 100.0;
        y_values[index] = static_cast<double>(EffectRandom::hash(3U, 1U, index) % 10000U) / 100.0;
    }
    static_cast<void>(xs.assign(x_values));
    static_cast<void>(ys.assign(y_values));
    const PlotTransform transform = transform_over(PlotRange{0.0, 100.0}, PlotRange{0.0, 100.0}, Rect{0.0, 0.0, 400.0, 400.0}, 1.0);
    SeriesPicker picker;
    picker.reserve(count, 1024U);
    const ColumnSamples<double, double> samples = samples_of(xs, ys);
    for (std::size_t query = 0U; query < 200U; ++query) {
        const Point device{static_cast<double>(EffectRandom::hash(9U, 0U, query) % 400U),
                           static_cast<double>(EffectRandom::hash(9U, 1U, query) % 400U)};
        PickResult picked{};
        static_cast<void>(picker.nearest_unordered(samples, SeriesIdentity{0U, 0U}, 1U, transform, device, 12.0, picked));
        double best = std::numeric_limits<double>::infinity();
        std::size_t best_index = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            const Point point = transform.to_device(x_values[index], y_values[index]);
            const double distance = std::hypot(point.x - device.x, point.y - device.y);
            if (distance <= 12.0 && distance < best) {
                best = distance;
                best_index = index;
            }
        }
        const bool expected = std::isfinite(best);
        require(picked.found == expected, "the grid picker finds what the oracle finds");
        if (expected) require(picked.element.value == best_index, "the grid picker picks the oracle's element");
    }
    require(picker.grid_builds() == 1U, "queries between changes reuse one grid");
    // Monotone picking by binary search.
    SeriesColumn<double> mono_x(SeriesMode::growable, 100U, XOrder::monotone);
    SeriesColumn<double> mono_y(SeriesMode::growable, 100U, XOrder::unordered);
    static_cast<void>(mono_x.assign(ramp(100U, 0.0, 1.0)));
    static_cast<void>(mono_y.assign(ramp(100U, 50.0, 0.0)));
    PickResult nearest{};
    const PlotTransform line = transform_over(PlotRange{0.0, 99.0}, PlotRange{0.0, 100.0}, Rect{0.0, 0.0, 99.0, 100.0}, 1.0);
    require(picker.nearest_monotone(samples_of(mono_x, mono_y), SeriesIdentity{1U, 0U}, line, Point{41.4, 50.0}, 5.0,
                                    nearest) &&
                nearest.element.value == 41U && nearest.series == (SeriesIdentity{1U, 0U}),
            "the monotone picker finds the nearest element");
    const std::array<Rect, 2> bars{{{0.0, 0.0, 10.0, 10.0}, {10.0, 0.0, 10.0, 20.0}}};
    require(pick_rect(bars, Point{12.0, 15.0}).index == 1U && !pick_rect(bars, Point{30.0, 1.0}).found,
            "bars pick by containment");
}

// -- colormaps ---------------------------------------------------------------------------------

void test_colormaps() {
    const Colormap& viridis = built_in_colormap(BuiltInColormap::viridis);
    require(valid_colormap(viridis), "viridis is valid");
    require(sample_colormap(viridis, 0.0) == Color::rgba(0x44, 0x01, 0x54), "viridis starts at its first colour");
    require(sample_colormap(viridis, 1.0) == Color::rgba(0xFD, 0xE7, 0x25), "viridis ends at its last colour");
    require(sample_colormap(viridis, 2.0) == sample_colormap(viridis, 1.0), "positions clamp");
    require(find_built_in_colormap("magma") == &built_in_colormap(BuiltInColormap::magma) &&
                find_built_in_colormap("jet") == nullptr,
            "built-ins are found by name");
    for (int encoded = 0; encoded < 256; ++encoded) {
        const std::uint8_t byte = static_cast<std::uint8_t>(encoded);
        require(linear_to_srgb(srgb_to_linear(byte)) == byte, "the sRGB transfer round-trips every byte");
    }
    Colormap qualitative{};
    qualitative.kind = ColormapKind::qualitative;
    qualitative.stop_count = 2U;
    qualitative.stops[0].color = Color::rgba(1, 2, 3);
    qualitative.stops[1].color = Color::rgba(4, 5, 6);
    require(sample_colormap(qualitative, 3.0) == Color::rgba(4, 5, 6) && sample_colormap(qualitative, -1.0) == Color::rgba(4, 5, 6),
            "a qualitative map wraps");
    Colormap broken = viridis;
    broken.stops[3].offset = 0.0;
    require(!valid_colormap(broken), "decreasing offsets are refused");
    Scale scale{};
    scale.domain = PlotRange{10.0, 20.0};
    const std::array<double, 3> values{10.0, 20.0, not_a_number};
    std::array<Color, 3> colours{};
    require(sample_colormap_span(viridis, scale, values, colours) && colours[1] == sample_colormap(viridis, 1.0) &&
                colours[2] == viridis.gap_color,
            "a span maps through its scale's domain");
}

// -- view model --------------------------------------------------------------------------------

PlotViewModel make_view_model() {
    PlotViewModel model;
    const std::array<Axis, 1> x{linear_axis(0.0, 100.0, AxisSide::bottom)};
    const std::array<Axis, 1> y{linear_axis(0.0, 10.0, AxisSide::left)};
    model.set_axes(x, y);
    model.set_plot(Rect{0.0, 0.0, 200.0, 100.0});
    return model;
}

void test_view_model_gestures() {
    PlotViewModel model = make_view_model();
    const PlotZone plot{PlotZoneKind::plot, 0U};
    static_cast<void>(model.pointer_pressed(Point{100.0, 50.0}, PlotPointerButton::primary, PlotModifiers{}, plot));
    const PlotViewChange panned = model.pointer_moved(Point{120.0, 40.0});
    static_cast<void>(model.pointer_released(Point{120.0, 40.0}));
    require(panned.view && model.x_axes()[0].range == (PlotRange{-10.0, 90.0}) &&
                model.y_axes()[0].range == (PlotRange{-1.0, 9.0}),
            "a left drag pans both axes");
    PlotViewModel zoomed = make_view_model();
    static_cast<void>(zoomed.wheel(Point{0.0, 100.0}, 1.0, plot));
    require(zoomed.x_axes()[0].range.minimum == 0.0 && std::fabs(zoomed.x_axes()[0].range.maximum - 100.0 / 1.1) < 1e-9,
            "a wheel notch zooms about the pointer");
    PlotViewModel axis_only = make_view_model();
    static_cast<void>(axis_only.wheel(Point{100.0, 50.0}, -1.0, PlotZone{PlotZoneKind::y_axis, 0U}));
    require(axis_only.x_axes()[0].range == (PlotRange{0.0, 100.0}) && axis_only.y_axes()[0].range.extent() > 10.0,
            "the wheel on an axis zooms that axis only");
    PlotViewModel boxed = make_view_model();
    static_cast<void>(boxed.pointer_pressed(Point{50.0, 25.0}, PlotPointerButton::secondary, PlotModifiers{}, plot));
    static_cast<void>(boxed.pointer_moved(Point{150.0, 75.0}));
    require(boxed.drag_box().has_value() && (*boxed.drag_box()).width == 100.0, "a right drag shows a box");
    const PlotViewChange box = boxed.pointer_released(Point{150.0, 75.0});
    require(box.view && boxed.x_axes()[0].range == (PlotRange{25.0, 75.0}) && boxed.y_axes()[0].range == (PlotRange{2.5, 7.5}),
            "a right drag zooms to the box");
    PlotViewModel query = make_view_model();
    PlotModifiers control{};
    control.control = true;
    static_cast<void>(query.pointer_pressed(Point{0.0, 0.0}, PlotPointerButton::secondary, control, plot));
    static_cast<void>(query.pointer_moved(Point{100.0, 50.0}));
    const PlotViewChange selected = query.pointer_released(Point{100.0, 50.0});
    require(selected.selection && !selected.view && query.selection().has_value() &&
                (*query.selection()).x == (PlotRange{0.0, 50.0}) && (*query.selection()).y == (PlotRange{5.0, 10.0}),
            "Ctrl and right drag queries without zooming");
    PlotViewModel clicked = make_view_model();
    static_cast<void>(clicked.pointer_pressed(Point{10.0, 10.0}, PlotPointerButton::primary, PlotModifiers{}, plot));
    const PlotViewChange click = clicked.pointer_released(Point{11.0, 10.0});
    require(!click.view, "a click under the drag threshold changes nothing");
}

void test_view_model_locks_keys_and_follow() {
    PlotViewModel model;
    Axis x = linear_axis(0.0, 100.0, AxisSide::bottom);
    x.lock = AxisLock::minimum;
    Axis y = linear_axis(0.0, 10.0, AxisSide::left);
    y.lock = AxisLock::both;
    const std::array<Axis, 1> xs{x};
    const std::array<Axis, 1> ys{y};
    model.set_axes(xs, ys);
    model.set_plot(Rect{0.0, 0.0, 200.0, 100.0});
    const PlotZone plot{PlotZoneKind::plot, 0U};
    static_cast<void>(model.wheel(Point{100.0, 50.0}, 1.0, plot));
    require(model.x_axes()[0].range.minimum == 0.0 && model.x_axes()[0].range.maximum < 100.0,
            "a zoom with the minimum locked zooms about it");
    require(model.y_axes()[0].range == (PlotRange{0.0, 10.0}), "an axis locked at both ends refuses zoom");
    const PlotDataBounds none{};
    require(!model.key(PlotKey::right, PlotModifiers{}, none).view, "a locked axis refuses pan");
    PlotViewModel keys = make_view_model();
    static_cast<void>(keys.key(PlotKey::right, PlotModifiers{}, none));
    require(keys.x_axes()[0].range == (PlotRange{10.0, 110.0}), "Right pans a tenth to later values");
    PlotModifiers shift{};
    shift.shift = true;
    static_cast<void>(keys.key(PlotKey::up, shift, none));
    require(keys.y_axes()[0].range == (PlotRange{10.0, 20.0}), "Shift and Up pans a page to larger values");
    PlotDataBounds bounds{};
    bounds.x[0] = PlotRange{0.0, 50.0};
    bounds.y[0] = PlotRange{-1.0, 1.0};
    static_cast<void>(keys.key(PlotKey::home, PlotModifiers{}, bounds));
    require(keys.x_axes()[0].range == (PlotRange{-1.0, 51.0}), "Home fits with padding");
    PlotViewModel live;
    Axis time = linear_axis(0.0, 10.0, AxisSide::bottom);
    time.follow = AxisFollow{true, 10.0};
    const std::array<Axis, 1> times{time};
    const std::array<Axis, 1> levels{linear_axis(-1.0, 1.0, AxisSide::left)};
    live.set_axes(times, levels);
    live.set_plot(Rect{0.0, 0.0, 200.0, 100.0});
    require(live.follow_tail(25.0) && live.x_axes()[0].range == (PlotRange{15.0, 25.0}), "a following axis tracks the tail");
    static_cast<void>(live.key(PlotKey::left, PlotModifiers{}, none));
    require(!live.x_following(0U) && !live.follow_tail(30.0), "a user pan stops following");
    live.set_axes(live.x_axes(), live.y_axes());
    require(!live.x_following(0U), "relaying the axes keeps the stopped state");
    static_cast<void>(live.key(PlotKey::end, PlotModifiers{}, none));
    require(live.x_following(0U) && live.follow_tail(30.0), "End resumes following");
}

// -- legend and annotations -----------------------------------------------------------------------

void test_legend_and_annotations() {
    LegendStyle style{};
    const std::array<double, 2> widths{40.0, 60.0};
    std::array<LegendEntryLayout, 2> entries{};
    const Rect plot{0.0, 0.0, 400.0, 300.0};
    const LegendLayout layout = layout_legend(style, plot, widths, entries);
    require(layout.entry_count == 2U && layout.frame.right() == 392.0 && layout.frame.y == 8.0,
            "a top-right legend sits inside the plot's corner");
    require(layout.frame.width == 6.0 * 2.0 + 12.0 + 6.0 + 60.0, "the frame fits its widest entry");
    require(legend_entry_at(entries, Point{entries[1].bounds.x + 1.0, entries[1].bounds.y + 1.0}) == 1U &&
                legend_entry_at(entries, Point{0.0, 0.0}) == 2U,
            "entries are found under a point");
    style.inside = false;
    style.place = LegendPlace::right;
    const LegendLayout outside = layout_legend(style, plot, widths, entries);
    require(outside.frame.x == 408.0, "an outside legend sits beside the plot");
    const std::array<AnnotationRequest, 3> requests{{
        {Point{100.0, 100.0}, Size{50.0, 16.0}},
        {Point{100.0, 100.0}, Size{50.0, 16.0}},
        {Point{398.0, 2.0}, Size{50.0, 16.0}},
    }};
    std::array<Rect, 3> placed{};
    require(layout_annotations(plot, requests, placed) == 3U, "every annotation is placed");
    require(!(placed[0] == placed[1]), "a second label at the same anchor moves aside");
    require(placed[2].right() <= plot.right() && placed[2].y >= plot.y, "labels are clamped inside the plot");
}

// -- signal programs --------------------------------------------------------------------------------

std::uint64_t checksum(const SignalSink& sink) {
    std::uint64_t sum = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < sink.count; ++index) {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &sink.values[index], sizeof(double));
        sum = (sum ^ bits) * 1099511628211ULL;
    }
    return sum;
}

void test_signal_programs() {
    SignalProgram program{};
    require(parse_signal_source("signal(sine, frequency 2Hz, amplitude 1)", program) == SignalParseOutcome::parsed &&
                program.kind == SignalKind::sine && program.sine.frequency == 2.0,
            "a form's signal source parses");
    SignalProgram untouched{};
    require(parse_signal_source("signal(laser, frequency 2Hz)", untouched) == SignalParseOutcome::unknown_kind,
            "an unregistered kind is refused");
    require(parse_signal_source("sine, duty 0.5", untouched) == SignalParseOutcome::unknown_parameter,
            "a parameter the kind does not take is refused");
    require(parse_signal_source("sine, frequency fast", untouched) == SignalParseOutcome::invalid_value,
            "a non-number is refused");
    require(parse_signal_source("oscillators, frequency 1, amplitude 0.5, frequency 3, amplitude 0.25", program) ==
                    SignalParseOutcome::parsed &&
                program.oscillators.count == 2U && program.oscillators.oscillators[1].amplitude == 0.25,
            "an oscillator bank takes repeated partials");
    // Frame grouping does not change samples: eight ticks run as 4 + 4 and
    // as 1 + 3 + 4 give the same values.
    SignalProgram sine{};
    sine.kind = SignalKind::sine;
    sine.sine.frequency = 3.0;
    SignalSink together{};
    SignalSink apart{};
    run_signal(sine, SignalFrame{0U, 4U, 16U}, together);
    run_signal(sine, SignalFrame{4U, 4U, 16U}, together);
    run_signal(sine, SignalFrame{0U, 1U, 16U}, apart);
    run_signal(sine, SignalFrame{1U, 3U, 16U}, apart);
    run_signal(sine, SignalFrame{4U, 4U, 16U}, apart);
    require(together.count == 128U && apart.count == 128U && checksum(together) == checksum(apart),
            "frame grouping never changes the samples");
    require(together.values[0] == 0.0 && std::fabs(together.values[80] - std::sin(6.283185307179586 * 3.0 * 80.0 / 960.0)) < 1e-12,
            "the sine is the sine");
    SignalProgram noise{};
    noise.kind = SignalKind::noise;
    SignalSink first{};
    SignalSink second{};
    run_signal(noise, SignalFrame{10U, 2U, 8U}, first);
    run_signal(noise, SignalFrame{10U, 2U, 8U}, second);
    require(checksum(first) == checksum(second), "noise is keyed by sample index, not by a running generator");
    bool bounded = true;
    for (std::size_t index = 0U; index < first.count; ++index) bounded = bounded && std::fabs(first.values[index]) <= 1.0;
    require(bounded, "noise stays within its amplitude");
    SignalClock clock;
    const FrameTime start{};
    const SignalFrame opening = clock.advance(start, 16U);
    require(opening.first_tick == 0U && opening.tick_count == 1U, "the first advance runs tick 0");
    const SignalFrame next = clock.advance(start + std::chrono::milliseconds(55), 16U);
    require(next.first_tick == 1U && next.tick_count == 3U, "advances run the ticks due");
    const SignalFrame late = clock.advance(start + std::chrono::seconds(1), 16U);
    require(late.tick_count == maximum_ticks_per_frame && clock.skipped_ticks() > 0U && late.first_tick + 4U == 61U,
            "a late frame runs at most four ticks and skips the rest");
    require(SineSignal::declaration.name == "sine" && signal_declaration(SignalKind::chirp).operations_per_sample != 0U,
            "every program declares its budget");
}

// -- mark painters: display traces --------------------------------------------------------------------

// Writes each batched call with its arguments (the plots' display-trace
// format; numbers at the stream's default precision).
class TracePainter final : public Painter {
public:
    void save() override { out << "save\n"; }
    void restore() override { out << "restore\n"; }
    void translate(Point) override {}
    void clip_rect(Rect rect) override { out << "clip " << rect.x << ',' << rect.y << ',' << rect.width << ',' << rect.height << '\n'; }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void stroke_polyline(std::span<const Point> points, const StrokeStyle&) override {
        out << "stroke_polyline";
        for (const Point point : points) out << ' ' << point.x << ',' << point.y;
        out << '\n';
    }
    void fill_band(std::span<const Point> upper, std::span<const Point> lower, Color) override {
        out << "fill_band " << upper.size() << '/' << lower.size() << '\n';
    }
    void fill_rects(std::span<const Rect> rects, Color) override {
        out << "fill_rects";
        for (const Rect rect : rects) out << ' ' << rect.x << ',' << rect.y << ',' << rect.width << ',' << rect.height;
        out << '\n';
    }
    void fill_rects_each(std::span<const Rect> rects, std::span<const Color>) override {
        out << "fill_rects_each " << rects.size() << '\n';
    }
    void stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle&) override {
        out << "stroke_segments";
        for (const PaintSegment segment : segments) {
            out << ' ' << segment.from.x << ',' << segment.from.y << '>' << segment.to.x << ',' << segment.to.y;
        }
        out << '\n';
    }
    void draw_markers(std::span<const Point> centers, const MarkerStyle&) override {
        out << "draw_markers";
        for (const Point point : centers) out << ' ' << point.x << ',' << point.y;
        out << '\n';
    }

    std::ostringstream out;
};

void test_mark_painter_traces() {
    SeriesColumn<double> xs(SeriesMode::growable, 4U, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, 4U, XOrder::unordered);
    const std::array<double, 4> x_values{0.0, 1.0, 2.0, 3.0};
    const std::array<double, 4> y_values{0.0, 2.0, 1.0, 3.0};
    static_cast<void>(xs.assign(x_values));
    static_cast<void>(ys.assign(y_values));
    const ColumnSamples<double, double> samples = samples_of(xs, ys);
    const PlotTransform transform = transform_over(PlotRange{0.0, 3.0}, PlotRange{0.0, 3.0}, Rect{0.0, 0.0, 30.0, 30.0}, 1.0);
    SeriesMarkPainter painter;
    painter.reserve(transform.column_count(), 16U);
    TracePainter trace;
    StrokeStyle stroke{};
    static_cast<void>(painter.paint_line(trace, samples, XOrder::monotone, transform, stroke, LineDecimation::envelope, nullptr));
    static_cast<void>(painter.paint_stairs(trace, samples, transform, stroke, StairsMode::post));
    static_cast<void>(painter.paint_bars(trace, samples, transform, 0.5, 0.0, false, Color{}));
    MarkerStyle marker{};
    static_cast<void>(painter.paint_scatter(trace, samples, transform, marker, nullptr));
    static_cast<void>(painter.paint_stems(trace, samples, transform, 0.0, stroke, marker));
    const std::array<double, 1> lines{1.5};
    static_cast<void>(painter.paint_infinite_lines(trace, lines, InfiniteLineOrientation::vertical, transform, stroke));
    const std::string expected =
        "save\nclip 0,0,30,30\nstroke_polyline 0,30 10,10 20,20 30,0\nrestore\n"
        "save\nclip 0,0,30,30\nstroke_polyline 0,30 10,30 10,10 20,10 20,20 30,20 30,0\nrestore\n"
        "save\nclip 0,0,30,30\nfill_rects -2.5,30,5,0 7.5,10,5,20 17.5,20,5,10 27.5,0,5,30\nrestore\n"
        "save\nclip 0,0,30,30\ndraw_markers 0,30 10,10 20,20 30,0\nrestore\n"
        "save\nclip 0,0,30,30\nstroke_segments 0,30>0,30 10,30>10,10 20,30>20,20 30,30>30,0\n"
        "draw_markers 0,30 10,10 20,20 30,0\nrestore\n"
        "save\nclip 0,0,30,30\nstroke_segments 15.5,0>15.5,30\nrestore\n";
    if (trace.out.str() != expected) std::cerr << trace.out.str();
    require(trace.out.str() == expected, "each mark painter is one batch call, traced exactly");
}

} // namespace

int main() {
    try {
        test_growable_column_edits();
        test_monotone_column_refuses_disorder();
        test_ring_column();
        test_lease_and_borrowed_views();
        test_ring_channel();
        test_scale_round_trips();
        test_scale_refusals();
        test_tick_golden_tables();
        test_labels_never_overlap();
        test_civil_time();
        test_time_ticks();
        test_axes_and_transform();
        test_envelope_matches_oracle();
        test_envelope_gaps();
        test_pyramid_equals_rebuild_and_envelope();
        test_occupancy_and_binning();
        test_picker_matches_oracle();
        test_colormaps();
        test_view_model_gestures();
        test_view_model_locks_keys_and_follow();
        test_legend_and_annotations();
        test_signal_programs();
        test_mark_painter_traces();
        std::cout << "gui_forms_plot_primitives_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_plot_primitives_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
