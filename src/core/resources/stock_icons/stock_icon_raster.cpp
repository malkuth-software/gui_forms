// Rasterizing stock icon vectors into coverage, and colouring coverage into
// a part raster (stock_icons.hpp: rasterize_stock_icon, bake_stock_icon).
// Bake work, run once per icon, size and scale; never per frame.
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "stxlib/math.hpp"
#include "gui_forms/resources/stock_icons/stock_icons.hpp"
#include "stock_icon_cache.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gui_forms {
namespace {

// The sub-rows each pixel row of a fill is sampled on.
constexpr std::size_t fill_sub_rows = 16U;
// The flattening tolerance, in device pixels.
constexpr double flattening_pixels = 0.2;
// The flattened storage one bake reuses for every layer: an icon flattened
// past it is drawn from its start (flatten_paint_path's truncation).
constexpr std::size_t maximum_flattened_points = 32768U;
constexpr std::size_t maximum_flattened_contours = 1024U;
constexpr std::uint32_t maximum_icon_side = 4096U;

// One edge of a fill in pixel coordinates, y0 below y1, with its winding.
struct FillEdge final {
    double x0;
    double y0;
    double x1;
    double y1;
    int winding;
};

// Where a sub-row crosses an edge.
struct Crossing final {
    double x;
    int winding;
};

struct CrossingBefore final {
    bool operator()(const Crossing& left, const Crossing& right) const noexcept { return left.x < right.x; }
};

// The storage one bake reuses across layers and rows.
struct BakeStorage final {
    std::vector<Point> points;
    std::vector<FlattenedContour> contours;
    std::vector<FillEdge> edges;
    std::vector<Crossing> crossings;
    std::vector<double> row;
    std::vector<double> layer;
};

// Adds the span [left, right) of one sub-row to `row`, each pixel by the
// part of it the span covers, divided by the sub-row count.
void add_span(std::vector<double>& row, double left, double right) {
    const double side = static_cast<double>(row.size());
    left = std::clamp(left, 0.0, side);
    right = std::clamp(right, 0.0, side);
    if (right <= left) return;
    const double weight = 1.0 / static_cast<double>(fill_sub_rows);
    const std::size_t first = static_cast<std::size_t>(left);
    const std::size_t last = std::min(static_cast<std::size_t>(right), row.size() - 1U);
    if (first == last) {
        row[first] += (right - left) * weight;
        return;
    }
    row[first] += (static_cast<double>(first) + 1.0 - left) * weight;
    for (std::size_t pixel = first + 1U; pixel < last; ++pixel) row[pixel] += weight;
    row[last] += (right - static_cast<double>(last)) * weight;
}

// The flattened path of `path` in pixel coordinates (the grid scaled by
// `factor`), into the storage's points and contours.
FlattenedPath flatten_scaled(PaintPathView path, double factor, BakeStorage& storage) {
    const FlattenedPath flattened =
        flatten_paint_path(path, flattening_pixels / factor, storage.points, storage.contours);
    for (std::size_t index = 0U; index < flattened.point_count; ++index) {
        storage.points[index].x *= factor;
        storage.points[index].y *= factor;
    }
    return flattened;
}

// Fills `storage.layer` (side * side) with the coverage of the flattened
// path filled by `rule`: every contour closed, sampled on sub-rows.
void fill_coverage(const FlattenedPath& flattened, PolygonFillRule rule, std::uint32_t side, BakeStorage& storage) {
    storage.edges.clear();
    std::size_t start = 0U;
    for (std::size_t contour = 0U; contour < flattened.contour_count; ++contour) {
        const std::size_t end = storage.contours[contour].end;
        for (std::size_t index = start; index < end; ++index) {
            const Point from = storage.points[index];
            const Point to = storage.points[index + 1U < end ? index + 1U : start];
            if (from.y == to.y) continue;
            const bool down = to.y > from.y;
            const FillEdge edge = down ? FillEdge{from.x, from.y, to.x, to.y, 1} : FillEdge{to.x, to.y, from.x, from.y, -1};
            storage.edges.push_back(edge);
        }
        start = end;
    }
    for (std::uint32_t y = 0U; y < side; ++y) {
        std::fill(storage.row.begin(), storage.row.end(), 0.0);
        for (std::size_t sub = 0U; sub < fill_sub_rows; ++sub) {
            const double sample_y =
                static_cast<double>(y) + (static_cast<double>(sub) + 0.5) / static_cast<double>(fill_sub_rows);
            storage.crossings.clear();
            for (const FillEdge& edge : storage.edges) {
                if (sample_y < edge.y0 || sample_y >= edge.y1) continue;
                const double x = edge.x0 + (sample_y - edge.y0) * (edge.x1 - edge.x0) / (edge.y1 - edge.y0);
                storage.crossings.push_back(Crossing{x, edge.winding});
            }
            std::sort(storage.crossings.begin(), storage.crossings.end(), CrossingBefore{});
            int winding = 0;
            for (std::size_t index = 0U; index + 1U < storage.crossings.size(); ++index) {
                winding += storage.crossings[index].winding;
                const bool inside = rule == PolygonFillRule::even_odd ? (winding & 1) != 0 : winding != 0;
                if (inside) add_span(storage.row, storage.crossings[index].x, storage.crossings[index + 1U].x);
            }
        }
        const std::size_t row_start = static_cast<std::size_t>(y) * side;
        for (std::uint32_t x = 0U; x < side; ++x) {
            storage.layer[row_start + x] = std::min(1.0, storage.row[x]);
        }
    }
}

// The coverage of one stroked segment at `pixel`: one where the pixel centre
// lies within half the width, falling to zero over one pixel beyond it. A
// butt end (`flat_start`, `flat_end`) cuts the stroke across at the end
// point, also over one pixel; other ends are round.
double segment_coverage(Point pixel, Point from, Point to, double half_width, bool flat_start, bool flat_end) {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared == 0.0) {
        if (flat_start || flat_end) return 0.0;
        const double distance = stxlib::math::hypot(pixel.x - from.x, pixel.y - from.y);
        return std::clamp(half_width + 0.5 - distance, 0.0, 1.0);
    }
    const double length = std::sqrt(length_squared);
    const double along = ((pixel.x - from.x) * dx + (pixel.y - from.y) * dy) / length;
    const double t = std::clamp(along / length, 0.0, 1.0);
    const Point nearest{from.x + dx * t, from.y + dy * t};
    double coverage = std::clamp(half_width + 0.5 - stxlib::math::hypot(pixel.x - nearest.x, pixel.y - nearest.y), 0.0, 1.0);
    if (flat_start) coverage *= std::clamp(along + 0.5, 0.0, 1.0);
    if (flat_end) coverage *= std::clamp(length - along + 0.5, 0.0, 1.0);
    return coverage;
}

// Fills `storage.layer` with the coverage of the flattened path stroked
// `half_width` pixels either side, with round joins and `cap` at the open
// contours' ends (a square cap is a butt cap moved half the width out).
void stroke_coverage(const FlattenedPath& flattened, double half_width, StrokeCap cap, std::uint32_t side,
                     BakeStorage& storage) {
    std::fill(storage.layer.begin(), storage.layer.end(), 0.0);
    const double limit = static_cast<double>(side);
    std::size_t start = 0U;
    for (std::size_t contour = 0U; contour < flattened.contour_count; ++contour) {
        const std::size_t end = storage.contours[contour].end;
        const bool open = !storage.contours[contour].closed;
        const bool flat = open && cap != StrokeCap::round;
        for (std::size_t index = start; index + 1U < end; ++index) {
            Point from = storage.points[index];
            Point to = storage.points[index + 1U];
            const bool first = index == start;
            const bool last = index + 2U == end;
            if (open && cap == StrokeCap::square) {
                const double length = stxlib::math::hypot(to.x - from.x, to.y - from.y);
                if (length > 0.0) {
                    const double ux = (to.x - from.x) / length * half_width;
                    const double uy = (to.y - from.y) / length * half_width;
                    if (first) from = Point{from.x - ux, from.y - uy};
                    if (last) to = Point{to.x + ux, to.y + uy};
                }
            }
            const double reach = half_width + 1.0;
            const double left = std::clamp(std::floor(std::min(from.x, to.x) - reach), 0.0, limit);
            const double right = std::clamp(std::ceil(std::max(from.x, to.x) + reach), 0.0, limit);
            const double top = std::clamp(std::floor(std::min(from.y, to.y) - reach), 0.0, limit);
            const double bottom = std::clamp(std::ceil(std::max(from.y, to.y) + reach), 0.0, limit);
            for (std::size_t y = static_cast<std::size_t>(top); y < static_cast<std::size_t>(bottom); ++y) {
                for (std::size_t x = static_cast<std::size_t>(left); x < static_cast<std::size_t>(right); ++x) {
                    const Point centre{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
                    const double coverage =
                        segment_coverage(centre, from, to, half_width, flat && first, flat && last);
                    double& cell = storage.layer[y * side + x];
                    cell = std::max(cell, coverage);
                }
            }
        }
        start = end;
    }
}

} // namespace

bool rasterize_stock_icon(const StockIconVectors& vectors, std::uint32_t side, std::span<std::uint8_t> coverage) {
    const std::size_t area = static_cast<std::size_t>(side) * side;
    if (side == 0U || side > maximum_icon_side || coverage.size() < area) return false;
    BakeStorage storage;
    storage.points.resize(maximum_flattened_points);
    storage.contours.resize(maximum_flattened_contours);
    storage.row.assign(side, 0.0);
    storage.layer.assign(area, 0.0);
    std::vector<double> total(area, 0.0);
    const double factor = static_cast<double>(side) / vectors.grid();
    for (std::size_t index = 0U; index < vectors.layer_count(); ++index) {
        const StockIconLayer& layer = vectors.layer(index);
        const FlattenedPath flattened = flatten_scaled(vectors.layer_path(index), factor, storage);
        switch (layer.paint) {
        case StockIconPaint::fill_nonzero:
            fill_coverage(flattened, PolygonFillRule::nonzero, side, storage);
            break;
        case StockIconPaint::fill_even_odd:
            fill_coverage(flattened, PolygonFillRule::even_odd, side, storage);
            break;
        case StockIconPaint::stroke:
            stroke_coverage(flattened, layer.stroke_width * factor * 0.5, layer.cap, side, storage);
            break;
        default:
            // Fallback: StockIconVectors::create refuses an unknown paint, so
            // none reaches here; such a layer would draw nothing.
            std::fill(storage.layer.begin(), storage.layer.end(), 0.0);
            break;
        }
        // One colour painted over itself: source-over of the coverages.
        for (std::size_t cell = 0U; cell < area; ++cell) {
            total[cell] = total[cell] + storage.layer[cell] * (1.0 - total[cell]);
        }
    }
    for (std::size_t cell = 0U; cell < area; ++cell) {
        coverage[cell] = static_cast<std::uint8_t>(std::lround(std::clamp(total[cell], 0.0, 1.0) * 255.0));
    }
    return true;
}

std::shared_ptr<const PartRaster> detail::coloured_stock_icon(std::span<const std::uint8_t> coverage,
                                                              std::uint32_t side, DeviceScale scale, Color colour) {
    const std::size_t area = static_cast<std::size_t>(side) * side;
    if (side == 0U || side > maximum_icon_side || coverage.size() < area) return nullptr;
    std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{side, side}, scale);
    if (!raster) return nullptr;
    const PartRasterView view = (*raster).view();
    for (std::size_t cell = 0U; cell < area; ++cell) {
        // Premultiplied BGRA: alpha is the colour's alpha times the coverage,
        // each channel the colour's times that alpha.
        const unsigned alpha = (static_cast<unsigned>(colour.alpha) * coverage[cell] + 127U) / 255U;
        std::uint8_t* const pixel = view.pixels.data() + cell * part_raster_bytes_per_pixel;
        pixel[0] = static_cast<std::uint8_t>((static_cast<unsigned>(colour.blue) * alpha + 127U) / 255U);
        pixel[1] = static_cast<std::uint8_t>((static_cast<unsigned>(colour.green) * alpha + 127U) / 255U);
        pixel[2] = static_cast<std::uint8_t>((static_cast<unsigned>(colour.red) * alpha + 127U) / 255U);
        pixel[3] = static_cast<std::uint8_t>(alpha);
    }
    (*raster).record_opacity();
    std::shared_ptr<const PartRaster> shared = std::move(raster);
    return shared;
}

std::shared_ptr<const PartRaster> bake_stock_icon(const StockIconVectors& vectors, std::uint32_t side,
                                                  DeviceScale scale, Color colour) {
    if (side == 0U || side > maximum_icon_side) return nullptr;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(side) * side);
    if (!rasterize_stock_icon(vectors, side, coverage)) return nullptr;
    const std::shared_ptr<const PartRaster> raster = detail::coloured_stock_icon(coverage, side, scale, colour);
    return raster;
}

} // namespace gui_forms
