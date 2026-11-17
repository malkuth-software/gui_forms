// The fill_triangles raster (shaded_triangle_raster.hpp). The main sequence
// is draw_shaded_triangles at the end: the triangles in device units, the
// clipped pixel rectangle, then band by band the owner of every sample and
// each pixel's colour from its samples.
//
// Samples. A pixel has n by n samples (n is 1 or 4, chosen from the
// smoothing mode before any loop), at (2i + 1) / 2n of a pixel across and
// down, which are whole 1/16 pixel units for both n. Corners are rounded to
// 1/16 pixel too, so every edge function is an exact integer: the two
// triangles on a shared edge compute it with opposite signs exactly, and
// the top-left rule gives each sample on it to one of them.
//
// Shading. A pixel's samples are grouped by owning triangle. Edge functions
// are affine, so a group's summed edge functions over its count and the
// triangle's doubled area are the barycentric weights at the group's mean
// sample position; that mean colour is exact in linear light, where the
// colour is affine too. The weights are rounded to 1/65536
// (gui_forms::linear_light_weight) and the corners' premultiplied linear
// light is summed with them in integers, as interpolate_linear_light does
// for two colours.

#include "shaded_triangle_raster.hpp"

#include "gui_forms/types/linear_light/linear_light.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace gui_drawing::render {
namespace {

// Device units per pixel: corners and samples are counted in 1/16 pixel.
constexpr std::int64_t units_per_pixel = 16;
// Pixel rows per band: a band's owners hold rows * n * n samples per pixel
// column.
constexpr std::int64_t band_pixel_rows = 4;
// The most samples a pixel has (4 by 4).
constexpr std::size_t maximum_pixel_samples = 16U;
// The full interpolation weight, 1 in 1/65536.
constexpr std::uint64_t weight_one = gui_forms::linear_light_weight_one;

struct DeviceCorner final {
    std::int64_t x{};
    std::int64_t y{};
};

// `point` through `transform` (as Matrix::transform maps it) rounded to
// 1/16 device pixel; none when a coordinate is not finite or lies beyond
// maximum_shaded_triangle_coordinate.
[[nodiscard]] std::optional<DeviceCorner> device_corner(const Matrix& transform, PointF point) noexcept {
    const double device_x = point.x * transform.m11() + point.y * transform.m21() + transform.dx();
    const double device_y = point.x * transform.m12() + point.y * transform.m22() + transform.dy();
    // !(|v| <= limit) refuses NaN as well.
    const bool within = std::abs(device_x) <= maximum_shaded_triangle_coordinate &&
                        std::abs(device_y) <= maximum_shaded_triangle_coordinate;
    if (!within) return std::nullopt;
    // Scaling by 16 is exact; halves round up.
    const double scaled_x = device_x * static_cast<double>(units_per_pixel);
    const double scaled_y = device_y * static_cast<double>(units_per_pixel);
    const double rounded_x = std::floor(scaled_x + 0.5);
    const double rounded_y = std::floor(scaled_y + 0.5);
    const DeviceCorner corner{static_cast<std::int64_t>(rounded_x), static_cast<std::int64_t>(rounded_y)};
    return corner;
}

// The edge function of the edge from (from_x, from_y) to (to_x, to_y) at
// (x, y): twice the signed area of the edge and the point, positive on the
// side of a positively oriented triangle's interior.
[[nodiscard]] constexpr std::int64_t edge_value(std::int64_t from_x, std::int64_t from_y, std::int64_t to_x,
                                                std::int64_t to_y, std::int64_t x, std::int64_t y) noexcept {
    const std::int64_t across = (to_x - from_x) * (y - from_y);
    const std::int64_t down = (to_y - from_y) * (x - from_x);
    const std::int64_t value = across - down;
    return value;
}

// Edge i of `triangle` (opposite corner i) at (x, y).
[[nodiscard]] std::int64_t triangle_edge(const ShadedTriangle& triangle, std::size_t edge, std::int64_t x,
                                         std::int64_t y) noexcept {
    const std::size_t from = (edge + 1U) % 3U;
    const std::size_t to = (edge + 2U) % 3U;
    const std::int64_t value = edge_value(triangle.x[from], triangle.y[from], triangle.x[to], triangle.y[to], x, y);
    return value;
}

// The top-left rule: a sample exactly on the edge from `from` to `to` of a
// positively oriented triangle belongs to it when the edge is a left edge
// (it runs up the surface) or a top edge (horizontal, running right, the
// interior below). The same edge run the other way is neither, so of two
// triangles sharing it exactly one owns its samples. 0 owns, 1 does not.
[[nodiscard]] constexpr std::int64_t edge_bias(DeviceCorner from, DeviceCorner to) noexcept {
    const std::int64_t rise = to.y - from.y;
    const std::int64_t run = to.x - from.x;
    const bool owned = rise < 0 || (rise == 0 && run > 0);
    return owned ? 0 : 1;
}

[[nodiscard]] constexpr std::int64_t floor_divide(std::int64_t value, std::int64_t divisor) noexcept {
    const std::int64_t quotient = value / divisor;
    const bool inexact_negative = (value % divisor != 0) && (value < 0);
    return inexact_negative ? quotient - 1 : quotient;
}

[[nodiscard]] constexpr std::int64_t ceil_divide(std::int64_t value, std::int64_t divisor) noexcept {
    const std::int64_t quotient = value / divisor;
    const bool inexact_positive = (value % divisor != 0) && (value > 0);
    return inexact_positive ? quotient + 1 : quotient;
}

// A pixel edge of the clip: the first pixel whose centre lies at or past
// `edge` (as a non-anti-aliased clip rounds), within [0, limit].
[[nodiscard]] std::int64_t clip_pixel_edge(double edge, std::int64_t limit) noexcept {
    // !(edge > 0) takes NaN to 0 as well.
    if (!(edge > 0.0)) return 0;
    if (edge >= static_cast<double>(limit)) return limit;
    const double rounded = std::floor(edge + 0.5);
    const std::int64_t pixel = static_cast<std::int64_t>(rounded);
    return std::min(pixel, limit);
}

[[nodiscard]] std::uint8_t premultiplied(std::uint8_t channel, std::uint8_t alpha) noexcept {
    const unsigned product = static_cast<unsigned>(channel) * alpha;
    return static_cast<std::uint8_t>((product + 127U) / 255U);
}

// Corner `corner` of the command as a triangle's colour terms; an empty
// colour is transparent.
void assign_corner_colour(ShadedTriangle& triangle, std::size_t corner, Color color) noexcept {
    if (color.is_empty()) {
        triangle.alpha[corner] = 0U;
        triangle.red[corner] = 0U;
        triangle.green[corner] = 0U;
        triangle.blue[corner] = 0U;
        return;
    }
    const std::uint64_t alpha = color.alpha();
    triangle.alpha[corner] = alpha;
    triangle.red[corner] = gui_forms::linear_light_of_srgb(color.red()).value * alpha;
    triangle.green[corner] = gui_forms::linear_light_of_srgb(color.green()).value * alpha;
    triangle.blue[corner] = gui_forms::linear_light_of_srgb(color.blue()).value * alpha;
}

// The triangle of corners first, first + 1 and first + 2 of `command`,
// oriented positively; none for corners on one line. The corners were
// checked by shaded_triangles_refusal.
[[nodiscard]] std::optional<ShadedTriangle> device_triangle(const DrawingCommand& command, std::size_t first) {
    std::array<DeviceCorner, 3> corners{};
    std::array<std::size_t, 3> sources{first, first + 1U, first + 2U};
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
        const std::optional<DeviceCorner> mapped = device_corner(command.state.transform, command.points[first + corner]);
        if (!mapped.has_value()) return std::nullopt;
        corners[corner] = *mapped;
    }
    std::int64_t area = edge_value(corners[0].x, corners[0].y, corners[1].x, corners[1].y, corners[2].x, corners[2].y);
    if (area == 0) return std::nullopt;
    if (area < 0) {
        // The other orientation: corners 1 and 2 (and their colours) trade
        // places, which makes the area positive.
        std::swap(corners[1], corners[2]);
        std::swap(sources[1], sources[2]);
        area = -area;
    }
    // The corner first in reading order (top, then left) becomes corner 0,
    // keeping the orientation: a triangle given in any order or turned
    // either way is then the same record, and shades to the same bytes
    // (the weights' rounding depends on which corner takes the rest).
    std::size_t lead = 0U;
    for (std::size_t corner = 1U; corner < 3U; ++corner) {
        const bool higher = corners[corner].y < corners[lead].y;
        const bool level_and_left = corners[corner].y == corners[lead].y && corners[corner].x < corners[lead].x;
        if (higher || level_and_left) lead = corner;
    }
    std::rotate(corners.begin(), corners.begin() + static_cast<std::ptrdiff_t>(lead), corners.end());
    std::rotate(sources.begin(), sources.begin() + static_cast<std::ptrdiff_t>(lead), sources.end());
    ShadedTriangle triangle;
    triangle.area = area;
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
        triangle.x[corner] = corners[corner].x;
        triangle.y[corner] = corners[corner].y;
        const DeviceCorner from = corners[(corner + 1U) % 3U];
        const DeviceCorner to = corners[(corner + 2U) % 3U];
        triangle.bias[corner] = edge_bias(from, to);
        assign_corner_colour(triangle, corner, command.colors[sources[corner]]);
    }
    triangle.left = std::min({corners[0].x, corners[1].x, corners[2].x});
    triangle.right = std::max({corners[0].x, corners[1].x, corners[2].x});
    triangle.top = std::min({corners[0].y, corners[1].y, corners[2].y});
    triangle.bottom = std::max({corners[0].y, corners[1].y, corners[2].y});
    return triangle;
}

// Orders triangle numbers by the top of their bounding boxes.
struct TopsFirst final {
    const std::vector<ShadedTriangle>* triangles{};
    [[nodiscard]] bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        return (*triangles)[left].top < (*triangles)[right].top;
    }
};

// True for a triangle wholly above `first_sample_y`, the band's first
// sample row: no later band reaches it.
struct EndsAbove final {
    const std::vector<ShadedTriangle>* triangles{};
    std::int64_t first_sample_y{};
    [[nodiscard]] bool operator()(std::uint32_t index) const noexcept {
        return (*triangles)[index].bottom < first_sample_y;
    }
};

// The sample grid of one command: samples per pixel axis, their spacing
// and offset in device units, and the clipped pixel rectangle they cover.
struct SampleGrid final {
    std::int64_t per_axis{};
    std::int64_t spacing{};
    std::int64_t offset{};
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
    // Sample columns across the clipped rectangle.
    std::int64_t columns{};
};

// One band of pixel rows of the grid: [top, bottom), and its first sample
// row's y in device units.
struct SampleBand final {
    std::int64_t top{};
    std::int64_t bottom{};
    std::int64_t rows{};
    std::int64_t first_y{};
};

// Marks the samples of `band` inside `triangle` as owned by `number`
// unless a later triangle (a larger number) owns them.
void cover_triangle(const ShadedTriangle& triangle, std::uint32_t number, const SampleGrid& grid,
                    const SampleBand& band, std::span<std::uint32_t> owners) noexcept {
    const std::int64_t first_x = grid.left * units_per_pixel + grid.offset;
    const std::int64_t first_row = std::max<std::int64_t>(0, ceil_divide(triangle.top - band.first_y, grid.spacing));
    const std::int64_t last_row = std::min(band.rows - 1, floor_divide(triangle.bottom - band.first_y, grid.spacing));
    const std::int64_t first_column = std::max<std::int64_t>(0, ceil_divide(triangle.left - first_x, grid.spacing));
    const std::int64_t last_column = std::min(grid.columns - 1, floor_divide(triangle.right - first_x, grid.spacing));
    if (first_row > last_row || first_column > last_column) return;
    // Each edge function falls by its rise per unit across: one sample
    // column steps it by -rise * spacing, exactly.
    std::array<std::int64_t, 3> steps{};
    for (std::size_t edge = 0U; edge < 3U; ++edge) {
        const std::size_t from = (edge + 1U) % 3U;
        const std::size_t to = (edge + 2U) % 3U;
        steps[edge] = -(triangle.y[to] - triangle.y[from]) * grid.spacing;
    }
    const std::int64_t start_x = first_x + first_column * grid.spacing;
    for (std::int64_t row = first_row; row <= last_row; ++row) {
        const std::int64_t sample_y = band.first_y + row * grid.spacing;
        std::int64_t edge0 = triangle_edge(triangle, 0U, start_x, sample_y);
        std::int64_t edge1 = triangle_edge(triangle, 1U, start_x, sample_y);
        std::int64_t edge2 = triangle_edge(triangle, 2U, start_x, sample_y);
        const std::size_t row_start = static_cast<std::size_t>(row * grid.columns);
        for (std::int64_t column = first_column; column <= last_column; ++column) {
            const bool inside = edge0 >= triangle.bias[0] && edge1 >= triangle.bias[1] && edge2 >= triangle.bias[2];
            if (inside) {
                std::uint32_t& owner = owners[row_start + static_cast<std::size_t>(column)];
                owner = std::max(owner, number);
            }
            edge0 += steps[0];
            edge1 += steps[1];
            edge2 += steps[2];
        }
    }
}

// A pixel's samples owned by one triangle: their count and the sums of
// edges 1 and 2 over them (edge 0's sum is count * area minus both).
struct OwnerGroup final {
    std::uint32_t number{};
    std::uint64_t count{};
    std::int64_t edge1_sum{};
    std::int64_t edge2_sum{};
};

// A pixel's premultiplied linear light summed over its samples: alpha in
// 1/65536 of a byte step per sample, and each channel as linear light
// times that alpha.
struct PixelSums final {
    std::uint64_t covered{};
    std::uint64_t alpha{};
    std::uint64_t red{};
    std::uint64_t green{};
    std::uint64_t blue{};
};

// Adds `group`'s samples, shaded at their mean position, to `sums`.
void add_group(const ShadedTriangle& triangle, const OwnerGroup& group, PixelSums& sums) noexcept {
    const double scale = static_cast<double>(group.count) * static_cast<double>(triangle.area);
    const double fraction1 = static_cast<double>(group.edge1_sum) / scale;
    const double fraction2 = static_cast<double>(group.edge2_sum) / scale;
    const std::uint64_t weight1 = gui_forms::linear_light_weight(fraction1);
    std::uint64_t weight2 = gui_forms::linear_light_weight(fraction2);
    // Rounding may take the two past one; corner 0 then weighs nothing.
    if (weight1 + weight2 > weight_one) weight2 = weight_one - weight1;
    const std::uint64_t weight0 = weight_one - weight1 - weight2;
    const std::uint64_t alpha =
        triangle.alpha[0] * weight0 + triangle.alpha[1] * weight1 + triangle.alpha[2] * weight2;
    const std::uint64_t red = triangle.red[0] * weight0 + triangle.red[1] * weight1 + triangle.red[2] * weight2;
    const std::uint64_t green =
        triangle.green[0] * weight0 + triangle.green[1] * weight1 + triangle.green[2] * weight2;
    const std::uint64_t blue = triangle.blue[0] * weight0 + triangle.blue[1] * weight1 + triangle.blue[2] * weight2;
    sums.covered += group.count;
    sums.alpha += alpha * group.count;
    sums.red += red * group.count;
    sums.green += green * group.count;
    sums.blue += blue * group.count;
}

// The sRGB byte of a channel's premultiplied linear sum over `alpha_sum`.
[[nodiscard]] std::uint8_t encoded_channel(std::uint64_t channel_sum, std::uint64_t alpha_sum) noexcept {
    const std::uint64_t level = (channel_sum + alpha_sum / 2U) / alpha_sum;
    const std::uint16_t bounded = static_cast<std::uint16_t>(std::min<std::uint64_t>(level, 65535U));
    const std::uint8_t encoded = gui_forms::srgb_of_linear_light(gui_forms::LinearLightLevel{bounded});
    return encoded;
}

// Composites one pixel's sums into `pixel` (B, G, R, A, premultiplied),
// `samples` being the samples a pixel has.
void composite_pixel(const PixelSums& sums, std::uint64_t samples, CompositingMode mode, std::byte* pixel) noexcept {
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;
    if (sums.alpha != 0U) {
        red = encoded_channel(sums.red, sums.alpha);
        green = encoded_channel(sums.green, sums.alpha);
        blue = encoded_channel(sums.blue, sums.alpha);
    }
    const std::array<std::uint8_t, 4> straight{blue, green, red, 0U};
    if (mode == CompositingMode::source_copy) {
        // The colour at full alpha over the covered samples, replacing the
        // target in proportion to the coverage.
        const std::uint64_t full_scale = sums.covered * weight_one;
        const std::uint8_t alpha = static_cast<std::uint8_t>((sums.alpha + full_scale / 2U) / full_scale);
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            const std::uint8_t source = channel == 3U ? alpha : premultiplied(straight[channel], alpha);
            const std::uint64_t target = std::to_integer<std::uint8_t>(pixel[channel]);
            const std::uint64_t mixed = (source * sums.covered + target * (samples - sums.covered) + samples / 2U) / samples;
            pixel[channel] = static_cast<std::byte>(mixed);
        }
        return;
    }
    if (sums.alpha == 0U) return;
    // Source over: the alpha is the coverage-weighted mean alpha.
    const std::uint64_t scale = samples * weight_one;
    const std::uint8_t alpha = static_cast<std::uint8_t>((sums.alpha + scale / 2U) / scale);
    const unsigned remaining = 255U - alpha;
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const unsigned source = channel == 3U ? alpha : premultiplied(straight[channel], alpha);
        const unsigned target = std::to_integer<std::uint8_t>(pixel[channel]);
        const unsigned kept = (target * remaining + 127U) / 255U;
        const unsigned blended = std::min(255U, source + kept);
        pixel[channel] = static_cast<std::byte>(blended);
    }
}

// Shades and composites every pixel of `band` from its samples' owners.
void resolve_band(const std::vector<ShadedTriangle>& triangles, const SampleGrid& grid, const SampleBand& band,
                  std::span<const std::uint32_t> owners, CompositingMode mode, ShadedTriangleTarget target) noexcept {
    const std::uint64_t samples = static_cast<std::uint64_t>(grid.per_axis * grid.per_axis);
    std::array<OwnerGroup, maximum_pixel_samples> groups{};
    for (std::int64_t pixel_y = band.top; pixel_y < band.bottom; ++pixel_y) {
        const std::int64_t first_row = (pixel_y - band.top) * grid.per_axis;
        std::byte* const row = target.pixels + static_cast<std::size_t>(pixel_y) * target.row_bytes;
        for (std::int64_t pixel_x = grid.left; pixel_x < grid.right; ++pixel_x) {
            const std::int64_t first_column = (pixel_x - grid.left) * grid.per_axis;
            std::size_t group_count = 0U;
            for (std::int64_t sample_row = 0; sample_row < grid.per_axis; ++sample_row) {
                const std::int64_t row_index = first_row + sample_row;
                const std::int64_t sample_y = band.first_y + row_index * grid.spacing;
                for (std::int64_t sample_column = 0; sample_column < grid.per_axis; ++sample_column) {
                    const std::int64_t column_index = first_column + sample_column;
                    const std::size_t at = static_cast<std::size_t>(row_index * grid.columns + column_index);
                    const std::uint32_t number = owners[at];
                    if (number == 0U) continue;
                    std::size_t group = 0U;
                    while (group < group_count && groups[group].number != number) ++group;
                    if (group == group_count) {
                        groups[group] = OwnerGroup{number, 0U, 0, 0};
                        ++group_count;
                    }
                    const ShadedTriangle& triangle = triangles[number - 1U];
                    const std::int64_t sample_x = grid.left * units_per_pixel + grid.offset + column_index * grid.spacing;
                    groups[group].count += 1U;
                    groups[group].edge1_sum += triangle_edge(triangle, 1U, sample_x, sample_y);
                    groups[group].edge2_sum += triangle_edge(triangle, 2U, sample_x, sample_y);
                }
            }
            if (group_count == 0U) continue;
            PixelSums sums{};
            for (std::size_t group = 0U; group < group_count; ++group) {
                add_group(triangles[groups[group].number - 1U], groups[group], sums);
            }
            std::byte* const pixel = row + static_cast<std::size_t>(pixel_x) * 4U;
            composite_pixel(sums, samples, mode, pixel);
        }
    }
}

} // namespace

RasterError shaded_triangles_refusal(const DrawingCommand& command) noexcept {
    const std::size_t count = command.points.size();
    const bool whole = count != 0U && count % 3U == 0U && count == command.colors.size() &&
                       count <= GraphicsRecorder::maximum_batch_elements;
    if (!whole) return RasterError::invalid_argument;
    for (const PointF point : command.points) {
        const std::optional<DeviceCorner> corner = device_corner(command.state.transform, point);
        if (!corner.has_value()) return RasterError::dimension_limit_exceeded;
    }
    return RasterError::none;
}

RasterError draw_shaded_triangles(const DrawingCommand& command, ShadedTriangleTarget target,
                                  ShadedTriangleScratch& scratch) {
    const RasterError refused = shaded_triangles_refusal(command);
    if (refused != RasterError::none) return refused;
    const double widest = static_cast<double>(std::max(target.width, target.height));
    if (widest > maximum_shaded_triangle_coordinate) return RasterError::dimension_limit_exceeded;

    // The clipped pixel rectangle.
    SampleGrid grid{};
    const bool hard_edges = command.state.smoothing == SmoothingMode::none ||
                            command.state.smoothing == SmoothingMode::high_speed;
    grid.per_axis = hard_edges ? 1 : 4;
    grid.spacing = units_per_pixel / grid.per_axis;
    grid.offset = grid.spacing / 2;
    grid.right = target.width;
    grid.bottom = target.height;
    if (command.state.clip.present()) {
        const RectF clip = (*command.state.clip.value());
        grid.left = clip_pixel_edge(clip.x, target.width);
        grid.top = clip_pixel_edge(clip.y, target.height);
        grid.right = clip_pixel_edge(clip.x + clip.width, target.width);
        grid.bottom = clip_pixel_edge(clip.y + clip.height, target.height);
    }
    if (grid.right <= grid.left || grid.bottom <= grid.top) return RasterError::none;
    grid.columns = (grid.right - grid.left) * grid.per_axis;

    // The triangles in device units and the band storage, made before any
    // pixel is written, so that a failed allocation leaves the target as
    // it was.
    const std::size_t triangle_count = command.points.size() / 3U;
    scratch.triangles.clear();
    scratch.triangles.reserve(triangle_count);
    for (std::size_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const std::optional<ShadedTriangle> made = device_triangle(command, triangle * 3U);
        if (made.has_value()) scratch.triangles.push_back(*made);
    }
    if (scratch.triangles.empty()) return RasterError::none;
    scratch.order.resize(scratch.triangles.size());
    for (std::size_t index = 0U; index < scratch.order.size(); ++index) {
        scratch.order[index] = static_cast<std::uint32_t>(index);
    }
    const TopsFirst tops_first{&scratch.triangles};
    std::sort(scratch.order.begin(), scratch.order.end(), tops_first);
    scratch.active.clear();
    scratch.active.reserve(scratch.triangles.size());
    const std::size_t band_samples =
        static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(band_pixel_rows * grid.per_axis);
    scratch.owners.resize(band_samples);

    std::size_t next = 0U;
    for (std::int64_t band_top = grid.top; band_top < grid.bottom; band_top += band_pixel_rows) {
        SampleBand band{};
        band.top = band_top;
        band.bottom = std::min(band_top + band_pixel_rows, grid.bottom);
        band.rows = (band.bottom - band.top) * grid.per_axis;
        band.first_y = band.top * units_per_pixel + grid.offset;
        const std::int64_t last_y = band.first_y + (band.rows - 1) * grid.spacing;
        while (next < scratch.order.size() && scratch.triangles[scratch.order[next]].top <= last_y) {
            scratch.active.push_back(scratch.order[next]);
            ++next;
        }
        const EndsAbove ends_above{&scratch.triangles, band.first_y};
        std::erase_if(scratch.active, ends_above);
        if (scratch.active.empty()) continue;
        const std::size_t used = static_cast<std::size_t>(grid.columns * band.rows);
        const std::span<std::uint32_t> owners(scratch.owners.data(), used);
        std::fill(owners.begin(), owners.end(), 0U);
        for (const std::uint32_t index : scratch.active) {
            cover_triangle(scratch.triangles[index], index + 1U, grid, band, owners);
        }
        resolve_band(scratch.triangles, grid, band, owners, command.state.compositing, target);
    }
    return RasterError::none;
}

} // namespace gui_drawing::render
