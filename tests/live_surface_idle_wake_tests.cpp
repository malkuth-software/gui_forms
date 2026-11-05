#include "gui_forms/gui_forms.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

struct CountWake final {
    std::atomic<unsigned int>& count;
    void operator()() const { count.fetch_add(1, std::memory_order_relaxed); }
};

void publish(gui_forms::LiveSurface& surface) {
    gui_forms::LiveSurfaceWriteLease write = surface.try_acquire_write();
    require(static_cast<bool>(write), "write unavailable");
    require(write.publish() != 0, "publication rejected");
}

// One producer thread publishes once per round, when the main thread lets it
// (`go` reaches the round), and reports the round done. A round costs two
// handshakes instead of creating and joining a thread: on a loaded machine a
// thread's creation and join took tens of milliseconds, and a hundred of them
// broke the test's wall-clock budget although nothing was wrong.
struct RacingProducer final {
    gui_forms::LiveSurface& surface;
    unsigned int rounds{};
    std::atomic<unsigned int> go{0};
    std::atomic<unsigned int> done{0};
    std::atomic<bool> stop{false};
};

// Waits until `value` reaches `wanted` or `stop` is set; false when stopped.
bool wait_for(const std::atomic<unsigned int>& value, const unsigned int wanted, const std::atomic<bool>& stop) {
    unsigned int seen = value.load(std::memory_order_acquire);
    while (seen < wanted) {
        if (stop.load(std::memory_order_acquire)) return false;
        value.wait(seen, std::memory_order_acquire);
        seen = value.load(std::memory_order_acquire);
    }
    return true;
}

void racing_producer(const gui_forms::CancellationFlag& cancellation, void* const address) {
    RacingProducer& producer = *static_cast<RacingProducer*>(address);
    for (unsigned int round = 1; round <= producer.rounds; ++round) {
        if (cancellation.requested() || !wait_for(producer.go, round, producer.stop)) return;
        try {
            publish(producer.surface);
        } catch (...) {
            // The main thread stops waiting; join rethrows the failure.
            producer.stop.store(true, std::memory_order_release);
            producer.done.notify_one();
            throw;
        }
        producer.done.store(round, std::memory_order_release);
        producer.done.notify_one();
    }
}

// Releases a producer still waiting for a round, so the worker can be joined
// when the main thread leaves early (a failed check). Declared after the
// worker, so it runs first.
struct StopProducer final {
    explicit StopProducer(RacingProducer& target) noexcept : producer(target) {}
    RacingProducer& producer;
    StopProducer(const StopProducer&) = delete;
    StopProducer& operator=(const StopProducer&) = delete;
    ~StopProducer() {
        producer.stop.store(true, std::memory_order_release);
        producer.go.fetch_add(1U, std::memory_order_acq_rel);
        producer.go.notify_one();
    }
};

void let_producer_go(RacingProducer& producer, const unsigned int round) {
    producer.go.store(round, std::memory_order_release);
    producer.go.notify_one();
}
} // namespace

int main() {
    try {
        std::atomic<unsigned int> wakes{0};
        const std::shared_ptr<gui_forms::Control> root =
            gui_forms::make_control<gui_forms::Control>(gui_forms::StableId("live"));
        const std::shared_ptr<gui_forms::LiveSurface> surface =
            gui_forms::LiveSurface::create({.width = 8, .height = 8});
        gui_forms::Window window(root, {8, 8});
        window.perform_layout();
        require(window.queue_live_surface_presentation(root, surface), "registration failed");
        window.set_live_surface_idle_wake_handler(CountWake{wakes});
        publish(*surface);
        require(wakes.load() == 0, "active publication posted UI work");
        // The producer won the race between empty drain and idle arming.
        window.set_live_surface_idle_waiting(true);
        require(wakes.load() == 1, "arm lost an already published generation");
        publish(*surface);
        require(wakes.load() == 1, "wake was not coalesced");
        static_cast<void>(window.take_live_surface_presentations());
        window.set_live_surface_idle_waiting(true);
        require(wakes.load() == 1, "unchanged generation woke idle host");
        publish(*surface);
        require(wakes.load() == 2, "idle publication did not wake host");
        window.set_live_surface_idle_waiting(false);
        publish(*surface);
        require(wakes.load() == 2, "disarmed host received producer wake");
        // Race a real pthread producer against arming. Either side must observe
        // the transition, and their common atomic gate must release it once.
        // Odd rounds arm before letting the producer go, even rounds after, so
        // both orders run besides the overlaps.
        RacingProducer producer{*surface, 100U};
        gui_forms::Worker worker(racing_producer, &producer);
        const StopProducer stop{producer};
        for (unsigned int round = 1; round <= producer.rounds; ++round) {
            static_cast<void>(window.take_live_surface_presentations());
            if (round % 2U == 1U) {
                window.set_live_surface_idle_waiting(true);
                let_producer_go(producer, round);
            } else {
                let_producer_go(producer, round);
                window.set_live_surface_idle_waiting(true);
            }
            require(wait_for(producer.done, round, producer.stop), "producer stopped");
            require(wakes.load() == round + 2U, "racing publication lost or duplicated wake");
        }
        worker.join();
        (*root).visible = false;
        publish(*surface);
        static_cast<void>(window.take_live_surface_presentations());
        window.set_live_surface_idle_waiting(true);
        require(wakes.load() == 102, "hidden generation continuously rearmed idle");
        window.set_live_surface_idle_waiting(false);
        window.set_live_surface_idle_wake_handler({});
        window.set_live_surface_idle_waiting(true);
        publish(*surface);
        require(wakes.load() == 102, "removed handler received a wake");
        std::cout << "Idle arming, coalescing, removal and publication races passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
