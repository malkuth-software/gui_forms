#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::interpolation {

// General-use interpolation of sampled values: lines (curves, tables, one
// image row) and interleaved images, in binary64 (owner decision 2(a),
// 2026-10-22). An internal seam: no public header names it, and the engine
// behind it (the owner's CONV field, stxlib::conv's LineField) never appears
// here.
//
// The engine's output is not bounded by its input: it can overshoot near
// sign transitions. Every call therefore takes the range its values live in,
// refuses a source sample outside it, and clamps every written value to it
// (owner decision 5(a): the clamp belongs to this seam, not to the engine).
// Channels are clamped independently; a premultiplied consumer that needs
// colour <= alpha imposes that itself.
//
// Each call validates everything before it allocates or writes, allocates
// its plan and working storage once, and releases them before returning; it
// never throws. A refusal leaves the target's bytes unchanged, except that
// resample_image may have written target columns before a numerical_failure
// (an engine failure that no admitted input is known to reach), which
// leaves the target unspecified.

enum class Sampling : std::uint8_t {
    // Endpoint-aligned nodes (the engine's preview): target node k is the
    // line at source position k (n - 1) / (N - 1), so the first and last
    // target values are the first and last source values, in both
    // directions. A reduction samples the line; it does not integrate. For
    // curves and tables.
    nodal,
    // Pixel footprints (the engine's canon): each target pixel k is the
    // line's average over its footprint (k + [0, 1)) n / N - 1/2, in source
    // nodes. A reduction of more than 340 : 1 is refused (invalid_shape).
    // For images.
    pixel_area
};

enum class Status : std::uint8_t {
    ok,
    // A zero or oversized length, zero channels or lanes, more than 64 lanes
    // or channels, or a stride shorter than one node or row.
    invalid_shape,
    // A span shorter than its shape and stride require.
    insufficient_storage,
    // A range bound that is not finite, or low > high.
    invalid_range,
    // A source sample that is NaN or outside the range.
    sample_out_of_range,
    // A position that is NaN or outside [0, source length - 1].
    position_out_of_range,
    // Image source and target storage overlap (lines may overlap: the source
    // is copied before the target is written).
    overlap,
    // The plan or working storage could not be allocated.
    out_of_memory,
    // The engine's tolerance fallback; the target is unspecified.
    numerical_failure
};

// The closed interval every source sample lies in and every result is
// clamped to.
struct ValueRange final {
    double low = 0.0;
    double high = 1.0;
};

// Node-major samples: node i, lane j is samples[i * node_stride + j].
// Padding between nodes is neither read nor written.
struct SourceLine final {
    std::span<const double> samples{};
    std::size_t length = 0;
    std::size_t lanes = 1;
    std::size_t node_stride = 1;
};
// The target has the source's lanes.
struct TargetLine final {
    std::span<double> samples{};
    std::size_t length = 0;
    std::size_t node_stride = 1;
};

// Row-major interleaved pixels: pixel (x, y), channel c is
// samples[y * row_stride + x * channels + c]. Strides are in elements.
struct SourceImage final {
    std::span<const double> samples{};
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t channels = 0;
    std::size_t row_stride = 0;
};
// The target has the source's channels.
struct TargetImage final {
    std::span<double> samples{};
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t row_stride = 0;
};

// Resamples a line to the target's length. A one-node source gives every
// target node its value.
[[nodiscard]] Status resample_line(const SourceLine& source, const TargetLine& target, Sampling sampling,
                                   ValueRange range) noexcept;

// Evaluates a line at arbitrary positions, measured in source nodes:
// position p lies between nodes floor(p) and floor(p) + 1, and an integer
// position is that node. One target node per position. A one-node source
// gives every target node its value.
[[nodiscard]] Status interpolate_line_at(const SourceLine& source, std::span<const double> positions,
                                         const TargetLine& target, ValueRange range) noexcept;

// Resamples an image to the target's dimensions, one axis after the other:
// each row to the target's width, then each column of that to the target's
// height, through the line engine with the sampling given. An unchanged axis
// is copied, and a one-pixel axis is spread. It works through a copy of
// target width x source height pixels, so source and target could share
// storage; they are still refused when they overlap (overlap).
[[nodiscard]] Status resample_image(const SourceImage& source, const TargetImage& target, Sampling sampling,
                                    ValueRange range) noexcept;

} // namespace gui_forms::interpolation
