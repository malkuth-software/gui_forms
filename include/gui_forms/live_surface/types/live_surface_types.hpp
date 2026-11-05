#pragma once

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

} // namespace gui_forms
