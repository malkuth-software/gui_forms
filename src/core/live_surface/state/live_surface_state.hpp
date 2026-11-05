#pragma once

#include "gui_forms/live_surface/types/live_surface_types.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace gui_forms::detail {

struct LiveSurfaceBuffer final {
    LiveSurfaceDescription description{};
    std::uint64_t row_bytes{};
    std::vector<std::byte> pixels;
};

struct LiveSurfaceWake final {
    std::uint64_t sequence{};
    std::atomic<bool> connected{true};
    std::function<void()> callback;
};

struct LiveSurfaceState final {
    mutable std::mutex mutex;
    LiveSurfaceDescription description{};
    std::vector<std::shared_ptr<LiveSurfaceBuffer>> buffers;
    std::size_t published_slot{};
    std::size_t writing_slot{};
    std::uint64_t epoch{1};
    std::uint64_t generation{};
    // The content tag of the published frame (LiveSurfaceWriteLease::publish).
    std::uint64_t content_tag{};
    std::uint64_t publishes{};
    std::uint64_t dropped_acquires{};
    std::uint64_t read_acquires{};
    std::uint64_t last_read_generation{};
    std::uint64_t next_wake_sequence{1};
    Rect damage{};
    // Invalid region: everything published since the presenter last took a
    // frame for presentation, like a window's update region. It starts as
    // the whole surface and is cleared only by acquire_for_presentation.
    Rect presentation_region{};
    // The one presenter that consumes presentation_region. Identity only.
    const void* presenter{};
    // The previous epoch's last published frame, presented (scaled to the
    // destination) until the first publication after reconfigure, so a
    // producer that is a frame late shows a stretched picture, not black.
    std::shared_ptr<LiveSurfaceBuffer> carried;
    std::uint64_t carried_epoch{};
    std::uint64_t carried_generation{};
    std::uint64_t carried_content_tag{};
    std::vector<std::shared_ptr<LiveSurfaceWake>> wakes;
    // Presentation feedback (design note sections 7 and 8): the presenter's
    // last report, the wakes it signals, and the condition producers'
    // pacers wait on (notified on every report and on LiveSurfacePacer::arm).
    LivePresentationRecord presentation_record{};
    std::vector<std::shared_ptr<LiveSurfaceWake>> presented_wakes;
    std::condition_variable presented;
};

} // namespace gui_forms::detail
