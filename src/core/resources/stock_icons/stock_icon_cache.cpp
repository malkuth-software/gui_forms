#include "stock_icon_cache.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/resources/stock_icons/stock_alert_icon.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The most entries of each kind the cache keeps.
constexpr std::size_t maximum_cached_coverage = 512U;
constexpr std::size_t maximum_cached_rasters = 512U;

struct CoverageEntry final {
    // Held so that the address the entry is found by cannot be reused while
    // the entry lives.
    std::shared_ptr<const StockIconVectors> vectors;
    std::uint32_t side;
    std::vector<std::uint8_t> coverage;
};

struct RasterEntry final {
    std::shared_ptr<const StockIconVectors> vectors;
    std::uint32_t side;
    double scale;
    Color colour;
    std::shared_ptr<const PartRaster> raster;
};

class StockIconCache final {
public:
    std::shared_ptr<const PartRaster> raster(const std::shared_ptr<const StockIconVectors>& vectors,
                                             std::uint32_t side, DeviceScale scale, Color colour) {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const RasterEntry& entry : rasters_) {
            if (entry.vectors == vectors && entry.side == side && entry.scale == scale.value &&
                entry.colour == colour) {
                return entry.raster;
            }
        }
        const CoverageEntry* const coverage = coverage_locked(vectors, side);
        if (coverage == nullptr) return nullptr;
        std::shared_ptr<const PartRaster> made = detail::coloured_stock_icon((*coverage).coverage, side, scale, colour);
        if (!made) return nullptr;
        ++raster_bakes_;
        if (rasters_.size() >= maximum_cached_rasters) rasters_.erase(rasters_.begin());
        rasters_.push_back(RasterEntry{vectors, side, scale.value, colour, made});
        return made;
    }

    void prebake(double scale) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (std::find(prebaked_scales_.begin(), prebaked_scales_.end(), scale) != prebaked_scales_.end()) return;
        prebaked_scales_.push_back(scale);
        static constexpr std::array<std::string_view, 5> status{stock_icon_information, stock_icon_warning,
                                                                stock_icon_error, stock_icon_question,
                                                                stock_icon_success};
        static constexpr std::array<StockIconSize, 3> sizes{StockIconSize::grid_16, StockIconSize::grid_20,
                                                            StockIconSize::grid_24};
        for (const std::string_view name : status) {
            for (const StockIconSize size : sizes) {
                const StockIcon icon{std::string(name), StockIconStyle::solid, size};
                const std::shared_ptr<const StockIconVectors> vectors = stock_icon_vectors(icon);
                if (!vectors) continue;
                const double device = std::round(static_cast<double>(size) * scale);
                const std::uint32_t side = static_cast<std::uint32_t>(std::clamp(device, 1.0, 4096.0));
                static_cast<void>(coverage_locked(vectors, side));
            }
        }
    }

    StockIconBakeCounts counts() {
        const std::lock_guard<std::mutex> lock(mutex_);
        StockIconBakeCounts result{};
        result.coverage_bakes = coverage_bakes_;
        result.raster_bakes = raster_bakes_;
        result.cached_coverage = coverage_.size();
        result.cached_rasters = rasters_.size();
        result.prebaked_scales = prebaked_scales_.size();
        return result;
    }

private:
    // The cached coverage of `vectors` at `side`, rasterized if absent;
    // null when it cannot be. Under mutex_. The pointer is valid until the
    // next change to coverage_.
    const CoverageEntry* coverage_locked(const std::shared_ptr<const StockIconVectors>& vectors, std::uint32_t side) {
        if (!vectors) return nullptr;
        for (const CoverageEntry& entry : coverage_) {
            if (entry.vectors == vectors && entry.side == side) return &entry;
        }
        std::vector<std::uint8_t> coverage(static_cast<std::size_t>(side) * side);
        if (!rasterize_stock_icon(*vectors, side, coverage)) return nullptr;
        ++coverage_bakes_;
        if (coverage_.size() >= maximum_cached_coverage) coverage_.erase(coverage_.begin());
        coverage_.push_back(CoverageEntry{vectors, side, std::move(coverage)});
        return &coverage_.back();
    }

    std::mutex mutex_;
    std::vector<CoverageEntry> coverage_;
    std::vector<RasterEntry> rasters_;
    std::vector<double> prebaked_scales_;
    std::uint64_t coverage_bakes_{};
    std::uint64_t raster_bakes_{};
};

StockIconCache& process_cache() {
    // A function-local static: made on first use, thread-safe.
    static StockIconCache cache;
    return cache;
}

// The square as wide as the shorter side of `destination`, centred in it.
Rect centred_square(Rect destination) noexcept {
    const double side = std::min(destination.width, destination.height);
    const Rect square{destination.x + (destination.width - side) * 0.5,
                      destination.y + (destination.height - side) * 0.5, side, side};
    return square;
}

// Draws `vectors` over `square` with the painter's own paths (a detached
// control, which has no window scale to bake for).
void paint_vectors(Painter& painter, const StockIconVectors& vectors, Rect square, Color colour, double opacity) {
    const double factor = square.width / vectors.grid();
    Color drawn = colour;
    drawn.alpha = static_cast<std::uint8_t>(std::lround(static_cast<double>(colour.alpha) * opacity));
    std::vector<Point> mapped;
    for (std::size_t index = 0U; index < vectors.layer_count(); ++index) {
        const StockIconLayer& layer = vectors.layer(index);
        const PaintPathView path = vectors.layer_path(index);
        mapped.clear();
        mapped.reserve(path.points.size());
        for (const Point point : path.points) {
            mapped.push_back(Point{square.x + point.x * factor, square.y + point.y * factor});
        }
        const PaintPathView placed{path.verbs, std::span<const Point>(mapped.data(), mapped.size())};
        switch (layer.paint) {
        case StockIconPaint::fill_nonzero:
            painter.fill_path(placed, drawn, PolygonFillRule::nonzero);
            break;
        case StockIconPaint::fill_even_odd:
            painter.fill_path(placed, drawn, PolygonFillRule::even_odd);
            break;
        case StockIconPaint::stroke: {
            StrokeStyle stroke{};
            stroke.color = drawn;
            stroke.width = layer.stroke_width * factor;
            stroke.join = StrokeJoin::round;
            stroke.cap = layer.cap;
            painter.stroke_path(placed, stroke);
            break;
        }
        default:
            // Fallback: create() refuses an unknown paint; nothing drawn.
            break;
        }
    }
}

} // namespace

std::shared_ptr<const PartRaster> detail::cached_stock_icon(const std::shared_ptr<const StockIconVectors>& vectors,
                                                            std::uint32_t side, DeviceScale scale, Color colour) {
    if (!vectors) return nullptr;
    std::shared_ptr<const PartRaster> raster = process_cache().raster(vectors, side, scale, colour);
    return raster;
}

void detail::prebake_stock_icons(double scale) {
    if (!std::isfinite(scale) || scale < minimum_device_scale || scale > maximum_device_scale) return;
    process_cache().prebake(scale);
}

void paint_stock_icon(Painter& painter, const Control& control, ImageId image, Rect destination, Color colour,
                      double opacity) {
    const Rect square = centred_square(destination);
    if (!std::isfinite(square.width) || square.width <= 0.0 || !std::isfinite(opacity) || opacity <= 0.0) return;
    const std::optional<StockIcon> icon = stock_icon_for_image(image, square.width);
    if (!icon) return;
    std::shared_ptr<const StockIconVectors> vectors = control.effective_theme().stock_icon_replacement(*icon);
    if (!vectors) vectors = stock_icon_vectors(*icon);
    if (!vectors) return;
    const Window* const window = control.attached_window();
    if (window == nullptr) {
        paint_vectors(painter, *vectors, square, colour, std::min(opacity, 1.0));
        return;
    }
    const double scale = (*window).scale();
    const double device = std::round(square.width * scale);
    const std::uint32_t side = static_cast<std::uint32_t>(std::clamp(device, 1.0, 4096.0));
    const std::shared_ptr<const PartRaster> raster = detail::cached_stock_icon(vectors, side, DeviceScale{scale}, colour);
    if (!raster) return;
    painter.draw_part_raster(raster, Point{square.x, square.y}, std::min(opacity, 1.0));
}

StockIconBakeCounts stock_icon_bake_counts() noexcept {
    try {
        return process_cache().counts();
    } catch (...) {
        // Fallback: a failed lock reports nothing counted.
        return StockIconBakeCounts{};
    }
}

} // namespace gui_forms

namespace gui_forms {

std::optional<HostImage> stock_alert_icon_image(const Window& window, HostMessageIcon icon) {
    std::string_view name;
    ControlSurfaceState surface = ControlSurfaceState::normal;
    switch (icon) {
    case HostMessageIcon::information:
        name = stock_icon_information;
        break;
    case HostMessageIcon::warning:
        name = stock_icon_warning;
        surface = ControlSurfaceState::pending;
        break;
    case HostMessageIcon::error:
        name = "x-circle";
        surface = ControlSurfaceState::invalid;
        break;
    case HostMessageIcon::question:
        name = stock_icon_question;
        surface = ControlSurfaceState::pending;
        break;
    case HostMessageIcon::none:
    default:
        // Fallback: no icon, and a value outside the enumeration shows none.
        return std::nullopt;
    }
    const Control::Ptr root = window.root();
    if (!root) return std::nullopt;
    constexpr double alert_icon_side = 32.0;
    const std::optional<StockIcon> chosen = nearest_stock_icon(name, StockIconStyle::solid, alert_icon_side);
    if (!chosen) return std::nullopt;
    std::shared_ptr<const StockIconVectors> vectors = (*root).effective_theme().stock_icon_replacement(*chosen);
    if (!vectors) vectors = stock_icon_vectors(*chosen);
    ControlVisualContext context = (*root).visual_context();
    context.surface = surface;
    const Color colour = (*root).effective_theme().resolve(ControlVisualRole::panel, context).glyph;
    const double scale = window.scale();
    const std::uint32_t side = static_cast<std::uint32_t>(std::clamp(std::round(alert_icon_side * scale), 1.0, 4096.0));
    const std::shared_ptr<const PartRaster> raster = detail::cached_stock_icon(vectors, side, DeviceScale{scale}, colour);
    if (!raster) return std::nullopt;
    HostImage image;
    image.width = side;
    image.height = side;
    image.row_bytes = (*raster).row_bytes();
    const BorrowedSpan<std::uint8_t> pixels = (*raster).pixels();
    image.pixels.resize(pixels.size());
    for (std::size_t index = 0U; index < pixels.size(); ++index) image.pixels[index] = static_cast<std::byte>(pixels[index]);
    return image;
}

} // namespace gui_forms
