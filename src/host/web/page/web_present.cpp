// The present record's layout (web_present.hpp).
#include "web_present.hpp"

#include <algorithm>
#include <type_traits>

namespace gui_forms::host::web_detail {

// web_host.js reads these by offset; a change here changes it there.
static_assert(std::is_standard_layout_v<WebPresentFrame>);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(sizeof(std::atomic<std::uint32_t>) == 4U);
static_assert(offsetof(WebPresentFrame, pending) == 0U);
static_assert(offsetof(WebPresentFrame, width) == 4U);
static_assert(offsetof(WebPresentFrame, height) == 8U);
static_assert(offsetof(WebPresentFrame, pixels) == 12U);
static_assert(offsetof(WebPresentFrame, row_bytes) == 16U);
static_assert(offsetof(WebPresentFrame, rect_count) == 20U);
static_assert(offsetof(WebPresentFrame, rects) == 32U);
static_assert(sizeof(WebPresentFrame) == 32U + 16U * WebPresentFrame::maximum_rects);

std::size_t fill_web_present_rects(WebPresentFrame& frame,
                                   std::span<const surface_detail::SurfaceDeviceRect> damage) noexcept {
    const std::size_t count = std::min(damage.size(), WebPresentFrame::maximum_rects);
    for (std::size_t index = 0U; index < count; ++index) {
        const surface_detail::SurfaceDeviceRect& rect = damage[index];
        frame.rects[index * 4U] = rect.left;
        frame.rects[index * 4U + 1U] = rect.top;
        frame.rects[index * 4U + 2U] = rect.right;
        frame.rects[index * 4U + 3U] = rect.bottom;
    }
    frame.rect_count = static_cast<std::uint32_t>(count);
    return count;
}

} // namespace gui_forms::host::web_detail
