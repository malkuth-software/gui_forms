#pragma once

#include "gui_forms/host/types/host_types.hpp"

namespace gui_forms::detail {
// Private lossless clipboard representation. 16-byte little-endian header:
// magic GFIM, version, width, height; followed by tight rows. Version 2, the
// one written, holds the host image's premultiplied BGRA8 pixels as they are.
// Version 1, written before SK2, holds straight RGBA8 and is still read,
// through the input kernel straight_rgba_to_premultiplied_bgra.
[[nodiscard]] std::vector<std::byte> encode_clipboard_image(HostImageView image);
// Reads version 2 or version 1. The result is read at `generation`; native
// allocations may pad the bytes when `allow_allocation_padding` is set.
// Another process may write the clipboard, so a version 2 colour channel
// above its pixel's alpha is clamped to the alpha on reading (owner ruling,
// 2026-11-10): every image read honours the premultiplied contract, and a
// valid premultiplied image still reads back byte for byte.
[[nodiscard]] HostClipboardImageResult decode_clipboard_image(
    std::span<const std::byte> bytes, std::uint64_t generation, bool allow_allocation_padding);
[[nodiscard]] HostImage copy_host_image(HostImageView image);
} // namespace gui_forms::detail
