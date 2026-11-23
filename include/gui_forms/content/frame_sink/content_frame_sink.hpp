#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/types/content_types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stxlib/stop_token.hpp>

namespace gui_forms {

namespace detail {
struct ContentChannel;
class ContentSinkAccess;
}

class ContentSurface;

// What the surface asks of its content: the device size it shows at and the
// display scale, under a generation that grows with every change.
struct GUI_FORMS_API ContentRequest final {
    ContentPixelSize size{};
    double scale{1.0};
    std::uint64_t generation{};
};

// The consumer's end of a content channel (ContentSurface::open_content): a
// handle any thread may hold, copy and call, for as long as it likes. It
// shares the channel's state, never the surface or its window: once the
// surface revokes the channel every call reports `revoked` and touches
// nothing the interface reads.
//
// Execution: every member is callable from any thread. publish copies the
// accepted frame into a surface buffer on the calling thread; two threads
// publishing at once are serialized against each other, never against the
// interface thread, which only reads the newest finished frame and never
// waits for a publication (LiveSurface). Nothing here calls back into the
// consumer.
class GUI_FORMS_API ContentFrameSink final {
public:
    // An empty sink: every publication reports `revoked`.
    ContentFrameSink() = default;

    // Validates `frame` (validate_content_frame), then copies it into a free
    // buffer and makes it the surface's newest frame. A frame of another size
    // than the last replaces the surface's buffers first (failed_allocation
    // when that fails; the previous frame stays). The first frame, and any
    // frame after a stale period, also counts as activity for the watchdog.
    [[nodiscard]] ContentPublishResult publish(const ContentFrame& frame);
    // Reports the content alive without a new frame, for content that
    // publishes only when something changes: the watchdog counts it as
    // activity. Does nothing once revoked.
    void heartbeat() noexcept;
    // Replaces the surface's semantic text (ContentSemantics) after
    // validate_content_semantics. The interface thread picks it up through
    // its window's dispatcher; the newest publication wins.
    [[nodiscard]] ContentSemanticsResult publish_semantics(ContentSemantics semantics);

    // The newest request (device size, scale and generation); a zero size
    // before the surface has been arranged in a window.
    [[nodiscard]] ContentRequest request() const noexcept;
    // Blocks the calling (content) thread until the request's generation
    // passes `seen_generation`, the channel is revoked, a stop is requested
    // (stop_requested), or `timeout` passes. Returns the request then. The
    // interface thread never calls it.
    [[nodiscard]] ContentRequest wait_for_request(std::uint64_t seen_generation,
                                                  std::chrono::milliseconds timeout) const;

    [[nodiscard]] bool revoked() const noexcept;
    // The cooperative stop of the content's operation: fires when the
    // surface asks the content to stop (ContentSurface::terminate, its
    // destruction, a ContentWorker's stop). Content polls it or waits on it;
    // GUI.Forms never stops a thread any other way. An empty sink's token
    // never fires.
    [[nodiscard]] stxlib::StopToken stop_token() const noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] ContentSurfaceStatistics statistics() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return channel_ != nullptr; }

private:
    friend class ContentSurface;
    friend class detail::ContentSinkAccess;
    explicit ContentFrameSink(std::shared_ptr<detail::ContentChannel> channel) noexcept;

    std::shared_ptr<detail::ContentChannel> channel_;
};

} // namespace gui_forms
