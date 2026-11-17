#include "gui_forms/content/types/content_types.hpp"

#include "gui_forms/text/types/text_types.hpp"

#include <limits>

namespace gui_forms {

std::string_view content_publish_result_name(ContentPublishResult result) noexcept {
    switch (result) {
    case ContentPublishResult::published: return "published";
    case ContentPublishResult::dropped_busy: return "dropped_busy";
    case ContentPublishResult::revoked: return "revoked";
    case ContentPublishResult::refused_empty: return "refused_empty";
    case ContentPublishResult::refused_too_large: return "refused_too_large";
    case ContentPublishResult::refused_row_bytes: return "refused_row_bytes";
    case ContentPublishResult::refused_extent: return "refused_extent";
    case ContentPublishResult::refused_format: return "refused_format";
    case ContentPublishResult::refused_damage: return "refused_damage";
    case ContentPublishResult::failed_allocation: return "failed_allocation";
    }
    // A value outside the enumeration (a cast from a foreign integer).
    return "unknown";
}

std::string_view content_surface_status_name(ContentSurfaceStatus status) noexcept {
    switch (status) {
    case ContentSurfaceStatus::disconnected: return "disconnected";
    case ContentSurfaceStatus::waiting: return "waiting";
    case ContentSurfaceStatus::live: return "live";
    case ContentSurfaceStatus::stale: return "stale";
    case ContentSurfaceStatus::terminating: return "terminating";
    case ContentSurfaceStatus::ended: return "ended";
    case ContentSurfaceStatus::abandoned: return "abandoned";
    case ContentSurfaceStatus::failed: return "failed";
    }
    return "unknown";
}

std::uint64_t content_frame_extent_bytes(std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes) noexcept {
    if (width == 0U || height == 0U) return 0U;
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t full_rows = static_cast<std::uint64_t>(height) - 1U;
    // row_bytes * full_rows, refusing an overflow.
    if (full_rows != 0U && row_bytes > maximum / full_rows) return 0U;
    const std::uint64_t leading = row_bytes * full_rows;
    const std::uint64_t last_row = static_cast<std::uint64_t>(width) * content_frame_pixel_bytes;
    if (leading > maximum - last_row) return 0U;
    const std::uint64_t extent = leading + last_row;
    return extent;
}

ContentPublishResult validate_content_frame(const ContentFrame& frame) noexcept {
    if (frame.width == 0U || frame.height == 0U) return ContentPublishResult::refused_empty;
    if (frame.width > maximum_content_frame_extent || frame.height > maximum_content_frame_extent) {
        return ContentPublishResult::refused_too_large;
    }
    // Both axes are at most 2^14, so the product and the byte count fit.
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(frame.width) * static_cast<std::uint64_t>(frame.height);
    const std::uint64_t pixel_bytes = pixel_count * content_frame_pixel_bytes;
    if (pixel_bytes > maximum_content_frame_bytes) return ContentPublishResult::refused_too_large;
    if (frame.pixel_format != LiveSurfacePixelFormat::bgra32_premultiplied_srgb) {
        return ContentPublishResult::refused_format;
    }
    const std::uint64_t row_minimum = static_cast<std::uint64_t>(frame.width) * content_frame_pixel_bytes;
    if (frame.row_bytes < row_minimum) return ContentPublishResult::refused_row_bytes;
    const std::uint64_t extent = content_frame_extent_bytes(frame.width, frame.height, frame.row_bytes);
    if (extent == 0U) return ContentPublishResult::refused_extent;
    // The span's size is what the consumer's memory holds; the sink reads no
    // byte past it.
    const std::uint64_t available = static_cast<std::uint64_t>(frame.pixels.size());
    if (frame.pixels.data() == nullptr || available < extent) return ContentPublishResult::refused_extent;
    if (!frame.damage.empty()) {
        // x + width and y + height in 64 bits: no 32-bit wrap.
        const std::uint64_t right = static_cast<std::uint64_t>(frame.damage.x) + frame.damage.width;
        const std::uint64_t bottom = static_cast<std::uint64_t>(frame.damage.y) + frame.damage.height;
        if (right > frame.width || bottom > frame.height) return ContentPublishResult::refused_damage;
    }
    return ContentPublishResult::published;
}

ContentSemanticsResult validate_content_semantics(const ContentSemantics& semantics) noexcept {
    const std::size_t name_bytes = semantics.name.size();
    const std::size_t text_bytes = semantics.text.size();
    const std::size_t description_bytes = semantics.description.size();
    // Each term is at most a string's maximum size; compare piecewise so the
    // sum cannot wrap.
    const std::size_t limit = maximum_content_semantic_text_bytes;
    if (name_bytes > limit || text_bytes > limit - name_bytes ||
        description_bytes > limit - name_bytes - text_bytes) {
        return ContentSemanticsResult::refused_too_long;
    }
    const Utf8ValidationResult name_check = validate_utf8(semantics.name);
    const Utf8ValidationResult text_check = validate_utf8(semantics.text);
    const Utf8ValidationResult description_check = validate_utf8(semantics.description);
    if (!name_check.valid() || !text_check.valid() || !description_check.valid()) {
        return ContentSemanticsResult::refused_invalid_utf8;
    }
    return ContentSemanticsResult::published;
}

} // namespace gui_forms
