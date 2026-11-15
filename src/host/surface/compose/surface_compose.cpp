// The single-surface window manager's compositing (surface_compose.hpp).
#include "surface_compose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace gui_forms::host::surface_detail {
namespace {

// The part of `rect` inside `clip` (empty when they do not meet).
SurfaceDeviceRect intersect(SurfaceDeviceRect rect, SurfaceDeviceRect clip) noexcept {
    const SurfaceDeviceRect result{std::max(rect.left, clip.left), std::max(rect.top, clip.top),
                                   std::min(rect.right, clip.right), std::min(rect.bottom, clip.bottom)};
    return result;
}

void fill_rows(const SurfaceTarget& target, SurfaceDeviceRect area, SurfacePixel color) noexcept {
    const std::size_t columns = static_cast<std::size_t>(area.right - area.left);
    for (std::int32_t row = area.top; row < area.bottom; ++row) {
        std::uint8_t* destination = target.pixels + static_cast<std::size_t>(row) * target.row_bytes +
                                    static_cast<std::size_t>(area.left) * 4U;
        for (std::size_t column = 0U; column < columns; ++column) {
            destination[column * 4U] = color.blue;
            destination[column * 4U + 1U] = color.green;
            destination[column * 4U + 2U] = color.red;
            destination[column * 4U + 3U] = color.alpha;
        }
    }
}

// Source-over of one premultiplied colour: d = c + d * (255 - ca) / 255,
// rounded to nearest.
void tint_rows(const SurfaceTarget& target, SurfaceDeviceRect area, SurfacePixel color) noexcept {
    const std::size_t columns = static_cast<std::size_t>(area.right - area.left);
    const std::uint32_t inverse = 255U - static_cast<std::uint32_t>(color.alpha);
    const std::array<std::uint32_t, 4> source{color.blue, color.green, color.red, color.alpha};
    for (std::int32_t row = area.top; row < area.bottom; ++row) {
        std::uint8_t* destination = target.pixels + static_cast<std::size_t>(row) * target.row_bytes +
                                    static_cast<std::size_t>(area.left) * 4U;
        for (std::size_t column = 0U; column < columns; ++column) {
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const std::size_t at = column * 4U + channel;
                const std::uint32_t kept = (static_cast<std::uint32_t>(destination[at]) * inverse + 127U) / 255U;
                const std::uint32_t sum = std::min(255U, source[channel] + kept);
                destination[at] = static_cast<std::uint8_t>(sum);
            }
        }
    }
}

void copy_rows(const SurfaceTarget& target, SurfaceDeviceRect area, const SurfaceComposeLayer& layer) noexcept {
    const std::size_t bytes = static_cast<std::size_t>(area.right - area.left) * 4U;
    for (std::int32_t row = area.top; row < area.bottom; ++row) {
        std::uint8_t* destination = target.pixels + static_cast<std::size_t>(row) * target.row_bytes +
                                    static_cast<std::size_t>(area.left) * 4U;
        const std::uint8_t* source = layer.pixels +
                                     static_cast<std::size_t>(row - layer.bounds.top) * layer.row_bytes +
                                     static_cast<std::size_t>(area.left - layer.bounds.left) * 4U;
        std::memcpy(destination, source, bytes);
    }
}

// Source-over with premultiplied alpha: d = s + d * (255 - sa) / 255,
// rounded to nearest.
void blend_rows(const SurfaceTarget& target, SurfaceDeviceRect area, const SurfaceComposeLayer& layer) noexcept {
    const std::size_t columns = static_cast<std::size_t>(area.right - area.left);
    for (std::int32_t row = area.top; row < area.bottom; ++row) {
        std::uint8_t* destination = target.pixels + static_cast<std::size_t>(row) * target.row_bytes +
                                    static_cast<std::size_t>(area.left) * 4U;
        const std::uint8_t* source = layer.pixels +
                                     static_cast<std::size_t>(row - layer.bounds.top) * layer.row_bytes +
                                     static_cast<std::size_t>(area.left - layer.bounds.left) * 4U;
        for (std::size_t column = 0U; column < columns; ++column) {
            const std::uint32_t inverse = 255U - static_cast<std::uint32_t>(source[column * 4U + 3U]);
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const std::size_t at = column * 4U + channel;
                const std::uint32_t kept = (static_cast<std::uint32_t>(destination[at]) * inverse + 127U) / 255U;
                const std::uint32_t sum = static_cast<std::uint32_t>(source[at]) + kept;
                destination[at] = static_cast<std::uint8_t>(std::min(sum, 255U));
            }
        }
    }
}

// The cursor masks: 'X' is the black shape; the white outline is every
// pixel next to it (surface_cursor_sprite adds a pixel of margin for it).
constexpr std::array<std::string_view, 17> arrow_mask{{
    "X",          "XX",         "XXX",        "XXXX",      "XXXXX",     "XXXXXX",
    "XXXXXXX",    "XXXXXXXX",   "XXXXXXXXX",  "XXXXXXXXXX", "XXXXXX",    "XXX XXX",
    "XX  XXX",    "X    XXX",   "     XXX",   "      XXX", "      XX",
}};
constexpr std::array<std::string_view, 13> text_mask{{
    "XX XX", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ",
    "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "XX XX",
}};
constexpr std::array<std::string_view, 15> crosshair_mask{{
    "       X       ", "       X       ", "       X       ", "       X       ", "       X       ",
    "       X       ", "               ", "XXXXXX   XXXXXX", "               ", "       X       ",
    "       X       ", "       X       ", "       X       ", "       X       ", "       X       ",
}};
constexpr std::array<std::string_view, 5> resize_mask{{
    "  X         X  ",
    " XX         XX ",
    "XXXXXXXXXXXXXXX",
    " XX         XX ",
    "  X         X  ",
}};
constexpr std::array<std::string_view, 11> diagonal_mask{{
    "XXXXX      ", "XXXX       ", "XXX        ", "XX X       ", "X   X      ", "     X     ",
    "      X   X", "       X XX", "        XXX", "       XXXX", "      XXXXX",
}};

struct CursorMask final {
    std::span<const std::string_view> rows{};
    std::int32_t hotspot_x{};
    std::int32_t hotspot_y{};
    // The mask read with rows and columns exchanged (vertical from
    // horizontal).
    bool transposed{};
    // The mask read right to left (up-diagonal from down-diagonal).
    bool mirrored{};
};

CursorMask cursor_mask(CursorKind kind) noexcept {
    switch (kind) {
    case CursorKind::text:
        return CursorMask{text_mask, 2, 6, false, false};
    case CursorKind::crosshair:
        return CursorMask{crosshair_mask, 7, 7, false, false};
    case CursorKind::resize_horizontal:
        return CursorMask{resize_mask, 7, 2, false, false};
    case CursorKind::resize_vertical:
        return CursorMask{resize_mask, 2, 7, true, false};
    case CursorKind::resize_diagonal_down:
        return CursorMask{diagonal_mask, 5, 5, false, false};
    case CursorKind::resize_diagonal_up:
        return CursorMask{diagonal_mask, 5, 5, false, true};
    case CursorKind::arrow:
    default:
        return CursorMask{arrow_mask, 0, 0, false, false};
    }
}

} // namespace

std::size_t surface_device_rects(std::span<const Rect> rects, double scale, std::uint32_t width,
                                 std::uint32_t height, std::span<SurfaceDeviceRect> out) noexcept {
    if (out.empty() || !(scale > 0.0) || !std::isfinite(scale) || width == 0U || height == 0U) return 0U;
    const double right_limit = static_cast<double>(width);
    const double bottom_limit = static_cast<double>(height);
    std::size_t count = 0U;
    bool overflowed = false;
    SurfaceDeviceRect bounds{};
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        const Rect logical = rects[index];
        if (!logical.finite() || logical.empty()) continue;
        // Outward to whole device pixels, then clipped to the surface.
        const double left = std::max(0.0, std::floor(logical.x * scale));
        const double top = std::max(0.0, std::floor(logical.y * scale));
        const double right = std::min(right_limit, std::ceil(logical.right() * scale));
        const double bottom = std::min(bottom_limit, std::ceil(logical.bottom() * scale));
        if (!(right > left) || !(bottom > top)) continue;
        const SurfaceDeviceRect device{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                                       static_cast<std::int32_t>(right), static_cast<std::int32_t>(bottom)};
        if (count == 0U && !overflowed) {
            bounds = device;
        } else {
            bounds.left = std::min(bounds.left, device.left);
            bounds.top = std::min(bounds.top, device.top);
            bounds.right = std::max(bounds.right, device.right);
            bounds.bottom = std::max(bounds.bottom, device.bottom);
        }
        if (count < out.size()) {
            out[count] = device;
            ++count;
        } else {
            overflowed = true;
        }
    }
    if (!overflowed) return count;
    out[0] = bounds;
    return 1U;
}

SurfacePixel surface_opaque_pixel(std::uint32_t rgb) noexcept {
    const SurfacePixel pixel{static_cast<std::uint8_t>(rgb & 0xFFU), static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU),
                             static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU), 255U};
    return pixel;
}

SurfacePixel surface_tint_pixel(std::uint32_t rgb, std::uint32_t alpha) noexcept {
    const std::uint32_t a = std::min(alpha, 255U);
    const std::uint32_t red = (((rgb >> 16U) & 0xFFU) * a + 127U) / 255U;
    const std::uint32_t green = (((rgb >> 8U) & 0xFFU) * a + 127U) / 255U;
    const std::uint32_t blue = ((rgb & 0xFFU) * a + 127U) / 255U;
    const SurfacePixel pixel{static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(green),
                             static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(a)};
    return pixel;
}

void surface_compose(const SurfaceTarget& target, SurfaceDeviceRect clip, SurfacePixel background,
                     std::span<const SurfaceComposeLayer> layers) noexcept {
    if (target.pixels == nullptr || target.row_bytes < static_cast<std::size_t>(target.width) * 4U) return;
    const SurfaceDeviceRect whole{0, 0, static_cast<std::int32_t>(target.width),
                                  static_cast<std::int32_t>(target.height)};
    const SurfaceDeviceRect area = intersect(clip, whole);
    if (area.empty()) return;
    fill_rows(target, area, background);
    for (std::size_t index = 0U; index < layers.size(); ++index) {
        const SurfaceComposeLayer& layer = layers[index];
        SurfaceDeviceRect bounds = layer.bounds;
        const bool image_layer =
            layer.kind == SurfaceLayerKind::opaque_image || layer.kind == SurfaceLayerKind::blended_image;
        if (image_layer) {
            // An image reaches no further than its own pixels. A solid or a
            // tint layer has none and covers its bounds.
            if (layer.pixels == nullptr || layer.row_bytes < static_cast<std::size_t>(layer.width) * 4U) continue;
            const SurfaceDeviceRect image{layer.bounds.left, layer.bounds.top,
                                          layer.bounds.left + static_cast<std::int32_t>(layer.width),
                                          layer.bounds.top + static_cast<std::int32_t>(layer.height)};
            bounds = intersect(bounds, image);
        }
        const SurfaceDeviceRect drawn = intersect(bounds, area);
        if (drawn.empty()) continue;
        switch (layer.kind) {
        case SurfaceLayerKind::opaque_image:
            copy_rows(target, drawn, layer);
            break;
        case SurfaceLayerKind::blended_image:
            blend_rows(target, drawn, layer);
            break;
        case SurfaceLayerKind::tint:
            tint_rows(target, drawn, layer.color);
            break;
        case SurfaceLayerKind::solid:
        default:
            fill_rows(target, drawn, layer.color);
            break;
        }
    }
}

void surface_cursor_sprite(CursorKind kind, std::uint32_t factor, SurfaceCursorSprite& sprite) {
    const std::uint32_t scale = std::clamp(factor, 1U, 8U);
    const CursorMask mask = cursor_mask(kind);
    std::size_t mask_width = 0U;
    for (std::size_t row = 0U; row < mask.rows.size(); ++row) mask_width = std::max(mask_width, mask.rows[row].size());
    const std::size_t mask_height = mask.rows.size();
    // The shape as read (transposed or mirrored), with one pixel of margin
    // on each side for the outline: 0 clear, 1 black, 2 white.
    const std::size_t cells_width = (mask.transposed ? mask_height : mask_width) + 2U;
    const std::size_t cells_height = (mask.transposed ? mask_width : mask_height) + 2U;
    std::vector<std::uint8_t> cells(cells_width * cells_height, 0U);
    for (std::size_t y = 0U; y + 2U < cells_height; ++y) {
        for (std::size_t x = 0U; x + 2U < cells_width; ++x) {
            const std::size_t read_x = mask.transposed ? y : (mask.mirrored ? mask_width - 1U - x : x);
            const std::size_t read_y = mask.transposed ? x : y;
            const std::string_view line = mask.rows[read_y];
            const bool black = read_x < line.size() && line[read_x] == 'X';
            if (black) cells[(y + 1U) * cells_width + x + 1U] = 1U;
        }
    }
    for (std::size_t y = 0U; y < cells_height; ++y) {
        for (std::size_t x = 0U; x < cells_width; ++x) {
            if (cells[y * cells_width + x] != 0U) continue;
            bool beside = false;
            for (std::size_t near_y = (y == 0U ? 0U : y - 1U); near_y <= y + 1U && near_y < cells_height; ++near_y) {
                for (std::size_t near_x = (x == 0U ? 0U : x - 1U); near_x <= x + 1U && near_x < cells_width;
                     ++near_x) {
                    if (cells[near_y * cells_width + near_x] == 1U) beside = true;
                }
            }
            if (beside) cells[y * cells_width + x] = 2U;
        }
    }
    sprite.width = static_cast<std::uint32_t>(cells_width) * scale;
    sprite.height = static_cast<std::uint32_t>(cells_height) * scale;
    sprite.pixels.assign(static_cast<std::size_t>(sprite.width) * sprite.height * 4U, 0U);
    for (std::uint32_t y = 0U; y < sprite.height; ++y) {
        for (std::uint32_t x = 0U; x < sprite.width; ++x) {
            const std::uint8_t cell = cells[(y / scale) * cells_width + x / scale];
            if (cell == 0U) continue;
            const std::uint8_t value = cell == 1U ? 0U : 255U;
            const std::size_t at = (static_cast<std::size_t>(y) * sprite.width + x) * 4U;
            sprite.pixels[at] = value;
            sprite.pixels[at + 1U] = value;
            sprite.pixels[at + 2U] = value;
            sprite.pixels[at + 3U] = 255U;
        }
    }
    const std::int32_t hotspot_x = mask.transposed ? mask.hotspot_y : mask.hotspot_x;
    const std::int32_t hotspot_y = mask.transposed ? mask.hotspot_x : mask.hotspot_y;
    sprite.hotspot_x = (hotspot_x + 1) * static_cast<std::int32_t>(scale);
    sprite.hotspot_y = (hotspot_y + 1) * static_cast<std::int32_t>(scale);
}

} // namespace gui_forms::host::surface_detail
