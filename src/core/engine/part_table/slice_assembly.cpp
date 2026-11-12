#include "slice_assembly.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace gui_forms::detail {
namespace {

// Which reference pixel shows at destination `index` along an axis of
// `length` pixels assembled from a reference of `reference_length`: within
// the repeated span, the run of `period` pixels from `start` repeats (a
// period of 0 is read as 1).
std::uint32_t reference_index(std::uint32_t index, const SliceAxis& axis, std::uint32_t length,
                              std::uint32_t reference_length) noexcept {
    if (axis.outcome != SliceAxisOutcome::sliced || index < axis.start) return index;
    if (index >= length - axis.end) return index - (length - reference_length);
    const std::uint32_t period = axis.period > 1U ? axis.period : 1U;
    const std::uint32_t repeated = axis.start + (index - axis.start) % period;
    return repeated;
}

} // namespace

void assemble_slices(const PartRaster& reference, const PrimitiveSlicePlan& plan,
                     const PartRasterView& destination) noexcept {
    const DeviceExtent from = reference.extent();
    const DeviceExtent to = destination.extent;
    const std::span<const std::uint8_t> source = reference.pixels();
    const std::size_t pixel = part_raster_bytes_per_pixel;
    const std::size_t row_length = static_cast<std::size_t>(to.width) * pixel;
    const SliceAxis across = plan.across;
    const std::size_t start_bytes = static_cast<std::size_t>(across.start) * pixel;
    const std::size_t end_bytes = static_cast<std::size_t>(across.end) * pixel;
    for (std::uint32_t row = 0U; row < to.height; ++row) {
        const std::uint32_t source_row = reference_index(row, plan.down, to.height, from.height);
        const std::uint8_t* const from_row = source.data() + static_cast<std::size_t>(source_row) * reference.row_bytes();
        std::uint8_t* const to_row = destination.pixels.data() + static_cast<std::size_t>(row) * destination.row_bytes;
        if (across.outcome != SliceAxisOutcome::sliced) {
            std::memcpy(to_row, from_row, row_length);
            continue;
        }
        std::memcpy(to_row, from_row, start_bytes);
        // The run from `start`, one pixel or a dither period, repeated.
        const std::size_t run_bytes = static_cast<std::size_t>(across.period > 1U ? across.period : 1U) * pixel;
        const std::uint8_t* const centre = from_row + start_bytes;
        std::size_t phase = 0U;
        for (std::size_t offset = start_bytes; offset < row_length - end_bytes; offset += pixel) {
            std::memcpy(to_row + offset, centre + phase, pixel);
            phase = phase + pixel;
            if (phase == run_bytes) phase = 0U;
        }
        const std::size_t from_end = static_cast<std::size_t>(from.width) * pixel - end_bytes;
        std::memcpy(to_row + (row_length - end_bytes), from_row + from_end, end_bytes);
    }
}

} // namespace gui_forms::detail
