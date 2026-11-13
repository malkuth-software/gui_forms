#include "gui_forms/content/frame_sink/content_frame_sink.hpp"

#include "../channel/content_channel.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace gui_forms {

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
    const std::scoped_lock lock((*channel_).request_mutex);
    const ContentRequest result = (*channel_).request;
    return result;
}

ContentRequest ContentFrameSink::wait_for_request(std::uint64_t seen_generation,
                                                  std::chrono::milliseconds timeout) const {
    if (!channel_) return ContentRequest{};
    detail::ContentChannel& channel = *channel_;
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(channel.request_mutex);
    // The revocation and the stop are read under the lock their setters take
    // before they notify (content_channel.cpp), so no wake is lost.
    while (channel.request.generation <= seen_generation && !channel.revoked.load(std::memory_order_acquire) &&
           !channel.stop.stop_requested()) {
        const std::cv_status status = channel.request_changed.wait_until(lock, deadline);
        if (status == std::cv_status::timeout) break;
    }
    const ContentRequest result = channel.request;
    return result;
}

bool ContentFrameSink::revoked() const noexcept {
    if (!channel_) return true;
    const bool result = (*channel_).revoked.load(std::memory_order_acquire);
    return result;
}

ContentSurfaceStatistics ContentFrameSink::statistics() const noexcept {
    if (!channel_) return ContentSurfaceStatistics{};
    const ContentSurfaceStatistics result = detail::content_channel_statistics(*channel_);
    return result;
}

} // namespace gui_forms
