#include "image_variant.hpp"

#include "../../../interpolation/interpolation.hpp"
#include "../kernel/pixel_kernel.hpp"
#include "../../../types/restrict/restrict.hpp"
#include "material_image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <vector>

// Identical bytes on every host: no fused multiply-add.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// Premultiplied BGRA: four lanes per pixel.
constexpr std::size_t lanes = 4U;

// One region of an axis: source pixels [source_start, source_end) resampled
// to variant pixels [target_start, target_end).
struct AxisRegion final {
    std::size_t source_start{};
    std::size_t source_end{};
    std::size_t target_start{};
    std::size_t target_end{};
};

// An axis's regions in order: one, or a nine-patch's three.
struct AxisRegions final {
    std::array<AxisRegion, 3> regions{};
    std::size_t count{};
};

// A source slice in whole pixels, within `room`.
std::size_t slice_pixels(double slice, std::size_t room) noexcept {
    const double rounded = std::clamp(std::round(slice), 0.0, static_cast<double>(room));
    const std::size_t pixels = static_cast<std::size_t>(rounded);
    return pixels;
}

AxisRegions axis_regions(std::size_t source, std::size_t target, double slice_start, double slice_end,
                         std::uint32_t fixed_start, std::uint32_t fixed_end, bool nine_patch) noexcept {
    AxisRegions axis{};
    if (!nine_patch) {
        axis.regions[0] = AxisRegion{0U, source, 0U, target};
        axis.count = 1U;
        return axis;
    }
    const std::size_t near = slice_pixels(slice_start, source);
    const std::size_t far = slice_pixels(slice_end, source - near);
    const std::size_t target_near = std::min<std::size_t>(fixed_start, target);
    const std::size_t target_far = std::min<std::size_t>(fixed_end, target - target_near);
    axis.regions[0] = AxisRegion{0U, near, 0U, target_near};
    axis.regions[1] = AxisRegion{near, source - far, target_near, target - target_far};
    axis.regions[2] = AxisRegion{source - far, source, target - target_far, target};
    axis.count = 3U;
    return axis;
}

// Where one line lies in a buffer: its first sample, and the samples from
// one node to the next.
struct LinePlace final {
    std::size_t offset{};
    std::size_t node_stride{};
};

// Fills `positions` (reset-size to `target`) with the source position of
// each target pixel's centre, clamped to the first and last source pixel.
void centre_positions(std::size_t source, std::size_t target, std::vector<double>& positions) {
    positions.assign(target, 0.0);
    const double step = static_cast<double>(source) / static_cast<double>(target);
    const double last = static_cast<double>(source - 1U);
    for (std::size_t output = 0U; output < target; ++output) {
        const double centre = (static_cast<double>(output) + 0.5) * step - 0.5;
        positions[output] = std::clamp(centre, 0.0, last);
    }
}

// Copies `length` pixels from `source` to `target`, each place's node
// stride apart. Restricted parameters: no element is reached through both
// (resample_one_line checks it).
void copy_line(const double* GUI_FORMS_RESTRICT source, LinePlace from_place, double* GUI_FORMS_RESTRICT target,
               LinePlace to_place, std::size_t length) noexcept {
    for (std::size_t node = 0U; node < length; ++node) {
        const std::size_t read = from_place.offset + node * from_place.node_stride;
        const std::size_t write = to_place.offset + node * to_place.node_stride;
        for (std::size_t lane = 0U; lane < lanes; ++lane) target[write + lane] = source[read + lane];
    }
}

// Resamples one line of `source_length` pixels in `from` to `target_length`
// pixels in `to`: copied when the lengths match, pixel-area when it
// shrinks, at `positions` (centre_positions) when it enlarges. Overlap
// rule: `from` and `to` share no element, and `to` shares none with
// `positions` (asserted in debug builds). The copy's restricted parameters
// rely on it; the interpolation seam takes the spans as they are.
interpolation::Status resample_one_line(std::span<const double> from, LinePlace from_place,
                                        std::size_t source_length, std::span<double> to, LinePlace to_place,
                                        std::size_t target_length, std::span<const double> positions) noexcept {
    gui_forms::detail::require_disjoint(from, to);
    gui_forms::detail::require_disjoint(positions, to);
    const interpolation::ValueRange range{0.0, 1.0};
    if (source_length == target_length) {
        copy_line(from.data(), from_place, to.data(), to_place, source_length);
        return interpolation::Status::ok;
    }
    const interpolation::SourceLine source{from.subspan(from_place.offset), source_length, lanes,
                                           from_place.node_stride};
    const interpolation::TargetLine target{to.subspan(to_place.offset), target_length, to_place.node_stride};
    if (target_length < source_length) {
        const interpolation::Status shrunk =
            interpolation::resample_line(source, target, interpolation::Sampling::pixel_area, range);
        return shrunk;
    }
    const interpolation::Status enlarged = interpolation::interpolate_line_at(source, positions, target, range);
    return enlarged;
}

// Writes `count` pixels of `line` (four lanes each, packed) into the
// variant from byte `offset`, `step` bytes apart: the colour clamped to
// alpha, each channel quantized once. The seam has clamped every lane to
// [0, 1].
void store_line(std::span<const double> line, std::size_t count, const PartRasterView& variant, std::size_t offset,
                std::size_t step) noexcept {
    for (std::size_t pixel = 0U; pixel < count; ++pixel) {
        const std::size_t read = pixel * lanes;
        const double alpha = line[read + 3U];
        const std::size_t write = offset + pixel * step;
        variant.pixels[write] = to_byte(std::min(line[read], alpha));
        variant.pixels[write + 1U] = to_byte(std::min(line[read + 1U], alpha));
        variant.pixels[write + 2U] = to_byte(std::min(line[read + 2U], alpha));
        variant.pixels[write + 3U] = to_byte(alpha);
    }
}

// Working storage for one variant, sized once for its largest region:
// the image as samples in [0, 1], the first axis's result, one line of the
// second, and the centre positions of each axis.
struct VariantWork final {
    std::vector<double> source;
    std::vector<double> middle;
    std::vector<double> line;
    std::vector<double> across_positions;
    std::vector<double> down_positions;
};

// Resamples the source region `across` × `down` (of work.source,
// `source_width` pixels wide) into the same region of `variant`: the first
// axis into work.middle, then the second a line at a time through work.line
// into the variant's bytes. False when the seam refuses.
bool resample_region(const AxisRegion& across, const AxisRegion& down, std::size_t source_width,
                     VariantWork& work, const PartRasterView& variant) {
    const std::size_t source_columns = across.source_end - across.source_start;
    const std::size_t source_rows = down.source_end - down.source_start;
    const std::size_t columns = across.target_end - across.target_start;
    const std::size_t rows = down.target_end - down.target_start;
    // A region with no source pixels stays transparent.
    if (columns == 0U || rows == 0U || source_columns == 0U || source_rows == 0U) return true;
    if (columns > source_columns) centre_positions(source_columns, columns, work.across_positions);
    if (rows > source_rows) centre_positions(source_rows, rows, work.down_positions);
    const std::size_t source_row = source_width * lanes;
    const std::size_t first = (down.source_start * source_width + across.source_start) * lanes;
    const std::size_t first_byte = down.target_start * variant.row_bytes + across.target_start * lanes;
    const std::span<const double> source(work.source);
    const std::span<double> middle(work.middle);
    const std::span<double> line(work.line);
    if (rows < source_rows) {
        // Down first, into a source_columns × rows middle; then across.
        const std::size_t middle_row = source_columns * lanes;
        for (std::size_t column = 0U; column < source_columns; ++column) {
            const LinePlace from{first + column * lanes, source_row};
            const LinePlace to{column * lanes, middle_row};
            const interpolation::Status status =
                resample_one_line(source, from, source_rows, middle, to, rows, work.down_positions);
            if (status != interpolation::Status::ok) return false;
        }
        for (std::size_t row = 0U; row < rows; ++row) {
            const LinePlace from{row * middle_row, lanes};
            const interpolation::Status status = resample_one_line(middle, from, source_columns, line,
                                                                   LinePlace{0U, lanes}, columns,
                                                                   work.across_positions);
            if (status != interpolation::Status::ok) return false;
            store_line(line, columns, variant, first_byte + row * variant.row_bytes, lanes);
        }
        return true;
    }
    // Across first, into a columns × source_rows middle; then down.
    const std::size_t middle_row = columns * lanes;
    for (std::size_t row = 0U; row < source_rows; ++row) {
        const LinePlace from{first + row * source_row, lanes};
        const LinePlace to{row * middle_row, lanes};
        const interpolation::Status status =
            resample_one_line(source, from, source_columns, middle, to, columns, work.across_positions);
        if (status != interpolation::Status::ok) return false;
    }
    for (std::size_t column = 0U; column < columns; ++column) {
        const LinePlace from{column * lanes, middle_row};
        const interpolation::Status status =
            resample_one_line(middle, from, source_rows, line, LinePlace{0U, lanes}, rows, work.down_positions);
        if (status != interpolation::Status::ok) return false;
        store_line(line, rows, variant, first_byte + column * lanes, variant.row_bytes);
    }
    return true;
}

// The most samples work.middle needs for any pair of regions.
std::size_t middle_samples(const AxisRegions& across, const AxisRegions& down) noexcept {
    std::size_t most = 0U;
    for (std::size_t column = 0U; column < across.count; ++column) {
        const AxisRegion& x = across.regions[column];
        for (std::size_t row = 0U; row < down.count; ++row) {
            const AxisRegion& y = down.regions[row];
            const std::size_t source_columns = x.source_end - x.source_start;
            const std::size_t source_rows = y.source_end - y.source_start;
            const std::size_t columns = x.target_end - x.target_start;
            const std::size_t rows = y.target_end - y.target_start;
            const std::size_t samples = rows < source_rows ? source_columns * rows : columns * source_rows;
            most = std::max(most, samples);
        }
    }
    const std::size_t total = most * lanes;
    return total;
}

} // namespace

std::shared_ptr<PartRaster> make_image_variant(const ImageRegistry& registry,
                                               const ImageVariantRequest& request) noexcept {
    try {
        if (request.extent.width == 0U || request.extent.height == 0U) return nullptr;
        std::shared_ptr<PartRaster> made = PartRaster::create(request.extent, request.scale);
        if (!made) return nullptr;
        MaterialImagePixels pixels{};
        if (!read_material_image(registry, request.image, request.source_width, request.source_height, pixels)) {
            return nullptr;
        }
        const std::size_t width = pixels.width;
        const std::size_t height = pixels.height;
        const Insets& slice = request.source_slice;
        const bool nine_patch = slice.left > 0.0 || slice.top > 0.0 || slice.right > 0.0 || slice.bottom > 0.0;
        const AxisRegions across = axis_regions(width, request.extent.width, slice.left, slice.right,
                                                request.fixed[0], request.fixed[2], nine_patch);
        const AxisRegions down = axis_regions(height, request.extent.height, slice.top, slice.bottom,
                                              request.fixed[1], request.fixed[3], nine_patch);
        // The working storage, sized once. The registry bounds an image's
        // sides and PartRaster::create a variant's, so the products fit.
        VariantWork work{};
        work.source.resize(pixels.bytes.size());
        for (std::size_t sample = 0U; sample < pixels.bytes.size(); ++sample) {
            work.source[sample] = static_cast<double>(pixels.bytes[sample]) / 255.0;
        }
        work.middle.resize(middle_samples(across, down));
        const std::size_t longest = std::max(request.extent.width, request.extent.height);
        work.line.resize(longest * lanes);
        const PartRasterView variant = (*made).view();
        for (std::size_t row = 0U; row < down.count; ++row) {
            for (std::size_t column = 0U; column < across.count; ++column) {
                if (!resample_region(across.regions[column], down.regions[row], width, work, variant)) {
                    return nullptr;
                }
            }
        }
        return made;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

} // namespace gui_forms::engine_primitives
