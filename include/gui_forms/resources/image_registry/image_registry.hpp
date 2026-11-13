#pragma once

#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

namespace detail { struct ImageRegistryAccess; struct ImageRegistryLiveness; }

// Renderer-neutral ownership for validated PNG files and premultiplied
// BGRA bytes. ImageId values are generational; views expire at
// the next registry mutation.
class GUI_FORMS_API ImageRegistry final {
public:
    explicit ImageRegistry(ImageRegistryLimits limits = {});
    ~ImageRegistry();
    // A moved-from registry is empty, with zero accounting, and may be
    // reused. Both registries take a new revision, so a consumer synchronized
    // to either one's previous revision resynchronizes.
    ImageRegistry(ImageRegistry&&) noexcept;
    ImageRegistry& operator=(ImageRegistry&&) noexcept;
    ImageRegistry(const ImageRegistry&) = delete;
    ImageRegistry& operator=(const ImageRegistry&) = delete;

    // Admits an encoded image file, its format chosen by its signature:
    // PNG is the only one admitted. Its framing is validated (validate_png)
    // and its dimensions held to the limits; a file of no admitted format is
    // invalid_signature.
    [[nodiscard]] ImageLoadResult load_image(std::span<const std::byte> encoded);
    // As load_image, for PNG files only.
    [[nodiscard]] ImageLoadResult load_png(std::span<const std::byte> encoded);
    [[nodiscard]] ImageLoadResult load_bgra32_premultiplied(
        std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
        std::span<const std::byte> pixels);
    // Replaces an image's contents with an encoded file, as load_image
    // admits it; a refusal leaves the image unchanged.
    [[nodiscard]] ImageLoadResult replace_image(
        ImageId image, std::span<const std::byte> encoded);
    // As replace_image, for PNG files only.
    [[nodiscard]] ImageLoadResult replace_png(
        ImageId image, std::span<const std::byte> encoded);
    [[nodiscard]] ImageLoadResult replace_bgra32_premultiplied(
        ImageId image, std::uint32_t width, std::uint32_t height,
        std::uint64_t row_bytes, std::span<const std::byte> pixels);
    [[nodiscard]] ImageLoadResult update_bgra32_premultiplied(
        ImageId image, std::uint32_t width, std::uint32_t height,
        std::uint64_t row_bytes, std::span<const std::byte> pixels);
    // Writes the pixels of a rectangle of a premultiplied BGRA image.
    // Returns the identity that replaces `image`: its generation advanced,
    // so `image` is stale from then on and every later call naming it
    // returns stale_image_id, as the replacements' old identities do. Keep
    // the returned identity. Refusals change nothing.
    [[nodiscard]] ImageLoadResult patch_bgra32_premultiplied(
        ImageId image, std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height,
        std::uint64_t source_row_bytes, std::span<const std::byte> pixels);
    // As patch_bgra32_premultiplied, but `image` stays valid: its generation
    // does not advance, so a display list that recorded it draws the new
    // pixels at its next replay. The content revision advances and the
    // patch run is recorded as patch_bgra32_premultiplied records it, with
    // base_image the unchanged ImageId, so a renderer copies only the run's
    // rectangle. The caller damages whatever shows the patched rectangle
    // (ImageAtlas damages its slot's users); nothing else need repaint, as
    // every other pixel is unchanged. Returns `image`, or the refusal
    // patch_bgra32_premultiplied would give, changing nothing.
    [[nodiscard]] ImageLoadResult patch_bgra32_premultiplied_in_place(
        ImageId image, std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height,
        std::uint64_t source_row_bytes, std::span<const std::byte> pixels);
    [[nodiscard]] bool remove(ImageId image) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::optional<ImageResourceView> find(
        ImageId image) const noexcept;
    [[nodiscard]] std::vector<ImageId> image_ids() const;
    [[nodiscard]] ImageRegistrySnapshot snapshot() const noexcept;
    [[nodiscard]] const ImageRegistryLimits& limits() const noexcept;

private:
    friend struct detail::ImageRegistryAccess;
    // An image's bytes, counted under AllocationSubsystem::image_resources.
    using ImageBytes = std::vector<std::byte, detail::CountedAllocator<std::byte, AllocationSubsystem::image_resources>>;
    struct Slot;
    struct EncodedImage;
    [[nodiscard]] EncodedImage validate_encoded(
        std::span<const std::byte> encoded, bool png_only) const noexcept;
    [[nodiscard]] ImageLoadResult load_encoded(
        std::span<const std::byte> encoded, bool png_only);
    [[nodiscard]] ImageLoadResult replace_encoded(
        ImageId image, std::span<const std::byte> encoded, bool png_only);
    [[nodiscard]] ImageLoadResult store_new(
        ImageResourceEncoding encoding, ImageBytes encoded,
        ImageMetadata metadata, std::uint64_t row_bytes,
        std::uint64_t content_hash);
    [[nodiscard]] ImageResourceError registry_quota_error(
        std::uint64_t encoded_bytes, std::uint64_t decoded_bytes,
        std::uint64_t replaced_encoded = 0,
        std::uint64_t replaced_decoded = 0) const noexcept;
    // The shared body of the two patches: validates, copies the pixels and
    // records the patch run; advances the generation only when
    // `advance_generation`.
    [[nodiscard]] ImageLoadResult patch_pixels(
        ImageId image, std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height,
        std::uint64_t source_row_bytes, std::span<const std::byte> pixels,
        bool advance_generation);
    // Empties this registry after its contents moved elsewhere.
    void release_moved_contents() noexcept;

    ImageRegistryLimits limits_;
    std::vector<Slot> slots_;
    std::uint64_t revision_{};
    std::uint64_t resource_count_{};
    std::uint64_t encoded_bytes_{};
    std::uint64_t decoded_bytes_{};
    // Made when an owner of images first asks (ImageRegistryAccess); it
    // names the registry holding these images.
    std::shared_ptr<detail::ImageRegistryLiveness> liveness_{};
};

} // namespace gui_forms
