#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

// Shared types of the theme engine (stage T2, notes:planning/stages/T2-engine-core/
// PLAN.md). The engine rasterizes parts with first-party kernels into
// premultiplied BGRA8 pixels and gives identical bytes on every host.

// The engine version a primitive was introduced in, and the version a theme
// declares it needs. Versions order by major, then minor.
struct GUI_FORMS_API EngineVersion final {
    std::uint16_t major{};
    std::uint16_t minor{};

    friend constexpr bool operator==(EngineVersion left, EngineVersion right) noexcept {
        return left.major == right.major && left.minor == right.minor;
    }
    friend constexpr bool operator<(EngineVersion left, EngineVersion right) noexcept {
        if (left.major != right.major) return left.major < right.major;
        return left.minor < right.minor;
    }
};

// The version this build of the engine implements. E1 (the kernels, the
// registry and the bulge pair) is 0.1; E8 (the relief primitives, shapes and
// choice parameters) is 0.2; T3's EC1 (authored material fields and the
// material primitive's `family` recipe) is 0.3.
inline constexpr EngineVersion current_engine_version{0, 3};

// Device pixels per logical unit. Kept apart from plain doubles so that a
// logical length is never passed where a scale is meant (house style §3).
struct GUI_FORMS_API DeviceScale final {
    double value{1.0};

    friend constexpr bool operator==(DeviceScale left, DeviceScale right) noexcept {
        return left.value == right.value;
    }
};

// The scales a primitive accepts. Below 0.5 the dithered lattice's dots
// would collide; above 8 no host reports a display.
inline constexpr double minimum_device_scale = 0.5;
inline constexpr double maximum_device_scale = 8.0;

// A raster's size in device pixels.
struct GUI_FORMS_API DeviceExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    friend constexpr bool operator==(DeviceExtent left, DeviceExtent right) noexcept {
        return left.width == right.width && left.height == right.height;
    }
};

// The largest part raster, per side. Parts are small; the bound keeps every
// size product far inside std::size_t.
inline constexpr std::uint32_t maximum_part_raster_side = 16384;

// Bytes in one pixel: blue, green, red, alpha in that memory order (BGRA8,
// the one pixel format of every surface and raster), each colour premultiplied
// by alpha (blue, green and red never exceed alpha).
inline constexpr std::size_t part_raster_bytes_per_pixel = 4;

// A borrowed destination for one part's pixels. `pixels` is caller-owned and
// must stay valid for the call that fills it. Row y starts at byte
// y * row_bytes; the bytes of a row past width * 4 are padding and are never
// read or written.
struct GUI_FORMS_API PartRasterView final {
    std::span<std::uint8_t> pixels{};
    DeviceExtent extent{};
    std::size_t row_bytes{};
};

enum class RasterViewDefect : std::uint8_t {
    none,
    extent_too_large,
    row_bytes_too_small,
    row_bytes_not_whole_pixels,
    buffer_too_small,
};

// Checks the view's shape against its buffer once, before any kernel runs.
// An empty extent is valid and has nothing to fill.
[[nodiscard]] GUI_FORMS_API RasterViewDefect validate_raster_view(const PartRasterView& view) noexcept;

// Registered (snake_case) name of a defect, for diagnostics.
[[nodiscard]] GUI_FORMS_API const char* raster_view_defect_name(RasterViewDefect defect) noexcept;

// A painted part's identity from frame to frame (T2 GIVEN H3-1), which lets
// the engine keep a record for it: its states and when it entered them, its
// transitions, its preparation and its cached raster.
//
// The full identity is the control's stable ID, the declared part, and for
// an item part the item's stable ID. The engine takes the control's stable
// ID from the control painting the part, so a key cannot name another
// control; the key carries the rest:
// - `part`: the part's declared name, as in the control's anatomy;
// - `item`: the item's stable ID, empty for a part that is not an item's or
//   for an item without one;
// - `item_index`: the item's index. It is part of the identity only when
//   `item` is empty: the index fallback, which the window counts in its
//   metrics, because the record then follows the position, not the item.
//
// Every view is borrowed for the call that paints the part.
struct GUI_FORMS_API PartKey final {
    std::string_view part{};
    std::string_view item{};
    std::optional<std::size_t> item_index{};

    // Whether the key falls back to an item index.
    [[nodiscard]] constexpr bool index_fallback() const noexcept {
        const bool fallback = item.empty() && item_index.has_value();
        return fallback;
    }
};

} // namespace gui_forms
