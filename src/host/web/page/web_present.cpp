// The present record's layout and rectangle arithmetic (web_present.hpp).
#include "web_present.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace gui_forms::host::web_detail {

// web_host.js reads these by offset; a change here changes it there.
static_assert(std::is_standard_layout_v<WebPresentFrame>);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(sizeof(std::atomic<std::uint32_t>) == 4U);
static_assert(sizeof(WebPresentSurface) == 32U);
static_assert(sizeof(WebPresentLayer) == 32U);
static_assert(offsetof(WebPresentFrame, pending) == 0U);
static_assert(offsetof(WebPresentFrame, canvas_width) == 4U);
static_assert(offsetof(WebPresentFrame, canvas_height) == 8U);
static_assert(offsetof(WebPresentFrame, surface_count) == 12U);
static_assert(offsetof(WebPresentFrame, layer_count) == 16U);
static_assert(offsetof(WebPresentFrame, rect_count) == 20U);
static_assert(offsetof(WebPresentFrame, released_count) == 24U);
static_assert(offsetof(WebPresentFrame, background) == 28U);
static_assert(offsetof(WebPresentFrame, surfaces) == 32U);
static_assert(offsetof(WebPresentFrame, layers) == 32U + 32U * WebPresentFrame::maximum_surfaces);
static_assert(offsetof(WebPresentFrame, rects) ==
              32U + 32U * WebPresentFrame::maximum_surfaces + 32U * WebPresentFrame::maximum_layers);
static_assert(offsetof(WebPresentFrame, released) ==
              32U + 32U * WebPresentFrame::maximum_surfaces + 32U * WebPresentFrame::maximum_layers +
                  16U * WebPresentFrame::maximum_rects);

std::size_t web_device_rects(std::span<const Rect> rects, double scale, std::uint32_t width,
                             std::uint32_t height, std::span<WebDeviceRect> out) noexcept {
    if (out.empty() || !(scale > 0.0) || !std::isfinite(scale) || width == 0U || height == 0U) return 0U;
    const double right_limit = static_cast<double>(width);
    const double bottom_limit = static_cast<double>(height);
    std::size_t count = 0U;
    bool overflowed = false;
    WebDeviceRect bounds{};
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        const Rect logical = rects[index];
        if (!logical.finite() || logical.empty()) continue;
        // Outward to whole device pixels, then clipped to the raster.
        const double left = std::max(0.0, std::floor(logical.x * scale));
        const double top = std::max(0.0, std::floor(logical.y * scale));
        const double right = std::min(right_limit, std::ceil(logical.right() * scale));
        const double bottom = std::min(bottom_limit, std::ceil(logical.bottom() * scale));
        if (!(right > left) || !(bottom > top)) continue;
        const WebDeviceRect device{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
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

} // namespace gui_forms::host::web_detail
