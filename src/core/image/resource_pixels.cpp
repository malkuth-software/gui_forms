#include "resource_pixels.hpp"

namespace gui_forms::image {
namespace {

ResourcePixelError png_error(PngPixelError error) noexcept {
    switch (error) {
    case PngPixelError::none: return ResourcePixelError::none;
    case PngPixelError::invalid_argument: return ResourcePixelError::invalid_argument;
    case PngPixelError::decode_failed: return ResourcePixelError::decode_failed;
    case PngPixelError::unsupported_color_profile: return ResourcePixelError::unsupported_color_profile;
    case PngPixelError::invalid_color_metadata: return ResourcePixelError::invalid_color_metadata;
    case PngPixelError::insufficient_storage: return ResourcePixelError::insufficient_storage;
    case PngPixelError::allocation_failed: return ResourcePixelError::allocation_failed;
    }
    return ResourcePixelError::decode_failed;
}

// Whether every pixel's alpha byte (the fourth in both admitted orders) is
// 255: the bytes of each row AND-ed with no exit inside the row, and the
// scan stopped at the first row that is not wholly opaque. The rows lie in
// `pixels` (the decode checked the layout).
bool alpha_all_opaque(const std::span<const std::byte> pixels, const std::size_t row_bytes,
                      const std::uint32_t width, const std::uint32_t height) noexcept {
    const std::size_t row_length = static_cast<std::size_t>(width) * 4U;
    if (row_bytes < row_length || height == 0U) return false;
    const std::size_t needed = static_cast<std::size_t>(height - 1U) * row_bytes + row_length;
    if (pixels.size() < needed) return false;
    for (std::uint32_t y = 0U; y < height; ++y) {
        const std::byte* const row = pixels.data() + static_cast<std::size_t>(y) * row_bytes;
        unsigned all = 255U;
        for (std::size_t offset = 3U; offset < row_length; offset += 4U) {
            all &= std::to_integer<unsigned>(row[offset]);
        }
        if (all != 255U) return false;
    }
    return true;
}

} // namespace

ResourcePixelDecoder::ResourcePixelDecoder() {}

ResourcePixelDecoder::~ResourcePixelDecoder() {}

void ResourcePixelDecoder::release() noexcept {
    png_.release();
}

ResourcePixelResult ResourcePixelDecoder::decode(const ImageResourceView& resource, PixelTarget target,
                                                 std::span<std::byte> pixels, std::size_t row_bytes) noexcept {
    ResourcePixelResult result;
    if (resource.encoding == ImageResourceEncoding::png) {
        PngPixelRequest request{};
        request.limits = png_limits_for_image(resource.metadata.width, resource.metadata.height,
                                              resource.encoded.size());
        request.target = target;
        request.purpose = PngPixelPurpose::resource;
        PngPixelResult decoded = png_.decode(resource.encoded, request, pixels, row_bytes);
        result.error = png_error(decoded.error);
        result.width = decoded.width;
        result.height = decoded.height;
    } else {
        result.error = ResourcePixelError::unsupported_encoding;
        return result;
    }
    bool admitted_size = result.width == resource.metadata.width && result.height == resource.metadata.height;
    if (result.error == ResourcePixelError::none && !admitted_size) {
        result.error = ResourcePixelError::decode_failed;
    }
    if (result.error == ResourcePixelError::none) {
        // An image whose file carries no alpha is opaque without a scan.
        const bool declared_opaque = resource.metadata.alpha == ImageAlpha::opaque;
        result.every_pixel_opaque =
            declared_opaque || alpha_all_opaque(pixels, row_bytes, result.width, result.height);
    }
    return result;
}

} // namespace gui_forms::image
