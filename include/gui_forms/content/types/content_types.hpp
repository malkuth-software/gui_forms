#pragma once

// The content boundary's values (owner decisions 2026-11-13, "Content editing
// mode and the integrity boundary"): what a consumer hands GUI.Forms from its
// own execution context, the limits every hand-over is validated against, and
// what a content surface reports back.
//
// The boundary is an integrity boundary, not a security boundary: content
// that fails cannot crash or corrupt the application, and the interface stays
// responsive and usable. It does not govern what the consumer's own modules
// do with direct access to the process (GUI.Forms is not a hypervisor).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

// The largest frame a consumer may publish: 16384 pixels on either axis and
// 256 MiB of pixels in all. Larger frames are refused before anything is
// allocated or copied.
inline constexpr std::uint32_t maximum_content_frame_extent = 16384U;
inline constexpr std::uint64_t maximum_content_frame_bytes = 256ULL * 1024ULL * 1024ULL;
// The most UTF-8 bytes of semantic text one publication may carry (1 MiB).
inline constexpr std::size_t maximum_content_semantic_text_bytes = 1024U * 1024U;
// Bytes per pixel of the one pixel format (LiveSurfacePixelFormat).
inline constexpr std::uint64_t content_frame_pixel_bytes = 4U;

// A device size in whole pixels.
struct GUI_FORMS_API ContentPixelSize final {
    std::uint32_t width{};
    std::uint32_t height{};
    [[nodiscard]] bool empty() const noexcept {
        const bool result = width == 0U || height == 0U;
        return result;
    }
    friend constexpr bool operator==(const ContentPixelSize& left, const ContentPixelSize& right) noexcept {
        const bool same = left.width == right.width && left.height == right.height;
        return same;
    }
};

// A rectangle of a frame in whole device pixels: x and y are the top-left
// pixel, width and height the extent. An empty rectangle names the whole
// frame where a damage is expected.
struct GUI_FORMS_API ContentPixelRect final {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
    [[nodiscard]] bool empty() const noexcept {
        const bool result = width == 0U || height == 0U;
        return result;
    }
    friend constexpr bool operator==(const ContentPixelRect& left, const ContentPixelRect& right) noexcept {
        const bool same = left.x == right.x && left.y == right.y && left.width == right.width &&
                          left.height == right.height;
        return same;
    }
};

// One finished frame, described by the consumer for one publish call
// (ContentFrameSink::publish). `pixels` is borrowed for the call only: the
// sink copies what it accepts into its own buffer before it returns, and
// reads nothing else. Rows run top to bottom, `row_bytes` apart; a row holds
// width pixels of `pixel_format` and may be followed by padding the sink
// never reads. The last row need not carry padding: `pixels` must hold at
// least row_bytes * (height - 1) + width * 4 bytes.
struct GUI_FORMS_API ContentFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t row_bytes{};
    LiveSurfacePixelFormat pixel_format{LiveSurfacePixelFormat::bgra32_premultiplied_srgb};
    // The consumer promises every pixel has alpha 255. The promise is not
    // checked per pixel (it costs a pass over the frame); a broken promise
    // shows whatever lies under the surface, and corrupts nothing.
    bool opaque{};
    // The pixels that differ from the previous frame of the same size, in
    // this frame's pixels; empty for the whole frame. A damage that does not
    // lie inside the frame is refused.
    ContentPixelRect damage{};
    std::span<const std::byte> pixels{};
};

// What one publication did. Every result but `published` leaves the surface
// showing what it showed before; none of them changes the consumer's buffer.
enum class ContentPublishResult : std::uint8_t {
    published,
    // Every buffer is held by readers: the frame was not copied; publish the
    // next one (a high-rate producer drops this one).
    dropped_busy,
    // The surface closed the channel (close_content, new content, the
    // surface left its window, or termination): nothing will be shown again.
    revoked,
    // Width or height zero.
    refused_empty,
    // An axis above maximum_content_frame_extent, or more pixel bytes than
    // maximum_content_frame_bytes.
    refused_too_large,
    // row_bytes smaller than width * 4.
    refused_row_bytes,
    // `pixels` shorter than the frame it describes, or the extent does not
    // fit the address space.
    refused_extent,
    // A pixel format the surface does not know.
    refused_format,
    // A damage rectangle outside the frame.
    refused_damage,
    // The surface could not allocate a buffer of the frame's size: the
    // operation's named failure (house style 2); the application goes on.
    failed_allocation,
};

// The result's name, for logs and tests ("published", "refused_extent").
[[nodiscard]] GUI_FORMS_API std::string_view content_publish_result_name(ContentPublishResult result) noexcept;

// Checks a frame against the limits without reading its pixels: shape,
// format, row stride, the extent `pixels` must cover (all products checked
// for overflow first) and the damage. `published` when the frame may be
// copied. The sink, the hosted-mode reader and the editing mode all call it
// before they touch a byte.
[[nodiscard]] GUI_FORMS_API ContentPublishResult validate_content_frame(const ContentFrame& frame) noexcept;

// The pixel bytes a frame of `width` by `height` with `row_bytes` occupies:
// row_bytes * (height - 1) + width * 4. Zero when the product does not fit
// in 64 bits or either axis is zero (callers check validate_content_frame
// first).
[[nodiscard]] GUI_FORMS_API std::uint64_t content_frame_extent_bytes(std::uint32_t width, std::uint32_t height,
                                                                     std::uint64_t row_bytes) noexcept;

// Where a content surface's content is in its life, as the interface thread
// last saw it.
enum class ContentSurfaceStatus : std::uint8_t {
    // No content is connected (before open_content, after close_content).
    disconnected,
    // Content is connected and has published no frame yet.
    waiting,
    // Content publishes, or reports itself alive, within the watchdog's
    // interval.
    live,
    // The watchdog saw no frame and no heartbeat for longer than its
    // interval. The last good frame stays on screen, veiled; the surface
    // returns to live on the next frame or heartbeat.
    stale,
    // A termination was asked for (ContentSurface::terminate) and the
    // content has not yet finished.
    terminating,
    // The content finished: it returned, or it honoured a termination.
    ended,
    // The content did not finish within the termination's bounded wait, or
    // a hosted program was killed. Its channel is revoked; whatever it does
    // from now on reaches nothing the interface reads.
    abandoned,
    // The content failed: its program threw, or a hosted program crashed or
    // broke the frame protocol. The last good frame stays on screen.
    failed,
};

[[nodiscard]] GUI_FORMS_API std::string_view content_surface_status_name(ContentSurfaceStatus status) noexcept;

// The text a consumer supplies for its surface, so that what it draws is not
// invisible to assistive technology (its pixels carry no text). `name` is the
// surface's accessible name, `text` the text it shows (published as the
// node's value), `description` a longer description. All three are UTF-8.
struct GUI_FORMS_API ContentSemantics final {
    std::string name{};
    std::string text{};
    std::string description{};
    friend bool operator==(const ContentSemantics& left, const ContentSemantics& right) noexcept {
        const bool same = left.name == right.name && left.text == right.text && left.description == right.description;
        return same;
    }
};

enum class ContentSemanticsResult : std::uint8_t {
    published,
    revoked,
    // A field is not valid UTF-8.
    refused_invalid_utf8,
    // The three fields hold more than maximum_content_semantic_text_bytes.
    refused_too_long,
};

// Checks semantic text without keeping it.
[[nodiscard]] GUI_FORMS_API ContentSemanticsResult validate_content_semantics(const ContentSemantics& semantics) noexcept;

// Counters of one content channel, written by the consumer's threads and the
// interface thread, read by anyone.
struct GUI_FORMS_API ContentSurfaceStatistics final {
    std::uint64_t frames_published{};
    std::uint64_t frames_refused{};
    std::uint64_t frames_dropped{};
    std::uint64_t heartbeats{};
    // The watchdog's transitions to stale and back to live.
    std::uint64_t stale_transitions{};
    std::uint64_t recoveries{};
    std::uint64_t semantics_published{};
    std::uint64_t semantics_refused{};
    // Publications after the channel was revoked.
    std::uint64_t after_revocation{};
    // The newest refusal, `published` while there was none.
    ContentPublishResult last_refusal{ContentPublishResult::published};
};

} // namespace gui_forms
