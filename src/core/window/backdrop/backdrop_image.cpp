#include "backdrop_image.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <vector>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::detail {

namespace {

constexpr std::size_t lanes = engine_primitives::box_blur_lanes;

// The blur's half width: CSS blur(r) is a Gaussian of standard deviation r,
// and box_blur_half takes the shadow convention σ = blur / 2.
std::uint32_t blur_half(const BackdropChain& chain, double scale) noexcept {
    const double radius = backdrop_chain_blur(chain);
    if (!(radius > 0.0)) return 0U;
    const std::uint32_t half = engine_primitives::box_blur_half(radius * 2.0, scale);
    return half;
}

std::uint32_t round_lane(double value, double ceiling) noexcept {
    double clamped = value;
    if (clamped < 0.0) clamped = 0.0;
    if (clamped > ceiling) clamped = ceiling;
    const std::uint32_t rounded = static_cast<std::uint32_t>(std::lround(clamped));
    return rounded;
}

// One per-pixel operation over `count` pixels of an image held as 32-bit
// lanes (premultiplied BGRA, each at most 255), rows `stride` elements
// apart, `columns` × `rows` pixels from the start of `pixels`.
void apply_pixel_operation(const BackdropOperation& operation, std::span<std::uint32_t> pixels, std::size_t stride,
                           std::size_t columns, std::size_t rows) noexcept {
    if (operation.kind == BackdropOperationKind::saturate) {
        // The CSS saturate() matrix (Filter Effects 1, §13.2), on
        // premultiplied colour, clamped to the pixel's alpha.
        const double s = operation.amount;
        const double rr = 0.213 + 0.787 * s;
        const double rg = 0.715 - 0.715 * s;
        const double rb = 0.072 - 0.072 * s;
        const double gr = 0.213 - 0.213 * s;
        const double gg = 0.715 + 0.285 * s;
        const double gb = 0.072 - 0.072 * s;
        const double br = 0.213 - 0.213 * s;
        const double bg = 0.715 - 0.715 * s;
        const double bb = 0.072 + 0.928 * s;
        for (std::size_t row = 0U; row < rows; ++row) {
            for (std::size_t column = 0U; column < columns; ++column) {
                const std::size_t at = row * stride + column * lanes;
                const double blue = static_cast<double>(pixels[at]);
                const double green = static_cast<double>(pixels[at + 1U]);
                const double red = static_cast<double>(pixels[at + 2U]);
                const double alpha = static_cast<double>(pixels[at + 3U]);
                const double new_red = rr * red + rg * green + rb * blue;
                const double new_green = gr * red + gg * green + gb * blue;
                const double new_blue = br * red + bg * green + bb * blue;
                pixels[at] = round_lane(new_blue, alpha);
                pixels[at + 1U] = round_lane(new_green, alpha);
                pixels[at + 2U] = round_lane(new_red, alpha);
            }
        }
        return;
    }
    if (operation.kind == BackdropOperationKind::tint) {
        // Source-over of the premultiplied tint.
        const double over_alpha = static_cast<double>(operation.colour.alpha) / 255.0;
        const double keep = 1.0 - over_alpha;
        const double over_red = static_cast<double>(operation.colour.red) * over_alpha;
        const double over_green = static_cast<double>(operation.colour.green) * over_alpha;
        const double over_blue = static_cast<double>(operation.colour.blue) * over_alpha;
        const double over_lane_alpha = 255.0 * over_alpha;
        for (std::size_t row = 0U; row < rows; ++row) {
            for (std::size_t column = 0U; column < columns; ++column) {
                const std::size_t at = row * stride + column * lanes;
                const double alpha = over_lane_alpha + static_cast<double>(pixels[at + 3U]) * keep;
                pixels[at] = round_lane(over_blue + static_cast<double>(pixels[at]) * keep, alpha);
                pixels[at + 1U] = round_lane(over_green + static_cast<double>(pixels[at + 1U]) * keep, alpha);
                pixels[at + 2U] = round_lane(over_red + static_cast<double>(pixels[at + 2U]) * keep, alpha);
                pixels[at + 3U] = round_lane(alpha, 255.0);
            }
        }
    }
}

} // namespace

std::uint32_t backdrop_margin(const BackdropChain& chain, double scale) noexcept {
    const std::uint32_t margin = blur_half(chain, scale) * 3U;
    return margin;
}

bool compute_backdrop(const BackdropChain& chain, double scale, const PartRasterView& source, std::uint32_t margin,
                      const PartRasterView& destination, BackdropWork& work) noexcept {
    const std::uint32_t half = blur_half(chain, scale);
    if (margin != half * 3U) return false;
    const std::size_t width = destination.extent.width;
    const std::size_t height = destination.extent.height;
    const std::size_t columns = width + std::size_t{margin} * 2U;
    const std::size_t rows = height + std::size_t{margin} * 2U;
    if (width == 0U || height == 0U) return true;
    // Validated whole before anything is read or written.
    if (source.extent.width != columns || source.extent.height != rows || source.row_bytes < columns * lanes ||
        source.pixels.size() < source.row_bytes * (rows - 1U) + columns * lanes ||
        destination.row_bytes < width * lanes ||
        destination.pixels.size() < destination.row_bytes * (height - 1U) + width * lanes) {
        return false;
    }
    try {
        engine_primitives::prepare_box_blur(work.blur, columns, rows, lanes);
    } catch (const std::exception&) {
        // std::bad_alloc or std::length_error: nothing is written.
        return false;
    }
    const std::size_t stride = columns * lanes;
    std::span<std::uint32_t> image(work.blur.first);
    for (std::size_t row = 0U; row < rows; ++row) {
        const std::size_t read = row * source.row_bytes;
        const std::size_t write = row * stride;
        for (std::size_t element = 0U; element < stride; ++element) image[write + element] = source.pixels[read + element];
    }
    // In order: before the blur the operations cover the grown image, after
    // it the destination's (the blur leaves it at the start of the image,
    // rows still `stride` apart). Without a blur the margin is 0.
    std::size_t live_columns = columns;
    std::size_t live_rows = rows;
    const std::size_t length = chain.length < BackdropChain::maximum_operations ? chain.length
                                                                                : BackdropChain::maximum_operations;
    for (std::size_t index = 0U; index < length; ++index) {
        const BackdropOperation& operation = chain.operations[index];
        if (operation.kind == BackdropOperationKind::blur) {
            if (half > 0U) engine_primitives::blur_three_boxes(work.blur, columns, rows, lanes, half);
            live_columns = width;
            live_rows = height;
            continue;
        }
        apply_pixel_operation(operation, image, stride, live_columns, live_rows);
    }
    // A chain whose blur rounds to no box reads no margin either; the
    // destination is the image's top-left `width` × `height` either way
    // once the blur has run, else its centre.
    const std::size_t offset_pixels = live_columns == width ? 0U : std::size_t{margin};
    for (std::size_t row = 0U; row < height; ++row) {
        const std::size_t read = (row + offset_pixels) * stride + offset_pixels * lanes;
        const std::size_t write = row * destination.row_bytes;
        for (std::size_t element = 0U; element < width * lanes; ++element) {
            destination.pixels[write + element] = static_cast<std::uint8_t>(image[read + element]);
        }
    }
    return true;
}

} // namespace gui_forms::detail
