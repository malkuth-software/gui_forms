#include "skia_splash_bake.hpp"

#include "../../../core/display/replay/replay_display_chunk.hpp"
#include "skia_raster.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace gui_forms::render {

namespace {

// The worker's SkiaRaster. Made, used and destroyed on the baker's worker
// thread only.
class SkiaSplashBakeRaster final : public gui_forms::detail::SplashBakeRaster {
public:
    explicit SkiaSplashBakeRaster(const std::shared_ptr<gui_forms::detail::BundledFontSet>& fonts) {
        if (fonts) static_cast<void>(raster_.register_bundled_fonts(fonts));
    }

    bool bake(const gui_forms::detail::DisplaySnapshot& snapshot, const PartRasterView target) override {
        const Size logical = snapshot.logical_size();
        static_cast<void>(raster_.resize(logical, snapshot.scale().value));
        const DeviceExtent extent = snapshot.device_extent();
        const std::uint32_t width = std::min(extent.width, raster_.pixel_width());
        const std::uint32_t height = std::min(extent.height, raster_.pixel_height());
        if (width == 0U || height == 0U) return false;
        DamageRegion damage;
        damage.add(Rect{0.0, 0.0, logical.width, logical.height});
        raster_.begin_frame(damage);
        // The raster is reused for unrelated snapshots: each starts clear.
        raster_.clear_transparent();
        raster_.save();
        static_cast<void>(gui_forms::detail::replay_display_chunk(snapshot.chunk(), raster_));
        raster_.restore();
        raster_.end_frame();
        const std::uint8_t* const source = static_cast<const std::uint8_t*>(raster_.pixels());
        const std::size_t source_row_bytes = raster_.row_bytes();
        if (source == nullptr) return false;
        // Both are premultiplied BGRA8. The target's rows are tight; a
        // device pixel the raster does not cover (its extent rounded
        // differently) stays transparent, as PartRaster::create left it.
        const std::size_t copied_row = static_cast<std::size_t>(width) * 4U;
        if (copied_row > source_row_bytes || copied_row > target.row_bytes) return false;
        if (static_cast<std::size_t>(height) * target.row_bytes > target.pixels.size()) return false;
        for (std::uint32_t row = 0U; row < height; ++row) {
            const std::uint8_t* const from = source + static_cast<std::size_t>(row) * source_row_bytes;
            std::uint8_t* const to = target.pixels.data() + static_cast<std::size_t>(row) * target.row_bytes;
            std::memcpy(to, from, copied_row);
        }
        return true;
    }

private:
    SkiaRaster raster_{};
};

} // namespace

SkiaSplashBakeRasterFactory::SkiaSplashBakeRasterFactory(std::shared_ptr<gui_forms::detail::BundledFontSet> fonts) noexcept
    : fonts_(std::move(fonts)) {}

std::unique_ptr<gui_forms::detail::SplashBakeRaster> SkiaSplashBakeRasterFactory::make() {
    std::unique_ptr<gui_forms::detail::SplashBakeRaster> made = std::make_unique<SkiaSplashBakeRaster>(fonts_);
    return made;
}

SkiaSplashBakeRun::SkiaSplashBakeRun(std::shared_ptr<gui_forms::detail::BundledFontSet> fonts) {
    std::unique_ptr<gui_forms::detail::SplashBakeRasterFactory> factory =
        std::make_unique<SkiaSplashBakeRasterFactory>(std::move(fonts));
    baker_ = std::make_unique<gui_forms::detail::SplashBaker>(std::move(factory));
    installation_ = std::make_unique<gui_forms::detail::SplashBakerInstallation>(*baker_);
}

SkiaSplashBakeRun::~SkiaSplashBakeRun() {
    installation_.reset();
    baker_.reset();
}

} // namespace gui_forms::render
