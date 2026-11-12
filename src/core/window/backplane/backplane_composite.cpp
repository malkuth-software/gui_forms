#include "backplane_composite.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms::detail {
namespace {

// What a save or a group restores: the translation, the clip (window
// logical units) and the opacity of what is drawn.
struct CompositeState final {
    Point offset{};
    Rect clip{};
    double opacity{1.0};
};

// Saves and groups nest at most this deep in a backplane chunk the
// composite follows; deeper nesting is a recording no first-party control
// makes.
constexpr std::size_t maximum_composite_depth = 64U;

// The first device pixel whose centre lies at or past `edge`, within
// [0, limit].
std::int64_t first_pixel(double edge, std::int64_t limit) noexcept {
    const double pixel = std::ceil(edge - 0.5);
    const double bounded = std::clamp(pixel, 0.0, static_cast<double>(limit));
    const std::int64_t first = static_cast<std::int64_t>(bounded);
    return first;
}

std::uint32_t scaled_channel(std::uint32_t value, std::uint32_t opacity) noexcept {
    const std::uint32_t scaled = (value * opacity + 127U) / 255U;
    return scaled;
}

// Composites `raster` over the window-logical rectangle `placed`, clipped
// to `clip`, at `opacity`, into `image` covering `region`.
void composite_raster(const PartRaster& raster, Rect placed, Rect clip, double opacity, const BackplaneRegion& region,
                      const PartRasterView& image) noexcept {
    const DeviceExtent source = raster.extent();
    if (source.width == 0U || source.height == 0U || !(placed.width > 0.0) || !(placed.height > 0.0)) return;
    const double scale = region.scale.value;
    const double left = placed.x * scale - region.origin.x;
    const double top = placed.y * scale - region.origin.y;
    const double width = placed.width * scale;
    const double height = placed.height * scale;
    const double clip_left = clip.x * scale - region.origin.x;
    const double clip_top = clip.y * scale - region.origin.y;
    const double clip_right = clip_left + clip.width * scale;
    const double clip_bottom = clip_top + clip.height * scale;
    const std::int64_t columns = static_cast<std::int64_t>(image.extent.width);
    const std::int64_t rows = static_cast<std::int64_t>(image.extent.height);
    const std::int64_t column_start = first_pixel(std::max(left, clip_left), columns);
    const std::int64_t column_end = first_pixel(std::min(left + width, clip_right), columns);
    const std::int64_t row_start = first_pixel(std::max(top, clip_top), rows);
    const std::int64_t row_end = first_pixel(std::min(top + height, clip_bottom), rows);
    const double alpha = std::clamp(opacity, 0.0, 1.0);
    const std::uint32_t weight = static_cast<std::uint32_t>(std::floor(alpha * 255.0 + 0.5));
    if (weight == 0U) return;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const double across = static_cast<double>(source.width) / width;
    const double down = static_cast<double>(source.height) / height;
    for (std::int64_t row = row_start; row < row_end; ++row) {
        const double source_y = std::floor((static_cast<double>(row) + 0.5 - top) * down);
        const std::size_t from_row = static_cast<std::size_t>(
            std::clamp(source_y, 0.0, static_cast<double>(source.height - 1U)));
        const std::size_t read_row = from_row * raster.row_bytes();
        const std::size_t write_row = static_cast<std::size_t>(row) * image.row_bytes;
        for (std::int64_t column = column_start; column < column_end; ++column) {
            const double source_x = std::floor((static_cast<double>(column) + 0.5 - left) * across);
            const std::size_t from_column = static_cast<std::size_t>(
                std::clamp(source_x, 0.0, static_cast<double>(source.width - 1U)));
            const std::size_t read = read_row + from_column * 4U;
            const std::size_t write = write_row + static_cast<std::size_t>(column) * 4U;
            const std::uint32_t source_alpha = scaled_channel(pixels[read + 3U], weight);
            const std::uint32_t keep = 255U - source_alpha;
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                const std::uint32_t over = scaled_channel(pixels[read + lane], weight);
                const std::uint32_t under = scaled_channel(image.pixels[write + lane], keep);
                image.pixels[write + lane] = static_cast<std::uint8_t>(std::min(over + under, 255U));
            }
        }
    }
}

// The window-logical rectangle `rect`, drawn at translation `offset`.
Rect translated(Rect rect, Point offset) noexcept {
    const Rect moved{rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
    return moved;
}

} // namespace

std::uint64_t composite_backplane_chunk(const DisplayChunk& chunk, Point origin, Rect clip,
                                        const BackplaneRegion& region, const PartRasterView& image) noexcept {
    std::array<CompositeState, maximum_composite_depth> saved{};
    std::size_t depth = 0U;
    CompositeState state{origin, clip, 1.0};
    std::uint64_t skipped = 0U;
    const DisplayCommandList& commands = chunk.commands();
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const DisplayCommand& command = commands[index];
        switch (command.operation) {
        case DisplayOperation::save:
        case DisplayOperation::begin_group:
            if (depth == saved.size()) {
                // Too deep to follow: what remains is skipped.
                const std::uint64_t remaining = static_cast<std::uint64_t>(commands.size() - index);
                skipped = skipped + remaining;
                return skipped;
            }
            saved[depth] = state;
            depth = depth + 1U;
            if (command.operation == DisplayOperation::begin_group) state.opacity = state.opacity * command.scalar;
            break;
        case DisplayOperation::restore:
        case DisplayOperation::end_group:
            if (depth > 0U) {
                depth = depth - 1U;
                state = saved[depth];
            }
            break;
        case DisplayOperation::translate:
            state.offset = Point{state.offset.x + command.first.x, state.offset.y + command.first.y};
            break;
        case DisplayOperation::clip_rect:
        case DisplayOperation::clip_rounded_rect:
        case DisplayOperation::clip_rounded_rect_corners:
            state.clip = Rect::intersection(state.clip, translated(command.rect, state.offset));
            break;
        case DisplayOperation::draw_part_raster: {
            if (!command.part_raster) break;
            const PartRaster& raster = *command.part_raster;
            const double scale = raster.scale().value;
            const Rect placed{command.first.x + state.offset.x, command.first.y + state.offset.y,
                              static_cast<double>(raster.extent().width) / scale,
                              static_cast<double>(raster.extent().height) / scale};
            composite_raster(raster, placed, state.clip, state.opacity * command.scalar, region, image);
            break;
        }
        case DisplayOperation::draw_part_raster_scaled:
            if (!command.part_raster) break;
            composite_raster(*command.part_raster, translated(command.rect, state.offset), state.clip,
                             state.opacity * command.scalar, region, image);
            break;
        default:
            // The backplane composites part rasters only: any other drawing
            // (fills, strokes, text, images) is counted as skipped.
            skipped = skipped + 1U;
            break;
        }
    }
    return skipped;
}

} // namespace gui_forms::detail
