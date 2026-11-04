#pragma once

#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace gui_forms::detail {

// What a sub-surface's thread and its parent share (design note sections 2
// and 9). Every field is read and written under `mutex`. Lock order: a
// poster's own locks, then the sub-surface dispatcher's, then this one (its
// wake handler takes it); neither side posts or calls into a SurfaceRoot
// while holding it.
struct SubSurfaceChannel final {
    std::mutex mutex;
    // Signalled on a new configuration, a stop request, and every dispatch
    // or paint wake of the sub-surface.
    std::condition_variable wake;
    // The newest configuration not yet applied.
    std::optional<SubSurfaceConfiguration> pending{};
    bool stop_requested{};
    // A dispatch or paint wake arrived since the loop last looked.
    bool signalled{};
    SubSurfaceState state{SubSurfaceState::starting};
    // Set once the tree is built (state running).
    WindowDispatcher dispatcher{};
    std::string failure{};
    SubSurfaceStatistics statistics{};
};

// The sub-surface's dispatch and paint wake handler: marks the channel and
// wakes the loop. It runs on whichever thread posted, under the
// dispatcher's lock, and takes only the channel's.
class SignalSubSurfaceChannel final {
public:
    explicit SignalSubSurfaceChannel(std::shared_ptr<SubSurfaceChannel> channel) noexcept
        : channel_(std::move(channel)) {}
    void operator()() const {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        (*channel_).signalled = true;
        (*channel_).wake.notify_one();
    }

private:
    std::shared_ptr<SubSurfaceChannel> channel_;
};

} // namespace gui_forms::detail
