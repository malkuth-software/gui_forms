#pragma once
// The single-surface window manager's compositing (owner decision,
// 2026-11-15): the scene is composed on the CPU into the host's one surface,
// a premultiplied BGRA8 image (bytes B, G, R, A in memory, Skia's
// kBGRA_8888 premultiplied, as every window raster is). The manager turns
// its damage into device rectangles (surface_device_rects), and for each
// rectangle composes the layers back to front (surface_compose). The drawn
// cursor of a host without one is a sprite layer (surface_cursor_sprite).
// Plain C++, tested on every platform (tests/surface_desk_tests.cpp).

#include "gui_forms/types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms::host::surface_detail {

// A rectangle in device pixels, edges exclusive at right and bottom.
struct SurfaceDeviceRect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] bool empty() const noexcept { return right <= left || bottom <= top; }
    friend bool operator==(const SurfaceDeviceRect& a, const SurfaceDeviceRect& b) noexcept {
        const bool equal = a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
        return equal;
    }
};

// The device rectangles of logical damage `rects` at `scale` on a surface
// of `width` by `height` pixels: each scaled, aligned outward to whole
// pixels, clipped to the surface and dropped when empty or not finite. When
// more than out.size() remain, `out` holds their one bounding rectangle
// instead. Fills `out` and returns the live count (0 when `out` is empty).
std::size_t surface_device_rects(std::span<const Rect> rects, double scale, std::uint32_t width,
                                 std::uint32_t height, std::span<SurfaceDeviceRect> out) noexcept;

// A premultiplied BGRA8 colour, in memory order.
struct SurfacePixel final {
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t alpha{255U};
};

// An opaque colour from 0xRRGGBB.
[[nodiscard]] SurfacePixel surface_opaque_pixel(std::uint32_t rgb) noexcept;

// The surface a compose writes: `pixels` (borrowed from the host) holds
// `height` rows of `width` BGRA8 pixels, `row_bytes` apart (at least
// width * 4).
struct SurfaceTarget final {
    std::uint8_t* pixels{};
    std::size_t row_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
};

enum class SurfaceLayerKind : std::uint8_t {
    // `color` over every pixel of `bounds` (replacing what is beneath).
    solid,
    // The image's pixels copied as they are (an opaque window).
    opaque_image,
    // The image drawn source-over, premultiplied (a popup, a translucent
    // window, the cursor).
    blended_image,
};

// One layer of the scene. An image layer's top-left pixel lands at
// bounds.left, bounds.top; the image is clipped to `bounds`, and `bounds` to
// the image's size. `pixels` is borrowed for the compose call.
struct SurfaceComposeLayer final {
    SurfaceLayerKind kind{SurfaceLayerKind::solid};
    SurfaceDeviceRect bounds{};
    SurfacePixel color{};
    const std::uint8_t* pixels{};
    std::size_t row_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// Composes `clip` of `target`: `background` first, then `layers` back to
// front, each within `clip` and the target. Writes only inside `clip`; the
// layers' images must not overlap the target's pixels.
void surface_compose(const SurfaceTarget& target, SurfaceDeviceRect clip, SurfacePixel background,
                     std::span<const SurfaceComposeLayer> layers) noexcept;

// The drawn cursor (an owner choice of 2026-11-15, open for theming: these
// procedural sprites stand until the theme engine draws cursor parts). A
// sprite is a premultiplied BGRA8 image, black outlined in white.
struct SurfaceCursorSprite final {
    // The sprite's pixels, `width` * 4 bytes a row.
    std::vector<std::uint8_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    // The pixel that points, from the top-left.
    std::int32_t hotspot_x{};
    std::int32_t hotspot_y{};
};

// Fills `sprite` (its storage reused) with `kind`'s sprite at `factor`
// device pixels per sprite pixel (at least 1): arrow, text, crosshair and the
// four resize shapes have their own; hand, wait and forbidden show the
// arrow until themed parts exist.
void surface_cursor_sprite(CursorKind kind, std::uint32_t factor, SurfaceCursorSprite& sprite);

} // namespace gui_forms::host::surface_detail
