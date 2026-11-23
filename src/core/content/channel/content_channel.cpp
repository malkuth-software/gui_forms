#include "content_channel.hpp"

#include <chrono>
#include <cstring>
#include <functional>
#include <stxlib/stop_token.hpp>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms::detail {

namespace {

// Posted to the interface thread when new semantic text arrives: tells the
// surface, if it is still there. Holds the listener weakly, so a surface
// destroyed before the task runs is not reached.
struct ContentNotice final {
    std::weak_ptr<ContentChannelListener> listener;
    void operator()() const {
        const std::shared_ptr<ContentChannelListener> live = listener.lock();
        if (live) (*live).content_notice();
    }
};

void note_refusal(ContentChannel& channel, ContentPublishResult result) noexcept {
    channel.frames_refused.fetch_add(1U, std::memory_order_relaxed);
    channel.last_refusal.store(static_cast<std::uint8_t>(result), std::memory_order_relaxed);
}

// Posts one notice to the listener unless one is pending. The dispatcher and
// listener are copied under the lock and the post made after it:
// begin_invoke takes the dispatcher's own lock. A window that is gone
// cancels the task; the listener looks again when it is attached
// (set_content_listener re-arms the notice). An empty dispatcher (no window
// yet) posts nothing.
void post_content_notice(ContentChannel& channel) {
    WindowDispatcher dispatcher{};
    std::weak_ptr<ContentChannelListener> listener{};
    {
        const stxlib::ScopedLock lock(channel.semantics_mutex);
        if (channel.notice_pending || channel.listener.expired()) return;
        channel.notice_pending = true;
        dispatcher = channel.notice_dispatcher;
        listener = channel.listener;
    }
    if (!dispatcher.window_alive()) return;
    const ContentNotice notice{listener};
    static_cast<void>(dispatcher.begin_invoke(notice));
}

// Wakes every content thread waiting on the request. The lock is taken and
// released before the notification, so a waiter that checked the revocation
// or the stop under the lock and is about to wait cannot miss it.
void wake_request_waiters(const ContentChannel& channel) noexcept {
    {
        const stxlib::ScopedLock lock(channel.request_mutex);
    }
    channel.request_changed.notify_all();
}

} // namespace

ContentChannel::ContentChannel(std::shared_ptr<LiveSurface> live_surface) noexcept
    : surface(std::move(live_surface)) {}

std::shared_ptr<ContentChannel> make_content_channel(ContentPixelSize size, bool opaque) {
    LiveSurfaceDescription description;
    description.width = size.width == 0U ? 1U : size.width;
    description.height = size.height == 0U ? 1U : size.height;
    description.opaque = opaque;
    std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
    if (!surface) return {};
    // The stop state is STXlib's allocation: refused as the surface is.
    stxlib::OperationResult<stxlib::StopSource> stop = stxlib::StopSource::create();
    if (stop.failed()) return {};
    std::shared_ptr<ContentChannel> channel = std::make_shared<ContentChannel>(std::move(surface));
    (*channel).stop = static_cast<stxlib::OperationResult<stxlib::StopSource>&&>(stop).take_value();
    // The first publication of any size reshapes the buffers: until then the
    // surface holds no frame, so its placeholder size is not a promise.
    (*channel).published_size = ContentPixelSize{};
    return channel;
}

namespace {

// The copy and publication under publish_mutex. `first_frame` is set when
// this publication is the channel's first, so the caller posts the notice
// after the lock (no lock is held while another is taken).
ContentPublishResult publish_validated(ContentChannel& channel, const ContentFrame& frame, bool& first_frame) {
    const stxlib::ScopedLock lock(channel.publish_mutex);
    LiveSurface& surface = *channel.surface;
    const ContentPixelSize size{frame.width, frame.height};
    const LiveSurfaceSnapshot before = surface.snapshot();
    const bool reshape = !(size == channel.published_size) || before.description.opaque != frame.opaque;
    if (reshape) {
        LiveSurfaceDescription description = before.description;
        description.width = frame.width;
        description.height = frame.height;
        description.opaque = frame.opaque;
        // The previous frame stays acquirable at its own size until this
        // one is published (LiveSurface::reconfigure), so a failure here
        // leaves the surface showing it.
        if (!surface.reconfigure(description)) {
            note_refusal(channel, ContentPublishResult::failed_allocation);
            return ContentPublishResult::failed_allocation;
        }
        channel.published_size = size;
    }
    LiveSurfaceWriteLease lease = surface.try_acquire_write(false);
    if (!lease) {
        channel.frames_dropped.fetch_add(1U, std::memory_order_relaxed);
        return ContentPublishResult::dropped_busy;
    }
    const std::uint64_t target_row_bytes = lease.row_bytes();
    const std::uint64_t row_copy = static_cast<std::uint64_t>(frame.width) * content_frame_pixel_bytes;
    BorrowedMutableSpan<std::byte> target = lease.pixels();
    // The lease's buffer was made for this size (reshape above); a buffer
    // shorter than the frame would be the surface's own defect, refused
    // rather than overrun.
    const std::uint64_t target_extent = content_frame_extent_bytes(frame.width, frame.height, target_row_bytes);
    if (lease.width() != frame.width || lease.height() != frame.height || target_row_bytes < row_copy ||
        target_extent == 0U || static_cast<std::uint64_t>(target.size()) < target_extent) {
        lease.abandon();
        note_refusal(channel, ContentPublishResult::failed_allocation);
        return ContentPublishResult::failed_allocation;
    }
    std::byte* const destination = target.data();
    const std::byte* const source = frame.pixels.data();
    const std::size_t row_length = static_cast<std::size_t>(row_copy);
    // Rows of the consumer's frame and of the surface buffer never overlap:
    // the buffer is the surface's own allocation.
    for (std::uint32_t row = 0U; row < frame.height; ++row) {
        const std::uint64_t source_offset = static_cast<std::uint64_t>(row) * frame.row_bytes;
        const std::uint64_t target_offset = static_cast<std::uint64_t>(row) * target_row_bytes;
        std::memcpy(destination + target_offset, source + source_offset, row_length);
    }
    // A revocation during the copy: the frame is dropped, not shown.
    if (channel.revoked.load(std::memory_order_acquire)) {
        lease.abandon();
        channel.after_revocation.fetch_add(1U, std::memory_order_relaxed);
        return ContentPublishResult::revoked;
    }
    Rect damage{};
    if (!frame.damage.empty() && !reshape) {
        damage = Rect{static_cast<double>(frame.damage.x), static_cast<double>(frame.damage.y),
                      static_cast<double>(frame.damage.width), static_cast<double>(frame.damage.height)};
    }
    static_cast<void>(lease.publish(damage, 0U));
    const std::uint64_t earlier = channel.frames_published.fetch_add(1U, std::memory_order_relaxed);
    channel.activity.fetch_add(1U, std::memory_order_release);
    first_frame = earlier == 0U;
    return ContentPublishResult::published;
}

} // namespace

ContentPublishResult publish_content_frame(ContentChannel& channel, const ContentFrame& frame) {
    if (channel.revoked.load(std::memory_order_acquire)) {
        channel.after_revocation.fetch_add(1U, std::memory_order_relaxed);
        return ContentPublishResult::revoked;
    }
    // Validation reads only the description, never a pixel.
    const ContentPublishResult validity = validate_content_frame(frame);
    if (validity != ContentPublishResult::published) {
        note_refusal(channel, validity);
        return validity;
    }
    bool first_frame = false;
    const ContentPublishResult result = publish_validated(channel, frame, first_frame);
    // The first frame moves the surface from waiting to live without a
    // watchdog check (a disabled watchdog never checks).
    if (first_frame) post_content_notice(channel);
    return result;
}

void record_content_heartbeat(ContentChannel& channel) noexcept {
    if (channel.revoked.load(std::memory_order_acquire)) {
        channel.after_revocation.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    channel.heartbeats.fetch_add(1U, std::memory_order_relaxed);
    channel.activity.fetch_add(1U, std::memory_order_release);
}

void notify_content_listener(ContentChannel& channel) {
    post_content_notice(channel);
}

ContentSemanticsResult publish_content_semantics(ContentChannel& channel, ContentSemantics semantics) {
    if (channel.revoked.load(std::memory_order_acquire)) {
        channel.after_revocation.fetch_add(1U, std::memory_order_relaxed);
        return ContentSemanticsResult::revoked;
    }
    const ContentSemanticsResult validity = validate_content_semantics(semantics);
    if (validity != ContentSemanticsResult::published) {
        channel.semantics_refused.fetch_add(1U, std::memory_order_relaxed);
        return validity;
    }
    {
        const stxlib::ScopedLock lock(channel.semantics_mutex);
        channel.semantics = std::move(semantics);
        ++channel.semantics_generation;
    }
    channel.semantics_published.fetch_add(1U, std::memory_order_relaxed);
    post_content_notice(channel);
    return ContentSemanticsResult::published;
}

void revoke_content_channel(ContentChannel& channel) noexcept {
    channel.revoked.store(true, std::memory_order_release);
    wake_request_waiters(channel);
}

void request_content_stop(ContentChannel& channel) noexcept {
    static_cast<void>(channel.stop.request_stop());
    wake_request_waiters(channel);
}

bool set_content_request(ContentChannel& channel, ContentPixelSize size, double scale) noexcept {
    bool changed = false;
    {
        const stxlib::ScopedLock lock(channel.request_mutex);
        if (!(channel.request.size == size) || channel.request.scale != scale) {
            channel.request.size = size;
            channel.request.scale = scale;
            ++channel.request.generation;
            changed = true;
        }
    }
    if (changed) channel.request_changed.notify_all();
    return changed;
}

void set_content_listener(ContentChannel& channel, WindowDispatcher dispatcher,
                          std::weak_ptr<ContentChannelListener> listener) {
    const stxlib::ScopedLock lock(channel.semantics_mutex);
    channel.notice_dispatcher = std::move(dispatcher);
    channel.listener = std::move(listener);
    channel.notice_pending = false;
}

bool take_content_semantics(ContentChannel& channel, std::uint64_t& seen_generation, ContentSemantics& semantics) {
    const stxlib::ScopedLock lock(channel.semantics_mutex);
    channel.notice_pending = false;
    if (channel.semantics_generation == seen_generation) return false;
    seen_generation = channel.semantics_generation;
    semantics = channel.semantics;
    return true;
}

ContentSurfaceStatistics content_channel_statistics(const ContentChannel& channel) noexcept {
    ContentSurfaceStatistics statistics{};
    statistics.frames_published = channel.frames_published.load(std::memory_order_relaxed);
    statistics.frames_refused = channel.frames_refused.load(std::memory_order_relaxed);
    statistics.frames_dropped = channel.frames_dropped.load(std::memory_order_relaxed);
    statistics.heartbeats = channel.heartbeats.load(std::memory_order_relaxed);
    statistics.stale_transitions = channel.stale_transitions.load(std::memory_order_relaxed);
    statistics.recoveries = channel.recoveries.load(std::memory_order_relaxed);
    statistics.semantics_published = channel.semantics_published.load(std::memory_order_relaxed);
    statistics.semantics_refused = channel.semantics_refused.load(std::memory_order_relaxed);
    statistics.after_revocation = channel.after_revocation.load(std::memory_order_relaxed);
    statistics.last_refusal = static_cast<ContentPublishResult>(channel.last_refusal.load(std::memory_order_relaxed));
    return statistics;
}

ContentFrameSink ContentSinkAccess::make(std::shared_ptr<ContentChannel> channel) noexcept {
    ContentFrameSink sink(std::move(channel));
    return sink;
}

std::shared_ptr<ContentChannel> ContentSinkAccess::channel(const ContentFrameSink& sink) noexcept {
    std::shared_ptr<ContentChannel> result = sink.channel_;
    return result;
}

} // namespace gui_forms::detail
