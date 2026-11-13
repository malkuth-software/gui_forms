#pragma once

#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::detail {
// The kernels where a host image (premultiplied BGRA8) crosses into or out of
// a straight-alpha representation: a native DIB, an icon, a pixel row read
// from a version-1 clipboard image. Nothing else converts host image pixels.
//
// premultiplied_channel: round(c * a / 255).
// straight_channel: min(255, (c * 255 + a / 2) / a), the nearest value with
// ties rounding up, and 0 where a = 0 (the PNG encoder's rule).
// straight_channel then premultiplied_channel gives back every premultiplied
// channel c <= a exactly, so a valid host image survives a straight
// interchange unchanged.
[[nodiscard]] GUI_FORMS_API std::uint8_t premultiplied_channel(std::uint8_t straight, std::uint8_t alpha) noexcept;
[[nodiscard]] GUI_FORMS_API std::uint8_t straight_channel(std::uint8_t premultiplied, std::uint8_t alpha) noexcept;

// Input kernel: `width` straight RGBA8 pixels of `source` (a version-1
// clipboard image row) as premultiplied BGRA8 in `destination`. Both hold at
// least width * 4 bytes; they may be the same span.
void straight_rgba_to_premultiplied_bgra(std::span<const std::byte> source, std::span<std::byte> destination,
                                         std::uint32_t width) noexcept;

// Output kernel: `width` premultiplied BGRA8 pixels of `source` as straight
// BGRA8 in `destination` (native DIBs and icons). Both hold at least
// width * 4 bytes; they may be the same span.
GUI_FORMS_API void premultiplied_bgra_to_straight_bgra(std::span<const std::byte> source, std::span<std::byte> destination,
                                         std::uint32_t width) noexcept;
} // namespace gui_forms::detail
