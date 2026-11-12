#pragma once

#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

enum class PngColorType : std::uint8_t {
    grayscale = 0, truecolor = 2, indexed = 3,
    grayscale_alpha = 4, truecolor_alpha = 6,
};

// How a registry resource holds its image: a PNG file, or premultiplied
// BGRA pixels as given.
enum class ImageResourceEncoding : std::uint8_t {
    png,
    bgra32_premultiplied,
};

// The alpha a resource's pixels carry: none (every pixel opaque), straight
// (colour independent of coverage, as in PNG files) or premultiplied.
enum class ImageAlpha : std::uint8_t {
    opaque,
    straight,
    premultiplied,
};

// What every resource reports, whatever its encoding. decoded_byte_count is
// width * height * 4, the size of its 8-bit four-channel pixels.
struct ImageMetadata final {
    std::uint32_t width{};
    std::uint32_t height{};
    ImageAlpha alpha{ImageAlpha::straight};
    std::uint64_t decoded_byte_count{};
};

struct ImageRegistryLimits final {
    std::size_t maximum_encoded_bytes_per_image{32U * 1024U * 1024U};
    std::uint32_t maximum_width{4096};
    std::uint32_t maximum_height{4096};
    std::uint64_t maximum_pixels{4096ULL * 4096ULL};
    std::uint64_t maximum_decoded_bytes_per_image{64ULL * 1024ULL * 1024ULL};
    std::size_t maximum_chunks_per_image{4096};
    std::size_t maximum_resources{1024};
    std::uint64_t maximum_total_encoded_bytes{64ULL * 1024ULL * 1024ULL};
    std::uint64_t maximum_total_decoded_bytes{256ULL * 1024ULL * 1024ULL};
};

enum class ImageResourceError : std::uint8_t {
    none, empty_input, encoded_limit_exceeded, invalid_signature,
    truncated_chunk, invalid_chunk_type, chunk_limit_exceeded, crc_mismatch,
    ihdr_not_first, duplicate_ihdr, invalid_ihdr, dimension_limit_exceeded,
    unsupported_color_format, unsupported_color_profile, invalid_palette,
    invalid_transparency, invalid_color_metadata, noncontiguous_idat,
    missing_idat, missing_iend, trailing_data, unknown_critical_chunk,
    resource_count_exceeded, registry_encoded_limit_exceeded,
    registry_decoded_limit_exceeded, stale_image_id, allocation_failed,
};

[[nodiscard]] std::string_view image_resource_error_name(
    ImageResourceError error) noexcept;

// PNG detail, reported only by validate_png.
struct PngMetadata final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint8_t bit_depth{};
    PngColorType color_type{PngColorType::truecolor_alpha};
    bool interlaced{};
    std::uint64_t source_row_bytes{};
    std::uint64_t decoded_byte_count{};
};

struct PngValidationResult final {
    ImageResourceError error{ImageResourceError::none};
    PngMetadata metadata{};

    [[nodiscard]] static constexpr PngValidationResult failure(
        ImageResourceError failure_error) noexcept {
        return PngValidationResult{failure_error, {}};
    }
    [[nodiscard]] static constexpr PngValidationResult success(
        PngMetadata validated_metadata) noexcept {
        return PngValidationResult{
            ImageResourceError::none, validated_metadata};
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ImageResourceError::none;
    }
};

[[nodiscard]] PngValidationResult validate_png(
    std::span<const std::byte> encoded,
    const ImageRegistryLimits& limits = {}) noexcept;

struct ImageLoadResult final {
    ImageId image{};
    ImageResourceError error{ImageResourceError::none};

    [[nodiscard]] static constexpr ImageLoadResult failure(
        ImageResourceError failure_error) noexcept {
        return ImageLoadResult{{}, failure_error};
    }
    [[nodiscard]] static constexpr ImageLoadResult success(
        ImageId loaded_image) noexcept {
        return ImageLoadResult{loaded_image, ImageResourceError::none};
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ImageResourceError::none && image.value != 0;
    }
};

// The part of an image patched since an earlier state of it (D4). A run of
// patch_bgra32_premultiplied calls with no other change between them is one
// run: base_image and base_hash name the image as it was before the run's
// first patch, and the rectangle is the union of the run's patches, so the
// pixels differ from that state, and from every state inside the run, only
// inside it. base_hash 0 means no run is open: a consumer resynchronizes the
// whole image.
struct ImagePatchRegion final {
    // ImageId::value before the run's first patch.
    std::uint64_t base_image{};
    std::uint64_t base_hash{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct ImageResourceView final {
    ImageId image{};
    ImageResourceEncoding encoding{ImageResourceEncoding::png};
    ImageMetadata metadata{};
    // The encoded file (png) or the pixels (bgra32_premultiplied).
    std::span<const std::byte> encoded{};
    // Bytes per row: of the pixels, or of the file's unfiltered samples.
    std::uint64_t row_bytes{};
    // Opaque cache revision paired with ImageId. Initial loads may seed this
    // from pixel bytes; updates advance it. It is not a content digest.
    std::uint64_t content_hash{};
    // The open patch run, if any (ImagePatchRegion).
    ImagePatchRegion patched{};
};

struct ImageRegistrySnapshot final {
    std::uint64_t revision{};
    std::uint64_t resource_count{};
    std::uint64_t encoded_bytes{};
    std::uint64_t decoded_bytes{};
};

} // namespace gui_forms
