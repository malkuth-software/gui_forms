#include "material_image.hpp"

#include "../../../image/resource_pixels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// The image's view when it is in the registry at `width` × `height` pixels.
std::optional<ImageResourceView> matching_view(const ImageRegistry& registry, ImageId image, std::uint32_t width,
                                               std::uint32_t height) noexcept {
    const std::optional<ImageResourceView> view = registry.find(image);
    if (!view) return std::nullopt;
    if ((*view).metadata.width != width || (*view).metadata.height != height) return std::nullopt;
    if (width == 0U || height == 0U) return std::nullopt;
    return view;
}

// A declared pixel length as a count: 0 unless it is a whole number in
// range.
std::uint32_t pixel_count(double declared) noexcept {
    if (!(declared >= 1.0) || declared > static_cast<double>(maximum_part_raster_side)) return 0U;
    if (std::floor(declared) != declared) return 0U;
    const std::uint32_t count = static_cast<std::uint32_t>(declared);
    return count;
}

// A device length rounded to whole pixels, at least 1 and at most the
// largest raster side.
std::uint32_t variant_length(double length) noexcept {
    const double bounded = std::clamp(std::round(length), 1.0, static_cast<double>(maximum_part_raster_side));
    const std::uint32_t pixels = static_cast<std::uint32_t>(bounded);
    return pixels;
}

// A fixed segment's device length rounded to whole pixels, within `room`.
std::uint32_t fixed_length(double length, std::uint32_t room) noexcept {
    const double rounded = std::clamp(std::round(length), 0.0, static_cast<double>(room));
    const std::uint32_t pixels = static_cast<std::uint32_t>(rounded);
    return pixels;
}

// One segment of an axis: destination centres in [destination_start,
// destination_end) map onto variant pixels [variant_start, variant_end).
struct AxisSegment final {
    double destination_start{};
    double destination_end{};
    std::uint32_t variant_start{};
    std::uint32_t variant_end{};
};

// The variant pixel a destination centre `centre` shows within `segment`,
// in [0, variant_length).
std::uint32_t segment_pixel(const AxisSegment& segment, double centre, std::uint32_t variant_length) noexcept {
    const double destination_length = segment.destination_end - segment.destination_start;
    const std::uint32_t pixels = segment.variant_end - segment.variant_start;
    double index = static_cast<double>(segment.variant_start);
    if (destination_length > 0.0 && pixels > 0U) {
        const double along = (centre - segment.destination_start) * static_cast<double>(pixels) / destination_length;
        const double last = static_cast<double>(pixels - 1U);
        index = index + std::clamp(std::floor(along), 0.0, last);
    }
    const double bounded = std::min(index, static_cast<double>(variant_length - 1U));
    const std::uint32_t pixel = static_cast<std::uint32_t>(bounded);
    return pixel;
}

// `anchor` and `offset` are the fill's position along the axis
// (MaterialFillLayer::image_anchor and image_offset).
ImageAxisPlacement placement_along(const MaterialFillLayer& fill, double box_start, double box_length, double scale,
                                   double source_length, double slice_start, double slice_end,
                                   double declared_length, double anchor, double offset) noexcept {
    ImageAxisPlacement placement{};
    placement.mode = fill.image_mode;
    placement.start = box_start;
    placement.length = box_length;
    placement.source_length = source_length;
    // Source pixels per logical unit: the fill's image scale.
    const double device_per_source = scale / fill.image_scale;
    placement.tile_length = declared_length * device_per_source;
    // At the default position (0, 0) this is the box's start exactly.
    const double room = box_length - placement.tile_length;
    placement.tile_origin = box_start + room * anchor + offset * scale;
    placement.slice_start = slice_start;
    placement.slice_end = slice_end;
    double near = slice_start * device_per_source;
    double far = slice_end * device_per_source;
    const double fixed = near + far;
    if (fixed > box_length && fixed > 0.0) {
        const double factor = box_length / fixed;
        near = near * factor;
        far = far * factor;
    }
    placement.destination_start = near;
    placement.destination_end = far;
    return placement;
}

} // namespace

bool material_image_available(const ImageRegistry& registry, const MaterialFillLayer& fill) noexcept {
    const std::uint32_t width = pixel_count(fill.image_pixel_size.width);
    const std::uint32_t height = pixel_count(fill.image_pixel_size.height);
    const bool available = matching_view(registry, fill.image, width, height).has_value();
    return available;
}

bool read_material_image(const ImageRegistry& registry, const ImageId image, const std::uint32_t width,
                         const std::uint32_t height, MaterialImagePixels& pixels) {
    pixels.bytes.clear();
    pixels.width = 0U;
    pixels.height = 0U;
    const std::optional<ImageResourceView> found = matching_view(registry, image, width, height);
    if (!found) return false;
    const ImageResourceView& view = *found;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    // The registry bounds an image's sides (4096 by default) and pixels, so
    // the product fits; checked anyway, as the decoder's storage is sized
    // from it.
    if (row_bytes / 4U != width || row_bytes > std::numeric_limits<std::size_t>::max() / height) return false;
    std::vector<std::uint8_t> bytes(row_bytes * height, 0U);
    if (view.encoding != ImageResourceEncoding::bgra32_premultiplied) {
        image::ResourcePixelDecoder decoder{};
        const image::PixelTarget target{image::PixelOrder::bgra, image::PixelAlpha::premultiplied};
        const std::span<std::byte> destination = std::as_writable_bytes(std::span<std::uint8_t>(bytes));
        const image::ResourcePixelResult decoded = decoder.decode(view, target, destination, row_bytes);
        if (decoded.error != image::ResourcePixelError::none || decoded.width != width || decoded.height != height) {
            return false;
        }
    } else {
        // Premultiplied BGRA rows of view.row_bytes each, copied into tightly
        // packed rows. The stride and both extents are checked before any
        // byte is copied, so a short or narrow resource fails.
        if (view.row_bytes > std::numeric_limits<std::size_t>::max()) return false;
        const std::size_t source_row = static_cast<std::size_t>(view.row_bytes);
        if (source_row < row_bytes || view.encoded.size() / source_row < height - 1U ||
            view.encoded.size() - source_row * (height - 1U) < row_bytes) {
            return false;
        }
        for (std::uint32_t row = 0U; row < height; ++row) {
            const std::byte* const from = view.encoded.data() + source_row * row;
            std::uint8_t* const to = bytes.data() + row_bytes * row;
            std::memcpy(to, from, row_bytes);
        }
    }
    pixels.bytes = std::move(bytes);
    pixels.width = width;
    pixels.height = height;
    return true;
}

ImageAxisPlacement image_placement_across(const MaterialFillLayer& fill, double box_start, double box_length,
                                          double scale, double source_width) noexcept {
    const ImageAxisPlacement placement =
        placement_along(fill, box_start, box_length, scale, source_width, fill.image_slice.left,
                        fill.image_slice.right, fill.image_pixel_size.width, fill.image_anchor.x, fill.image_offset.x);
    return placement;
}

ImageAxisPlacement image_placement_down(const MaterialFillLayer& fill, double box_start, double box_length,
                                        double scale, double source_height) noexcept {
    const ImageAxisPlacement placement =
        placement_along(fill, box_start, box_length, scale, source_height, fill.image_slice.top,
                        fill.image_slice.bottom, fill.image_pixel_size.height, fill.image_anchor.y,
                        fill.image_offset.y);
    return placement;
}

ImageVariantRequest image_variant_request(const MaterialFillLayer& fill, const std::size_t layer, const Rect box,
                                          const double scale) noexcept {
    ImageVariantRequest request{};
    request.layer = layer;
    request.image = fill.image;
    request.source_width = pixel_count(fill.image_pixel_size.width);
    request.source_height = pixel_count(fill.image_pixel_size.height);
    request.scale = DeviceScale{scale};
    const ImageAxisPlacement across = image_placement_across(fill, box.x, box.width, scale, fill.image_pixel_size.width);
    const ImageAxisPlacement down = image_placement_down(fill, box.y, box.height, scale, fill.image_pixel_size.height);
    if (fill.image_mode == MaterialImageMode::tile || fill.image_mode == MaterialImageMode::single ||
        fill.image_mode == MaterialImageMode::mirror || fill.image_mode == MaterialImageMode::extend) {
        request.extent = DeviceExtent{variant_length(across.tile_length), variant_length(down.tile_length)};
        return request;
    }
    request.extent = DeviceExtent{variant_length(across.length), variant_length(down.length)};
    if (fill.image_mode != MaterialImageMode::nine_patch) return request;
    request.source_slice = fill.image_slice;
    const std::uint32_t left = fixed_length(across.destination_start, request.extent.width);
    const std::uint32_t top = fixed_length(down.destination_start, request.extent.height);
    const std::uint32_t right = fixed_length(across.destination_end, request.extent.width - left);
    const std::uint32_t bottom = fixed_length(down.destination_end, request.extent.height - top);
    request.fixed = std::array<std::uint32_t, 4>{left, top, right, bottom};
    return request;
}

void map_image_variant(const ImageAxisPlacement& placement, const std::uint32_t variant_length,
                       const std::uint32_t fixed_start, const std::uint32_t fixed_end, const std::uint32_t outputs,
                       std::vector<std::uint32_t>& map) {
    map.assign(outputs, 0U);
    const double pixels = static_cast<double>(variant_length);
    if (placement.mode == MaterialImageMode::tile) {
        // Each centre's place within its tile, counted from where one tile
        // starts.
        const double tile = placement.tile_length;
        if (!(tile > 0.0)) return;
        for (std::uint32_t output = 0U; output < outputs; ++output) {
            const double along = static_cast<double>(output) + 0.5 - placement.tile_origin;
            const double within = along - tile * std::floor(along / tile);
            const double index = std::clamp(std::floor(within * pixels / tile), 0.0, pixels - 1.0);
            map[output] = static_cast<std::uint32_t>(index);
        }
        return;
    }
    if (placement.mode == MaterialImageMode::mirror) {
        // Each centre's pixel within a pair of tiles, the second reflected:
        // with m in [0, 2n), m itself in the first, 2n - 1 - m in the
        // second (ImagePatternWrap::mirror).
        const double tile = placement.tile_length;
        if (!(tile > 0.0)) return;
        const double pair = tile * 2.0;
        const double last = pixels * 2.0 - 1.0;
        for (std::uint32_t output = 0U; output < outputs; ++output) {
            const double along = static_cast<double>(output) + 0.5 - placement.tile_origin;
            const double within = along - pair * std::floor(along / pair);
            double index = std::clamp(std::floor(within * pixels / tile), 0.0, last);
            if (index >= pixels) index = last - index;
            map[output] = static_cast<std::uint32_t>(index);
        }
        return;
    }
    if (placement.mode == MaterialImageMode::extend) {
        // The one tile, its first pixel before it and its last after it
        // (ImagePatternWrap::clamp).
        const double tile = placement.tile_length;
        if (!(tile > 0.0)) return;
        for (std::uint32_t output = 0U; output < outputs; ++output) {
            const double along = static_cast<double>(output) + 0.5 - placement.tile_origin;
            const double index = std::clamp(std::floor(along * pixels / tile), 0.0, pixels - 1.0);
            map[output] = static_cast<std::uint32_t>(index);
        }
        return;
    }
    if (placement.mode == MaterialImageMode::single) {
        // Only the centres within the one tile show it.
        const double tile = placement.tile_length;
        for (std::uint32_t output = 0U; output < outputs; ++output) {
            map[output] = outside_image;
            const double along = static_cast<double>(output) + 0.5 - placement.tile_origin;
            if (!(tile > 0.0) || along < 0.0 || along >= tile) continue;
            const double index = std::clamp(std::floor(along * pixels / tile), 0.0, pixels - 1.0);
            map[output] = static_cast<std::uint32_t>(index);
        }
        return;
    }
    // The segments, in order along the axis; a stretch has one.
    std::array<AxisSegment, 3> segments{};
    std::size_t segment_count = 1U;
    const double end = placement.start + placement.length;
    segments[0] = AxisSegment{placement.start, end, 0U, variant_length};
    if (placement.mode == MaterialImageMode::nine_patch) {
        const double near_end = placement.start + placement.destination_start;
        const double far_start = end - placement.destination_end;
        const std::uint32_t middle_end = variant_length - fixed_end;
        segments[0] = AxisSegment{placement.start, near_end, 0U, fixed_start};
        segments[1] = AxisSegment{near_end, far_start, fixed_start, middle_end};
        segments[2] = AxisSegment{far_start, end, middle_end, variant_length};
        segment_count = 3U;
    }
    for (std::uint32_t output = 0U; output < outputs; ++output) {
        const double centre = static_cast<double>(output) + 0.5;
        // The last segment that starts at or before the centre; centres
        // before the first belong to the first. An empty segment is passed
        // over, since the next one starts where it does.
        std::size_t chosen = 0U;
        for (std::size_t index = 1U; index < segment_count; ++index) {
            if (centre >= segments[index].destination_start) chosen = index;
        }
        map[output] = segment_pixel(segments[chosen], centre, variant_length);
    }
}

} // namespace gui_forms::engine_primitives
