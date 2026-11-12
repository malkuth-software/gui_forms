#include "gui_forms/resources.hpp"

#include "image_registry_access.hpp"

#include "stx_zlib/checksum.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace gui_forms {
namespace {

constexpr std::array<std::uint8_t, 8> png_signature{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
constexpr std::uint64_t slot_mask = 0xffffULL;
constexpr std::uint64_t maximum_generation = (1ULL << 48U) - 1ULL;

constexpr std::uint32_t chunk_type(char a, char b, char c, char d) noexcept {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

constexpr std::uint32_t ihdr = chunk_type('I', 'H', 'D', 'R');
constexpr std::uint32_t plte = chunk_type('P', 'L', 'T', 'E');
constexpr std::uint32_t idat = chunk_type('I', 'D', 'A', 'T');
constexpr std::uint32_t iend = chunk_type('I', 'E', 'N', 'D');
constexpr std::uint32_t trns = chunk_type('t', 'R', 'N', 'S');
constexpr std::uint32_t srgb = chunk_type('s', 'R', 'G', 'B');
constexpr std::uint32_t gama = chunk_type('g', 'A', 'M', 'A');
constexpr std::uint32_t chrm = chunk_type('c', 'H', 'R', 'M');
constexpr std::uint32_t iccp = chunk_type('i', 'C', 'C', 'P');
constexpr std::uint32_t cicp = chunk_type('c', 'I', 'C', 'P');

std::uint32_t read_u32(const std::byte* bytes) noexcept {
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8U) |
           static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
}

bool ascii_letter(std::uint8_t value) noexcept {
    return (value >= static_cast<std::uint8_t>('A') &&
            value <= static_cast<std::uint8_t>('Z')) ||
           (value >= static_cast<std::uint8_t>('a') &&
            value <= static_cast<std::uint8_t>('z'));
}

std::uint64_t hash_bytes(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool valid_depth(std::uint8_t color, std::uint8_t depth) noexcept {
    switch (color) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 2: return depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 4:
    case 6: return depth == 8 || depth == 16;
    default: return false;
    }
}

std::uint8_t channel_count(std::uint8_t color) noexcept {
    switch (color) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    default: return 0;
    }
}

ImageId make_image_id(std::size_t slot, std::uint64_t generation) noexcept {
    return ImageId{(generation << 16U) | (static_cast<std::uint64_t>(slot) + 1ULL)};
}

bool split_image_id(ImageId image,
                    std::size_t& slot,
                    std::uint64_t& generation) noexcept {
    const std::uint64_t encoded_slot = image.value & slot_mask;
    generation = image.value >> 16U;
    if (encoded_slot == 0 || generation == 0) {
        return false;
    }
    slot = static_cast<std::size_t>(encoded_slot - 1ULL);
    return true;
}

std::uint64_t next_generation(std::uint64_t generation) noexcept {
    return generation >= maximum_generation ? 1ULL : generation + 1ULL;
}

} // namespace

std::string_view image_resource_error_name(ImageResourceError error) noexcept {
    switch (error) {
    case ImageResourceError::none: return "none";
    case ImageResourceError::empty_input: return "empty_input";
    case ImageResourceError::encoded_limit_exceeded: return "encoded_limit_exceeded";
    case ImageResourceError::invalid_signature: return "invalid_signature";
    case ImageResourceError::truncated_chunk: return "truncated_chunk";
    case ImageResourceError::invalid_chunk_type: return "invalid_chunk_type";
    case ImageResourceError::chunk_limit_exceeded: return "chunk_limit_exceeded";
    case ImageResourceError::crc_mismatch: return "crc_mismatch";
    case ImageResourceError::ihdr_not_first: return "ihdr_not_first";
    case ImageResourceError::duplicate_ihdr: return "duplicate_ihdr";
    case ImageResourceError::invalid_ihdr: return "invalid_ihdr";
    case ImageResourceError::dimension_limit_exceeded: return "dimension_limit_exceeded";
    case ImageResourceError::unsupported_color_format: return "unsupported_color_format";
    case ImageResourceError::unsupported_color_profile: return "unsupported_color_profile";
    case ImageResourceError::invalid_palette: return "invalid_palette";
    case ImageResourceError::invalid_transparency: return "invalid_transparency";
    case ImageResourceError::invalid_color_metadata: return "invalid_color_metadata";
    case ImageResourceError::noncontiguous_idat: return "noncontiguous_idat";
    case ImageResourceError::missing_idat: return "missing_idat";
    case ImageResourceError::missing_iend: return "missing_iend";
    case ImageResourceError::trailing_data: return "trailing_data";
    case ImageResourceError::unknown_critical_chunk: return "unknown_critical_chunk";
    case ImageResourceError::resource_count_exceeded: return "resource_count_exceeded";
    case ImageResourceError::registry_encoded_limit_exceeded:
        return "registry_encoded_limit_exceeded";
    case ImageResourceError::registry_decoded_limit_exceeded:
        return "registry_decoded_limit_exceeded";
    case ImageResourceError::stale_image_id: return "stale_image_id";
    case ImageResourceError::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

namespace {

// validate_png's checks. `transparency` reports a tRNS chunk, which gives an
// image without an alpha channel transparent pixels.
PngValidationResult validate_png_chunks(std::span<const std::byte> encoded,
                                        const ImageRegistryLimits& limits,
                                        bool& transparency) noexcept {
    transparency = false;
    if (encoded.empty()) {
        return PngValidationResult::failure(ImageResourceError::empty_input);
    }
    if (encoded.size() > limits.maximum_encoded_bytes_per_image) {
        return PngValidationResult::failure(ImageResourceError::encoded_limit_exceeded);
    }
    if (encoded.size() < png_signature.size()) {
        return PngValidationResult::failure(ImageResourceError::invalid_signature);
    }
    for (std::size_t index = 0; index < png_signature.size(); ++index) {
        if (std::to_integer<std::uint8_t>(encoded[index]) != png_signature[index]) {
            return PngValidationResult::failure(ImageResourceError::invalid_signature);
        }
    }

    PngMetadata metadata;
    std::size_t offset = png_signature.size();
    std::size_t chunk_count = 0;
    std::uint64_t idat_bytes = 0;
    std::uint32_t palette_entries = 0;
    bool saw_ihdr = false;
    bool saw_plte = false;
    bool saw_idat = false;
    bool idat_ended = false;
    bool saw_trns = false;
    bool saw_srgb = false;
    bool saw_gama = false;
    bool saw_chrm = false;
    bool saw_cicp = false;

    while (offset < encoded.size()) {
        if (++chunk_count > limits.maximum_chunks_per_image) {
            return PngValidationResult::failure(ImageResourceError::chunk_limit_exceeded);
        }
        if (encoded.size() - offset < 12U) {
            return PngValidationResult::failure(ImageResourceError::truncated_chunk);
        }
        const std::uint32_t length = read_u32(encoded.data() + offset);
        const std::size_t remaining = encoded.size() - offset;
        if (length > 0x7fffffffU ||
            static_cast<std::uint64_t>(length) + 12ULL > remaining) {
            return PngValidationResult::failure(ImageResourceError::truncated_chunk);
        }

        const std::byte* const type_bytes = encoded.data() + offset + 4U;
        for (std::size_t index = 0; index < 4; ++index) {
            if (!ascii_letter(std::to_integer<std::uint8_t>(type_bytes[index]))) {
                return PngValidationResult::failure(ImageResourceError::invalid_chunk_type);
            }
        }
        if ((std::to_integer<std::uint8_t>(type_bytes[2]) & 0x20U) != 0U) {
            return PngValidationResult::failure(ImageResourceError::invalid_chunk_type);
        }
        const std::uint32_t type = read_u32(type_bytes);
        const std::span<const std::byte> crc_input(
            type_bytes, static_cast<std::size_t>(length) + 4U);
        const std::uint32_t expected_crc =
            read_u32(encoded.data() + offset + 8U + length);
        if (stx_zlib::crc32(crc_input) != expected_crc) {
            return PngValidationResult::failure(ImageResourceError::crc_mismatch);
        }
        const std::span<const std::byte> data(encoded.data() + offset + 8U, length);

        if (!saw_ihdr && type != ihdr) {
            return PngValidationResult::failure(ImageResourceError::ihdr_not_first);
        }
        if (saw_idat && type != idat) {
            idat_ended = true;
        }

        if (type == ihdr) {
            if (saw_ihdr) {
                return PngValidationResult::failure(ImageResourceError::duplicate_ihdr);
            }
            if (length != 13U) {
                return PngValidationResult::failure(ImageResourceError::invalid_ihdr);
            }
            metadata.width = read_u32(data.data());
            metadata.height = read_u32(data.data() + 4U);
            metadata.bit_depth = std::to_integer<std::uint8_t>(data[8]);
            const std::uint8_t color = std::to_integer<std::uint8_t>(data[9]);
            const std::uint8_t compression = std::to_integer<std::uint8_t>(data[10]);
            const std::uint8_t filter = std::to_integer<std::uint8_t>(data[11]);
            const std::uint8_t interlace = std::to_integer<std::uint8_t>(data[12]);
            if (metadata.width == 0 || metadata.height == 0 || compression != 0 ||
                filter != 0 || interlace > 1) {
                return PngValidationResult::failure(ImageResourceError::invalid_ihdr);
            }
            if (!valid_depth(color, metadata.bit_depth)) {
                return PngValidationResult::failure(ImageResourceError::unsupported_color_format);
            }
            const std::uint64_t pixel_count =
                static_cast<std::uint64_t>(metadata.width) * metadata.height;
            if (metadata.width > limits.maximum_width ||
                metadata.height > limits.maximum_height ||
                pixel_count > limits.maximum_pixels ||
                pixel_count > std::numeric_limits<std::uint64_t>::max() / 4ULL) {
                return PngValidationResult::failure(ImageResourceError::dimension_limit_exceeded);
            }
            const std::uint64_t row_bits = static_cast<std::uint64_t>(metadata.width) *
                                           channel_count(color) * metadata.bit_depth;
            metadata.source_row_bytes = (row_bits + 7ULL) / 8ULL;
            metadata.decoded_byte_count = pixel_count * 4ULL;
            if (metadata.decoded_byte_count > limits.maximum_decoded_bytes_per_image) {
                return PngValidationResult::failure(ImageResourceError::dimension_limit_exceeded);
            }
            metadata.color_type = static_cast<PngColorType>(color);
            metadata.interlaced = interlace != 0;
            saw_ihdr = true;
        } else if (type == plte) {
            const std::uint8_t color = static_cast<std::uint8_t>(metadata.color_type);
            if (saw_plte || saw_idat || length == 0 || length > 768U || length % 3U != 0 ||
                color == 0 || color == 4) {
                return PngValidationResult::failure(ImageResourceError::invalid_palette);
            }
            palette_entries = length / 3U;
            if (color == 3 && palette_entries > (1U << metadata.bit_depth)) {
                return PngValidationResult::failure(ImageResourceError::invalid_palette);
            }
            saw_plte = true;
        } else if (type == idat) {
            if (idat_ended) {
                return PngValidationResult::failure(ImageResourceError::noncontiguous_idat);
            }
            if (metadata.color_type == PngColorType::indexed && !saw_plte) {
                return PngValidationResult::failure(ImageResourceError::invalid_palette);
            }
            saw_idat = true;
            idat_bytes += length;
        } else if (type == iend) {
            if (length != 0) {
                return PngValidationResult::failure(ImageResourceError::missing_iend);
            }
            if (!saw_idat || idat_bytes == 0) {
                return PngValidationResult::failure(ImageResourceError::missing_idat);
            }
            offset += 12U;
            if (offset != encoded.size()) {
                return PngValidationResult::failure(ImageResourceError::trailing_data);
            }
            transparency = saw_trns;
            return PngValidationResult::success(metadata);
        } else if (type == iccp) {
            // Compressed ICC profiles are an independent decompression boundary.
            // This proving slice fixes decoded output to sRGB and rejects them.
            return PngValidationResult::failure(ImageResourceError::unsupported_color_profile);
        } else if (type == cicp) {
            // Coding-independent code points outrank every other colour
            // chunk. Only the sRGB-equivalent values (BT.709 primaries, sRGB
            // transfer, RGB matrix, full range) match the fixed sRGB output.
            if (saw_cicp || saw_plte || saw_idat || length != 4U) {
                return PngValidationResult::failure(ImageResourceError::invalid_color_metadata);
            }
            const bool srgb_equivalent = data[0] == std::byte{1} && data[1] == std::byte{13} &&
                                         data[2] == std::byte{0} && data[3] == std::byte{1};
            if (!srgb_equivalent) {
                return PngValidationResult::failure(ImageResourceError::unsupported_color_profile);
            }
            saw_cicp = true;
        } else if (type == trns) {
            if (saw_trns || saw_idat) {
                return PngValidationResult::failure(ImageResourceError::invalid_transparency);
            }
            const PngColorType color = metadata.color_type;
            const bool valid =
                (color == PngColorType::grayscale && length == 2U) ||
                (color == PngColorType::truecolor && length == 6U) ||
                (color == PngColorType::indexed && saw_plte && length > 0 &&
                 length <= palette_entries);
            if (!valid) {
                return PngValidationResult::failure(ImageResourceError::invalid_transparency);
            }
            saw_trns = true;
        } else if (type == srgb) {
            if (saw_srgb || saw_plte || saw_idat || length != 1U ||
                std::to_integer<std::uint8_t>(data[0]) > 3U) {
                return PngValidationResult::failure(ImageResourceError::invalid_color_metadata);
            }
            saw_srgb = true;
        } else if (type == gama) {
            if (saw_gama || saw_plte || saw_idat || length != 4U ||
                read_u32(data.data()) == 0U) {
                return PngValidationResult::failure(ImageResourceError::invalid_color_metadata);
            }
            saw_gama = true;
        } else if (type == chrm) {
            if (saw_chrm || saw_plte || saw_idat || length != 32U) {
                return PngValidationResult::failure(ImageResourceError::invalid_color_metadata);
            }
            saw_chrm = true;
        } else if ((std::to_integer<std::uint8_t>(type_bytes[0]) & 0x20U) == 0U) {
            return PngValidationResult::failure(ImageResourceError::unknown_critical_chunk);
        }

        offset += static_cast<std::size_t>(length) + 12U;
    }
    return PngValidationResult::failure(
        saw_idat ? ImageResourceError::missing_iend
                 : ImageResourceError::missing_idat);
}

ImageMetadata png_image_metadata(const PngMetadata& png, bool transparency) noexcept {
    bool alpha_channel = png.color_type == PngColorType::grayscale_alpha ||
                         png.color_type == PngColorType::truecolor_alpha;
    ImageAlpha alpha = alpha_channel || transparency ? ImageAlpha::straight : ImageAlpha::opaque;
    ImageMetadata metadata{png.width, png.height, alpha, png.decoded_byte_count};
    return metadata;
}

} // namespace

PngValidationResult validate_png(std::span<const std::byte> encoded,
                                 const ImageRegistryLimits& limits) noexcept {
    bool transparency = false;
    PngValidationResult result = validate_png_chunks(encoded, limits, transparency);
    return result;
}

// An encoded file the registry has validated: its encoding, what it reports,
// and its unfiltered sample row length.
struct ImageRegistry::EncodedImage final {
    ImageResourceError error{ImageResourceError::none};
    ImageResourceEncoding encoding{ImageResourceEncoding::png};
    ImageMetadata metadata{};
    std::uint64_t row_bytes{};
};

struct ImageRegistry::Slot final {
    std::uint64_t generation{1};
    bool occupied{};
    ImageResourceEncoding encoding{ImageResourceEncoding::png};
    ImageMetadata metadata{};
    ImageBytes encoded;
    std::uint64_t row_bytes{};
    std::uint64_t content_hash{};
    // The open patch run (ImagePatchRegion); closed by every other change.
    ImagePatchRegion patched{};
};

ImageRegistry::ImageRegistry(ImageRegistryLimits limits) : limits_(limits) {}
ImageRegistry::~ImageRegistry() = default;
ImageRegistry::ImageRegistry(ImageRegistry&& other) noexcept
    : limits_(other.limits_), slots_(std::move(other.slots_)), revision_(other.revision_ + 1U),
      resource_count_(other.resource_count_), encoded_bytes_(other.encoded_bytes_),
      decoded_bytes_(other.decoded_bytes_), liveness_(std::move(other.liveness_)) {
    // The images, and so the owners holding the handle, move here.
    if (liveness_ != nullptr) (*liveness_).registry = this;
    other.release_moved_contents();
}

ImageRegistry& ImageRegistry::operator=(ImageRegistry&& other) noexcept {
    if (this == &other) return *this;
    // Above both previous revisions: a consumer of either must resynchronize.
    const std::uint64_t revision = std::max(revision_, other.revision_) + 1U;
    limits_ = other.limits_;
    slots_ = std::move(other.slots_);
    revision_ = revision;
    resource_count_ = other.resource_count_;
    encoded_bytes_ = other.encoded_bytes_;
    decoded_bytes_ = other.decoded_bytes_;
    // This registry's own images are gone: the handle on them expires, and
    // the moved images' handle names this registry.
    liveness_ = std::move(other.liveness_);
    if (liveness_ != nullptr) (*liveness_).registry = this;
    other.release_moved_contents();
    return *this;
}

std::weak_ptr<detail::ImageRegistryLiveness> detail::ImageRegistryAccess::liveness(ImageRegistry& registry) {
    if (registry.liveness_ == nullptr) {
        registry.liveness_ = std::make_shared<ImageRegistryLiveness>(ImageRegistryLiveness{&registry});
    }
    std::weak_ptr<ImageRegistryLiveness> handle(registry.liveness_);
    return handle;
}

void ImageRegistry::release_moved_contents() noexcept {
    slots_.clear();
    ++revision_;
    resource_count_ = 0U;
    encoded_bytes_ = 0U;
    decoded_bytes_ = 0U;
}

ImageResourceError ImageRegistry::registry_quota_error(
    std::uint64_t encoded_bytes,
    std::uint64_t decoded_bytes,
    std::uint64_t replaced_encoded,
    std::uint64_t replaced_decoded) const noexcept {
    const std::uint64_t encoded_base = encoded_bytes_ - replaced_encoded;
    const std::uint64_t decoded_base = decoded_bytes_ - replaced_decoded;
    if (encoded_bytes > limits_.maximum_total_encoded_bytes ||
        encoded_base > limits_.maximum_total_encoded_bytes - encoded_bytes) {
        return ImageResourceError::registry_encoded_limit_exceeded;
    }
    if (decoded_bytes > limits_.maximum_total_decoded_bytes ||
        decoded_base > limits_.maximum_total_decoded_bytes - decoded_bytes) {
        return ImageResourceError::registry_decoded_limit_exceeded;
    }
    return ImageResourceError::none;
}

ImageRegistry::EncodedImage ImageRegistry::validate_encoded(
    std::span<const std::byte> encoded, bool png_only) const noexcept {
    EncodedImage result;
    bool transparency = false;
    const PngValidationResult png = validate_png_chunks(encoded, limits_, transparency);
    if (png || png.error != ImageResourceError::invalid_signature || png_only) {
        result.error = png.error;
        result.encoding = ImageResourceEncoding::png;
        result.metadata = png_image_metadata(png.metadata, transparency);
        result.row_bytes = png.metadata.source_row_bytes;
        return result;
    }
    // Not a PNG file (validate_png checked emptiness and the encoded size
    // first). PNG is the only admitted format; another format's admission,
    // chosen by its signature, would be tried here.
    result.error = ImageResourceError::invalid_signature;
    return result;
}

ImageLoadResult ImageRegistry::load_image(std::span<const std::byte> encoded) {
    return load_encoded(encoded, false);
}

ImageLoadResult ImageRegistry::load_png(std::span<const std::byte> encoded) {
    return load_encoded(encoded, true);
}

ImageLoadResult ImageRegistry::load_encoded(std::span<const std::byte> encoded, bool png_only) {
    const EncodedImage validated = validate_encoded(encoded, png_only);
    if (validated.error != ImageResourceError::none) {
        return ImageLoadResult::failure(validated.error);
    }
    if (resource_count_ >= limits_.maximum_resources) {
        return ImageLoadResult::failure(ImageResourceError::resource_count_exceeded);
    }
    if (const ImageResourceError quota = registry_quota_error(
            encoded.size(), validated.metadata.decoded_byte_count);
        quota != ImageResourceError::none) {
        return ImageLoadResult::failure(quota);
    }
    try {
        return store_new(validated.encoding,
                         ImageBytes(encoded.begin(), encoded.end()),
                         validated.metadata, validated.row_bytes,
                         hash_bytes(encoded));
    } catch (const std::bad_alloc&) {
        return ImageLoadResult::failure(ImageResourceError::allocation_failed);
    }
}

ImageLoadResult ImageRegistry::store_new(ImageResourceEncoding encoding,
                                         ImageBytes encoded,
                                         ImageMetadata metadata,
                                         std::uint64_t row_bytes,
                                         std::uint64_t content_hash) {
    std::size_t slot_index = 0;
    for (; slot_index < slots_.size(); ++slot_index) {
        if (!slots_[slot_index].occupied) {
            break;
        }
    }
    if (slot_index >= slot_mask) {
        return ImageLoadResult::failure(ImageResourceError::resource_count_exceeded);
    }
    if (slot_index == slots_.size()) {
        slots_.emplace_back();
    } else {
        slots_[slot_index].generation = next_generation(slots_[slot_index].generation);
    }
    Slot& slot = slots_[slot_index];
    slot.occupied = true;
    slot.encoding = encoding;
    slot.metadata = metadata;
    slot.encoded = std::move(encoded);
    slot.row_bytes = row_bytes;
    slot.content_hash = content_hash;
    slot.patched = {};
    ++resource_count_;
    encoded_bytes_ += slot.encoded.size();
    decoded_bytes_ += slot.metadata.decoded_byte_count;
    ++revision_;
    return ImageLoadResult::success(make_image_id(slot_index, slot.generation));
}

ImageLoadResult ImageRegistry::load_bgra32_premultiplied(
    std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
    std::span<const std::byte> pixels) {
    if (width == 0 || height == 0 || width > limits_.maximum_width ||
        height > limits_.maximum_height ||
        static_cast<std::uint64_t>(width) * height > limits_.maximum_pixels ||
        row_bytes < static_cast<std::uint64_t>(width) * 4U ||
        row_bytes > std::numeric_limits<std::size_t>::max() ||
        height > std::numeric_limits<std::size_t>::max() / row_bytes ||
        pixels.size() != static_cast<std::size_t>(row_bytes) * height) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    const std::uint64_t tight_row_bytes = static_cast<std::uint64_t>(width) * 4U;
    const std::uint64_t decoded_bytes = tight_row_bytes * height;
    if (decoded_bytes > limits_.maximum_decoded_bytes_per_image) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    if (resource_count_ >= limits_.maximum_resources) {
        return ImageLoadResult::failure(ImageResourceError::resource_count_exceeded);
    }
    if (const ImageResourceError quota = registry_quota_error(decoded_bytes, decoded_bytes);
        quota != ImageResourceError::none) {
        return ImageLoadResult::failure(quota);
    }
    try {
        ImageBytes tight(static_cast<std::size_t>(decoded_bytes));
        for (std::uint32_t row = 0; row < height; ++row) {
            std::copy_n(pixels.data() + static_cast<std::size_t>(row_bytes) * row,
                        static_cast<std::size_t>(tight_row_bytes),
                        tight.data() + static_cast<std::size_t>(tight_row_bytes) * row);
        }
        ImageMetadata metadata{width, height, ImageAlpha::premultiplied, decoded_bytes};
        std::uint64_t hash = hash_bytes(tight);
        hash ^= (static_cast<std::uint64_t>(width) << 32U) | height;
        return store_new(ImageResourceEncoding::bgra32_premultiplied,
                         std::move(tight), metadata, tight_row_bytes, hash);
    } catch (const std::bad_alloc&) {
        return ImageLoadResult::failure(ImageResourceError::allocation_failed);
    }
}

ImageLoadResult ImageRegistry::replace_image(ImageId image,
                                             std::span<const std::byte> encoded) {
    return replace_encoded(image, encoded, false);
}

ImageLoadResult ImageRegistry::replace_png(ImageId image,
                                           std::span<const std::byte> encoded) {
    return replace_encoded(image, encoded, true);
}

ImageLoadResult ImageRegistry::replace_encoded(ImageId image,
                                               std::span<const std::byte> encoded,
                                               bool png_only) {
    std::size_t slot_index = 0;
    std::uint64_t generation = 0;
    if (!split_image_id(image, slot_index, generation) || slot_index >= slots_.size()) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    Slot& slot = slots_[slot_index];
    if (!slot.occupied || slot.generation != generation) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    const EncodedImage validated = validate_encoded(encoded, png_only);
    if (validated.error != ImageResourceError::none) {
        return ImageLoadResult::failure(validated.error);
    }
    if (const ImageResourceError quota = registry_quota_error(
            encoded.size(), validated.metadata.decoded_byte_count,
            slot.encoded.size(), slot.metadata.decoded_byte_count);
        quota != ImageResourceError::none) {
        return ImageLoadResult::failure(quota);
    }

    ImageBytes replacement;
    try {
        replacement.assign(encoded.begin(), encoded.end());
    } catch (const std::bad_alloc&) {
        return ImageLoadResult::failure(ImageResourceError::allocation_failed);
    }
    encoded_bytes_ -= slot.encoded.size();
    decoded_bytes_ -= slot.metadata.decoded_byte_count;
    slot.generation = next_generation(slot.generation);
    slot.encoding = validated.encoding;
    slot.metadata = validated.metadata;
    slot.encoded = std::move(replacement);
    slot.row_bytes = validated.row_bytes;
    slot.content_hash = hash_bytes(encoded);
    slot.patched = {};
    encoded_bytes_ += slot.encoded.size();
    decoded_bytes_ += slot.metadata.decoded_byte_count;
    ++revision_;
    return ImageLoadResult::success(make_image_id(slot_index, slot.generation));
}

ImageLoadResult ImageRegistry::replace_bgra32_premultiplied(
    ImageId image, std::uint32_t width, std::uint32_t height,
    std::uint64_t row_bytes, std::span<const std::byte> pixels) {
    std::size_t slot_index = 0;
    std::uint64_t generation = 0;
    if (!split_image_id(image, slot_index, generation) || slot_index >= slots_.size()) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    Slot& slot = slots_[slot_index];
    if (!slot.occupied || slot.generation != generation) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    if (width == 0 || height == 0 || width > limits_.maximum_width ||
        height > limits_.maximum_height ||
        static_cast<std::uint64_t>(width) * height > limits_.maximum_pixels ||
        row_bytes < static_cast<std::uint64_t>(width) * 4U ||
        row_bytes > std::numeric_limits<std::size_t>::max() ||
        height > std::numeric_limits<std::size_t>::max() / row_bytes ||
        pixels.size() != static_cast<std::size_t>(row_bytes) * height) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    const std::uint64_t tight_row_bytes = static_cast<std::uint64_t>(width) * 4U;
    const std::uint64_t decoded_bytes = tight_row_bytes * height;
    if (decoded_bytes > limits_.maximum_decoded_bytes_per_image) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    if (const ImageResourceError quota = registry_quota_error(
            decoded_bytes, decoded_bytes, slot.encoded.size(),
            slot.metadata.decoded_byte_count);
        quota != ImageResourceError::none) {
        return ImageLoadResult::failure(quota);
    }
    try {
        ImageBytes tight(static_cast<std::size_t>(decoded_bytes));
        for (std::uint32_t row = 0; row < height; ++row) {
            std::copy_n(pixels.data() + static_cast<std::size_t>(row_bytes) * row,
                        static_cast<std::size_t>(tight_row_bytes),
                        tight.data() + static_cast<std::size_t>(tight_row_bytes) * row);
        }
        encoded_bytes_ -= slot.encoded.size();
        decoded_bytes_ -= slot.metadata.decoded_byte_count;
        slot.generation = next_generation(slot.generation);
        slot.encoding = ImageResourceEncoding::bgra32_premultiplied;
        slot.metadata = ImageMetadata{width, height, ImageAlpha::premultiplied, decoded_bytes};
        slot.encoded = std::move(tight);
        slot.row_bytes = tight_row_bytes;
        slot.content_hash = hash_bytes(slot.encoded) ^
            ((static_cast<std::uint64_t>(width) << 32U) | height);
        slot.patched = {};
        encoded_bytes_ += slot.encoded.size();
        decoded_bytes_ += decoded_bytes;
        ++revision_;
        return ImageLoadResult::success(make_image_id(slot_index, slot.generation));
    } catch (const std::bad_alloc&) {
        return ImageLoadResult::failure(ImageResourceError::allocation_failed);
    }
}

ImageLoadResult ImageRegistry::update_bgra32_premultiplied(
    ImageId image, std::uint32_t width, std::uint32_t height,
    std::uint64_t row_bytes, std::span<const std::byte> pixels) {
    std::size_t slot_index = 0;
    std::uint64_t generation = 0;
    if (!split_image_id(image, slot_index, generation) ||
        slot_index >= slots_.size()) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    Slot& slot = slots_[slot_index];
    if (!slot.occupied || slot.generation != generation) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    if (slot.encoding != ImageResourceEncoding::bgra32_premultiplied ||
        width != slot.metadata.width || height != slot.metadata.height) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    const std::uint64_t tight_row_bytes = static_cast<std::uint64_t>(width) * 4U;
    if (row_bytes < tight_row_bytes ||
        row_bytes > std::numeric_limits<std::size_t>::max() ||
        height > std::numeric_limits<std::size_t>::max() / row_bytes ||
        pixels.size() != static_cast<std::size_t>(row_bytes) * height ||
        slot.encoded.size() != static_cast<std::size_t>(tight_row_bytes) * height) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    for (std::uint32_t row = 0; row < height; ++row) {
        std::copy_n(pixels.data() + static_cast<std::size_t>(row_bytes) * row,
                    static_cast<std::size_t>(tight_row_bytes),
                    slot.encoded.data() + static_cast<std::size_t>(tight_row_bytes) * row);
    }
    slot.content_hash = slot.content_hash ==
            std::numeric_limits<std::uint64_t>::max()
        ? 1U : slot.content_hash + 1U;
    slot.patched = {};
    ++revision_;
    return ImageLoadResult::success(image);
}

ImageLoadResult ImageRegistry::patch_bgra32_premultiplied(
    ImageId image, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height,
    std::uint64_t source_row_bytes, std::span<const std::byte> pixels) {
    const ImageLoadResult result = patch_pixels(image, x, y, width, height, source_row_bytes, pixels, true);
    return result;
}

ImageLoadResult ImageRegistry::patch_bgra32_premultiplied_in_place(
    ImageId image, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height,
    std::uint64_t source_row_bytes, std::span<const std::byte> pixels) {
    const ImageLoadResult result = patch_pixels(image, x, y, width, height, source_row_bytes, pixels, false);
    return result;
}

ImageLoadResult ImageRegistry::patch_pixels(
    ImageId image, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height,
    std::uint64_t source_row_bytes, std::span<const std::byte> pixels,
    bool advance_generation) {
    std::size_t slot_index = 0;
    std::uint64_t generation = 0;
    if (!split_image_id(image, slot_index, generation) ||
        slot_index >= slots_.size()) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    Slot& slot = slots_[slot_index];
    if (!slot.occupied || slot.generation != generation) {
        return ImageLoadResult::failure(ImageResourceError::stale_image_id);
    }
    if (slot.encoding != ImageResourceEncoding::bgra32_premultiplied) {
        return ImageLoadResult::failure(ImageResourceError::unsupported_color_format);
    }
    const std::uint64_t right = static_cast<std::uint64_t>(x) + width;
    const std::uint64_t bottom = static_cast<std::uint64_t>(y) + height;
    const std::uint64_t patch_row_bytes = static_cast<std::uint64_t>(width) * 4U;
    if (width == 0U || height == 0U || right > slot.metadata.width ||
        bottom > slot.metadata.height || source_row_bytes < patch_row_bytes ||
        source_row_bytes > std::numeric_limits<std::size_t>::max()) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }
    // Check the available span before multiplying an untrusted stride. The
    // last row needs only its pixels; trailing padding is deliberately optional.
    if (patch_row_bytes > pixels.size() ||
        static_cast<std::uint64_t>(height - 1U) >
            (pixels.size() - patch_row_bytes) / source_row_bytes) {
        return ImageLoadResult::failure(ImageResourceError::dimension_limit_exceeded);
    }

    const std::size_t destination_origin =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(slot.row_bytes) +
        static_cast<std::size_t>(x) * 4U;
    for (std::uint32_t row = 0; row < height; ++row) {
        std::copy_n(pixels.data() +
                        static_cast<std::size_t>(row) *
                            static_cast<std::size_t>(source_row_bytes),
                    static_cast<std::size_t>(patch_row_bytes),
                    slot.encoded.data() + destination_origin +
                        static_cast<std::size_t>(row) *
                            static_cast<std::size_t>(slot.row_bytes));
    }
    if (slot.patched.base_hash == 0U) {
        slot.patched.base_image = image.value;
        slot.patched.base_hash = slot.content_hash;
        slot.patched.x = x;
        slot.patched.y = y;
        slot.patched.width = width;
        slot.patched.height = height;
    } else {
        const std::uint32_t left = std::min(slot.patched.x, x);
        const std::uint32_t top = std::min(slot.patched.y, y);
        const std::uint64_t run_right =
            std::max(static_cast<std::uint64_t>(slot.patched.x) + slot.patched.width, right);
        const std::uint64_t run_bottom =
            std::max(static_cast<std::uint64_t>(slot.patched.y) + slot.patched.height, bottom);
        slot.patched.x = left;
        slot.patched.y = top;
        // Both edges lie within the image, so the extents fit 32 bits.
        slot.patched.width = static_cast<std::uint32_t>(run_right - left);
        slot.patched.height = static_cast<std::uint32_t>(run_bottom - top);
    }
    if (advance_generation) slot.generation = next_generation(slot.generation);
    // This is an opaque cache revision, as in the full-surface update path.
    // A bounded patch must not scan the rest of a potentially large canvas.
    slot.content_hash = slot.content_hash ==
            std::numeric_limits<std::uint64_t>::max()
        ? 1U : slot.content_hash + 1U;
    ++revision_;
    return ImageLoadResult::success(make_image_id(slot_index, slot.generation));
}

bool ImageRegistry::remove(ImageId image) noexcept {
    std::size_t slot_index = 0;
    std::uint64_t generation = 0;
    if (!split_image_id(image, slot_index, generation) || slot_index >= slots_.size()) {
        return false;
    }
    Slot& slot = slots_[slot_index];
    if (!slot.occupied || slot.generation != generation) {
        return false;
    }
    encoded_bytes_ -= slot.encoded.size();
    decoded_bytes_ -= slot.metadata.decoded_byte_count;
    --resource_count_;
    slot.occupied = false;
    slot.encoding = ImageResourceEncoding::png;
    slot.metadata = {};
    ImageBytes{}.swap(slot.encoded);
    slot.row_bytes = 0;
    slot.content_hash = 0;
    slot.patched = {};
    ++revision_;
    return true;
}

void ImageRegistry::clear() noexcept {
    if (resource_count_ == 0) {
        return;
    }
    for (Slot& slot : slots_) {
        if (slot.occupied) {
            slot.occupied = false;
            slot.encoding = ImageResourceEncoding::png;
            slot.metadata = {};
            ImageBytes{}.swap(slot.encoded);
            slot.row_bytes = 0;
            slot.content_hash = 0;
            slot.patched = {};
        }
    }
    resource_count_ = 0;
    encoded_bytes_ = 0;
    decoded_bytes_ = 0;
    ++revision_;
}

std::optional<ImageResourceView> ImageRegistry::find(ImageId image) const noexcept {
    std::size_t slot_index = 0;
    std::uint64_t generation = 0;
    if (!split_image_id(image, slot_index, generation) || slot_index >= slots_.size()) {
        return std::nullopt;
    }
    const Slot& slot = slots_[slot_index];
    if (!slot.occupied || slot.generation != generation) {
        return std::nullopt;
    }
    return ImageResourceView{image, slot.encoding, slot.metadata, slot.encoded,
                             slot.row_bytes, slot.content_hash, slot.patched};
}

std::vector<ImageId> ImageRegistry::image_ids() const {
    std::vector<ImageId> result;
    result.reserve(static_cast<std::size_t>(resource_count_));
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].occupied) {
            result.push_back(make_image_id(index, slots_[index].generation));
        }
    }
    return result;
}

ImageRegistrySnapshot ImageRegistry::snapshot() const noexcept {
    return {revision_, resource_count_, encoded_bytes_, decoded_bytes_};
}

const ImageRegistryLimits& ImageRegistry::limits() const noexcept {
    return limits_;
}

} // namespace gui_forms
