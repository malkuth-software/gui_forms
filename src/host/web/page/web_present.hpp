#pragma once
// The present record the program's thread hands to the page's main thread
// (owner decisions, 2026-11-15: WebGL2 present only; one surface composed
// by GUI.Forms' window manager). It lives in the shared wasm memory;
// web_host.js reads it by the offsets below, which the static_asserts in
// web_present.cpp fix.
//
// The handshake, one present at a time: the window manager composes the
// damage into the surface (WebSurfaceHost's buffer), the host fills the
// record (the surface's address, size and damaged device rectangles),
// stores 1 in `pending` and asks for an animation frame. The main thread,
// in requestAnimationFrame, uploads each damaged rectangle into its texture
// (BGRA bytes uploaded as RGBA and swizzled in the shader), draws it, stores
// 0 in `pending` and posts the presented record. Until then the manager
// writes no pixel of the surface (SurfacePresentResult::pending), so the
// main thread reads stable pixels without a copy. The windows keep painting
// into their own rasters meanwhile.
//
// Plain C++ with no Emscripten; the layout is checked on every platform.

#include "../../surface/compose/surface_compose.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::host::web_detail {

struct WebPresentFrame final {
    // Rectangles one present uploads at most; the manager unites more into
    // their bounds (surface_device_rects).
    static constexpr std::size_t maximum_rects = 32U;

    // 1 from the program's handing over until the main thread presented.
    std::atomic<std::uint32_t> pending{0U};
    // The surface (the canvas's backing store) in device pixels.
    std::uint32_t width{};
    std::uint32_t height{};
    // Its pixels: premultiplied BGRA8, `row_bytes` apart (a wasm32 address).
    std::uint32_t pixels{};
    std::uint32_t row_bytes{};
    // The live length of `rects`.
    std::uint32_t rect_count{};
    std::uint32_t reserved0{};
    std::uint32_t reserved1{};
    // Each damaged rectangle as left, top, right, bottom in device pixels.
    std::array<std::int32_t, maximum_rects * 4U> rects{};
};

// Fills `frame`'s rectangles from `damage` (at most maximum_rects, the rest
// dropped: the manager never hands more). Returns the count written.
std::size_t fill_web_present_rects(WebPresentFrame& frame, std::span<const surface_detail::SurfaceDeviceRect> damage) noexcept;

} // namespace gui_forms::host::web_detail
