// Termination of content (owner decisions 2026-11-13, "What this asks of
// GUI.Forms", item 2): cooperative cancel of a content operation; close,
// replacement, departure from the window and destruction never join a hung
// content thread: a bounded wait, then abandonment.

#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/operation/content_operation.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/content_surface.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/window/window.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// One 8 by 8 frame, published by the programs below.
ContentPublishResult publish_square(ContentFrameSink& sink, std::uint8_t value) {
    std::vector<std::byte> pixels(8U * 8U * 4U, static_cast<std::byte>(value));
    ContentFrame frame;
    frame.width = 8U;
    frame.height = 8U;
    frame.row_bytes = 32U;
    frame.opaque = true;
    frame.pixels = std::span<const std::byte>(pixels.data(), pixels.size());
    const ContentPublishResult result = sink.publish(frame);
    return result;
}

// Publishes a frame per request and honours the stop.
class CooperativeProgram final : public ContentProgram {
public:
    explicit CooperativeProgram(std::shared_ptr<std::atomic<bool>> saw_stop) : saw_stop_(std::move(saw_stop)) {}
    void run(ContentContext& context) override {
        std::uint64_t seen = 0U;
        std::uint8_t shade = 0U;
        while (!context.stop_requested()) {
            static_cast<void>(publish_square(context.sink(), shade));
            ++shade;
            const ContentRequest request = context.wait_for_request(seen, std::chrono::milliseconds(20));
            seen = request.generation;
        }
        (*saw_stop_).store(true);
    }

private:
    std::shared_ptr<std::atomic<bool>> saw_stop_;
};

// What a hung program waits on: released only by the test, at the end.
struct HangLatch final {
    std::mutex mutex;
    std::condition_variable changed;
    bool released{};
    std::promise<void> destroyed;

    void release() {
        {
            const std::scoped_lock lock(mutex);
            released = true;
        }
        changed.notify_all();
    }
};

// Publishes one frame, then ignores the stop until the latch is released;
// publishes once more after that (which must find the channel revoked).
class HungProgram final : public ContentProgram {
public:
    HungProgram(std::shared_ptr<HangLatch> latch, std::shared_ptr<ContentPublishResult> late)
        : latch_(std::move(latch)), late_(std::move(late)) {}
    ~HungProgram() override { (*latch_).destroyed.set_value(); }
    void run(ContentContext& context) override {
        static_cast<void>(publish_square(context.sink(), 0x40U));
        std::unique_lock<std::mutex> lock((*latch_).mutex);
        while (!(*latch_).released) (*latch_).changed.wait(lock);
        lock.unlock();
        *late_ = publish_square(context.sink(), 0x80U);
    }

private:
    std::shared_ptr<HangLatch> latch_;
    std::shared_ptr<ContentPublishResult> late_;
};

class ThrowingProgram final : public ContentProgram {
public:
    void run(ContentContext&) override { throw std::runtime_error("renderer lost its device"); }
};

// Releases a hung program and waits (bounded) for it to be destroyed on its
// own thread, then for the abandoned count to fall back.
void release_and_wait(HangLatch& latch, std::uint64_t abandoned_before) {
    std::future<void> destroyed = latch.destroyed.get_future();
    latch.release();
    require(destroyed.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
            "the released program is destroyed on its own thread");
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (abandoned_content_workers() > abandoned_before && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    require(abandoned_content_workers() == abandoned_before, "the abandoned thread leaves the count when it ends");
}

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ContentSurface> surface;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.surface = make_control<ContentSurface>(StableId(std::string(id) + ".surface"));
    (*fixture.surface).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*fixture.root).add_child(fixture.surface);
    fixture.window = std::make_unique<Window>(fixture.root, Size{200.0, 120.0});
    (*fixture.window).perform_layout();
    return fixture;
}

// Drains posted work and checks the surface until it reaches `wanted` or
// five seconds pass.
bool settle(Fixture& fixture, ContentSurfaceStatus wanted) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>((*fixture.window).drain_posted_work());
        (*fixture.surface).check_watchdog(FrameClock::now());
        if ((*fixture.surface).status() == wanted) return true;
        std::this_thread::yield();
    }
    return false;
}

void worker_honours_a_cooperative_stop() {
    std::shared_ptr<ContentSurface> surface = make_control<ContentSurface>(StableId("termination.worker"));
    std::shared_ptr<std::atomic<bool>> saw_stop = std::make_shared<std::atomic<bool>>(false);
    ContentWorker worker(std::make_unique<CooperativeProgram>(saw_stop), (*surface).open_content(true));
    require(worker.state() == ContentWorkerState::running, "a new worker runs");
    require(worker.stop(std::chrono::seconds(5)) == ContentWorkerState::finished, "a cooperative program finishes");
    require((*saw_stop).load(), "the program saw the stop through its context");
    require(worker.stop(std::chrono::seconds(5)) == ContentWorkerState::finished, "stop is idempotent");
}

void worker_abandons_a_hung_program() {
    std::shared_ptr<ContentSurface> surface = make_control<ContentSurface>(StableId("termination.hung"));
    ContentFrameSink sink = (*surface).open_content(true);
    std::shared_ptr<HangLatch> latch = std::make_shared<HangLatch>();
    std::shared_ptr<ContentPublishResult> late = std::make_shared<ContentPublishResult>(ContentPublishResult::published);
    const std::uint64_t before = abandoned_content_workers();
    {
        ContentWorker worker(std::make_unique<HungProgram>(latch, late), sink);
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        require(worker.stop(std::chrono::milliseconds(50)) == ContentWorkerState::abandoned,
                "a program that ignores the stop is abandoned after the bounded wait");
        const std::chrono::steady_clock::duration waited = std::chrono::steady_clock::now() - started;
        require(waited < std::chrono::seconds(3), "the stop waited a bounded time");
        require(sink.revoked(), "abandoning revokes the channel");
        require(abandoned_content_workers() == before + 1U, "the abandoned thread is counted");
    }
    release_and_wait(*latch, before);
    require(*late == ContentPublishResult::revoked, "what the abandoned program publishes later reaches nothing");
}

void surface_terminates_a_cooperative_program() {
    Fixture fixture = make_fixture("termination.cooperative");
    ContentSurface& surface = *fixture.surface;
    std::shared_ptr<std::atomic<bool>> saw_stop = std::make_shared<std::atomic<bool>>(false);
    surface.start_program(std::make_unique<CooperativeProgram>(saw_stop), true);
    require(surface.program_running(), "the surface runs the program");
    require(settle(fixture, ContentSurfaceStatus::live), "the program's frames make the surface live");
    surface.terminate();
    require(settle(fixture, ContentSurfaceStatus::ended), "a terminated program that stops ends the surface");
    require(!surface.program_running() && (*saw_stop).load(), "the program stopped and was reaped");
    require(surface.live_surface() && (*surface.live_surface()).snapshot().has_frame,
            "the last frame stays after the end");
}

void surface_abandons_a_hung_program() {
    Fixture fixture = make_fixture("termination.abandon");
    ContentSurface& surface = *fixture.surface;
    surface.set_termination_grace(std::chrono::milliseconds(50));
    std::shared_ptr<HangLatch> latch = std::make_shared<HangLatch>();
    std::shared_ptr<ContentPublishResult> late = std::make_shared<ContentPublishResult>(ContentPublishResult::published);
    const std::uint64_t before = abandoned_content_workers();
    surface.start_program(std::make_unique<HungProgram>(latch, late), true);
    require(settle(fixture, ContentSurfaceStatus::live), "the hung program's first frame shows");
    const FrameTime asked = FrameClock::now();
    surface.terminate();
    require(surface.status() == ContentSurfaceStatus::terminating, "terminate returns at once, terminating");
    require(has_semantic_state(surface.semantic_descriptor().states, SemanticState::busy), "terminating is busy");
    surface.check_watchdog(asked);
    require(surface.status() == ContentSurfaceStatus::terminating, "within the grace it is still terminating");
    surface.check_watchdog(asked + std::chrono::seconds(1));
    require(surface.status() == ContentSurfaceStatus::abandoned, "past the grace the program is abandoned");
    require(!surface.program_running() && abandoned_content_workers() == before + 1U, "the thread was let go");
    require(surface.live_surface() && (*surface.live_surface()).snapshot().has_frame,
            "the last good frame stays, veiled");
    require(surface.status_caption(ContentSurfaceStatus::abandoned) == "Stopped", "the abandoned caption");
    release_and_wait(*latch, before);
    require(*late == ContentPublishResult::revoked, "the abandoned program's late frame is refused");
}

void surface_reports_a_failed_program() {
    Fixture fixture = make_fixture("termination.failed");
    ContentSurface& surface = *fixture.surface;
    surface.start_program(std::make_unique<ThrowingProgram>(), false);
    require(settle(fixture, ContentSurfaceStatus::failed), "a program that throws fails the surface");
    require(surface.failure() == "renderer lost its device", "the failure carries what it threw");
    require(!surface.program_running(), "the failed program is reaped");
}

void closing_the_window_never_joins_a_hung_program() {
    Fixture fixture = make_fixture("termination.close");
    std::shared_ptr<ContentSurface> surface = fixture.surface;
    (*surface).set_termination_grace(std::chrono::milliseconds(50));
    std::shared_ptr<HangLatch> latch = std::make_shared<HangLatch>();
    std::shared_ptr<ContentPublishResult> late = std::make_shared<ContentPublishResult>(ContentPublishResult::published);
    const std::uint64_t before = abandoned_content_workers();
    (*surface).start_program(std::make_unique<HungProgram>(latch, late), true);
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    fixture.window.reset();
    fixture.root.reset();
    fixture.surface.reset();
    surface.reset();
    const std::chrono::steady_clock::duration waited = std::chrono::steady_clock::now() - started;
    require(waited < std::chrono::seconds(3), "closing the window waited only the bounded grace");
    require(abandoned_content_workers() == before + 1U, "the hung program was abandoned, not joined");
    release_and_wait(*latch, before);
}

void terminating_consumer_threads_revokes_at_once() {
    Fixture fixture = make_fixture("termination.raw");
    ContentSurface& surface = *fixture.surface;
    ContentFrameSink sink = surface.open_content(true);
    const std::stop_token token = sink.stop_token();
    require(!token.stop_requested(), "no stop before terminate");
    surface.terminate();
    require(token.stop_requested() && sink.stop_requested(), "terminate fires the sink's stop token");
    require(sink.revoked(), "content on the consumer's threads is revoked at once");
    require(surface.status() == ContentSurfaceStatus::ended, "and the surface ends");
    surface.terminate();
    require(surface.status() == ContentSurfaceStatus::ended, "a second terminate changes nothing");
}

} // namespace

int main() {
    try {
        worker_honours_a_cooperative_stop();
        worker_abandons_a_hung_program();
        surface_terminates_a_cooperative_program();
        surface_abandons_a_hung_program();
        surface_reports_a_failed_program();
        closing_the_window_never_joins_a_hung_program();
        terminating_consumer_threads_revokes_at_once();
    } catch (const std::exception& error) {
        std::cerr << "content termination tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "content termination tests passed\n";
    return 0;
}
