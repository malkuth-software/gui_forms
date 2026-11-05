#pragma once

#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::detail {

// One queued input event and the sequence post_input gave it.
struct SubSurfaceInputRecord final {
    std::uint64_t sequence{};
    SubSurfaceInput input{};
};

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
    // Input not yet dispatched, in posting order. The loop swaps the whole
    // vector with its own empty one, so both keep their capacity.
    std::vector<SubSurfaceInputRecord> inputs{};
    std::uint64_t next_input_sequence{1};
    // Where unhandled keys go (SubSurfaceThread::set_key_return): the
    // parent's dispatcher, and the target, held weakly so a released host
    // receives nothing.
    WindowDispatcher key_return_dispatcher{};
    std::weak_ptr<SubSurfaceKeyReturn> key_return{};
    // Where requests go (SubSurfaceThread::set_request_target), held weakly
    // as the key return is, and the id the next request takes.
    WindowDispatcher request_dispatcher{};
    std::weak_ptr<SubSurfaceRequestTarget> request_target{};
    std::uint64_t next_request_id{1};
    // The parent's answers not yet handed to the tree, in posting order;
    // swapped whole into the loop as the input is.
    std::vector<SurfaceRequestOutcome> outcomes{};
    // Where semantic snapshot notices go (SubSurfaceThread::set_semantics_listener),
    // the newest snapshot published, and whether a notice is posted and not
    // yet answered by take_semantics (one notice stands for every
    // publication until then).
    WindowDispatcher semantics_dispatcher{};
    std::weak_ptr<SubSurfaceSemanticsListener> semantics_listener{};
    std::shared_ptr<const SemanticSnapshot> semantics{};
    bool semantics_notice_pending{};
    // The newest text input caret, in the sub-surface's coordinates.
    std::optional<Rect> text_input_caret{};
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
