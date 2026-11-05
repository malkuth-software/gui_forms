#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"

#include "../buffer/live_surface_buffer.hpp"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace gui_forms {

LiveSurfaceWriteLease::LiveSurfaceWriteLease(
    std::shared_ptr<detail::LiveSurfaceState> state,
    std::shared_ptr<detail::LiveSurfaceBuffer> buffer, std::size_t slot,
    std::uint64_t epoch) noexcept
    : state_(std::move(state)), buffer_(std::move(buffer)), slot_(slot),
      epoch_(epoch) {}

LiveSurfaceWriteLease::~LiveSurfaceWriteLease() { abandon(); }

LiveSurfaceWriteLease::LiveSurfaceWriteLease(
    LiveSurfaceWriteLease&& other) noexcept
    : state_(std::move(other.state_)), buffer_(std::move(other.buffer_)),
      slot_(other.slot_), epoch_(other.epoch_) {}

LiveSurfaceWriteLease& LiveSurfaceWriteLease::operator=(
    LiveSurfaceWriteLease&& other) noexcept {
    if (this != &other) {
        abandon();
        state_ = std::move(other.state_);
        buffer_ = std::move(other.buffer_);
        slot_ = other.slot_;
        epoch_ = other.epoch_;
    }
    return *this;
}

std::uint32_t LiveSurfaceWriteLease::width() const noexcept {
    return buffer_ ? (*buffer_).description.width : 0U;
}

std::uint32_t LiveSurfaceWriteLease::height() const noexcept {
    return buffer_ ? (*buffer_).description.height : 0U;
}

std::uint64_t LiveSurfaceWriteLease::row_bytes() const noexcept {
    return buffer_ ? (*buffer_).row_bytes : 0U;
}

std::span<std::byte> LiveSurfaceWriteLease::pixels() noexcept {
    return buffer_ ? std::span<std::byte>((*buffer_).pixels)
                   : std::span<std::byte>{};
}

std::uint64_t LiveSurfaceWriteLease::publish(Rect damage, const std::uint64_t content_tag) {
    if (!state_ || !buffer_) return 0U;
    std::vector<std::shared_ptr<detail::LiveSurfaceWake>> wakes;
    std::uint64_t published{};
    {
        std::scoped_lock lock((*state_).mutex);
        if ((*state_).epoch != epoch_ || (*state_).writing_slot != slot_ ||
            slot_ >= (*state_).buffers.size() ||
            (*state_).buffers[slot_].get() != buffer_.get()) {
            state_.reset();
            buffer_.reset();
            return 0U;
        }
        const Rect bounds = detail::full_live_surface_damage((*buffer_).description);
        if (damage.empty()) damage = bounds;
        damage = Rect::intersection(damage, bounds);
        (*state_).published_slot = slot_;
        (*state_).writing_slot = (*state_).buffers.size();
        (*state_).carried.reset();
        (*state_).content_tag = content_tag;
        (*state_).damage = damage.empty() ? bounds : damage;
        (*state_).presentation_region = Rect::united(
            (*state_).presentation_region, (*state_).damage);
        ++(*state_).generation;
        ++(*state_).publishes;
        published = (*state_).generation;
        wakes = (*state_).wakes;
    }
    state_.reset();
    buffer_.reset();
    for (const std::shared_ptr<gui_forms::detail::LiveSurfaceWake>& wake : wakes) {
        if (!wake || !(*wake).connected.load(std::memory_order_acquire) ||
            !(*wake).callback) {
            continue;
        }
        try {
            (*wake).callback();
        } catch (...) {
            // Publication remains committed when a retiring consumer faults.
        }
    }
    return published;
}

void LiveSurfaceWriteLease::abandon() noexcept {
    if (state_) {
        std::scoped_lock lock((*state_).mutex);
        if ((*state_).epoch == epoch_ && (*state_).writing_slot == slot_) {
            (*state_).writing_slot = (*state_).buffers.size();
        }
    }
    state_.reset();
    buffer_.reset();
}

} // namespace gui_forms
