#include "interpolation.hpp"

#include <stxlib/conv.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms::interpolation {
namespace {

// The elements a strided span needs for `count` nodes or rows of `width`
// elements each: (count - 1) * stride + width, or none when that overflows.
// Callers have checked count >= 1 and width <= stride.
std::optional<std::size_t> required_elements(std::size_t count, std::size_t stride, std::size_t width) noexcept {
    const std::size_t steps = count - 1;
    const std::size_t room = std::numeric_limits<std::size_t>::max() - width;
    if (steps != 0 && stride > room / steps) return std::nullopt;
    const std::size_t required = steps * stride + width;
    return required;
}

bool range_valid(ValueRange range) noexcept {
    const bool valid = std::isfinite(range.low) && std::isfinite(range.high) && range.low <= range.high;
    return valid;
}

// True when every live sample is in the range; NaN fails both comparisons.
bool samples_in_range(std::span<const double> samples, std::size_t count, std::size_t stride, std::size_t width,
                      ValueRange range) noexcept {
    for (std::size_t node = 0; node < count; ++node) {
        const std::size_t first = node * stride;
        for (std::size_t index = first; index < first + width; ++index) {
            const double value = samples[index];
            if (!(value >= range.low && value <= range.high)) return false;
        }
    }
    return true;
}

void clamp_written(std::span<double> samples, std::size_t count, std::size_t stride, std::size_t width,
                   ValueRange range) noexcept {
    for (std::size_t node = 0; node < count; ++node) {
        const std::size_t first = node * stride;
        for (std::size_t index = first; index < first + width; ++index) {
            samples[index] = std::clamp(samples[index], range.low, range.high);
        }
    }
}

// True when the two spans share an element; std::less orders pointers into
// different arrays.
bool spans_overlap(std::span<const double> first, std::span<const double> second) noexcept {
    const std::less<const double*> before{};
    const bool apart = !before(first.data(), second.data() + second.size()) ||
                       !before(second.data(), first.data() + first.size());
    return !apart;
}

stxlib::conv::Mode engine_mode(Sampling sampling) noexcept {
    if (sampling == Sampling::pixel_area) return stxlib::conv::Mode::canon;
    return stxlib::conv::Mode::preview;
}

// The engine's refusals after this seam's own validation. invalid_argument
// and limit_exceeded are shapes the engine takes no further (more nodes than
// it holds, a canon reduction past 340 : 1); unsupported and anything else
// cannot follow this seam's checks and are grouped as numerical_failure.
Status from_engine(const stxlib::OperationStatus& status) noexcept {
    if (status.succeeded()) return Status::ok;
    const stxlib::ErrorRecord* const record = status.error();
    if (record == nullptr) return Status::numerical_failure;
    switch ((*record).kind()) {
    case stxlib::ErrorKind::invalid_argument:
    case stxlib::ErrorKind::limit_exceeded:
        return Status::invalid_shape;
    case stxlib::ErrorKind::out_of_range:
        return Status::sample_out_of_range;
    case stxlib::ErrorKind::out_of_memory:
        return Status::out_of_memory;
    default:
        return Status::numerical_failure;
    }
}

// A set of lines laid out in one span: line `l`, node `i`, lane `j` is
// samples[l * line_stride + i * node_stride + j]. Every line has `nodes`
// nodes; the extents have been checked.
struct SourceLines final {
    std::span<const double> samples{};
    std::size_t lines = 0;
    std::size_t nodes = 0;
    std::size_t node_stride = 0;
    std::size_t line_stride = 0;
};
struct TargetLines final {
    std::span<double> samples{};
    std::size_t nodes = 0;
    std::size_t node_stride = 0;
    std::size_t line_stride = 0;
};

// The span one line of `source` occupies.
std::span<const double> source_line(const SourceLines& source, std::size_t line, std::size_t lanes) noexcept {
    const std::size_t first = line * source.line_stride;
    const std::size_t extent = (source.nodes - 1) * source.node_stride + lanes;
    return source.samples.subspan(first, extent);
}

std::span<double> target_line(const TargetLines& target, std::size_t line, std::size_t lanes) noexcept {
    const std::size_t first = line * target.line_stride;
    const std::size_t extent = (target.nodes - 1) * target.node_stride + lanes;
    return target.samples.subspan(first, extent);
}

// Each line copied node for node (source and target have the same nodes).
void copy_lines(const SourceLines& source, const TargetLines& target, std::size_t lanes) noexcept {
    for (std::size_t line = 0; line < source.lines; ++line) {
        const std::span<const double> from = source_line(source, line, lanes);
        const std::span<double> to = target_line(target, line, lanes);
        for (std::size_t node = 0; node < target.nodes; ++node) {
            for (std::size_t lane = 0; lane < lanes; ++lane) {
                to[node * target.node_stride + lane] = from[node * source.node_stride + lane];
            }
        }
    }
}

// Each line's single node spread over every target node.
void spread_lines(const SourceLines& source, const TargetLines& target, std::size_t lanes) noexcept {
    for (std::size_t line = 0; line < source.lines; ++line) {
        const std::span<const double> from = source_line(source, line, lanes);
        const std::span<double> to = target_line(target, line, lanes);
        for (std::size_t node = 0; node < target.nodes; ++node) {
            for (std::size_t lane = 0; lane < lanes; ++lane) to[node * target.node_stride + lane] = from[lane];
        }
    }
}

// How the engine's field reaches each target line.
struct LineRequest final {
    stxlib::conv::Mode mode = stxlib::conv::Mode::canon;
    // Not empty: one position per target node, evaluated at those (the
    // same positions for every line); the mode is then unused.
    std::span<const double> positions{};
};

// The field compiled from one source line (the field copies it, so the
// target line may overlap it), and the target line as the engine's samples.
Status compile_line(stxlib::conv::LineField& field, const SourceLines& source, std::size_t line,
                    std::size_t lanes) noexcept {
    const std::span<const double> from = source_line(source, line, lanes);
    const stxlib::conv::LineSamples samples{stxlib::View<double>(from.data(), from.size()), source.nodes, lanes,
                                            source.node_stride};
    const Status compiled = from_engine(field.compile(samples));
    return compiled;
}

stxlib::conv::MutableLineSamples engine_target(const TargetLines& target, std::size_t line,
                                               std::size_t lanes) noexcept {
    const std::span<double> to = target_line(target, line, lanes);
    const stxlib::conv::MutableLineSamples out{stxlib::MutableView<double>(to.data(), to.size()), target.nodes, lanes,
                                               target.node_stride};
    return out;
}

// Every line compiled into `field` and transferred by the mode. Stops at the
// first failure.
Status transfer_lines(stxlib::conv::LineField& field, const SourceLines& source, const TargetLines& target,
                      std::size_t lanes, stxlib::conv::Mode mode) noexcept {
    for (std::size_t line = 0; line < source.lines; ++line) {
        const Status compiled = compile_line(field, source, line, lanes);
        if (compiled != Status::ok) return compiled;
        const Status result = from_engine(stxlib::conv::transfer(field, mode, engine_target(target, line, lanes)));
        if (result != Status::ok) return result;
    }
    return Status::ok;
}

// Every line compiled into `field` and evaluated at the positions.
Status transfer_line_points(stxlib::conv::LineField& field, const SourceLines& source, const TargetLines& target,
                            std::size_t lanes, std::span<const double> positions) noexcept {
    const stxlib::View<double> at(positions.data(), positions.size());
    for (std::size_t line = 0; line < source.lines; ++line) {
        const Status compiled = compile_line(field, source, line, lanes);
        if (compiled != Status::ok) return compiled;
        const Status result = from_engine(stxlib::conv::transfer_points(field, at, engine_target(target, line, lanes)));
        if (result != Status::ok) return result;
    }
    return Status::ok;
}

// Every line through one field of `source.nodes` (>= 2) nodes, made once and
// compiled for each line.
Status engine_lines(const SourceLines& source, const TargetLines& target, std::size_t lanes,
                    const LineRequest& request) noexcept {
    stxlib::OperationResult<stxlib::conv::LineField> made =
        stxlib::conv::LineField::make(source.nodes, lanes, stxlib::conv::FieldOptions{});
    if (made.failed()) return from_engine(made.status());
    stxlib::conv::LineField field = std::move(made).take_value();
    if (!request.positions.empty()) {
        const Status at_points = transfer_line_points(field, source, target, lanes, request.positions);
        return at_points;
    }
    const Status transferred = transfer_lines(field, source, target, lanes, request.mode);
    return transferred;
}

// The lines resampled: a one-node source spread, else the engine.
Status resample_lines(const SourceLines& source, const TargetLines& target, std::size_t lanes,
                      const LineRequest& request) noexcept {
    if (source.nodes == 1) {
        spread_lines(source, target, lanes);
        return Status::ok;
    }
    const Status result = engine_lines(source, target, lanes, request);
    return result;
}

// The validated extents of one line call.
struct LineExtents final {
    std::span<const double> source{};
    std::span<double> target{};
};

// Checks shape, range, storage and samples in that order, before anything is
// allocated or written, and records the spans the engine may touch.
Status check_line(const SourceLine& source, const TargetLine& target, ValueRange range, LineExtents& extents) noexcept {
    if (source.length == 0 || target.length == 0 || source.lanes == 0) return Status::invalid_shape;
    if (source.lanes > stxlib::conv::max_line_lanes) return Status::invalid_shape;
    if (source.length > stxlib::conv::max_line_nodes || target.length > stxlib::conv::max_line_nodes) {
        return Status::invalid_shape;
    }
    if (source.node_stride < source.lanes || target.node_stride < source.lanes) return Status::invalid_shape;
    if (!range_valid(range)) return Status::invalid_range;
    const std::optional<std::size_t> source_needed = required_elements(source.length, source.node_stride, source.lanes);
    const std::optional<std::size_t> target_needed = required_elements(target.length, target.node_stride, source.lanes);
    if (!source_needed.has_value() || !target_needed.has_value()) return Status::invalid_shape;
    if (source.samples.size() < *source_needed || target.samples.size() < *target_needed) {
        return Status::insufficient_storage;
    }
    extents.source = source.samples.first(*source_needed);
    extents.target = target.samples.first(*target_needed);
    if (!samples_in_range(extents.source, source.length, source.node_stride, source.lanes, range)) {
        return Status::sample_out_of_range;
    }
    return Status::ok;
}

// The one line of a line call, as lines.
SourceLines one_source_line(const SourceLine& source, const LineExtents& extents) noexcept {
    const SourceLines lines{extents.source, 1, source.length, source.node_stride, 0};
    return lines;
}

TargetLines one_target_line(const TargetLine& target, const LineExtents& extents) noexcept {
    const TargetLines lines{extents.target, target.length, target.node_stride, 0};
    return lines;
}

} // namespace

// The engine reports failures as statuses and never throws; a refusal from it
// comes before anything is written to the target.
Status resample_line(const SourceLine& source, const TargetLine& target, Sampling sampling, ValueRange range) noexcept {
    LineExtents extents{};
    const Status checked = check_line(source, target, range, extents);
    if (checked != Status::ok) return checked;
    LineRequest request{};
    request.mode = engine_mode(sampling);
    const Status result =
        resample_lines(one_source_line(source, extents), one_target_line(target, extents), source.lanes, request);
    if (result == Status::ok) clamp_written(extents.target, target.length, target.node_stride, source.lanes, range);
    return result;
}

Status interpolate_line_at(const SourceLine& source, std::span<const double> positions, const TargetLine& target,
                           ValueRange range) noexcept {
    if (positions.size() != target.length) return Status::invalid_shape;
    LineExtents extents{};
    const Status checked = check_line(source, target, range, extents);
    if (checked != Status::ok) return checked;
    const double last_node = static_cast<double>(source.length - 1);
    for (const double position : positions) {
        if (!(position >= 0.0 && position <= last_node)) return Status::position_out_of_range;
    }
    LineRequest request{};
    request.positions = positions;
    const Status result =
        resample_lines(one_source_line(source, extents), one_target_line(target, extents), source.lanes, request);
    if (result == Status::ok) clamp_written(extents.target, target.length, target.node_stride, source.lanes, range);
    return result;
}

// Rows, then columns. The working copy holds the rows at the target's width
// (target.width x source.height pixels, rows packed); its allocation is the
// only one besides the engine's, and a failure of it is out_of_memory.
Status resample_image(const SourceImage& source, const TargetImage& target, Sampling sampling,
                      ValueRange range) noexcept {
    if (source.width == 0 || source.height == 0 || source.channels == 0 || target.width == 0 || target.height == 0) {
        return Status::invalid_shape;
    }
    if (source.channels > stxlib::conv::max_line_lanes) return Status::invalid_shape;
    // A row is width x channels elements, overflow-checked.
    const std::optional<std::size_t> source_row = required_elements(source.width, source.channels, source.channels);
    const std::optional<std::size_t> target_row = required_elements(target.width, source.channels, source.channels);
    if (!source_row.has_value() || !target_row.has_value()) return Status::invalid_shape;
    if (source.row_stride < *source_row || target.row_stride < *target_row) return Status::invalid_shape;
    if (!range_valid(range)) return Status::invalid_range;
    const std::optional<std::size_t> source_needed = required_elements(source.height, source.row_stride, *source_row);
    const std::optional<std::size_t> target_needed = required_elements(target.height, target.row_stride, *target_row);
    const std::optional<std::size_t> working_needed = required_elements(source.height, *target_row, *target_row);
    if (!source_needed.has_value() || !target_needed.has_value() || !working_needed.has_value()) {
        return Status::invalid_shape;
    }
    if (source.samples.size() < *source_needed || target.samples.size() < *target_needed) {
        return Status::insufficient_storage;
    }
    const std::span<const double> source_samples = source.samples.first(*source_needed);
    const std::span<double> target_samples = target.samples.first(*target_needed);
    if (spans_overlap(source_samples, target_samples)) return Status::overlap;
    if (!samples_in_range(source_samples, source.height, source.row_stride, *source_row, range)) {
        return Status::sample_out_of_range;
    }
    std::vector<double> working;
    try {
        working.resize(*working_needed);
    } catch (const std::bad_alloc&) {
        return Status::out_of_memory;
    } catch (const std::length_error&) {
        return Status::out_of_memory;
    }
    const std::size_t channels = source.channels;
    LineRequest request{};
    request.mode = engine_mode(sampling);
    // The rows: source.height lines of source.width pixels to target.width.
    const SourceLines rows_from{source_samples, source.height, source.width, channels, source.row_stride};
    const TargetLines rows_to{working, target.width, channels, *target_row};
    // The columns: target.width lines of source.height pixels to target.height.
    const SourceLines columns_from{working, target.width, source.height, *target_row, channels};
    const TargetLines columns_to{target_samples, target.height, target.row_stride, channels};
    Status rows = Status::ok;
    if (source.width == target.width) copy_lines(rows_from, rows_to, channels);
    else rows = resample_lines(rows_from, rows_to, channels, request);
    if (rows != Status::ok) return rows;
    Status columns = Status::ok;
    if (source.height == target.height) copy_lines(columns_from, columns_to, channels);
    else columns = resample_lines(columns_from, columns_to, channels, request);
    if (columns == Status::ok) clamp_written(target_samples, target.height, target.row_stride, *target_row, range);
    return columns;
}

} // namespace gui_forms::interpolation
