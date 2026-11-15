#pragma once
// The frame record the program's thread hands to the page's main thread
// for presentation (owner decision, 2026-11-15: WebGL2 present only). It
// lives in the shared wasm memory; web_host.js reads it by the offsets
// below, which the static_asserts in web_present.cpp fix.
//
// The handshake, one frame at a time: the program's thread paints every
// surface with damage, fills the record (each surface's raster address,
// size and damaged device rectangles, then the layers to draw back to
// front), stores 1 in `pending` and asks for an animation frame. The main
// thread, in requestAnimationFrame, uploads each damaged rectangle straight
// from the raster into the surface's texture (BGRA bytes uploaded as RGBA
// and swizzled in the shader), draws the layers, stores 0 in `pending` and
// posts PageRecordKind::presented. While `pending` is 1 the program's thread
// does not paint, resize or release any raster, so the main thread reads
// stable pixels without a copy; its damage waits for the next frame.
//
// Plain C++ with no Emscripten; the rectangle arithmetic is tested on every
// platform (tests/web_desk_tests.cpp).

#include "gui_forms/types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::host::web_detail {

// A raster surface the page keeps a texture for. `id` is the host's own
// number for it (never reused in a run); the page creates the texture on
// the first frame naming it and deletes it when `released` names it.
struct WebPresentSurface final {
    std::uint32_t id{};
    // The raster's pixels: premultiplied BGRA8, `row_bytes` apart (a wasm32
    // address).
    std::uint32_t pixels{};
    std::uint32_t row_bytes{};
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    // Its damaged rectangles this frame: rect_count entries of `rects` from
    // first_rect, each left, top, right, bottom in device pixels within the
    // raster. A resized raster is uploaded whole.
    std::uint32_t first_rect{};
    std::uint32_t rect_count{};
    // 1 when the raster's alpha is presented (a non-opaque window or a
    // popup); 0 draws it opaque.
    std::uint32_t alpha{};
};

enum class WebPresentLayerKind : std::uint32_t {
    // A surface's texture at the destination rectangle.
    surface = 0,
    // A solid rectangle (a frame's border) in `color` (0xRRGGBBAA).
    solid = 1,
};

// One quad of the frame, back to front, in canvas device pixels.
struct WebPresentLayer final {
    WebPresentLayerKind kind{WebPresentLayerKind::surface};
    std::uint32_t surface{};
    std::uint32_t color{};
    std::uint32_t reserved{};
    float x{};
    float y{};
    float width{};
    float height{};
};

struct WebPresentFrame final {
    static constexpr std::size_t maximum_surfaces = 160U;
    static constexpr std::size_t maximum_layers = 320U;
    static constexpr std::size_t maximum_rects = 1024U;
    static constexpr std::size_t maximum_released = 64U;
    // Rectangles one surface uploads at most; more are united into their
    // bounds (web_device_rects).
    static constexpr std::size_t maximum_rects_per_surface = 32U;

    // 1 from the program's handing over until the main thread presented.
    std::atomic<std::uint32_t> pending{0U};
    // The canvas's backing size in device pixels.
    std::uint32_t canvas_width{};
    std::uint32_t canvas_height{};
    // The live lengths of the arrays below.
    std::uint32_t surface_count{};
    std::uint32_t layer_count{};
    std::uint32_t rect_count{};
    std::uint32_t released_count{};
    // The canvas's clear colour behind every layer (0xRRGGBBAA).
    std::uint32_t background{0x1E1E1EFFU};
    std::array<WebPresentSurface, maximum_surfaces> surfaces{};
    std::array<WebPresentLayer, maximum_layers> layers{};
    std::array<std::int32_t, maximum_rects * 4U> rects{};
    std::array<std::uint32_t, maximum_released> released{};
};

// A rectangle in device pixels, edges exclusive at right and bottom.
struct WebDeviceRect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    friend bool operator==(const WebDeviceRect& a, const WebDeviceRect& b) noexcept {
        const bool equal = a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
        return equal;
    }
};

// The device rectangles of logical damage `rects` at `scale` on a raster of
// `width` by `height` pixels: each scaled, aligned outward to whole pixels,
// clipped to the raster and dropped when empty or not finite. When more
// than `capacity` remain, `out` holds their one bounding rectangle instead.
// Fills `out` (its storage reused) and returns the live count; `out` must
// hold at least `capacity` entries, and capacity at least 1.
std::size_t web_device_rects(std::span<const Rect> rects, double scale, std::uint32_t width,
                             std::uint32_t height, std::span<WebDeviceRect> out) noexcept;

} // namespace gui_forms::host::web_detail
