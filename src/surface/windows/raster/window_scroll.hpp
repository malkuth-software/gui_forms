#pragma once
// Host scroll on Windows (docs/user/presentation.md, "Host scroll"; owner
// 2026-11-16, the surfaces study's recommendation 8.b): the window's own
// pixels are moved with ScrollWindowEx, a user32 call over GDI with no GPU
// symbol, so that the copy path then copies only the strip the move
// exposed instead of the scroll's whole area.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../../../core/host/presentation/host_scroll.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace gui_forms::host::detail {

// The rectangles of ScrollWindowEx's uncovered region read back: a whole
// move reports the exposed bands, one or two rectangles. A region of more
// rectangles than this is judged partial without being read.
inline constexpr std::size_t window_scroll_region_rects = 16U;

// Moves plan.source to plan.destination inside `window`'s pixels with
// ScrollWindowEx, scrolling and clipping to plan.area (pixels moved out of
// the area are dropped, as the plan says). Nothing is invalidated
// (no SW_INVALIDATE): the caller copies what the move left from its frame.
//
// Returns true when every pixel moved: the region ScrollWindowEx reports as
// uncovered lies inside the plan's exposed bands
// (presentation::classify_host_scroll_move). Returns false when nothing
// could be moved (no window, a plan that moves nothing, no region, or
// ScrollWindowEx failing) and when only part moved: the region reaches past
// the bands (part of the area was clipped, covered or off screen), holds
// more than window_scroll_region_rects rectangles, cannot be read, or is
// empty (a window that showed nothing). On false the window's pixels in
// plan.area are unspecified, and the caller copies the whole area from its
// frame, which overwrites them.
//
// ScrollWindowEx also offsets the window's pending update region inside
// the area, as it moves the pixels that region marks stale. One execution
// context: the window's thread.
[[nodiscard]] inline bool scroll_window_pixels(HWND window, const presentation::PresentScrollPlan& plan) noexcept {
    if (window == nullptr || !plan.moves()) return false;
    const presentation::PresentOffset offset = presentation::present_scroll_offset(plan);
    const RECT area{plan.area.left, plan.area.top, plan.area.right, plan.area.bottom};
    HRGN uncovered = CreateRectRgn(0, 0, 0, 0);
    if (uncovered == nullptr) return false;
    const int kind = ScrollWindowEx(window, offset.dx, offset.dy, &area, &area, uncovered, nullptr, 0U);
    bool whole = false;
    if (kind == SIMPLEREGION || kind == COMPLEXREGION) {
        // RGNDATA is a header followed by its rectangles; the buffer holds
        // the header and window_scroll_region_rects of them.
        alignas(RGNDATAHEADER) std::array<std::byte, sizeof(RGNDATAHEADER) + window_scroll_region_rects * sizeof(RECT)>
            storage{};
        const DWORD needed = GetRegionData(uncovered, 0U, nullptr);
        const bool fits = needed != 0U && needed <= storage.size();
        RGNDATA* const data = reinterpret_cast<RGNDATA*>(storage.data());
        const DWORD read = fits ? GetRegionData(uncovered, needed, data) : 0U;
        // Read back by copy: the bytes were written through GDI's RGNDATA.
        RGNDATAHEADER header{};
        std::memcpy(&header, storage.data(), sizeof(header));
        const bool usable = read != 0U && header.iType == RDH_RECTANGLES &&
                            header.nCount <= window_scroll_region_rects &&
                            header.dwSize >= sizeof(RGNDATAHEADER) &&
                            header.dwSize + header.nCount * sizeof(RECT) <= storage.size();
        if (usable) {
            std::array<presentation::PresentRect, window_scroll_region_rects> reported{};
            for (DWORD index = 0U; index < header.nCount; ++index) {
                RECT native{};
                std::memcpy(&native, storage.data() + header.dwSize + index * sizeof(RECT), sizeof(native));
                reported[index] = presentation::PresentRect{native.left, native.top, native.right, native.bottom};
            }
            const std::span<const presentation::PresentRect> region(reported.data(), header.nCount);
            whole = presentation::classify_host_scroll_move(plan, region) == presentation::HostScrollMove::whole;
        }
    }
    DeleteObject(uncovered);
    return whole;
}

} // namespace gui_forms::host::detail
