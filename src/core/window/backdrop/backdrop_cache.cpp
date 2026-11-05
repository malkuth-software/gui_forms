#include "backdrop_cache.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace gui_forms::detail {

namespace {

std::uint32_t device_side(double logical, double scale) noexcept {
    const double device = std::ceil(logical * scale);
    if (!(device > 0.0)) return 0U;
    if (device >= static_cast<double>(maximum_part_raster_side)) return 0U;
    return static_cast<std::uint32_t>(device);
}

std::uint32_t clamp_index(double value, std::uint32_t limit) noexcept {
    if (!(value > 0.0)) return 0U;
    if (value >= static_cast<double>(limit)) return limit;
    return static_cast<std::uint32_t>(value);
}

} // namespace

bool BackdropCache::prepare(const BackdropChain& chain, Rect bounds, double scale) {
    const bool same = raster_ != nullptr && chain == chain_ && bounds == bounds_ && scale == scale_;
    if (same) return true;
    chain_ = chain;
    bounds_ = bounds;
    scale_ = scale;
    margin_ = backdrop_margin(chain, scale);
    raster_.reset();
    valid_.clear();
    tile_columns_ = 0U;
    tile_rows_ = 0U;
    const std::uint32_t width = device_side(bounds.width, scale);
    const std::uint32_t height = device_side(bounds.height, scale);
    if (width == 0U || height == 0U) return false;
    raster_ = PartRaster::create(DeviceExtent{width, height}, DeviceScale{scale});
    if (raster_ == nullptr) return false;
    tile_columns_ = (width + tile_side - 1U) / tile_side;
    tile_rows_ = (height + tile_side - 1U) / tile_side;
    valid_.assign(static_cast<std::size_t>(tile_columns_) * tile_rows_, 0U);
    return true;
}

BackdropCache::TileSpan BackdropCache::tiles_under(Rect window_rect, double grown_device) const noexcept {
    TileSpan span{};
    if (raster_ == nullptr || window_rect.empty()) return span;
    const double left = (window_rect.x - bounds_.x) * scale_ - grown_device;
    const double top = (window_rect.y - bounds_.y) * scale_ - grown_device;
    const double right = (window_rect.x + window_rect.width - bounds_.x) * scale_ + grown_device;
    const double bottom = (window_rect.y + window_rect.height - bounds_.y) * scale_ + grown_device;
    const double side = static_cast<double>(tile_side);
    span.first_column = clamp_index(std::floor(left / side), tile_columns_);
    span.last_column = clamp_index(std::ceil(right / side), tile_columns_);
    span.first_row = clamp_index(std::floor(top / side), tile_rows_);
    span.last_row = clamp_index(std::ceil(bottom / side), tile_rows_);
    return span;
}

std::uint64_t BackdropCache::invalidate(Rect damage) noexcept {
    // A pixel of the backdrop reads its source up to the margin away.
    const TileSpan span = tiles_under(damage, static_cast<double>(margin_) + 1.0);
    std::uint64_t dropped = 0U;
    for (std::uint32_t row = span.first_row; row < span.last_row; ++row) {
        for (std::uint32_t column = span.first_column; column < span.last_column; ++column) {
            std::uint8_t& flag = valid_[static_cast<std::size_t>(row) * tile_columns_ + column];
            if (flag != 0U) dropped = dropped + 1U;
            flag = 0U;
        }
    }
    return dropped;
}

void BackdropCache::invalidate_all() noexcept {
    std::fill(valid_.begin(), valid_.end(), std::uint8_t{0U});
}

void BackdropCache::missing_strips(Rect exposed, std::vector<BackdropStrip>& strips) const {
    strips.clear();
    if (raster_ == nullptr) return;
    const TileSpan span = tiles_under(exposed, 0.0);
    const std::uint32_t width = (*raster_).extent().width;
    const std::uint32_t height = (*raster_).extent().height;
    for (std::uint32_t row = span.first_row; row < span.last_row; ++row) {
        std::uint32_t column = span.first_column;
        while (column < span.last_column) {
            if (valid_[static_cast<std::size_t>(row) * tile_columns_ + column] != 0U) {
                column = column + 1U;
                continue;
            }
            const std::uint32_t start = column;
            while (column < span.last_column && valid_[static_cast<std::size_t>(row) * tile_columns_ + column] == 0U) {
                column = column + 1U;
            }
            BackdropStrip strip{};
            strip.left = start * tile_side;
            strip.top = row * tile_side;
            strip.width = std::min(column * tile_side, width) - strip.left;
            strip.height = std::min((row + 1U) * tile_side, height) - strip.top;
            strips.push_back(strip);
        }
    }
}

void BackdropCache::mark_valid(const BackdropStrip& strip) noexcept {
    const std::uint32_t first_column = strip.left / tile_side;
    const std::uint32_t last_column = (strip.left + strip.width + tile_side - 1U) / tile_side;
    const std::uint32_t first_row = strip.top / tile_side;
    const std::uint32_t last_row = (strip.top + strip.height + tile_side - 1U) / tile_side;
    for (std::uint32_t row = first_row; row < last_row && row < tile_rows_; ++row) {
        for (std::uint32_t column = first_column; column < last_column && column < tile_columns_; ++column) {
            valid_[static_cast<std::size_t>(row) * tile_columns_ + column] = 1U;
        }
    }
}

PartRaster* BackdropCache::writable_raster() {
    if (raster_ == nullptr) return nullptr;
    // A recorded command holds the current raster read-only: write a copy.
    if (raster_.use_count() > 1) {
        const std::shared_ptr<PartRaster> copy = PartRaster::create((*raster_).extent(), (*raster_).scale());
        if (copy == nullptr) return nullptr;
        const PartRasterView view = (*copy).view();
        const std::span<const std::uint8_t> pixels = (*raster_).pixels();
        std::memcpy(view.pixels.data(), pixels.data(), pixels.size());
        raster_ = copy;
    }
    PartRaster* const writable = raster_.get();
    return writable;
}

} // namespace gui_forms::detail
