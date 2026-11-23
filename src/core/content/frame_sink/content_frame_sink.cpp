#include "gui_forms/content/frame_sink/content_frame_sink.hpp"

#include "../channel/content_channel.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stxlib/clocks.hpp>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms {

namespace {

// What wait_for_request waits for, read under the request mutex: a newer
// request, the revocation or the stop.
struct RequestWait final {
    const detail::ContentChannel* channel{};
    std::uint64_t seen_generation{};
};

bool request_wait_over(const void* const context) noexcept {
    const RequestWait& wait = *static_cast<const RequestWait*>(context);
    const detail::ContentChannel& channel = *wait.channel;
    return channel.request.generation > wait.seen_generation || channel.revoked.load(std::memory_order_acquire) ||
           channel.stop.stop_requested();
}

} // namespace

ContentFrameSink::ContentFrameSink(std::shared_ptr<detail::ContentChannel> channel) noexcept
    : channel_(std::move(channel)) {}

ContentPublishResult ContentFrameSink::publish(const ContentFrame& frame) {
    if (!channel_) return ContentPublishResult::revoked;
    const ContentPublishResult result = detail::publish_content_frame(*channel_, frame);
    return result;
}

void ContentFrameSink::heartbeat() noexcept {
    if (!channel_) return;
    detail::record_content_heartbeat(*channel_);
}

ContentSemanticsResult ContentFrameSink::publish_semantics(ContentSemantics semantics) {
    if (!channel_) return ContentSemanticsResult::revoked;
    const ContentSemanticsResult result = detail::publish_content_semantics(*channel_, std::move(semantics));
    return result;
}

ContentRequest ContentFrameSink::request() const noexcept {
    if (!channel_) return ContentRequest{};
    const stxlib::ScopedLock lock((*channel_).request_mutex);
    const ContentRequest result = (*channel_).request;
    return result;
}

ContentRequest ContentFrameSink::wait_for_request(std::uint64_t seen_generation,
                                                  std::chrono::milliseconds timeout) const {
    if (!channel_) return ContentRequest{};
    detail::ContentChannel& channel = *channel_;
    const RequestWait wait{.channel = &channel, .seen_generation = seen_generation};
    stxlib::ScopedLock lock(channel.request_mutex);
    // The revocation and the stop are read under the lock their setters take
    // before they notify (content_channel.cpp), so no wake is lost. A timeout
    // returns the request as it stands.
    static_cast<void>(channel.request_changed.wait_for(lock, &request_wait_over, &wait,
                                                       stxlib::Duration::milliseconds(timeout.count())));
    const ContentRequest result = channel.request;
    return result;
}

bool ContentFrameSink::revoked() const noexcept {
    if (!channel_) return true;
    const bool result = (*channel_).revoked.load(std::memory_order_acquire);
    return result;
}

std::stop_token ContentFrameSink::stop_token() const noexcept {
    if (!channel_) return std::stop_token{};
    std::stop_token token = (*channel_).stop.get_token();
    return token;
}

bool ContentFrameSink::stop_requested() const noexcept {
    if (!channel_) return false;
    const bool result = (*channel_).stop.stop_requested();
    return result;
}

ContentSurfaceStatistics ContentFrameSink::statistics() const noexcept {
    if (!channel_) return ContentSurfaceStatistics{};
    const ContentSurfaceStatistics result = detail::content_channel_statistics(*channel_);
    return result;
}

} // namespace gui_forms
