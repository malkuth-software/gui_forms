// The Splash baker's display snapshot and worker (APP_WINDOW_RULES_DESIGN
// Part S, phase 2): what a snapshot copies, skips and refuses, and the
// baker's queue, refusals, bounds and shutdown, with a fake raster.

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "../src/core/display/snapshot/display_snapshot.hpp"
#include "../src/core/display/snapshot/splash_baker.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::detail;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

DisplayCommand fill_command(Rect rect, Color color) {
    DisplayCommand command{};
    command.operation = DisplayOperation::fill_rect;
    command.rect = rect;
    command.color = color;
    return command;
}

DisplayChunk chunk_of(std::vector<DisplayCommand> commands) {
    DisplayChunk chunk(7U, PaintPlane::overlay, Rect{0.0, 0.0, 10.0, 6.0}, std::move(commands));
    return chunk;
}

void test_snapshot_skips_and_copies() {
    DisplayCommand fill = fill_command(Rect{0.0, 0.0, 10.0, 6.0}, Color{255, 0, 0, 255});
    fill.retained_cache = std::make_shared<RetainedDrawCache>();
    fill.text = "kept";

    std::shared_ptr<PartRaster> source = PartRaster::create(DeviceExtent{3U, 2U}, DeviceScale{1.0});
    require(source != nullptr, "the source raster is made");
    const PartRasterView view = (*source).view();
    for (std::size_t index = 0U; index < view.pixels.size(); ++index) {
        view.pixels[index] = static_cast<std::uint8_t>(index + 1U);
    }
    DisplayCommand raster{};
    raster.operation = DisplayOperation::draw_part_raster;
    raster.first = Point{1.0, 1.0};
    raster.scalar = 1.0;
    raster.part_raster = source;

    std::vector<DisplayCommand> commands;
    commands.push_back(fill);
    commands.push_back(raster);
    const DisplayChunk chunk = chunk_of(std::move(commands));
    const DisplaySnapshotResult made = make_display_snapshot(chunk, Size{10.0, 6.0}, DeviceScale{2.0});
    require(made.refusal == DisplaySnapshotRefusal::none && made.snapshot != nullptr, "a plain chunk snapshots");
    const DisplaySnapshot& snapshot = *made.snapshot;
    require(snapshot.device_extent().width == 20U && snapshot.device_extent().height == 12U,
            "the extent is the logical size at the scale");
    const std::vector<DisplayCommand>& copied = snapshot.chunk().commands();
    require(copied.size() == 2U, "every command is copied");
    require(copied[0].retained_cache == nullptr, "the retained draw cache is skipped");
    require(chunk.commands()[0].retained_cache != nullptr, "the recording keeps its cache");
    require(copied[0].text == "kept", "strings are copied");
    require(copied[1].part_raster != nullptr && copied[1].part_raster != chunk.commands()[1].part_raster,
            "the part raster is a copy the snapshot owns");
    const std::span<const std::uint8_t> left = (*copied[1].part_raster).pixels();
    const std::span<const std::uint8_t> right = (*source).pixels();
    require(left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size()) == 0,
            "the copied raster has the source's pixels");
    require(snapshot.chunk().generation() == 7U && snapshot.chunk().plane() == PaintPlane::overlay,
            "the chunk's identity is kept");
}

void test_snapshot_refusals() {
    DisplayCommand image{};
    image.operation = DisplayOperation::draw_image;
    std::vector<DisplayCommand> with_image;
    with_image.push_back(fill_command(Rect{0.0, 0.0, 1.0, 1.0}, Color{0, 0, 0, 255}));
    with_image.push_back(image);
    const DisplaySnapshotResult images = make_display_snapshot(chunk_of(std::move(with_image)), Size{10.0, 6.0},
                                                               DeviceScale{1.0});
    require(images.snapshot == nullptr && images.refusal == DisplaySnapshotRefusal::image, "an image is refused");

    DisplayCommand live{};
    live.operation = DisplayOperation::draw_live_surface;
    std::vector<DisplayCommand> with_live;
    with_live.push_back(live);
    const DisplaySnapshotResult lives = make_display_snapshot(chunk_of(std::move(with_live)), Size{10.0, 6.0},
                                                              DeviceScale{1.0});
    require(lives.snapshot == nullptr && lives.refusal == DisplaySnapshotRefusal::live_surface,
            "a live surface is refused");

    DisplayCommand prepared{};
    prepared.operation = DisplayOperation::draw_prepared_text;
    std::vector<DisplayCommand> with_prepared;
    with_prepared.push_back(prepared);
    const DisplaySnapshotResult texts = make_display_snapshot(chunk_of(std::move(with_prepared)), Size{10.0, 6.0},
                                                              DeviceScale{1.0});
    require(texts.snapshot == nullptr && texts.refusal == DisplaySnapshotRefusal::prepared_text,
            "prepared text is refused");

    const DisplaySnapshotResult empty = make_display_snapshot(chunk_of({}), Size{10.0, 6.0}, DeviceScale{1.0});
    require(empty.snapshot == nullptr && empty.refusal == DisplaySnapshotRefusal::empty, "no commands is empty");
    std::vector<DisplayCommand> one;
    one.push_back(fill_command(Rect{0.0, 0.0, 1.0, 1.0}, Color{0, 0, 0, 255}));
    const DisplaySnapshotResult zero = make_display_snapshot(chunk_of(one), Size{0.0, 6.0}, DeviceScale{1.0});
    require(zero.refusal == DisplaySnapshotRefusal::empty, "a zero extent is empty");
    const DisplaySnapshotResult huge = make_display_snapshot(chunk_of(one), Size{20000.0, 6.0}, DeviceScale{1.0});
    require(huge.refusal == DisplaySnapshotRefusal::empty, "an extent past the part raster side is empty");
}

// How long a test waits for the worker before it fails. A wall-clock
// bound, not a count of polls: under a parallel ctest run the worker may
// not be scheduled for a long stretch, and a fixed number of yields was
// over in a few milliseconds (the -j6 failure). Below the test's 30 s
// ctest timeout so the failure names its wait.
constexpr std::chrono::seconds worker_wait_limit{20};

// The fake renderer: fills the target with one byte, refuses when told,
// and can hold a bake until released. `changed` is notified, under
// `mutex`, whenever `hold` is cleared or a bake is entered.
struct FakeControl final {
    std::mutex mutex{};
    std::condition_variable changed{};
    std::uint8_t fill{0x5AU};
    bool refuse{};
    bool hold{};
    std::atomic<int> made{};
    // Bakes the worker has started (before any hold).
    std::atomic<int> entered{};
    std::atomic<int> destroyed{};
    std::atomic<int> baked{};
};

class FakeRaster final : public SplashBakeRaster {
public:
    explicit FakeRaster(FakeControl& control) noexcept : control_(control) {}
    ~FakeRaster() override { ++control_.destroyed; }

    bool bake(const DisplaySnapshot&, const PartRasterView target) override {
        std::unique_lock<std::mutex> lock(control_.mutex);
        ++control_.entered;
        control_.changed.notify_all();
        // Blocks, rather than spinning on the mutex, while the test holds
        // the bake: a spinning worker starved the test thread's lock.
        while (control_.hold) control_.changed.wait(lock);
        ++control_.baked;
        if (control_.refuse) return false;
        std::memset(target.pixels.data(), control_.fill, target.pixels.size());
        return true;
    }

private:
    FakeControl& control_;
};

class FakeFactory final : public SplashBakeRasterFactory {
public:
    explicit FakeFactory(FakeControl& control) noexcept : control_(control) {}
    std::unique_ptr<SplashBakeRaster> make() override {
        ++control_.made;
        std::unique_ptr<SplashBakeRaster> raster = std::make_unique<FakeRaster>(control_);
        return raster;
    }

private:
    FakeControl& control_;
};

std::shared_ptr<const DisplaySnapshot> small_snapshot() {
    std::vector<DisplayCommand> one;
    one.push_back(fill_command(Rect{0.0, 0.0, 4.0, 3.0}, Color{0, 0, 255, 255}));
    DisplaySnapshotResult made = make_display_snapshot(chunk_of(std::move(one)), Size{4.0, 3.0}, DeviceScale{1.0});
    require(made.snapshot != nullptr, "the small snapshot is made");
    return made.snapshot;
}

// Takes `ticket` until it leaves pending, within worker_wait_limit. The
// baker has no completion signal, so this polls, sleeping between takes.
SplashBakeOutcome take_settled(SplashBaker& baker, SplashBakeTicket ticket) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + worker_wait_limit;
    while (std::chrono::steady_clock::now() < deadline) {
        SplashBakeOutcome outcome = baker.take(ticket);
        if (outcome.state != SplashBakeState::pending) return outcome;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("a bake settles within the wait limit");
}

// Waits until the worker has entered `count` bakes, within worker_wait_limit.
bool wait_entered(FakeControl& control, int count) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + worker_wait_limit;
    std::unique_lock<std::mutex> lock(control.mutex);
    while (control.entered.load() < count) {
        if (control.changed.wait_until(lock, deadline) == std::cv_status::timeout) break;
    }
    const bool entered = control.entered.load() >= count;
    return entered;
}

// Lets held bakes go on.
void release_hold(FakeControl& control) {
    const std::lock_guard<std::mutex> lock(control.mutex);
    control.hold = false;
    control.changed.notify_all();
}

void test_baker_finishes_and_refuses() {
    FakeControl control{};
    {
        SplashBaker baker(std::make_unique<FakeFactory>(control));
        const SplashBakeTicket first = baker.submit(small_snapshot());
        const SplashBakeOutcome done = take_settled(baker, first);
        require(done.state == SplashBakeState::finished && done.raster != nullptr, "a bake finishes");
        require((*done.raster).extent().width == 4U && (*done.raster).extent().height == 3U,
                "the raster has the snapshot's extent");
        require((*done.raster).pixels()[0] == 0x5AU, "the raster holds what the renderer painted");
        require(baker.take(first).state == SplashBakeState::unknown, "a taken ticket is unknown");
        require(baker.take(SplashBakeTicket{999U}).state == SplashBakeState::unknown, "an unissued ticket is unknown");

        {
            const std::lock_guard<std::mutex> lock(control.mutex);
            control.refuse = true;
        }
        const SplashBakeTicket second = baker.submit(small_snapshot());
        const SplashBakeOutcome refused = take_settled(baker, second);
        require(refused.state == SplashBakeState::refused && refused.raster == nullptr,
                "a renderer refusal reads refused");
        require(baker.take(second).state == SplashBakeState::unknown, "a refusal is read once");
    }
    require(control.made.load() == 1, "the raster is made once, warm");
    require(control.destroyed.load() == 1, "the raster is destroyed when the baker stops");
}

void test_baker_drops_oldest_when_full() {
    FakeControl control{};
    {
        const std::lock_guard<std::mutex> lock(control.mutex);
        control.hold = true;
    }
    SplashBaker baker(std::make_unique<FakeFactory>(control));
    // The first is taken by the worker and held; the rest fill the queue,
    // and one more pushes the oldest waiting one out.
    const SplashBakeTicket held = baker.submit(small_snapshot());
    require(wait_entered(control, 1) && control.entered.load() == 1, "the worker starts the first bake");
    require(baker.take(held).state == SplashBakeState::pending, "a bake being painted is pending");
    std::vector<SplashBakeTicket> queued;
    for (std::size_t index = 0U; index < SplashBaker::maximum_waiting_bakes + 1U; ++index) {
        queued.push_back(baker.submit(small_snapshot()));
    }
    require(baker.take(queued[0]).state == SplashBakeState::refused,
            "a submit past the bound drops the oldest waiting bake");
    require(baker.take(queued.back()).state == SplashBakeState::pending, "the newest stays queued");
    release_hold(control);
    const SplashBakeOutcome newest = take_settled(baker, queued.back());
    require(newest.state == SplashBakeState::finished, "the newest finishes once released");
    require(take_settled(baker, held).state == SplashBakeState::finished, "the held bake finishes too");
}

void test_baker_stops_with_work_waiting() {
    FakeControl control{};
    {
        const std::lock_guard<std::mutex> lock(control.mutex);
        control.hold = true;
    }
    {
        SplashBaker baker(std::make_unique<FakeFactory>(control));
        static_cast<void>(baker.submit(small_snapshot()));
        static_cast<void>(baker.submit(small_snapshot()));
        release_hold(control);
    }
    require(control.baked.load() <= 2, "waiting bakes are dropped or finished before the join");
    require(control.destroyed.load() == control.made.load(), "the worker's raster is destroyed on stop");
}

void test_installation() {
    require(current_splash_baker().baker == nullptr, "no baker is installed outside a run");
    FakeControl control{};
    SplashBaker baker(std::make_unique<FakeFactory>(control));
    std::uint64_t first_run = 0U;
    {
        const SplashBakerInstallation installed(baker);
        first_run = current_splash_baker().run;
        require(current_splash_baker().baker == &baker, "the installed baker is current");
    }
    require(current_splash_baker().baker == nullptr, "the installation is undone");
    const SplashBakerInstallation again(baker);
    require(current_splash_baker().run != first_run, "each installation is another run");
}

} // namespace

int main() {
    try {
        test_snapshot_skips_and_copies();
        test_snapshot_refusals();
        test_baker_finishes_and_refuses();
        test_baker_drops_oldest_when_full();
        test_baker_stops_with_work_waiting();
        test_installation();
    } catch (const std::exception& error) {
        std::cerr << "splash_baker_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "splash_baker_tests: passed\n";
    return EXIT_SUCCESS;
}
