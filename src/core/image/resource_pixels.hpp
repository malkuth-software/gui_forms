#pragma once

#include "gui_forms/resources/types/image_resource_types.hpp"
#include "pixel_target.hpp"
#include "png_pixels.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::image {

// The one decode of encoded registry resources to 8-bit pixels, shared by
// every renderer and host. PNG is the only encoded resource format; this
// decoder chooses the codec from the resource's encoding, so another format
// enters as one more encoding and one more owned decoder here. It is a
// concrete owner of its decoder, not a codec interface (house style section
// 7). Clipboard and GUI.Drawing decoding use the PNG path directly
// (png_pixels.hpp).

enum class ResourcePixelError : std::uint8_t {
    none,
    // A premultiplied BGRA resource: its bytes are already pixels, which the
    // consumer copies itself.
    unsupported_encoding,
    invalid_argument,
    decode_failed,
    unsupported_color_profile,
    invalid_color_metadata,
    insufficient_storage,
    allocation_failed
};

struct ResourcePixelResult final {
    ResourcePixelError error{ResourcePixelError::none};
    std::uint32_t width{};
    std::uint32_t height{};
    // On success, whether every decoded pixel has alpha 255: from the
    // resource's metadata when it carries no alpha (ImageAlpha::opaque),
    // else from one pass over the decoded alpha bytes. A renderer may then
    // draw the image as an opaque source (a copy); the pixels are the same.
    bool every_pixel_opaque{};
};

// Owns the PNG decoder (purpose resource, limits from the admitted
// dimensions). It keeps its storage, grown to the largest image decoded,
// until release() or destruction, so a consumer that decodes a batch keeps
// one decoder for it. One execution context per decoder.
class ResourcePixelDecoder final {
public:
    ResourcePixelDecoder();
    ~ResourcePixelDecoder();
    ResourcePixelDecoder(const ResourcePixelDecoder&) = delete;
    ResourcePixelDecoder& operator=(const ResourcePixelDecoder&) = delete;
    ResourcePixelDecoder(ResourcePixelDecoder&&) = delete;
    ResourcePixelDecoder& operator=(ResourcePixelDecoder&&) = delete;

    // Decodes the resource's encoded bytes into caller pixels (layout as
    // valid_pixel_destination states). The resource's bytes and the pixels
    // must be disjoint. A decoded size other than the resource's metadata is
    // decode_failed. On success the result says whether every pixel is
    // opaque (ResourcePixelResult::every_pixel_opaque). On failure the destination may hold a partial,
    // unverified prefix: discard it.
    [[nodiscard]] ResourcePixelResult decode(const ImageResourceView& resource, PixelTarget target,
                                             std::span<std::byte> pixels, std::size_t row_bytes) noexcept;

    // Frees the decoder's storage; the next decode allocates again.
    void release() noexcept;

private:
    PngPixelDecoder png_;
};

} // namespace gui_forms::image
