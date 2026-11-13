#include "finish_world.hpp"

namespace gui_forms::engine_primitives {

std::size_t paint_world_finish(const FinishFill& fill, double scale, std::int64_t origin_x, std::int64_t origin_y,
                               const PartRasterView& destination, FinishScratch& scratch) noexcept {
    // The surface in destination pixels: world pixel -reach is the kernel's
    // position 0, so destination pixel x reads position
    // origin_x + x + reach. Its side covers the world's whole reach.
    const std::int64_t side = 2 * finish_world_reach;
    const FinishRect surface{-origin_x - finish_world_reach, -origin_y - finish_world_reach, side, side};
    const FinishRect whole{0, 0, static_cast<std::int64_t>(destination.extent.width),
                           static_cast<std::int64_t>(destination.extent.height)};
    const std::size_t written = paint_finish(fill, scale, surface, whole, destination, scratch);
    return written;
}

} // namespace gui_forms::engine_primitives
