#pragma once

// The state a content surface shares with its consumer's threads
// (ContentFrameSink) and, for hosted content, with the reader of the hosted
// program's frames. The interface thread owns the surface; any thread may
// hold the channel. What is guarded by what:
// - the counters, the activity count and the revocation are atomics;
// - publish_mutex serializes publishers against each other (never taken by
//   the interface thread), and guards published_size;
// - request_mutex guards the request and the stop flag, and request_changed
//   wakes content threads waiting on them; the interface thread takes it
//   only to write a new request or a stop (a few stores);
// - semantics_mutex guards the semantic text, its generation, the notice
//   dispatcher and the listener; held only around moves of those fields.
// Lock order: none is taken while another is held.

#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>

namespace gui_forms::detail {

// Told on the interface thread, through the window's dispatcher, that the
// consumer published its first frame or new semantic text. One notice stands
// for every publication until the listener takes the text
// (take_content_semantics), which re-arms it.
class ContentChannelListener {
public:
    virtual ~ContentChannelListener() = default;
    virtual void content_notice() = 0;
};

struct ContentChannel final {
    explicit ContentChannel(std::shared_ptr<LiveSurface> live_surface) noexcept;
    ContentChannel(const ContentChannel&) = delete;
    ContentChannel& operator=(const ContentChannel&) = delete;

    // Fixed at creation; the LiveSurface is thread-safe on its own.
    const std::shared_ptr<LiveSurface> surface;

    std::atomic<bool> revoked{};
    // Frames published plus heartbeats: only grows (the watchdog's input).
    std::atomic<std::uint64_t> activity{};
    std::atomic<std::uint64_t> frames_published{};
    std::atomic<std::uint64_t> frames_refused{};
    std::atomic<std::uint64_t> frames_dropped{};
    std::atomic<std::uint64_t> heartbeats{};
    std::atomic<std::uint64_t> semantics_published{};
    std::atomic<std::uint64_t> semantics_refused{};
    std::atomic<std::uint64_t> after_revocation{};
    std::atomic<std::uint64_t> stale_transitions{};
    std::atomic<std::uint64_t> recoveries{};
    std::atomic<std::uint8_t> last_refusal{static_cast<std::uint8_t>(ContentPublishResult::published)};

    std::mutex publish_mutex;
    // The size of the surface's buffers as the last publisher left them.
    ContentPixelSize published_size{};

    mutable std::mutex request_mutex;
    mutable std::condition_variable request_changed;
    ContentRequest request{};
    // The cooperative stop of the content's operation (ContentSurface::
    // terminate, ContentWorker::request_stop): set once, never cleared.
    std::stop_source stop{};

    std::mutex semantics_mutex;
    ContentSemantics semantics{};
    std::uint64_t semantics_generation{};
    // Whether a notice is posted and not yet taken.
    bool notice_pending{};
    WindowDispatcher notice_dispatcher{};
    std::weak_ptr<ContentChannelListener> listener{};
};

// The publication path every producer takes (ContentFrameSink::publish, the
// hosted reader): validates, reshapes the surface when the size changed,
// copies into a write lease and publishes. Any thread but the interface
// thread.
[[nodiscard]] ContentPublishResult publish_content_frame(ContentChannel& channel, const ContentFrame& frame);
void record_content_heartbeat(ContentChannel& channel) noexcept;
[[nodiscard]] ContentSemanticsResult publish_content_semantics(ContentChannel& channel, ContentSemantics semantics);

// Interface thread: revokes the channel (every later call reports revoked)
// and wakes content threads waiting on a request. Never waits.
void revoke_content_channel(ContentChannel& channel) noexcept;
// Interface thread: asks for a stop and wakes content threads waiting on a
// request. Idempotent.
void request_content_stop(ContentChannel& channel) noexcept;
// Interface thread: a new device size or scale; the generation grows only
// when one of them changed. Returns whether it changed.
bool set_content_request(ContentChannel& channel, ContentPixelSize size, double scale) noexcept;
// Interface thread: where semantics notices go. An empty dispatcher posts
// nothing.
void set_content_listener(ContentChannel& channel, WindowDispatcher dispatcher,
                          std::weak_ptr<ContentChannelListener> listener);
// Interface thread: the newest semantic text, and whether it is newer than
// `seen_generation` (which it updates). Re-arms the notice.
[[nodiscard]] bool take_content_semantics(ContentChannel& channel, std::uint64_t& seen_generation,
                                          ContentSemantics& semantics);
[[nodiscard]] ContentSurfaceStatistics content_channel_statistics(const ContentChannel& channel) noexcept;

// Makes a channel over a new LiveSurface of `size` (at least one pixel on
// each axis); null when the surface cannot be made.
[[nodiscard]] std::shared_ptr<ContentChannel> make_content_channel(ContentPixelSize size, bool opaque);

// The library's own makers and readers of ContentFrameSink (it has a
// private constructor): the surfaces, the editing mode and the hosted
// reader.
class ContentSinkAccess final {
public:
    [[nodiscard]] static ContentFrameSink make(std::shared_ptr<ContentChannel> channel) noexcept;
    [[nodiscard]] static std::shared_ptr<ContentChannel> channel(const ContentFrameSink& sink) noexcept;
};

} // namespace gui_forms::detail
