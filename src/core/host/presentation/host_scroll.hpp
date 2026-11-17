#pragma once
// Host scroll's arithmetic shared by the platform copy paths
// (docs/user/presentation.md, "Host scroll"): an engine scroll copy in
// device pixels, the offset a scroll plan moves by, and whether a window
// system's report of what a move left unpainted shows that every pixel
// moved.
//
// Plain portable C++ over the presentation geometry and GUI.Forms' own
// window coordinates: no window system's types. Pure functions. Tested on
// every platform (tests/host_scroll_damage_tests.cpp).

#include "presentation_geometry.hpp"

#include "gui_forms/window/presentation/presentation_types.hpp"

#include <cstdint>
#include <span>

namespace gui_forms::host::presentation {

// One scroll copy (window coordinates) in device pixels at `scale`, rounded
// as the raster's scroll_copy rounds it: the clip's edges clamped at 0 and
// kept ordered, the offset as is. False, leaving `scroll` as it was, when
// the clip or the offset is not on whole device pixels (within 1e-6), not
// finite, or beyond maximum_present_side (request_scroll_copy admits only
// whole pixels; the raster refuses the rest).
[[nodiscard]] bool device_present_scroll(const ScrollCopy& copy, double scale, PresentScroll& scroll) noexcept;

// The offset a plan moves its pixels by: destination minus source. {0, 0}
// for a plan that moves nothing.
struct PresentOffset final {
    std::int32_t dx{};
    std::int32_t dy{};
};
[[nodiscard]] PresentOffset present_scroll_offset(const PresentScrollPlan& plan) noexcept;

// What a window system's move did, judged from the region it reports as
// left for the program to paint (Win32's ScrollWindowEx update region):
// - whole: every reported rectangle lies inside the plan's exposed bands,
//   so every pixel of plan.destination is the moved pixel of plan.source
//   and copying the bands completes the frame;
// - partial: some reported pixel lies outside the bands (part of the area
//   was clipped or covered and did not move); the caller copies the whole
//   area instead, which overwrites whatever moved.
// Empty reported rectangles are ignored. A plan that moves nothing is
// partial whatever is reported.
enum class HostScrollMove : std::uint8_t {
    whole,
    partial,
};
[[nodiscard]] HostScrollMove classify_host_scroll_move(const PresentScrollPlan& plan,
                                                       std::span<const PresentRect> uncovered) noexcept;

} // namespace gui_forms::host::presentation
