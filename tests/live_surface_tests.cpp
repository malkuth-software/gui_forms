#include "gui_forms/gui_forms.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

// Throws on failure, so the check runs in every build type (Release defines
// NDEBUG); an uncaught throw fails the test.
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

void fill(gui_forms::LiveSurfaceWriteLease& lease, std::byte value) {
    for (std::byte& pixel : lease.pixels()) pixel = value;
}

class ObservePresentationWake final {
public:
    ObservePresentationWake(gui_forms::LiveSurface& surface,
                            std::atomic<std::uint64_t>& wakes)
        : surface_(surface), wakes_(wakes) {}

    void operator()() const {
        // Publication must not hold the surface mutex while signaling. A
        // scheduler is allowed to inspect the newest state from this thread.
        // Publication swallows a throw from a wake; the count below then
        // falls short, which the wake-count checks report.
        require(surface_.snapshot().description.width != 0U, "surface_.snapshot().description.width != 0U");
        ++wakes_;
    }

private:
    gui_forms::LiveSurface& surface_;
    std::atomic<std::uint64_t>& wakes_;
};

} // namespace

int main() {
    using namespace gui_forms;

    require(!LiveSurface::create({}), "!LiveSurface::create({})");
    require(!LiveSurface::create(
        {4U, 3U, LiveSurfacePixelFormat::bgra32_premultiplied_srgb, 1U}), "!LiveSurface::create( {4U, 3U, LiveSurfacePixelFormat::bgra32_premultiplied_srgb, 1U})");
    require(!LiveSurface::create(
        {4U, 3U, LiveSurfacePixelFormat::bgra32_premultiplied_srgb, 9U}), "!LiveSurface::create( {4U, 3U, LiveSurfacePixelFormat::bgra32_premultiplied_srgb, 9U})");
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({4U, 3U});
    require(static_cast<bool>(surface), "surface");
    require((*surface).snapshot().description.buffer_count ==
           default_live_surface_buffer_count, "(*surface).snapshot().description.buffer_count == default_live_surface_buffer_count");
    require(!(*surface).acquire_latest(), "!(*surface).acquire_latest()");

    std::atomic<std::uint64_t> wakes{};
    LiveSurfaceWakeConnection wake = (*surface).connect_presentation_wake(
        ObservePresentationWake(*surface, wakes));
    require(wake.connected(), "wake.connected()");

    LiveSurfaceWriteLease first_write = (*surface).try_acquire_write();
    require(static_cast<bool>(first_write), "first_write");
    require(first_write.width() == 4U, "first_write.width() == 4U");
    require(first_write.height() == 3U, "first_write.height() == 3U");
    require(first_write.row_bytes() == 16U, "first_write.row_bytes() == 16U");
    fill(first_write, std::byte{0x11});
    require(first_write.publish({1.0, 1.0, 2.0, 1.0}) == 1U, "first_write.publish({1.0, 1.0, 2.0, 1.0}) == 1U");
    require(wakes == 1U, "wakes == 1U");

    LiveSurfaceFrame first = (*surface).acquire_latest();
    require(static_cast<bool>(first), "first");
    require(first.generation() == 1U, "first.generation() == 1U");
    require(first.damage() == Rect(1.0, 1.0, 2.0, 1.0), "first.damage() == Rect(1.0, 1.0, 2.0, 1.0)");
    require(first.pixels().front() == std::byte{0x11}, "first.pixels().front() == std::byte{0x11}");

    LiveSurfaceWriteLease second_write = (*surface).try_acquire_write(true);
    require(static_cast<bool>(second_write), "second_write");
    require(second_write.pixels().front() == std::byte{0x11}, "second_write.pixels().front() == std::byte{0x11}");
    fill(second_write, std::byte{0x22});
    require(second_write.publish() == 2U, "second_write.publish() == 2U");
    require(wakes == 2U, "wakes == 2U");
    LiveSurfaceFrame second = (*surface).acquire_latest();
    require(second && second.generation() == 2U, "second && second.generation() == 2U");
    require(second.pixels().front() == std::byte{0x22}, "second.pixels().front() == std::byte{0x22}");
    // A compositor lease remains immutable across newer publications.
    require(first.pixels().front() == std::byte{0x11}, "first.pixels().front() == std::byte{0x11}");

    LiveSurfaceWriteLease third_write = (*surface).try_acquire_write();
    require(static_cast<bool>(third_write), "third_write");
    fill(third_write, std::byte{0x33});
    require(third_write.publish() == 3U, "third_write.publish() == 3U");
    require(wakes == 3U, "wakes == 3U");
    LiveSurfaceFrame third = (*surface).acquire_latest();
    require(third && third.pixels().front() == std::byte{0x33}, "third && third.pixels().front() == std::byte{0x33}");
    // All three candidates are leased. A high-rate producer drops instead of
    // blocking or mutating a frame currently owned by the compositor.
    require(!(*surface).try_acquire_write(), "!(*surface).try_acquire_write()");
    require((*surface).snapshot().dropped_acquires == 1U, "(*surface).snapshot().dropped_acquires == 1U");

    first = {};
    LiveSurfaceWriteLease recovered = (*surface).try_acquire_write();
    require(static_cast<bool>(recovered), "recovered");
    recovered.abandon();
    require(!(*surface).snapshot().write_active, "!(*surface).snapshot().write_active");

    second = {};
    third = {};
    require((*surface).reconfigure({2U, 2U}), "(*surface).reconfigure({2U, 2U})");
    require(wakes == 4U, "wakes == 4U");
    const LiveSurfaceSnapshot reconfigured = (*surface).snapshot();
    require(reconfigured.epoch == 2U, "reconfigured.epoch == 2U");
    require(!reconfigured.has_frame, "!reconfigured.has_frame");
    require(reconfigured.description.width == 2U, "reconfigured.description.width == 2U");
    require(reconfigured.description.height == 2U, "reconfigured.description.height == 2U");
    // Until the new epoch publishes, the previous epoch's last frame is still
    // acquirable at its own size, whole, so a presenter shows it scaled
    // rather than nothing.
    require(reconfigured.carries_previous_frame, "reconfigured.carries_previous_frame");
    LiveSurfaceFrame carried = (*surface).acquire_latest();
    require(carried && carried.epoch() == 1U && carried.width() == 4U &&
           carried.height() == 3U && carried.pixels().front() == std::byte{0x33}, "carried && carried.epoch() == 1U && carried.width() == 4U && carried.height() == 3U && carried.pixels().front() == std::byte{0x33}");
    require((carried.damage() == Rect{0.0, 0.0, 4.0, 3.0}), "(carried.damage() == Rect{0.0, 0.0, 4.0, 3.0})");
    LiveSurfaceFrame carried_presentation = (*surface).acquire_for_presentation(&wakes);
    require((carried_presentation && carried_presentation.epoch() == 1U &&
            carried_presentation.damage() == Rect{0.0, 0.0, 4.0, 3.0}), "(carried_presentation && carried_presentation.epoch() == 1U && carried_presentation.damage() == Rect{0.0, 0.0, 4.0, 3.0})");
    carried = {};
    carried_presentation = {};

    LiveSurfaceWriteLease final_write = (*surface).try_acquire_write();
    require(final_write && final_write.pixels().size() == 16U, "final_write && final_write.pixels().size() == 16U");
    fill(final_write, std::byte{0x7f});
    require(final_write.publish() == 1U, "final_write.publish() == 1U");
    require(wakes == 5U, "wakes == 5U");
    require(!(*surface).snapshot().carries_previous_frame, "!(*surface).snapshot().carries_previous_frame");
    LiveSurfaceFrame final_frame = (*surface).acquire_latest();
    require(final_frame && final_frame.epoch() == 2U, "final_frame && final_frame.epoch() == 2U");
    require(final_frame.pixels().back() == std::byte{0x7f}, "final_frame.pixels().back() == std::byte{0x7f}");

    wake.disconnect();
    require(!wake.connected(), "!wake.connected()");
    final_frame = {};
    LiveSurfaceWriteLease disconnected_write = (*surface).try_acquire_write();
    require(static_cast<bool>(disconnected_write), "disconnected_write");
    fill(disconnected_write, std::byte{0x55});
    require(disconnected_write.publish() == 2U, "disconnected_write.publish() == 2U");
    require(wakes == 5U, "wakes == 5U");

    std::shared_ptr<LiveSurface> low_latency_surface = LiveSurface::create(
        {2U, 2U, LiveSurfacePixelFormat::bgra32_premultiplied_srgb, 2U});
    require(static_cast<bool>(low_latency_surface), "low_latency_surface");
    LiveSurfaceWriteLease low_latency_write =
        (*low_latency_surface).try_acquire_write();
    require(low_latency_write && low_latency_write.publish() == 1U, "low_latency_write && low_latency_write.publish() == 1U");
    LiveSurfaceFrame low_latency_first =
        (*low_latency_surface).acquire_latest();
    low_latency_write = (*low_latency_surface).try_acquire_write();
    require(low_latency_write && low_latency_write.publish() == 2U, "low_latency_write && low_latency_write.publish() == 2U");
    LiveSurfaceFrame low_latency_second =
        (*low_latency_surface).acquire_latest();
    require(low_latency_first && low_latency_second, "low_latency_first && low_latency_second");
    require(!(*low_latency_surface).try_acquire_write(), "!(*low_latency_surface).try_acquire_write()");

    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("live.root"));
    std::shared_ptr<gui_forms::Control> live_control = make_control<Control>(StableId("live.content"));
    std::shared_ptr<gui_forms::Control> overlay = make_control<Control>(StableId("live.overlay"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    (*live_control).set_requested_bounds({10.0, 10.0, 70.0, 50.0});
    (*overlay).set_requested_bounds({20.0, 20.0, 30.0, 20.0});
    (*overlay).set_paint_plane(PaintPlane::overlay);
    (*root).add_child(live_control);
    (*root).add_child(overlay);
    Window window(root, {100.0, 80.0});
    window.perform_layout();

    std::shared_ptr<LiveSurface> composited_surface = LiveSurface::create({4U, 3U});
    require(static_cast<bool>(composited_surface), "composited_surface");
    LiveSurfaceWriteLease composited_write =
        (*composited_surface).try_acquire_write();
    require(static_cast<bool>(composited_write), "composited_write");
    fill(composited_write, std::byte{0x31});
    require(composited_write.publish() == 1U, "composited_write.publish() == 1U");
    require(window.queue_live_surface_presentation(live_control,
                                                  composited_surface), "window.queue_live_surface_presentation(live_control, composited_surface)");
    // An intersecting retained overlay owns only its pixels. The direct lane
    // continues presenting every uncovered fragment around it.
    const std::vector<LiveSurfacePresentation> clipped = window.take_live_surface_presentations();
    require(clipped.size() == 4U, "clipped.size() == 4U");
    for (const gui_forms::LiveSurfacePresentation& presentation : clipped) {
        require(presentation.control == (*live_control).runtime_id(), "presentation.control == (*live_control).runtime_id()");
        require(Rect::intersection(presentation.clip,
                                  {20.0, 20.0, 30.0, 20.0}).empty(), "Rect::intersection(presentation.clip, {20.0, 20.0, 30.0, 20.0}).empty()");
    }

    (*overlay).visible = false;
    LiveSurfaceWriteLease resumed_write =
        (*composited_surface).try_acquire_write();
    require(static_cast<bool>(resumed_write), "resumed_write");
    fill(resumed_write, std::byte{0x32});
    require(resumed_write.publish() == 2U, "resumed_write.publish() == 2U");
    const std::vector<LiveSurfacePresentation> resumed = window.take_live_surface_presentations();
    require(resumed.size() == 1U &&
           resumed.front().control == (*live_control).runtime_id(), "resumed.size() == 1U && resumed.front().control == (*live_control).runtime_id()");
    require(resumed.front().paired, "resumed.front().paired");

    // Content tags travel with the frame, and survive a reconfigure as the
    // carried frame's tag until the new epoch publishes.
    LiveSurfaceWriteLease tagged_write = (*composited_surface).try_acquire_write();
    require(static_cast<bool>(tagged_write), "tagged_write");
    fill(tagged_write, std::byte{0x33});
    require(tagged_write.publish({}, 7U) == 3U, "tagged_write.publish({}, 7U) == 3U");
    require((*composited_surface).snapshot().content_tag == 7U, "(*composited_surface).snapshot().content_tag == 7U");
    require((*composited_surface).acquire_latest().content_tag() == 7U, "(*composited_surface).acquire_latest().content_tag() == 7U");
    require((*composited_surface).reconfigure({6U, 3U}), "(*composited_surface).reconfigure({6U, 3U})");
    require((*composited_surface).snapshot().carries_previous_frame, "(*composited_surface).snapshot().carries_previous_frame");
    require((*composited_surface).snapshot().content_tag == 7U, "(*composited_surface).snapshot().content_tag == 7U");
    require((*composited_surface).acquire_latest().content_tag() == 7U, "(*composited_surface).acquire_latest().content_tag() == 7U");
    LiveSurfaceWriteLease reconfigured_write = (*composited_surface).try_acquire_write();
    require(static_cast<bool>(reconfigured_write), "reconfigured_write");
    fill(reconfigured_write, std::byte{0x34});
    require(reconfigured_write.publish({}, 8U) == 1U, "reconfigured_write.publish({}, 8U) == 1U");
    require((*composited_surface).snapshot().content_tag == 8U, "(*composited_surface).snapshot().content_tag == 8U");
    const std::vector<LiveSurfacePresentation> tagged = window.take_live_surface_presentations();
    require(tagged.size() == 1U && tagged.front().paired, "tagged.size() == 1U && tagged.front().paired");

    // Pairing: content 9 is required. The tick that finds only content 8
    // keeps the previous frame (no presentation, one wait); the next tick
    // presents it unpaired; a frame at content 9 is paired again.
    window.reset_activity_metrics();
    require(window.require_live_surface_content(live_control, 9U), "window.require_live_surface_content(live_control, 9U)");
    require(!window.require_live_surface_content(overlay, 9U), "!window.require_live_surface_content(overlay, 9U)");
    require(window.require_live_surface_content(live_control, 4U), "window.require_live_surface_content(live_control, 4U)");
    LiveSurfaceWriteLease stale_write = (*composited_surface).try_acquire_write();
    require(static_cast<bool>(stale_write), "stale_write");
    fill(stale_write, std::byte{0x35});
    require(stale_write.publish({}, 8U) == 2U, "stale_write.publish({}, 8U) == 2U");
    const std::vector<LiveSurfacePresentation> waited = window.take_live_surface_presentations();
    require(waited.empty(), "waited.empty()");
    require(window.metrics_snapshot().live_pairing_waits == 1U, "window.metrics_snapshot().live_pairing_waits == 1U");
    const std::vector<LiveSurfacePresentation> unpaired = window.take_live_surface_presentations();
    require(unpaired.size() == 1U && !unpaired.front().paired, "unpaired.size() == 1U && !unpaired.front().paired");
    require(window.metrics_snapshot().live_unpaired_presentations == 1U, "window.metrics_snapshot().live_unpaired_presentations == 1U");
    LiveSurfaceWriteLease paired_write = (*composited_surface).try_acquire_write();
    require(static_cast<bool>(paired_write), "paired_write");
    fill(paired_write, std::byte{0x36});
    require(paired_write.publish({}, 9U) == 3U, "paired_write.publish({}, 9U) == 3U");
    const std::vector<LiveSurfacePresentation> paired = window.take_live_surface_presentations();
    require(paired.size() == 1U && paired.front().paired, "paired.size() == 1U && paired.front().paired");
    const MetricsSnapshot pairing = window.metrics_snapshot();
    require(pairing.live_pairing_waits == 1U && pairing.live_unpaired_presentations == 1U, "pairing.live_pairing_waits == 1U && pairing.live_unpaired_presentations == 1U");
}
