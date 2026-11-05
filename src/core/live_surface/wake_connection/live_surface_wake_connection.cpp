#include "gui_forms/live_surface/wake_connection/live_surface_wake_connection.hpp"

#include "../state/live_surface_state.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace gui_forms {
namespace {

struct DisconnectedOrMatchingWake final {
    std::uint64_t sequence{};

    [[nodiscard]] bool operator()(
        const std::shared_ptr<detail::LiveSurfaceWake>& wake) const noexcept {
        return !wake || (*wake).sequence == sequence;
    }
};

} // namespace

LiveSurfaceWakeConnection::LiveSurfaceWakeConnection(
    std::weak_ptr<detail::LiveSurfaceState> state,
    std::shared_ptr<detail::LiveSurfaceWake> wake) noexcept
    : state_(std::move(state)), wake_(std::move(wake)) {}

LiveSurfaceWakeConnection::~LiveSurfaceWakeConnection() { disconnect(); }

LiveSurfaceWakeConnection::LiveSurfaceWakeConnection(
    LiveSurfaceWakeConnection&& other) noexcept
    : state_(std::move(other.state_)), wake_(std::move(other.wake_)) {}

LiveSurfaceWakeConnection& LiveSurfaceWakeConnection::operator=(
    LiveSurfaceWakeConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        state_ = std::move(other.state_);
        wake_ = std::move(other.wake_);
    }
    return *this;
}

bool LiveSurfaceWakeConnection::connected() const noexcept {
    return wake_ && (*wake_).connected.load(std::memory_order_acquire);
}

void LiveSurfaceWakeConnection::disconnect() noexcept {
    if (!wake_) return;
    (*wake_).connected.store(false, std::memory_order_release);
    if (const std::shared_ptr<gui_forms::detail::LiveSurfaceState> state = state_.lock()) {
        std::scoped_lock lock((*state).mutex);
        std::erase_if((*state).wakes,
                      DisconnectedOrMatchingWake{(*wake_).sequence});
        std::erase_if((*state).presented_wakes,
                      DisconnectedOrMatchingWake{(*wake_).sequence});
    }
    state_.reset();
    wake_.reset();
}

} // namespace gui_forms
