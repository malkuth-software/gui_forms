#pragma once

#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

// Premultiplied sRGB BGRA8, the one pixel format of every surface and raster
// (SK2): blue, green, red and alpha bytes in memory order.
enum class LiveSurfacePixelFormat : std::uint8_t {
    bgra32_premultiplied_srgb,
};

// Valid before attachment: bgra32_premultiplied_srgb on every host. Opaque
// pixels allow a straight copy for pixel-aligned, unscaled presentation at
// full opacity.
[[nodiscard]] LiveSurfacePixelFormat native_live_surface_pixel_format() noexcept;

inline constexpr std::size_t default_live_surface_buffer_count = 3U;
inline constexpr std::size_t minimum_live_surface_buffer_count = 2U;
inline constexpr std::size_t maximum_live_surface_buffer_count = 8U;

// Who produces a surface's frames, for diagnostics (the invalidation
// overlay names a frame's presentation by it): a program's producer or a
// sub-surface, or a compatibility paint endpoint presenting external
// double-buffered content (child-window or GDI drawing).
enum class LiveSurfaceOrigin : std::uint8_t { producer, compatibility_paint };

struct LiveSurfaceDescription final {
    std::uint32_t width{};
    std::uint32_t height{};
    LiveSurfacePixelFormat pixel_format{
        LiveSurfacePixelFormat::bgra32_premultiplied_srgb};
    // A smaller pool favors latency and memory; a larger pool tolerates more
    // compositor-held read leases before a producer must drop a frame.
    std::size_t buffer_count{default_live_surface_buffer_count};
    // The producer promises every pixel has alpha 255; the presenter may copy
    // instead of blend, and need not paint what lies under it.
    bool opaque{};
    LiveSurfaceOrigin origin{LiveSurfaceOrigin::producer};
};

struct LiveSurfaceSnapshot final {
    LiveSurfaceDescription description{};
    std::uint64_t epoch{};
    std::uint64_t published_generation{};
    std::uint64_t publishes{};
    std::uint64_t dropped_acquires{};
    std::uint64_t read_acquires{};
    // Newest generation ever sampled by a terminal renderer. This is a
    // sampling diagnostic, not a compositor-present acknowledgement.
    std::uint64_t last_read_generation{};
    bool write_active{};
    bool has_frame{};
    // True between reconfigure and the first publication of the new epoch,
    // while acquire_latest()/acquire_for_presentation() return the previous
    // epoch's last frame (at its own size) instead of nothing.
    bool carries_previous_frame{};
    // The content tag of the frame acquire_latest() returns (the carried
    // frame's while carries_previous_frame); zero when there is none.
    std::uint64_t content_tag{};
};

// Where a host's present tick takes its phase from (threaded sub-surfaces
// design note sections 7 and 8).
enum class LiveTimingSource : std::uint8_t {
    // No presenter has reported yet.
    none,
    // The display's own vertical blank: the compositor's timing on Windows,
    // the display link on macOS, frame callbacks or Present completions on
    // Linux.
    display_phased,
    // A timer phased from the last presentation the display completed.
    phased_timer,
    // A fixed cadence (60 Hz) without display timing.
    fallback,
};

// The display timing a presenter publishes after each present tick: data
// only, read by producers without the presenter's thread.
struct LiveDisplayTiming final {
    // The display's refresh period; zero when unknown.
    FrameInterval refresh_period{};
    // The vertical blank the tick was phased to (FrameClock time).
    FrameTime last_vblank{};
    LiveTimingSource source{LiveTimingSource::none};
};

// What the presenter last reported for one surface (LiveSurface::
// notify_presented).
struct LivePresentationRecord final {
    LiveDisplayTiming timing{};
    // The newest generation the presenter put on the screen.
    std::uint64_t presented_generation{};
    // Reports so far: one per present tick that showed this surface.
    std::uint64_t presents{};
};

} // namespace gui_forms
