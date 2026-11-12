#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <thread>
#include <limits>
#include <vector>

namespace {

using namespace gui_forms;

constexpr std::array<std::uint8_t, 70> one_pixel_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::byte> valid_bytes() {
    const std::span<const std::uint8_t, 70> source(one_pixel_png);
    const std::span<const std::byte, 70> bytes = std::as_bytes(source);
    return {bytes.begin(), bytes.end()};
}

class LoadPngOffThread final {
public:
    LoadPngOffThread(Window& window, const std::vector<std::byte>& bytes,
                     bool& rejected)
        : window_(window), bytes_(bytes), rejected_(rejected) {}

    void operator()() const {
        try {
            static_cast<void>(window_.load_png(bytes_));
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    Window& window_;
    const std::vector<std::byte>& bytes_;
    bool& rejected_;
};

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const std::byte value : bytes) {
        crc ^= std::to_integer<std::uint8_t>(value);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

void write_u32(std::span<std::byte> destination, std::uint32_t value) {
    destination[0] = static_cast<std::byte>(value >> 24U);
    destination[1] = static_cast<std::byte>(value >> 16U);
    destination[2] = static_cast<std::byte>(value >> 8U);
    destination[3] = static_cast<std::byte>(value);
}

void repair_chunk_crc(std::vector<std::byte>& bytes, std::size_t chunk_offset) {
    const std::uint32_t length =
        (std::to_integer<std::uint32_t>(bytes[chunk_offset]) << 24U) |
        (std::to_integer<std::uint32_t>(bytes[chunk_offset + 1]) << 16U) |
        (std::to_integer<std::uint32_t>(bytes[chunk_offset + 2]) << 8U) |
        std::to_integer<std::uint32_t>(bytes[chunk_offset + 3]);
    const std::uint32_t crc = crc32(std::span<const std::byte>(
        bytes.data() + chunk_offset + 4U, static_cast<std::size_t>(length) + 4U));
    write_u32(std::span<std::byte>(bytes.data() + chunk_offset + 8U + length, 4), crc);
}

std::vector<std::byte> chunk(const std::array<char, 4>& type,
                             std::span<const std::byte> data = {}) {
    std::vector<std::byte> result(12U + data.size());
    write_u32(std::span<std::byte>(result.data(), 4),
              static_cast<std::uint32_t>(data.size()));
    for (std::size_t index = 0; index < type.size(); ++index) {
        result[4U + index] = static_cast<std::byte>(type[index]);
    }
    for (std::size_t index = 0; index < data.size(); ++index) {
        result[8U + index] = data[index];
    }
    repair_chunk_crc(result, 0);
    return result;
}

void chunk_checksum_matches_bitwise_oracle() {
    const std::array<std::size_t, 8> lengths{0U, 1U, 2U, 255U, 256U, 257U, 4095U, 65536U};
    const std::vector<std::byte> original = valid_bytes();
    std::vector<std::byte> payload(65536U);
    for (const unsigned int seed : {0U, 73U, 255U}) {
        for (std::size_t index = 0U; index < payload.size(); ++index) {
            const unsigned int byte = (static_cast<unsigned int>(index) + seed) & 0xffU;
            payload[index] = static_cast<std::byte>(byte);
        }
        for (const std::size_t length : lengths) {
            // A legal unknown ancillary chunk preserves the real image payload.
            // chunk() uses the independent bit-at-a-time checksum oracle above.
            const std::span<const std::byte> data(payload.data(), length);
            const std::vector<std::byte> ancillary = chunk({'t', 'e', 'S', 't'}, data);
            std::vector<std::byte> encoded = original;
            encoded.insert(encoded.begin() + 33, ancillary.begin(), ancillary.end());
            const PngValidationResult accepted = validate_png(encoded);
            require(static_cast<bool>(accepted), "table checksum must agree with bitwise chunk oracle");
            const std::size_t checksum_byte = 33U + 8U + length;
            encoded[checksum_byte] ^= std::byte{1};
            require(validate_png(encoded).error == ImageResourceError::crc_mismatch,
                    "checksum mismatch must remain rejected for every oracle chunk length");
        }
    }
}

void parser_contract() {
    const std::vector<std::byte> bytes = valid_bytes();
    const PngValidationResult valid = validate_png(bytes);
    require(static_cast<bool>(valid), "valid PNG was rejected");
    require(valid.metadata.width == 1 && valid.metadata.height == 1,
            "IHDR dimensions were not retained");
    require(valid.metadata.bit_depth == 8 &&
                valid.metadata.color_type == PngColorType::truecolor_alpha,
            "IHDR color contract was not retained");
    require(valid.metadata.source_row_bytes == 4 &&
                valid.metadata.decoded_byte_count == 4,
            "row/decode budgets were computed incorrectly");

    require(validate_png({}).error == ImageResourceError::empty_input,
            "empty PNG did not return a typed error");
    std::vector<std::byte> bad_signature = bytes;
    bad_signature[0] ^= std::byte{1};
    require(validate_png(bad_signature).error == ImageResourceError::invalid_signature,
            "signature mutation was not rejected");

    std::vector<std::byte> bad_crc = bytes;
    bad_crc[44] ^= std::byte{1};
    require(validate_png(bad_crc).error == ImageResourceError::crc_mismatch,
            "chunk CRC mutation was not rejected");

    std::vector<std::byte> oversized = bytes;
    write_u32(std::span<std::byte>(oversized.data() + 16, 4), 4097);
    repair_chunk_crc(oversized, 8);
    require(validate_png(oversized).error == ImageResourceError::dimension_limit_exceeded,
            "oversized width was not rejected before decode");

    std::vector<std::byte> overflow = bytes;
    write_u32(std::span<std::byte>(overflow.data() + 16, 4), 0xffffffffU);
    write_u32(std::span<std::byte>(overflow.data() + 20, 4), 0xffffffffU);
    repair_chunk_crc(overflow, 8);
    ImageRegistryLimits widened;
    widened.maximum_width = 0xffffffffU;
    widened.maximum_height = 0xffffffffU;
    widened.maximum_pixels = std::numeric_limits<std::uint64_t>::max();
    widened.maximum_decoded_bytes_per_image =
        std::numeric_limits<std::uint64_t>::max();
    require(validate_png(overflow, widened).error ==
                ImageResourceError::dimension_limit_exceeded,
            "decoded RGBA byte multiplication was allowed to overflow");

    std::vector<std::byte> invalid_color = bytes;
    invalid_color[25] = std::byte{5};
    repair_chunk_crc(invalid_color, 8);
    require(validate_png(invalid_color).error == ImageResourceError::unsupported_color_format,
            "invalid PNG color type was accepted");

    std::vector<std::byte> profile = bytes;
    const std::array<std::byte, 3> profile_data{
        std::byte{'x'}, std::byte{0}, std::byte{0}};
    const std::vector<std::byte> iccp = chunk({'i', 'C', 'C', 'P'}, profile_data);
    profile.insert(profile.begin() + 33, iccp.begin(), iccp.end());
    require(validate_png(profile).error == ImageResourceError::unsupported_color_profile,
            "compressed ICC profile crossed the bounded color policy");

    // cICP: sRGB-equivalent values are accepted, others refused, and a
    // malformed chunk is invalid colour metadata.
    const std::array<std::byte, 4> cicp_srgb{std::byte{1}, std::byte{13}, std::byte{0}, std::byte{1}};
    const std::array<std::byte, 4> cicp_pq{std::byte{9}, std::byte{16}, std::byte{0}, std::byte{1}};
    const std::array<std::byte, 3> cicp_short{std::byte{1}, std::byte{13}, std::byte{0}};
    std::vector<std::byte> cicp_accepted = bytes;
    const std::vector<std::byte> srgb_points = chunk({'c', 'I', 'C', 'P'}, cicp_srgb);
    cicp_accepted.insert(cicp_accepted.begin() + 33, srgb_points.begin(), srgb_points.end());
    require(validate_png(cicp_accepted).error == ImageResourceError::none,
            "sRGB-equivalent cICP was refused");
    std::vector<std::byte> cicp_refused = bytes;
    const std::vector<std::byte> pq_points = chunk({'c', 'I', 'C', 'P'}, cicp_pq);
    cicp_refused.insert(cicp_refused.begin() + 33, pq_points.begin(), pq_points.end());
    require(validate_png(cicp_refused).error == ImageResourceError::unsupported_color_profile,
            "non-sRGB cICP was accepted");
    std::vector<std::byte> cicp_malformed = bytes;
    const std::vector<std::byte> short_points = chunk({'c', 'I', 'C', 'P'}, cicp_short);
    cicp_malformed.insert(cicp_malformed.begin() + 33, short_points.begin(), short_points.end());
    require(validate_png(cicp_malformed).error == ImageResourceError::invalid_color_metadata,
            "malformed cICP was accepted");

    std::vector<std::byte> unknown = bytes;
    const std::vector<std::byte> critical = chunk({'A', 'B', 'C', 'D'});
    unknown.insert(unknown.begin() + 33, critical.begin(), critical.end());
    require(validate_png(unknown).error == ImageResourceError::unknown_critical_chunk,
            "unknown critical PNG chunk was ignored");

    std::vector<std::byte> no_end(bytes.begin(), bytes.end() - 12);
    require(validate_png(no_end).error == ImageResourceError::missing_iend,
            "missing IEND was not rejected");
    std::vector<std::byte> trailing = bytes;
    trailing.push_back(std::byte{0});
    require(validate_png(trailing).error == ImageResourceError::trailing_data,
            "trailing data was not rejected");
}

void ownership_and_quota_contract() {
    const std::vector<std::byte> bytes = valid_bytes();
    ImageRegistry registry;
    const ImageLoadResult first = registry.load_png(bytes);
    require(static_cast<bool>(first), "registry rejected valid PNG");
    require(registry.find(first.image).has_value(), "new image ID did not resolve");
    require(registry.snapshot().resource_count == 1 &&
                registry.snapshot().encoded_bytes == bytes.size() &&
                registry.snapshot().decoded_bytes == 4,
            "registry accounting did not include the loaded resource");

    std::vector<std::byte> invalid = bytes;
    invalid[0] = std::byte{0};
    const ImageRegistrySnapshot before_invalid = registry.snapshot();
    const ImageLoadResult rejected = registry.replace_png(first.image, invalid);
    require(!rejected && rejected.error == ImageResourceError::invalid_signature,
            "invalid replacement did not report its parser error");
    require(registry.snapshot().revision == before_invalid.revision &&
                registry.find(first.image).has_value(),
            "failed replacement was not atomic");

    const ImageLoadResult replacement = registry.replace_png(first.image, bytes);
    require(replacement && replacement.image != first.image,
            "replacement did not advance the image generation");
    require(!registry.find(first.image).has_value() &&
                registry.find(replacement.image).has_value(),
            "stale image ID resolved after replacement");
    require(registry.replace_png(first.image, bytes).error ==
                ImageResourceError::stale_image_id,
            "stale replacement ID was not typed");
    require(registry.remove(replacement.image), "current image could not be removed");
    require(!registry.remove(replacement.image) && registry.snapshot().resource_count == 0 &&
                registry.snapshot().encoded_bytes == 0 &&
                registry.snapshot().decoded_bytes == 0,
            "removal was not deterministic and idempotent");
    const ImageLoadResult reused = registry.load_png(bytes);
    require(reused && reused.image != replacement.image,
            "slot reuse revived a stale image generation");

    ImageRegistryLimits count_limits;
    count_limits.maximum_resources = 1;
    ImageRegistry count_bounded(count_limits);
    require(static_cast<bool>(count_bounded.load_png(bytes)),
            "bounded registry rejected first image");
    require(count_bounded.load_png(bytes).error == ImageResourceError::resource_count_exceeded,
            "resource-count quota was not enforced");

    ImageRegistryLimits encoded_limits;
    encoded_limits.maximum_total_encoded_bytes = bytes.size();
    ImageRegistry encoded_bounded(encoded_limits);
    require(static_cast<bool>(encoded_bounded.load_png(bytes)),
            "encoded quota rejected first image");
    require(encoded_bounded.load_png(bytes).error ==
                ImageResourceError::registry_encoded_limit_exceeded,
            "aggregate encoded-byte quota was not enforced");

    ImageRegistryLimits decoded_limits;
    decoded_limits.maximum_total_decoded_bytes = 4;
    ImageRegistry decoded_bounded(decoded_limits);
    require(static_cast<bool>(decoded_bounded.load_png(bytes)),
            "decoded quota rejected first image");
    require(decoded_bounded.load_png(bytes).error ==
                ImageResourceError::registry_decoded_limit_exceeded,
            "aggregate decoded-byte quota was not enforced");
}

void deterministic_mutation_oracle() {
    const std::vector<std::byte> source = valid_bytes();
    std::vector<ImageResourceError> first;
    std::vector<ImageResourceError> second;
    for (std::vector<ImageResourceError>* outcomes : {&first, &second}) {
        (*outcomes).reserve(source.size());
        for (std::size_t index = 0; index < source.size(); ++index) {
            std::vector<std::byte> mutation = source;
            mutation[index] ^= std::byte{0x01};
            const ImageResourceError error = validate_png(mutation).error;
            require(!image_resource_error_name(error).empty(),
                    "parser returned an unnamed result");
            (*outcomes).push_back(error);
        }
    }
    require(first == second, "PNG mutation oracle was nondeterministic");
}

void owned_pixel_surface_contract() {
    ImageRegistry registry;
    const std::array<std::byte, 24> padded{
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
        std::byte{99}, std::byte{99}, std::byte{99}, std::byte{99},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{88}, std::byte{88}, std::byte{88}, std::byte{88},
    };
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(
        2, 2, 12, padded);
    require(static_cast<bool>(loaded), "owned BGRA surface was rejected");
    const std::optional<ImageResourceView> resource = registry.find(loaded.image);
    require(resource.has_value() &&
                (*resource).encoding == ImageResourceEncoding::bgra32_premultiplied &&
                (*resource).row_bytes == 8 && (*resource).encoded.size() == 16 &&
                (*resource).metadata.decoded_byte_count == 16,
            "owned BGRA surface was not tightly normalized");
    require((*resource).encoded[8] == std::byte{255},
            "owned BGRA surface retained source row padding");
    require(registry.load_bgra32_premultiplied(2, 2, 7, padded).error ==
                ImageResourceError::dimension_limit_exceeded,
            "owned BGRA surface accepted an undersized stride");

    std::array<std::byte, 16> replacement{};
    const ImageLoadResult replaced = registry.replace_bgra32_premultiplied(
        loaded.image, 2, 2, 8, replacement);
    require(replaced && replaced.image != loaded.image &&
                !registry.find(loaded.image).has_value(),
            "owned BGRA replacement did not stale its prior generation");

    const std::array<std::byte, 4> green{
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    const ImageRegistrySnapshot before_patch = registry.snapshot();
    const ImageLoadResult patched = registry.patch_bgra32_premultiplied(
        replaced.image, 1, 0, 1, 1, 4, green);
    require(patched && patched.image != replaced.image &&
                !registry.find(replaced.image).has_value(),
            "bounded BGRA patch did not advance the resource generation");
    const std::optional<ImageResourceView> patched_view = registry.find(patched.image);
    require(patched_view && (*patched_view).encoded[4] == std::byte{0} &&
                (*patched_view).encoded[5] == std::byte{255} &&
                (*patched_view).encoded[7] == std::byte{255},
            "bounded BGRA patch did not update the selected pixel");
    require(registry.patch_bgra32_premultiplied(
                patched.image, 2, 0, 1, 1, 4, green).error ==
                ImageResourceError::dimension_limit_exceeded &&
                registry.snapshot().revision == before_patch.revision + 1U &&
                registry.find(patched.image).has_value(),
            "failed bounded BGRA patch was not atomic");
}

void bounded_patch_stride_and_revision_contract() {
    ImageRegistry registry;
    const std::array<std::byte, 24> original{};
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(2, 3, 8, original);
    require(static_cast<bool>(loaded), "stride fixture load failed");
    const std::uint64_t initial_revision = registry.snapshot().revision;
    const std::uint64_t initial_stamp = (*registry.find(loaded.image)).content_hash;
    const std::array<std::byte, 4> short_patch{};
    require(registry.patch_bgra32_premultiplied(
                loaded.image, 0, 0, 1, 3, std::uint64_t{1} << 63U, short_patch).error ==
                ImageResourceError::dimension_limit_exceeded,
            "overflowing patch stride was accepted");
    require(registry.snapshot().revision == initial_revision &&
                (*registry.find(loaded.image)).content_hash == initial_stamp,
            "rejected stride changed registry state");

    // Two rows with padding between them, but no padding after the final row.
    std::array<std::byte, 12> padded_patch{};
    padded_patch[0] = std::byte{9};
    padded_patch[8] = std::byte{17};
    const ImageLoadResult patched = registry.patch_bgra32_premultiplied(
        loaded.image, 1, 1, 1, 2, 8, padded_patch);
    require(static_cast<bool>(patched), "patch required nonexistent final-row padding");
    const ImageResourceView view = *registry.find(patched.image);
    require(view.content_hash != initial_stamp && !registry.find(loaded.image),
            "patch failed to invalidate the renderer cache identity");
    for (std::size_t index = 0; index < view.encoded.size(); ++index) {
        const std::byte expected = index == 12U ? std::byte{9} :
                                  index == 20U ? std::byte{17} : std::byte{0};
        require(view.encoded[index] == expected, "patch changed pixels outside its rectangle");
    }
}

void window_thread_boundary() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("resource.root"));
    Window window(root, {32.0, 32.0});
    const std::vector<std::byte> bytes = valid_bytes();
    bool rejected = false;
    std::thread worker(LoadPngOffThread(window, bytes, rejected));
    worker.join();
    require(rejected &&
                window.metrics_snapshot().rejected_wrong_thread_operations == 1,
            "window image ownership did not enforce the UI thread");
}

void scoped_window_replacement_damage() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("resource.damage.root"));
    std::shared_ptr<gui_forms::Control> consumer = make_control<Control>(StableId("resource.damage.consumer"));
    (*root).set_requested_bounds({0.0, 0.0, 32.0, 32.0});
    (*consumer).set_requested_bounds({5.0, 6.0, 10.0, 8.0});
    (*root).add_child(consumer);
    Window window(root, {32.0, 32.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());

    const std::vector<std::byte> bytes = valid_bytes();
    const ImageLoadResult loaded = window.load_png(bytes);
    require(static_cast<bool>(loaded), "window rejected scoped-damage fixture PNG");
    require(window.take_damage().empty(),
            "loading an unreferenced PNG unexpectedly damaged the window");

    const ImageLoadResult replaced = window.replace_png(loaded.image, bytes, *consumer);
    require(static_cast<bool>(replaced), "scoped PNG replacement was rejected");
    const DamageRegion damage = window.take_damage();
    require(damage.rectangle_count() == 1 &&
                damage.bounds() == Rect{5.0, 6.0, 10.0, 8.0},
            "scoped PNG replacement damaged more than its consumer bounds");
    require(damage.area() == 80.0,
            "scoped PNG replacement reported the wrong damaged area");

    const std::array<std::byte, 16> raw{};
    const ImageLoadResult raw_image = window.load_bgra32_premultiplied(
        2, 2, 8, raw);
    require(raw_image && window.take_damage().empty(),
            "loading an unreferenced raw surface unexpectedly caused damage");
    const std::array<std::byte, 4> pixel{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    const ImageLoadResult raw_patch = window.patch_bgra32_premultiplied(
        raw_image.image, 1, 1, 1, 1, 4, pixel, *consumer,
        {2.0, 1.0, 3.0, 2.0});
    require(static_cast<bool>(raw_patch),
            "scoped raw-surface patch was rejected");
    const DamageRegion patch_damage = window.take_damage();
    require(patch_damage.rectangle_count() == 1 &&
                patch_damage.bounds() == Rect{7.0, 7.0, 3.0, 2.0},
            "scoped raw-surface patch did not retain exact local damage");
}

} // namespace

// A moved-from registry is empty with zero accounting and can be reused up
// to its limits; both registries take a new revision (sanitizer review,
// 2026-10-11).
void move_contract() {
    const std::vector<std::byte> bytes = valid_bytes();
    ImageRegistryLimits limits{};
    limits.maximum_resources = 1;
    ImageRegistry source(limits);
    const ImageLoadResult loaded = source.load_png(bytes);
    require(static_cast<bool>(loaded), "move fixture loads");
    const std::uint64_t source_revision = source.snapshot().revision;

    ImageRegistry constructed(std::move(source));
    require(constructed.find(loaded.image).has_value() && constructed.snapshot().resource_count == 1 &&
                constructed.snapshot().revision > source_revision,
            "move construction kept the image and advanced the revision");
    require(source.image_ids().empty() && source.snapshot().resource_count == 0 &&
                source.snapshot().encoded_bytes == 0 && source.snapshot().decoded_bytes == 0 &&
                source.snapshot().revision > source_revision,
            "moved-from registry is empty with zero accounting");
    require(static_cast<bool>(source.load_png(bytes)), "moved-from registry accepts a load within its limit");

    ImageRegistry assigned(limits);
    const std::uint64_t replaced_revision = assigned.snapshot().revision;
    const std::uint64_t moved_revision = constructed.snapshot().revision;
    assigned = std::move(constructed);
    require(assigned.find(loaded.image).has_value() && assigned.snapshot().resource_count == 1 &&
                assigned.snapshot().revision > replaced_revision && assigned.snapshot().revision > moved_revision,
            "move assignment kept the image and advanced past both revisions");
    require(constructed.image_ids().empty() && constructed.snapshot().resource_count == 0 &&
                constructed.snapshot().encoded_bytes == 0 && constructed.snapshot().decoded_bytes == 0,
            "move-assigned-from registry is empty with zero accounting");
    require(static_cast<bool>(constructed.load_png(bytes)), "move-assigned-from registry accepts a load");

    ImageRegistry& alias = assigned;
    const ImageRegistrySnapshot before_self = assigned.snapshot();
    assigned = std::move(alias);
    require(assigned.find(loaded.image).has_value() && assigned.snapshot().revision == before_self.revision &&
                assigned.snapshot().resource_count == 1,
            "self-move leaves the registry unchanged");
}

int main() {
    chunk_checksum_matches_bitwise_oracle();
    parser_contract();
    ownership_and_quota_contract();
    move_contract();
    deterministic_mutation_oracle();
    owned_pixel_surface_contract();
    bounded_patch_stride_and_revision_contract();
    window_thread_boundary();
    scoped_window_replacement_damage();
    return 0;
}
