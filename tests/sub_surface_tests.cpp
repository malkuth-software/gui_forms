// Stage S2.2: a SubSurface built, laid out and painted on its own thread,
// publishing into a LiveSurface its SubSurfaceHost composes (threaded
// sub-surfaces design note sections 2, 6, 7 and 9), stage S3, input and
// focus forwarding (section 4), S3.3, tooltips asked of the parent
// (section 15), S4, effects on a sub-surface (section 14), S5,
// accessibility (section 5), and the host's stopped and pairing_missed
// notices (decision 6, section 9).

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "headless_host.hpp"
#include "support/fill_raster.hpp"
#include "support/theme_definition_heap.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
using gui_forms::test_support::FillFramebuffer;
using gui_forms::test_support::FillRaster;

// Throws on failure, so the check runs in every build type (Release defines
// NDEBUG); an uncaught throw fails the test.
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

// What the sub-surface's thread reports back to the test: written there,
// read here, under the mutex. The control is held weakly, so the test never
// keeps a sub-surface control alive past its thread.
struct Probe final {
    std::mutex mutex;
    std::weak_ptr<DrawingSurface> surface;
    std::thread::id built_on{};
    bool saw_no_window{};
    bool saw_surface_root{};
    double text_scale{};
    const Theme* application_theme{};
};

class SolidContent final : public SubSurfaceContent {
public:
    SolidContent(std::shared_ptr<Probe> probe, Color color) : probe_(std::move(probe)), color_(color) {}
    Control::Ptr build_root() override {
        std::shared_ptr<DrawingSurface> surface = std::make_shared<DrawingSurface>(StableId("sub.solid"));
        (*surface).background = color_;
        const std::lock_guard<std::mutex> lock((*probe_).mutex);
        (*probe_).surface = surface;
        (*probe_).built_on = std::this_thread::get_id();
        return surface;
    }

private:
    std::shared_ptr<Probe> probe_;
    Color color_{};
};

class NullContent final : public SubSurfaceContent {
public:
    Control::Ptr build_root() override { return {}; }
};

// Posted to the sub-surface's thread: repaints its root in a new colour and
// records what a control there sees of its roots.
class Recolour final {
public:
    Recolour(std::shared_ptr<Probe> probe, Color color) : probe_(std::move(probe)), color_(color) {}
    void operator()() const {
        const std::lock_guard<std::mutex> lock((*probe_).mutex);
        const std::shared_ptr<DrawingSurface> surface = (*probe_).surface.lock();
        if (!surface) return;
        (*surface).background = color_;
        (*probe_).saw_no_window = (*surface).attached_window() == nullptr;
        (*probe_).saw_surface_root = (*surface).attached_surface_root() != nullptr;
        if (SurfaceRoot* const root = (*surface).attached_surface_root()) {
            (*probe_).text_scale = (*root).presentation_settings().text_scale;
            (*probe_).application_theme = &(*root).application_theme();
        }
    }

private:
    std::shared_ptr<Probe> probe_;
    Color color_{};
};

constexpr std::chrono::seconds wait_limit{10};

// Waits until the surface's newest frame carries at least `tag`.
[[nodiscard]] bool wait_for_content_tag(const LiveSurface& surface, std::uint64_t tag) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        const LiveSurfaceSnapshot snapshot = surface.snapshot();
        if (snapshot.has_frame && !snapshot.carries_previous_frame && snapshot.content_tag >= tag) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] bool wait_for_state(const SubSurfaceThread& thread, SubSurfaceState state) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (thread.state() == state) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] bool wait_for_published(const SubSurfaceThread& thread, std::uint64_t frames) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (thread.statistics().frames_published >= frames) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Whether every pixel of the frame is `color`.
[[nodiscard]] bool frame_is(const LiveSurfaceFrame& frame, Color color) {
    if (!frame) return false;
    const std::span<const std::byte> pixels = frame.pixels();
    for (std::uint32_t row = 0; row < frame.height(); ++row) {
        for (std::uint32_t column = 0; column < frame.width(); ++column) {
            const std::size_t at = static_cast<std::size_t>(row) * frame.row_bytes() + column * 4U;
            if (pixels[at] != static_cast<std::byte>(color.blue) ||
                pixels[at + 1U] != static_cast<std::byte>(color.green) ||
                pixels[at + 2U] != static_cast<std::byte>(color.red) ||
                pixels[at + 3U] != static_cast<std::byte>(color.alpha)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] SubSurfaceConfiguration configuration_of(std::uint64_t epoch, std::uint32_t width,
                                                       std::uint32_t height) {
    SubSurfaceConfiguration configuration;
    configuration.content_epoch = epoch;
    configuration.logical_size = Size{static_cast<double>(width), static_cast<double>(height)};
    configuration.scale = 1.0;
    configuration.pixel_width = width;
    configuration.pixel_height = height;
    configuration.opaque = true;
    configuration.framebuffer = std::make_unique<FillFramebuffer>(width, height, 1.0);
    return configuration;
}

constexpr Color red = Color::rgba(200, 20, 30);
constexpr Color green = Color::rgba(10, 180, 40);

// The thread alone: it builds on its own thread, produces nothing before the
// first configuration, publishes tagged frames, applies posted work, carries
// newest-wins configurations, and stops by joining.
void thread_produces_tagged_frames() {
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({40U, 20U});
    require(static_cast<bool>(surface), "surface");
    SubSurfaceThread thread(std::make_unique<SolidContent>(probe, red), surface, default_theme(), nullptr);
    require(wait_for_state(thread, SubSurfaceState::running), "thread reaches running");
    {
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).built_on != std::thread::id{} && (*probe).built_on != std::this_thread::get_id(),
                "content built on the sub-surface's own thread");
    }
    require(!(*surface).snapshot().has_frame, "nothing produced before the first agreed size");

    thread.configure(configuration_of(1U, 40U, 20U));
    require(wait_for_content_tag(*surface, 1U), "first frame published at epoch 1");
    require(frame_is((*surface).acquire_latest(), red), "first frame is the content's colour");

    const DispatchOperation posted = thread.dispatcher().begin_invoke(Recolour(probe, green));
    static_cast<void>(posted);
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (!frame_is((*surface).acquire_latest(), green) && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(frame_is((*surface).acquire_latest(), green), "posted work repaints on the sub-surface");
    {
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).saw_no_window, "a control in a sub-surface sees no window");
        require((*probe).saw_surface_root, "a control in a sub-surface sees its surface root");
    }

    // Idle: with nothing posted, dirty or due, the loop does not turn.
    const std::uint64_t turns = thread.statistics().loop_turns;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(thread.statistics().loop_turns == turns, "an idle sub-surface does not turn its loop");

    // Newest wins: a burst of configurations ends at the last one's size and
    // epoch; none is lost from the count.
    for (std::uint64_t epoch = 2U; epoch <= 21U; ++epoch) {
        const std::uint32_t width = 30U + static_cast<std::uint32_t>(epoch);
        thread.configure(configuration_of(epoch, width, 25U));
    }
    require(wait_for_content_tag(*surface, 21U), "the burst ends at epoch 21");
    const LiveSurfaceFrame last = (*surface).acquire_latest();
    require(last.width() == 51U && last.height() == 25U, "the newest configuration's device size");
    require(frame_is(last, green), "the resized frame is whole");
    const SubSurfaceStatistics statistics = thread.statistics();
    require(statistics.configurations_received == 21U, "every configuration counted");
    require(statistics.configurations_applied + statistics.configurations_coalesced == 21U,
            "each configuration applied or coalesced");
    require(statistics.published_content_epoch == 21U, "published epoch is the newest");
    require(statistics.deadline_wakes >= 1U, "without a presenter the pacer releases frames at its deadline");

    bool refused = false;
    try {
        thread.configure(configuration_of(21U, 10U, 10U));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a content epoch that does not grow is refused");

    thread.stop();
    require(thread.state() == SubSurfaceState::stopped, "stopped after stop");
    const std::uint64_t published = thread.statistics().frames_published;
    thread.configure(configuration_of(30U, 12U, 12U));
    require(thread.statistics().frames_published == published, "a configuration after stop is dropped");
    {
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).surface.expired(), "the tree is released when the thread stops");
    }
    require(!thread.dispatcher().window_alive(), "the dispatcher is closed after stop");
}

// Raises a flag on the thread that runs it: a posted call's completion.
class RaiseFlag final {
public:
    explicit RaiseFlag(std::shared_ptr<std::atomic<bool>> flag) : flag_(std::move(flag)) {}
    void operator()() const { (*flag_).store(true, std::memory_order_release); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

[[nodiscard]] bool wait_for_flag(const std::atomic<bool>& flag) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (flag.load(std::memory_order_acquire)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Posts `call` to the host's sub-surface thread, then a flag after it, and
// waits until the flag is raised: `call` has run, in that turn or before.
[[nodiscard]] bool run_on_sub_surface(SubSurfaceHost& host, const Recolour& call) {
    static_cast<void>(host.dispatcher().begin_invoke(call));
    std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
    static_cast<void>(host.dispatcher().begin_invoke(RaiseFlag(done)));
    const bool ran = wait_for_flag(*done);
    return ran;
}

// Waits until a call posted now has run: every turn before it has ended.
[[nodiscard]] bool finish_turn(SubSurfaceHost& host) {
    std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
    static_cast<void>(host.dispatcher().begin_invoke(RaiseFlag(done)));
    const bool ran = wait_for_flag(*done);
    return ran;
}

// Pairing (design note section 6): a newly configured epoch is produced at
// once, not at the pacer's next tick, and once per configuration. The
// presenter reports an hour's refresh period, so while a published frame is
// unshown the pacer holds the next for an hour and a half: anything
// produced within the test's wait went out without pacing.
void new_epochs_are_produced_without_pacing() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("paced.root"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("paced.host"));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 60.0, 30.0});
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    (*host).set_content(std::make_unique<SolidContent>(probe, red), true);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(static_cast<bool>(surface) && wait_for_content_tag(*surface, 1U), "the first epoch is published");

    // The first frame is shown; the second epoch's frame is left unshown.
    LiveDisplayTiming slow{};
    slow.refresh_period = std::chrono::duration_cast<FrameInterval>(std::chrono::hours(1));
    slow.last_vblank = FrameClock::now();
    slow.source = LiveTimingSource::display_phased;
    const LiveSurfaceFrame first = (*surface).acquire_latest();
    require(static_cast<bool>(first), "the first frame can be read");
    (*surface).notify_presented(first.generation(), slow);
    const std::uint64_t second = (*host).redraw_all(nullptr);
    require(second == 2U && wait_for_content_tag(*surface, second), "the second epoch is published");

    // The pacer now waits on an unshown frame; a third epoch does not.
    const SubSurfaceStatistics before = (*host).statistics();
    const std::uint64_t third = (*host).redraw_all(nullptr);
    require(third == 3U && wait_for_content_tag(*surface, third), "a new epoch is produced without waiting for the pacer");

    // Dirt without a configuration waits for the pacer: a recolour posted
    // after the third epoch, and a call after it, leave one frame only.
    require(run_on_sub_surface(*host, Recolour(probe, green)), "the recolour runs");
    require(finish_turn(*host), "a later turn runs");
    const SubSurfaceStatistics after = (*host).statistics();
    require(after.frames_published == before.frames_published + 1U, "one frame for the configuration");
    require(after.deadline_wakes == before.deadline_wakes, "no pacer deadline passed");
    require((*surface).snapshot().content_tag == third, "the newest frame is the third epoch's");
    static_cast<void>((*root).remove_child((*host).runtime_id()));
}

// Holds the sub-surface's thread inside a posted call until released.
class HoldThread final {
public:
    HoldThread(std::shared_ptr<std::atomic<bool>> entered, std::shared_ptr<std::atomic<bool>> released)
        : entered_(std::move(entered)), released_(std::move(released)) {}
    void operator()() const {
        (*entered_).store(true, std::memory_order_release);
        const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
        while (!(*released_).load(std::memory_order_acquire) && std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    std::shared_ptr<std::atomic<bool>> entered_;
    std::shared_ptr<std::atomic<bool>> released_;
};

// A configuration of the same device size carries no framebuffer (the host
// makes one per size). When it replaces the first configuration before the
// loop applied it, the first one's framebuffer is kept: the newer epoch is
// produced. (A grid sending its cells from its first paint did this on a
// slow machine, and its region never published.)
void coalesced_first_configuration_keeps_its_framebuffer() {
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({40U, 20U});
    require(static_cast<bool>(surface), "surface");
    SubSurfaceThread thread(std::make_unique<SolidContent>(probe, red), surface, default_theme(), nullptr);
    require(wait_for_state(thread, SubSurfaceState::running), "thread reaches running");
    std::shared_ptr<std::atomic<bool>> entered = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> released = std::make_shared<std::atomic<bool>>(false);
    static_cast<void>(thread.dispatcher().begin_invoke(HoldThread(entered, released)));
    require(wait_for_flag(*entered), "the thread is held");
    thread.configure(configuration_of(1U, 40U, 20U));
    SubSurfaceConfiguration same_size = configuration_of(2U, 40U, 20U);
    same_size.framebuffer.reset();
    thread.configure(std::move(same_size));
    (*released).store(true, std::memory_order_release);
    require(wait_for_content_tag(*surface, 2U), "the newer epoch is produced with the first one's framebuffer");
    const SubSurfaceStatistics statistics = thread.statistics();
    require(statistics.configurations_coalesced == 1U && statistics.configurations_applied == 1U,
            "the two configurations were applied as one");
}

void content_failure_is_reported() {
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({8U, 8U});
    SubSurfaceThread thread(std::make_unique<NullContent>(), surface, default_theme(), nullptr);
    require(wait_for_state(thread, SubSurfaceState::failed), "a null root fails the sub-surface");
    require(!thread.failure().empty(), "the failure is named");
    thread.configure(configuration_of(1U, 8U, 8U));
    thread.stop();
    require(thread.state() == SubSurfaceState::failed, "stop keeps the failure");
}

// Start and stop in every phase: before the tree exists, before the first
// configuration, and while frames are produced.
void start_stop_stress() {
    for (int round = 0; round < 60; ++round) {
        std::shared_ptr<Probe> probe = std::make_shared<Probe>();
        std::shared_ptr<LiveSurface> surface = LiveSurface::create({16U, 16U});
        SubSurfaceThread thread(std::make_unique<SolidContent>(probe, red), surface, default_theme(), nullptr);
        if (round % 3 >= 1) thread.configure(configuration_of(1U, 16U, 16U));
        if (round % 3 == 2) {
            for (std::uint64_t epoch = 2U; epoch < 6U; ++epoch) {
                thread.configure(configuration_of(epoch, 16U + static_cast<std::uint32_t>(epoch), 16U));
                static_cast<void>(thread.dispatcher().window_alive());
            }
        }
        thread.stop();
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).surface.expired(), "every round releases its tree");
    }
}

// The host control in a window: starts on attach at its first size,
// registers its surface for presentation with the pairing requirement,
// reconfigures on a device-size change only, and stops on detach.
void host_in_window() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.root"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("sub.host"));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 60.0, 30.0});
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    (*host).set_content(std::make_unique<SolidContent>(probe, red), true);
    require(!(*host).started(), "no thread before the host is in a window");
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require((*host).started(), "the host starts once arranged in a window");
    require((*host).content_epoch() == 1U, "one configuration for the first size");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(static_cast<bool>(surface), "the host made its surface");
    require(wait_for_content_tag(*surface, 1U), "the sub-surface publishes at epoch 1");
    const LiveSurfaceFrame first = (*surface).acquire_latest();
    require(first.width() == 60U && first.height() == 30U && first.opaque(), "device-sized opaque frames");
    require(frame_is(first, red), "the host's region shows the content");
    const std::vector<LiveSurfacePresentation> presentations = window.take_live_surface_presentations(true);
    bool registered = false;
    for (const LiveSurfacePresentation& presentation : presentations) {
        if (presentation.surface == surface) registered = true;
    }
    require(registered, "the window composes the host's surface");

    // Same device size: nothing. New size: one new epoch, new surface size.
    (*host).invalidate(Dirty::arrange);
    window.flush();
    require((*host).content_epoch() == 1U, "an arrangement at the same size signals nothing");
    (*host).set_requested_bounds(Rect{10.0, 10.0, 80.0, 40.0});
    window.flush();
    require((*host).content_epoch() == 2U, "a new device size opens a new content epoch");
    require(wait_for_content_tag(*surface, 2U), "the sub-surface publishes at epoch 2");
    const LiveSurfaceFrame resized = (*surface).acquire_latest();
    require(resized.width() == 80U && resized.height() == 40U, "the surface follows the device size");
    require(frame_is(resized, red), "the resized frame is whole");

    // A scale change is a device-size change too.
    window.set_scale(2.0);
    window.flush();
    require((*host).content_epoch() == 3U, "a scale change opens a new content epoch");
    require(wait_for_content_tag(*surface, 3U), "the sub-surface publishes at epoch 3");
    const LiveSurfaceFrame scaled = (*surface).acquire_latest();
    require(scaled.width() == 160U && scaled.height() == 80U, "device pixels at scale 2");
    require(frame_is(scaled, red), "the scaled frame is whole");

    // The environment travels with a new epoch, without a new size: the
    // sub-surface takes the window's presentation settings and theme.
    PresentationSettings settings = window.presentation_settings();
    settings.text_scale = 1.5;
    window.set_presentation_settings(settings);
    window.flush();
    require((*host).content_epoch() == 4U, "presentation settings open a new content epoch");
    const std::shared_ptr<const Theme> other = Theme::create(windows_professional_theme_definition());
    window.set_theme(other);
    window.flush();
    require((*host).content_epoch() == 5U, "a theme change opens a new content epoch");
    require(wait_for_content_tag(*surface, 5U), "the sub-surface publishes at epoch 5");
    const DispatchOperation probed = (*host).dispatcher().begin_invoke(Recolour(probe, red));
    static_cast<void>(probed);
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    bool environment_seen = false;
    while (!environment_seen && std::chrono::steady_clock::now() < limit) {
        {
            const std::lock_guard<std::mutex> lock((*probe).mutex);
            environment_seen = (*probe).text_scale == 1.5 && (*probe).application_theme == other.get();
        }
        if (!environment_seen) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(environment_seen, "the sub-surface shows the window's text scale and theme");

    static_cast<void>((*root).remove_child((*host).runtime_id()));
    require(!(*host).started(), "leaving the window stops and joins the thread");
    const std::lock_guard<std::mutex> lock((*probe).mutex);
    require((*probe).surface.expired(), "the sub-surface's tree is released");
}

// What the host's notices raised, in order: written and read on the test's
// (the window's) thread.
struct NoticeLog final {
    std::vector<SubSurfaceStopped> stopped;
    std::vector<std::uint64_t> pairing_missed;
};

class RecordStopped final {
public:
    explicit RecordStopped(std::shared_ptr<NoticeLog> log) : log_(std::move(log)) {}
    void operator()(const SubSurfaceStopped& stopped) const { (*log_).stopped.push_back(stopped); }

private:
    std::shared_ptr<NoticeLog> log_;
};

class RecordPairingMissed final {
public:
    explicit RecordPairingMissed(std::shared_ptr<NoticeLog> log) : log_(std::move(log)) {}
    void operator()(std::uint64_t epoch) const { (*log_).pairing_missed.push_back(epoch); }

private:
    std::shared_ptr<NoticeLog> log_;
};

// A redraw-all update the sub-surface holds until the test releases it, so
// the epoch it rides publishes no frame in time.
struct HeldUpdate final : public SubSurfaceContentUpdate {
    explicit HeldUpdate(std::shared_ptr<std::atomic<bool>> released) : released(std::move(released)) {}
    std::shared_ptr<std::atomic<bool>> released;
};

// A solid root whose updates wait for their release (HeldUpdate).
class HoldingContent final : public SubSurfaceContent {
public:
    Control::Ptr build_root() override {
        std::shared_ptr<DrawingSurface> surface = std::make_shared<DrawingSurface>(StableId("sub.holding"));
        (*surface).background = green;
        return surface;
    }
    void apply_update(SubSurfaceContentUpdate& update) override {
        HeldUpdate* const held = dynamic_cast<HeldUpdate*>(&update);
        if (held == nullptr) return;
        const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
        while (!(*(*held).released).load(std::memory_order_acquire) && std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

// Runs the window's posted work until the log holds `stopped` stop notices
// and `missed` pairing notices.
[[nodiscard]] bool drain_until_noticed(Window& window, const NoticeLog& log, std::size_t stopped,
                                       std::size_t missed) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        static_cast<void>(window.drain_posted_work());
        if (log.stopped.size() >= stopped && log.pairing_missed.size() >= missed) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Decision 6 and section 9: `stopped` once per thread, for a failure and
// for a stop the host made, and `pairing_missed` once per epoch presented
// unpaired; each raised by posted work, never inside the stop or the
// present that found it.
void host_notices_stops_and_missed_pairings() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.root"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("sub.host"));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 40.0, 20.0});
    std::shared_ptr<NoticeLog> log = std::make_shared<NoticeLog>();
    SubscriptionToken stopped = (*host).stopped().subscribe(RecordStopped(log));
    SubscriptionToken missed = (*host).pairing_missed().subscribe(RecordPairingMissed(log));
    (*host).set_content(std::make_unique<NullContent>(), true);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require(drain_until_noticed(window, *log, 1U, 0U), "a failed content raises stopped");
    require((*log).stopped[0].state == SubSurfaceState::failed && !(*log).stopped[0].failure.empty(),
            "stopped names the failure");

    // Replacing the failed content: its end was told already.
    (*host).set_content(std::make_unique<HoldingContent>(), true);
    window.flush();
    static_cast<void>(window.drain_posted_work());
    require((*log).stopped.size() == 1U, "a noticed failure is not told again when stopped");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(static_cast<bool>(surface), "the new content started");
    const std::uint64_t first = (*host).content_epoch();
    require(wait_for_content_tag(*surface, first), "the new content publishes");
    static_cast<void>(window.take_live_surface_presentations(true));

    // An epoch held past the one-frame wait is presented unpaired once and
    // told once, after the present.
    std::shared_ptr<std::atomic<bool>> released = std::make_shared<std::atomic<bool>>(false);
    const std::uint64_t held = (*host).redraw_all(std::make_unique<HeldUpdate>(released));
    require(held == first + 1U, "the held update rides a new epoch");
    static_cast<void>(window.take_live_surface_presentations(true));
    require(window.metrics().snapshot().live_unpaired_presentations == 0U, "the first present waits");
    static_cast<void>(window.take_live_surface_presentations(true));
    require(window.metrics().snapshot().live_unpaired_presentations >= 1U, "the second presents unpaired");
    require((*log).pairing_missed.empty(), "nothing is raised inside the present");
    require(drain_until_noticed(window, *log, 1U, 1U), "the missed pairing is told by posted work");
    require((*log).pairing_missed[0] == held, "pairing_missed carries the required epoch");
    static_cast<void>(window.take_live_surface_presentations(true));
    static_cast<void>(window.drain_posted_work());
    require((*log).pairing_missed.size() == 1U, "one notice per epoch");
    (*released).store(true, std::memory_order_release);
    require(wait_for_content_tag(*surface, held), "the released epoch publishes");

    // A stop the host makes is told after it, not within it.
    (*host).set_content(nullptr, true);
    require((*log).stopped.size() == 1U, "stopped is not raised inside set_content");
    require(drain_until_noticed(window, *log, 2U, 1U), "the host's stop raises stopped");
    require((*log).stopped[1].state == SubSurfaceState::stopped && (*log).stopped[1].failure.empty(),
            "a host stop is stopped, without a failure");

    // A host destroyed before its notice runs raises nothing.
    (*host).set_content(std::make_unique<HoldingContent>(), true);
    window.flush();
    static_cast<void>((*root).remove_child((*host).runtime_id()));
    host.reset();
    static_cast<void>(window.drain_posted_work());
    require((*log).stopped.size() == 2U, "a released host raises nothing");
}

// Stage S3: input and focus forwarding (design note section 4).

// What the sub-surface's input controls saw, in order: written on the
// sub-surface's thread, read by the test, under the mutex.
struct InputLog final {
    std::mutex mutex;
    std::vector<std::string> events;
    Point last_pointer{};
};

// A focusable control that logs its focus, keys and pointer presses. It
// handles every key but Tab, which goes on to traversal, and names a text
// input caret at (5, 3), one unit wide and twelve high.
class InputRecorder final : public Control {
public:
    InputRecorder(StableId stable_id, std::string name, std::shared_ptr<InputLog> log)
        : Control(std::move(stable_id)), name_(std::move(name)), log_(std::move(log)) {
        focusable = true;
    }

    [[nodiscard]] std::optional<Rect> text_input_caret() const override { return Rect{5.0, 3.0, 1.0, 12.0}; }

protected:
    void on_focus_changed(bool focused) override {
        record(focused ? ":focus" : ":blur");
    }
    void on_key(KeyEvent& event) override {
        if (event.physical_key == PhysicalKey::tab) return;
        if (event.action == KeyAction::down) record(":key");
        event.handled = true;
    }
    void on_pointer(PointerEvent& event) override {
        if (event.action != PointerAction::down) return;
        record(":down");
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        (*log_).last_pointer = point_from_window(event.position);
        event.handled = true;
    }

private:
    void record(const char* what) {
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        (*log_).events.push_back(name_ + what);
    }

    std::string name_;
    std::shared_ptr<InputLog> log_;
};

// Two input recorders side by side: "A" at (0, 0, 50, 30) and "B" at
// (50, 0, 50, 30).
class TwoStopsContent final : public SubSurfaceContent {
public:
    explicit TwoStopsContent(std::shared_ptr<InputLog> log) : log_(std::move(log)) {}
    Control::Ptr build_root() override {
        std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.stops"));
        std::shared_ptr<InputRecorder> first = std::make_shared<InputRecorder>(StableId("sub.a"), "A", log_);
        std::shared_ptr<InputRecorder> second = std::make_shared<InputRecorder>(StableId("sub.b"), "B", log_);
        (*first).set_requested_bounds(Rect{0.0, 0.0, 50.0, 30.0});
        (*second).set_requested_bounds(Rect{50.0, 0.0, 50.0, 30.0});
        (*root).add_child(first);
        (*root).add_child(second);
        return root;
    }

private:
    std::shared_ptr<InputLog> log_;
};

// The test's key return target: collects returned keys on the test thread.
class CollectReturnedKeys final : public SubSurfaceKeyReturn {
public:
    void return_unhandled_key(const KeyEvent& event, std::uint64_t sequence) override {
        keys.push_back(event);
        sequences.push_back(sequence);
    }
    std::vector<KeyEvent> keys;
    std::vector<std::uint64_t> sequences;
};

[[nodiscard]] KeyEvent key_down(std::uint32_t key, Modifier modifiers = Modifier::none) {
    KeyEvent event;
    event.action = KeyAction::down;
    event.physical_key = key;
    event.modifiers = modifiers;
    return event;
}

[[nodiscard]] SubSurfaceInput key_input(std::uint32_t key, Modifier modifiers = Modifier::none) {
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::key;
    input.key = key_down(key, modifiers);
    return input;
}

[[nodiscard]] std::vector<std::string> events_of(InputLog& log) {
    const std::lock_guard<std::mutex> lock(log.mutex);
    return log.events;
}

[[nodiscard]] bool wait_for_inputs(const SubSurfaceThread& thread, std::uint64_t inputs) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (thread.statistics().inputs_dispatched >= inputs) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] bool wait_for_host_inputs(const SubSurfaceHost& host, std::uint64_t inputs) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (host.statistics().inputs_dispatched >= inputs) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// A focusable control that names no caret.
class PlainStop final : public Control {
public:
    explicit PlainStop(StableId stable_id) : Control(std::move(stable_id)) { focusable = true; }
};

// Waits until the window's text input caret is `expected`.
[[nodiscard]] bool wait_for_caret(Window& window, Rect expected) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        const std::optional<Rect> caret = window.text_input_caret();
        if (caret && *caret == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Runs the window's posted work until `focused` has the window's focus.
[[nodiscard]] bool drain_until_focused(Window& window, const Control::Ptr& focused) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        static_cast<void>(window.drain_posted_work());
        if (window.focused_control() == focused) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// The thread alone: input is dispatched in posting order with growing
// sequences and none dropped; focus enters at the first stop; Tab moves
// inside and, at the last stop, comes back to the parent instead of
// wrapping; pointer positions arrive in the sub-surface's coordinates.
// Without a caret of its own, the focused control's left edge is used.
void caret_falls_back_to_left_edge() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("caret.root"));
    std::shared_ptr<PlainStop> stop = std::make_shared<PlainStop>(StableId("caret.stop"));
    (*stop).set_requested_bounds(Rect{10.0, 12.0, 30.0, 20.0});
    (*root).add_child(stop);
    Window window(root, Size{100.0, 100.0});
    window.flush();
    require(!window.text_input_caret(), "no caret without focus");
    require(window.request_focus(stop), "focus the stop");
    const std::optional<Rect> caret = window.text_input_caret();
    require(caret && *caret == Rect{10.0, 12.0, 1.0, 20.0}, "the left edge, one unit wide");
}

void input_reaches_sub_surface_in_order() {
    std::shared_ptr<InputLog> log = std::make_shared<InputLog>();
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({100U, 30U});
    require(static_cast<bool>(surface), "surface");
    SubSurfaceThread thread(std::make_unique<TwoStopsContent>(log), surface, default_theme(), nullptr);
    std::shared_ptr<Panel> parent_root = std::make_shared<Panel>(StableId("sub.parent"));
    Window parent(parent_root, Size{100.0, 100.0});
    std::shared_ptr<CollectReturnedKeys> returned = std::make_shared<CollectReturnedKeys>();
    thread.set_key_return(parent.dispatcher(), returned);
    require(wait_for_state(thread, SubSurfaceState::running), "thread reaches running");
    thread.configure(configuration_of(1U, 100U, 30U));

    SubSurfaceInput enter;
    enter.kind = SubSurfaceInputKind::focus_enter;
    enter.focus_entry = SubSurfaceFocusEntry::first;
    require(thread.post_input(enter) == 1U, "the first input has sequence 1");
    constexpr std::uint64_t letters = 200U;
    for (std::uint64_t index = 0; index < letters; ++index) {
        require(thread.post_input(key_input(PhysicalKey::a)) == index + 2U, "sequences grow by one");
    }
    const std::uint64_t to_b = thread.post_input(key_input(PhysicalKey::tab));
    const std::uint64_t out = thread.post_input(key_input(PhysicalKey::tab));
    SubSurfaceInput press;
    press.kind = SubSurfaceInputKind::pointer;
    press.pointer.action = PointerAction::down;
    press.pointer.button = PointerButton::primary;
    press.pointer.position = Point{62.0, 7.0};
    const std::uint64_t last = thread.post_input(press);
    require(last == letters + 4U, "every input was sequenced");
    require(wait_for_inputs(thread, last), "every input is dispatched");
    const SubSurfaceStatistics statistics = thread.statistics();
    require(statistics.inputs_received == last && statistics.inputs_dispatched == last,
            "no input is dropped");
    require(statistics.last_input_sequence == last, "the newest dispatched is the newest posted");
    require(statistics.keys_returned == 1U, "only the Tab past the last stop comes back");

    const std::vector<std::string> events = events_of(*log);
    require(events.size() == letters + 4U, "focus, the letters, the Tab move and the press");
    require(events.front() == "A:focus", "focus enters at the first stop");
    for (std::uint64_t index = 1; index <= letters; ++index) {
        require(events[index] == "A:key", "the letters reach the focused control in order");
    }
    require(events[letters + 1U] == "A:blur" && events[letters + 2U] == "B:focus", "Tab moves inside");
    require(events.back() == "B:down", "the press reaches the control under it");
    {
        const std::lock_guard<std::mutex> lock((*log).mutex);
        require((*log).last_pointer.x == 12.0 && (*log).last_pointer.y == 7.0,
                "pointer positions are in the sub-surface's coordinates");
    }

    // The returned Tab runs on the parent's thread, at its dispatcher turn.
    require((*returned).keys.empty(), "nothing is delivered before the parent's turn");
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while ((*returned).keys.empty() && std::chrono::steady_clock::now() < limit) {
        static_cast<void>(parent.drain_posted_work());
        if ((*returned).keys.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require((*returned).keys.size() == 1U, "the Tab comes back once");
    require((*returned).keys.front().physical_key == PhysicalKey::tab, "it is the Tab");
    require((*returned).sequences.front() == out && out == to_b + 1U, "with its own sequence");

    // After stop() input is refused, not queued.
    thread.stop();
    require(thread.post_input(key_input(PhysicalKey::a)) == 0U, "a stopped sub-surface takes no input");
}

// The host in a window: Tab enters the sub-surface at its first stop and
// leaves it past the last; Shift+Tab enters at the last stop; a press on
// the host reaches the control under it in local coordinates; window
// deactivation leaves the sub-surface's keyboard focus.
void host_forwards_input_and_focus() {
    std::shared_ptr<InputLog> outer = std::make_shared<InputLog>();
    std::shared_ptr<InputLog> inner = std::make_shared<InputLog>();
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.root"));
    std::shared_ptr<InputRecorder> before =
        std::make_shared<InputRecorder>(StableId("sub.before"), "before", outer);
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("sub.host"));
    std::shared_ptr<InputRecorder> after = std::make_shared<InputRecorder>(StableId("sub.after"), "after", outer);
    (*before).set_requested_bounds(Rect{0.0, 0.0, 20.0, 20.0});
    (*host).set_requested_bounds(Rect{20.0, 40.0, 100.0, 30.0});
    (*after).set_requested_bounds(Rect{130.0, 0.0, 20.0, 20.0});
    (*host).set_content(std::make_unique<TwoStopsContent>(inner), true);
    (*root).add_child(before);
    (*root).add_child(host);
    (*root).add_child(after);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require((*host).started(), "the host starts");
    require(window.request_focus(before), "focus starts before the host");

    // Tab onto the host: focus enters at A.
    require(window.dispatch_key(key_down(PhysicalKey::tab)), "Tab moves onto the host");
    require(window.focused_control() == host, "the host has the window's focus");
    require(wait_for_host_inputs(*host, 1U), "focus entry is dispatched");
    std::vector<std::string> events = events_of(*inner);
    require(!events.empty() && events.back() == "A:focus", "Tab enters at the first stop");
    // The input method follows A's caret, published by the sub-surface and
    // placed in the window by the host's offset.
    require(wait_for_caret(window, Rect{25.0, 43.0, 1.0, 12.0}), "the caret is the sub-surface's, in the window");

    // Tab inside: B. Tab again: back to the window, which moves past the host.
    require(window.dispatch_key(key_down(PhysicalKey::tab)), "the host takes Tab");
    require(window.dispatch_key(key_down(PhysicalKey::tab)), "the host takes the second Tab");
    require(drain_until_focused(window, after), "the returned Tab leaves the sub-surface");
    events = events_of(*inner);
    require(events.size() >= 3U && events[events.size() - 2U] == "A:blur" && events.back() == "B:focus",
            "the first Tab moved inside");

    // Shift+Tab from after: focus enters at the last stop, B.
    require(window.dispatch_key(key_down(PhysicalKey::tab, Modifier::shift)), "Shift+Tab onto the host");
    require(window.focused_control() == host, "the host has the window's focus again");
    const std::uint64_t entered = (*host).statistics().inputs_received;
    require(wait_for_host_inputs(*host, entered), "the second entry is dispatched");
    events = events_of(*inner);
    require(events.back() == "B:focus", "Shift+Tab enters at the last stop");
    require(wait_for_caret(window, Rect{75.0, 43.0, 1.0, 12.0}), "the caret follows focus to B");

    // A press on A's part of the host, in window coordinates.
    PointerEvent press;
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = Point{20.0 + 13.0, 40.0 + 9.0};
    require(window.dispatch_pointer(press), "the host takes the press");
    PointerEvent release = press;
    release.action = PointerAction::up;
    static_cast<void>(window.dispatch_pointer(release));
    const std::uint64_t pressed = (*host).statistics().inputs_received;
    require(wait_for_host_inputs(*host, pressed), "the press is dispatched");
    events = events_of(*inner);
    require(std::find(events.begin(), events.end(), std::string("A:down")) != events.end(),
            "the press reaches A");
    {
        const std::lock_guard<std::mutex> lock((*inner).mutex);
        require((*inner).last_pointer.x == 13.0 && (*inner).last_pointer.y == 9.0,
                "in the sub-surface's coordinates");
    }

    // A secondary press holds the window's capture for the host, so a drag
    // that leaves the region still reaches the sub-surface; the release
    // gives it back.
    PointerEvent context = press;
    context.button = PointerButton::secondary;
    require(window.dispatch_pointer(context), "the host takes a secondary press");
    require(window.captured_control() == host, "the host holds the capture");
    PointerEvent outside;
    outside.action = PointerAction::move;
    outside.position = Point{195.0, 95.0};
    const std::uint64_t before_drag = (*host).statistics().inputs_received;
    static_cast<void>(window.dispatch_pointer(outside));
    require((*host).statistics().inputs_received == before_drag + 1U, "a move outside the region still goes in");
    PointerEvent context_up = context;
    context_up.action = PointerAction::up;
    context_up.position = outside.position;
    static_cast<void>(window.dispatch_pointer(context_up));
    require(!window.captured_control(), "the release gives the capture back");

    // Deactivation leaves the sub-surface's keyboard focus and activation
    // restores it, at the control that had it; letters reach that control.
    const std::uint64_t before_activation = (*host).statistics().inputs_received;
    window.set_active(false);
    window.set_active(true);
    require((*host).statistics().inputs_received == before_activation + 2U,
            "deactivation and activation each tell the sub-surface");
    require(window.dispatch_key(key_down(PhysicalKey::a)), "the host takes a letter");
    const std::uint64_t typed = (*host).statistics().inputs_received;
    require(wait_for_host_inputs(*host, typed), "the letter is dispatched");
    events = events_of(*inner);
    require(events.back() == "A:key", "the letter reaches the restored focus");
    const SubSurfaceStatistics statistics = (*host).statistics();
    require(statistics.inputs_dispatched == statistics.inputs_received, "no input is dropped");
    require(statistics.keys_returned == 1U, "one key came back");
    require((*host).returned_keys_discarded() == 0U, "and it was used");
    events = events_of(*outer);
    require(std::find(events.begin(), events.end(), std::string("after:focus")) != events.end(),
            "the window's own traversal moved on");
}

// Stage S5: accessibility (design note section 5).

// A focusable button-like node that logs its semantic actions, and one
// virtual child, "<id>.item", that takes select.
class SemanticStop final : public Control {
public:
    SemanticStop(StableId stable_id, std::string name, std::shared_ptr<InputLog> log)
        : Control(std::move(stable_id)), name_(std::move(name)), log_(std::move(log)) {
        focusable = true;
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::button;
        descriptor.name = name_;
        descriptor.actions = {SemanticAction::press};
        descriptor.exposed = true;
        return descriptor;
    }
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        SemanticNode item;
        item.stable_id = std::string(stable_id().value()) + ".item";
        item.role = SemanticRole::list_item;
        item.name = name_ + " item";
        item.bounds = Rect{absolute_bounds().x + 2.0, absolute_bounds().y + 2.0, 10.0, 10.0};
        item.actions = {SemanticAction::select};
        std::vector<SemanticNode> children;
        children.push_back(std::move(item));
        return children;
    }
    bool on_semantic_action(SemanticAction action, std::string_view) override {
        if (action != SemanticAction::press) return false;
        record(":press");
        return true;
    }
    bool on_semantic_child_action(std::string_view child, SemanticAction action, std::string_view) override {
        if (child != std::string(stable_id().value()) + ".item" || action != SemanticAction::select) return false;
        record(":item:select");
        return true;
    }
    void rename(std::string name) {
        name_ = std::move(name);
        invalidate(Dirty::semantics);
    }

protected:
    void on_focus_changed(bool focused) override { record(focused ? ":focus" : ":blur"); }

private:
    void record(const char* what) {
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        (*log_).events.push_back(name_ + what);
    }

    std::string name_;
    std::shared_ptr<InputLog> log_;
};

// The sub-surface's semantic stops, held weakly for posted work.
struct SemanticStops final {
    std::mutex mutex;
    std::weak_ptr<SemanticStop> first;
};

// Two semantic stops: "first" at (0, 0, 50, 30) and "second" at
// (50, 0, 50, 30).
class SemanticContent final : public SubSurfaceContent {
public:
    SemanticContent(std::shared_ptr<InputLog> log, std::shared_ptr<SemanticStops> stops)
        : log_(std::move(log)), stops_(std::move(stops)) {}
    Control::Ptr build_root() override {
        std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.semantic"));
        std::shared_ptr<SemanticStop> first = std::make_shared<SemanticStop>(StableId("sub.first"), "first", log_);
        std::shared_ptr<SemanticStop> second =
            std::make_shared<SemanticStop>(StableId("sub.second"), "second", log_);
        (*first).set_requested_bounds(Rect{0.0, 0.0, 50.0, 30.0});
        (*second).set_requested_bounds(Rect{50.0, 0.0, 50.0, 30.0});
        (*root).add_child(first);
        (*root).add_child(second);
        const std::lock_guard<std::mutex> lock((*stops_).mutex);
        (*stops_).first = first;
        return root;
    }

private:
    std::shared_ptr<InputLog> log_;
    std::shared_ptr<SemanticStops> stops_;
};

// Posted to the sub-surface's thread: renames the first stop.
class RenameFirst final {
public:
    RenameFirst(std::shared_ptr<SemanticStops> stops, std::string name)
        : stops_(std::move(stops)), name_(std::move(name)) {}
    void operator()() const {
        std::shared_ptr<SemanticStop> first;
        {
            const std::lock_guard<std::mutex> lock((*stops_).mutex);
            first = (*stops_).first.lock();
        }
        if (first) (*first).rename(name_);
    }

private:
    std::shared_ptr<SemanticStops> stops_;
    std::string name_;
};

// The node with `stable_id` in `nodes` and below, or null.
[[nodiscard]] const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* const below = find_node(node.children, stable_id);
        if (below != nullptr) return below;
    }
    return nullptr;
}

const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

// What wait_for_node checks of a node.
enum class NodeCheck : std::uint8_t { present, absent, focused, named_renamed };

[[nodiscard]] bool node_passes(const SemanticSnapshot& snapshot, std::string_view stable_id, NodeCheck check) {
    const SemanticNode* const node = find_node(snapshot.roots, stable_id);
    switch (check) {
    case NodeCheck::present:
        return node != nullptr;
    case NodeCheck::absent:
        return node == nullptr;
    case NodeCheck::focused:
        return node != nullptr && has_semantic_state((*node).states, SemanticState::focused);
    case NodeCheck::named_renamed:
        return node != nullptr && (*node).name == "renamed";
    default:
        return false;
    }
}

// Runs the window's posted work until its semantic snapshot passes `check`
// for `stable_id`.
[[nodiscard]] bool wait_for_node(Window& window, std::string_view stable_id, NodeCheck check) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        static_cast<void>(window.drain_posted_work());
        const SemanticSnapshot snapshot = window.semantic_snapshot();
        if (node_passes(snapshot, stable_id, check)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] bool wait_for_event(InputLog& log, const std::string& event) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        {
            const std::lock_guard<std::mutex> lock(log.mutex);
            if (std::find(log.events.begin(), log.events.end(), event) != log.events.end()) return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return false;
}

[[nodiscard]] double elapsed_microseconds(std::chrono::steady_clock::time_point since) {
    const std::chrono::duration<double, std::micro> elapsed = std::chrono::steady_clock::now() - since;
    return elapsed.count();
}

// The thread alone: it publishes a snapshot once configured and again when
// its semantic generation moves, and a posted action naming a runtime
// identity its tree does not hold is refused there.
void thread_publishes_semantics() {
    std::shared_ptr<InputLog> log = std::make_shared<InputLog>();
    std::shared_ptr<SemanticStops> stops = std::make_shared<SemanticStops>();
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({100U, 30U});
    require(static_cast<bool>(surface), "surface");
    SubSurfaceThread thread(std::make_unique<SemanticContent>(log, stops), surface, default_theme(), nullptr);
    require(wait_for_state(thread, SubSurfaceState::running), "thread reaches running");
    require(!thread.take_semantics(), "nothing is published before the first configuration");
    thread.configure(configuration_of(1U, 100U, 30U));
    require(wait_for_published(thread, 1U), "the first frame is published");
    std::shared_ptr<const SemanticSnapshot> first = thread.take_semantics();
    require(static_cast<bool>(first), "a snapshot is published with the first configuration");
    const SemanticNode* const stop = find_node((*first).roots, "sub.first");
    require(stop != nullptr && (*stop).bounds == Rect{0.0, 0.0, 50.0, 30.0} &&
                !is_virtual_semantic_runtime_id((*stop).runtime_id),
            "the snapshot is the tree's own, in its coordinates");
    require(find_node((*first).roots, "sub.first.item") != nullptr, "with the virtual children");
    require(thread.statistics().semantic_snapshots_published == 1U, "one snapshot");

    const DispatchOperation renamed = thread.dispatcher().begin_invoke(RenameFirst(stops, "renamed"));
    static_cast<void>(renamed);
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (thread.statistics().semantic_snapshots_published < 2U && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::shared_ptr<const SemanticSnapshot> second = thread.take_semantics();
    require(second && (*second).generation > (*first).generation, "a semantic change publishes again");
    const SemanticNode* const named = find_node((*second).roots, "sub.first");
    require(named != nullptr && (*named).name == "renamed", "the new snapshot shows the change");
    require((*find_node((*first).roots, "sub.first")).name == "first", "the old snapshot is unchanged");

    SubSurfaceInput stale;
    stale.kind = SubSurfaceInputKind::semantic_action;
    stale.semantic.control = RuntimeId{(*stop).runtime_id + 1000000U};
    stale.semantic.action = SemanticAction::press;
    const std::uint64_t sequence = thread.post_input(stale);
    require(sequence != 0U && wait_for_inputs(thread, sequence), "the action is dispatched");
    const SubSurfaceStatistics statistics = thread.statistics();
    require(statistics.semantic_actions_refused == 1U && statistics.semantic_actions_performed == 0U,
            "an identity the tree does not hold is refused");
}

// The host in a window: the sub-surface's nodes are the host's children in
// the window's snapshot, translated into the window; actions on them are
// posted and performed on the sub-surface's thread; focus follows the
// host; a stopped sub-surface's nodes leave at once, and a restarted one's
// are new nodes.
void host_stitches_semantics() {
    std::shared_ptr<InputLog> log = std::make_shared<InputLog>();
    std::shared_ptr<SemanticStops> stops = std::make_shared<SemanticStops>();
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.root"));
    std::shared_ptr<PlainStop> before = std::make_shared<PlainStop>(StableId("sub.before"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("sub.host"));
    (*before).set_requested_bounds(Rect{0.0, 0.0, 20.0, 20.0});
    (*host).set_requested_bounds(Rect{20.0, 40.0, 100.0, 30.0});
    (*host).set_content(std::make_unique<SemanticContent>(log, stops), true);
    (*root).add_child(before);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require(window.request_focus(before), "focus starts outside the host");
    require(wait_for_node(window, "sub.host.sub.second", NodeCheck::present), "the sub-surface's nodes arrive");

    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* const host_node = find_node(snapshot.roots, "sub.host");
    require(host_node != nullptr && find_node((*host_node).children, "sub.host.sub.first") != nullptr &&
                find_node((*host_node).children, "sub.host.sub.second") != nullptr,
            "the sub-surface's nodes are below the host node");
    const SemanticNode* const first = find_node(snapshot.roots, "sub.host.sub.first");
    require(first != nullptr && (*first).role == SemanticRole::button && (*first).name == "first",
            "a node keeps its role and name");
    require(is_virtual_semantic_runtime_id((*first).runtime_id), "it is a virtual child of the host");
    require((*first).bounds == Rect{20.0, 40.0, 50.0, 30.0}, "its bounds are in the window");
    const SemanticNode* const item = find_node(snapshot.roots, "sub.host.sub.first.item");
    require(item != nullptr && (*item).bounds == Rect{22.0, 42.0, 10.0, 10.0}, "so are a virtual child's");
    require(item != nullptr && (*item).runtime_id != (*first).runtime_id &&
                (*item).runtime_id != virtual_semantic_runtime_id((*item).stable_id),
            "runtime identities come from the sub-surface's, under the host");
    require(!has_semantic_state((*first).states, SemanticState::focused), "nothing inside is focused");

    // Actions, posted. M8 (design note section 12): the round trip of a
    // posted action and the cost of the window's snapshot with the
    // sub-surface's nodes in it.
    const std::chrono::steady_clock::time_point acted = std::chrono::steady_clock::now();
    require(window.perform_semantic_action((*host).runtime_id(), "sub.host.sub.first", SemanticAction::press),
            "an action on a sub-surface node is posted");
    require(wait_for_event(*log, "first:press"), "and performed on the sub-surface's thread");
    const double round_trip = elapsed_microseconds(acted);
    require(window.perform_semantic_action("sub.host.sub.first.item", SemanticAction::select),
            "a virtual child's action, by stable identity");
    require(wait_for_event(*log, "first:item:select"), "reaches the sub-surface's virtual child");
    require(!window.perform_semantic_action((*host).runtime_id(), "sub.host.sub.first", SemanticAction::expand),
            "an action the node does not offer is refused");
    require(!window.perform_semantic_action((*host).runtime_id(), "sub.host.sub.none", SemanticAction::press),
            "a node the sub-surface does not publish is refused");
    require(!window.perform_semantic_action("sub.first", SemanticAction::press),
            "the sub-surface's own identity is not the window's");
    const std::chrono::steady_clock::time_point read = std::chrono::steady_clock::now();
    constexpr int reads = 100;
    for (int index = 0; index < reads; ++index) {
        const SemanticSnapshot again = window.semantic_snapshot();
        require(again.node_count == snapshot.node_count, "reads are stable");
    }
    const double read_cost = elapsed_microseconds(read) / static_cast<double>(reads);
    std::printf("m8 sub-surface action round trip %.1f us; window snapshot read with %zu nodes %.1f us\n",
                round_trip, snapshot.node_count, read_cost);

    // Focus: the host takes the window's focus and the node its sub-surface's.
    require(window.perform_semantic_action((*host).runtime_id(), "sub.host.sub.second", SemanticAction::focus),
            "focus on a sub-surface node is posted");
    require(window.focused_control() == host, "the host takes the window's focus");
    require(wait_for_event(*log, "second:focus"), "the node's control takes the sub-surface's");
    require(wait_for_node(window, "sub.host.sub.second", NodeCheck::focused), "the node is focused in the window");
    require(window.request_focus(before), "focus leaves the host");
    require(wait_for_node(window, "sub.host.sub.second", NodeCheck::present), "the node stays");
    require(!node_passes(window.semantic_snapshot(), "sub.host.sub.second", NodeCheck::focused),
            "and is not focused while the host is not");

    // A change inside reaches the window's snapshot.
    const std::uint64_t generation = window.semantic_generation();
    const DispatchOperation renamed = (*host).dispatcher().begin_invoke(RenameFirst(stops, "renamed"));
    static_cast<void>(renamed);
    require(wait_for_node(window, "sub.host.sub.first", NodeCheck::named_renamed), "the rename arrives");
    require(window.semantic_generation() > generation, "as a new semantic generation of the window");
    const SubSurfaceStatistics statistics = (*host).statistics();
    require(statistics.semantic_actions_performed == 3U && statistics.semantic_actions_refused == 0U,
            "three actions performed");

    // Stop, then a new sub-surface: its nodes are new nodes.
    const SemanticSnapshot before_stop = window.semantic_snapshot();
    const SemanticNode* const old_node = find_node(before_stop.roots, "sub.host.sub.first");
    require(old_node != nullptr, "the node is there before the stop");
    const std::uint64_t old_identity = (*old_node).runtime_id;
    (*host).set_content(nullptr, true);
    require(node_passes(window.semantic_snapshot(), "sub.host.sub.first", NodeCheck::absent),
            "a stopped sub-surface's nodes leave at once");
    (*host).set_content(std::make_unique<SemanticContent>(log, stops), true);
    window.flush();
    require(wait_for_node(window, "sub.host.sub.first", NodeCheck::present), "the new sub-surface's nodes arrive");
    const SemanticSnapshot after_restart = window.semantic_snapshot();
    const SemanticNode* const restarted = find_node(after_restart.roots, "sub.host.sub.first");
    require(restarted != nullptr && (*restarted).runtime_id != old_identity, "under new runtime identities");
    static_cast<void>((*root).remove_child((*host).runtime_id()));
    require(!(*host).started(), "leaving the window stops the sub-surface");
}

// Stage S3.3: tooltips asked of the parent (design note section 15).

// A control with a tooltip, focusable or not.
class TipControl final : public Control {
public:
    TipControl(StableId stable_id, std::string tip, bool takes_focus) : Control(std::move(stable_id)) {
        focusable = takes_focus;
        tool_tip_text = std::move(tip);
    }
};

// The sub-surface's tooltip controls, published by its thread for the
// test's program requests (weak: the sub-surface owns them).
struct TipControls final {
    std::mutex mutex;
    std::weak_ptr<Control> hover{};
};

// A pointer tip at (10, 5, 30, 15) and a focusable tip at (60, 5, 30, 15),
// in the sub-surface's coordinates.
class TipContent final : public SubSurfaceContent {
public:
    explicit TipContent(std::shared_ptr<TipControls> controls) : controls_(std::move(controls)) {}
    Control::Ptr build_root() override {
        std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("tips.root"));
        std::shared_ptr<TipControl> hover = std::make_shared<TipControl>(StableId("tips.hover"), "hover tip", false);
        std::shared_ptr<TipControl> focus = std::make_shared<TipControl>(StableId("tips.focus"), "focus tip", true);
        (*hover).set_requested_bounds(Rect{10.0, 5.0, 30.0, 15.0});
        (*focus).set_requested_bounds(Rect{60.0, 5.0, 30.0, 15.0});
        (*root).add_child(hover);
        (*root).add_child(focus);
        const std::lock_guard<std::mutex> lock((*controls_).mutex);
        (*controls_).hover = hover;
        return root;
    }

private:
    std::shared_ptr<TipControls> controls_;
};

// Posted to the sub-surface's thread: shows or hides `control`'s tooltip
// through its root (a program request).
class ProgramTip final {
public:
    ProgramTip(std::weak_ptr<Control> control, bool show) noexcept : control_(std::move(control)), show_(show) {}
    void operator()() const {
        const Control::Ptr control = control_.lock();
        if (!control) return;
        SurfaceRoot* const root = (*control).attached_surface_root();
        if (root == nullptr) return;
        if (show_) {
            (*root).show_tool_tip(control, std::chrono::milliseconds(0));
            return;
        }
        (*root).hide_tool_tip();
    }

private:
    std::weak_ptr<Control> control_;
    bool show_{};
};

// Runs the window's posted work until `done` holds of the host's request
// counters.
template <typename Predicate>
[[nodiscard]] bool drain_until_requests(Window& window, const SubSurfaceHost& host, Predicate done) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        static_cast<void>(window.drain_posted_work());
        if (done(host.request_statistics())) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

struct ReceivedAtLeast final {
    std::uint64_t count{};
    [[nodiscard]] bool operator()(const SubSurfaceRequestStatistics& statistics) const noexcept {
        return statistics.received >= count;
    }
};

struct WithdrawnAtLeast final {
    std::uint64_t count{};
    [[nodiscard]] bool operator()(const SubSurfaceRequestStatistics& statistics) const noexcept {
        return statistics.withdrawn >= count;
    }
};

[[nodiscard]] bool wait_for_outcomes(const SubSurfaceHost& host, std::uint64_t outcomes) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (host.statistics().outcomes_received >= outcomes) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] PointerEvent pointer_move(Point position) {
    PointerEvent move;
    move.action = PointerAction::move;
    move.position = position;
    return move;
}

[[nodiscard]] bool starts_with(const std::string& text, std::string_view prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

// The pointer over a sub-surface's control: the sub-surface asks, the
// window's presenter shows the bubble anchored in the window and answers;
// the pointer leaving the host revokes it. Keyboard focus asks the same
// way; the program's request shows at once and its hide withdraws it; the
// window's switch declines.
void tool_tips_are_asked_of_the_parent() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("tips.window"));
    std::shared_ptr<PlainStop> before = std::make_shared<PlainStop>(StableId("tips.before"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("tips.host"));
    (*before).set_requested_bounds(Rect{0.0, 0.0, 20.0, 20.0});
    (*host).set_requested_bounds(Rect{20.0, 40.0, 100.0, 30.0});
    std::shared_ptr<TipControls> controls = std::make_shared<TipControls>();
    (*host).set_content(std::make_unique<TipContent>(controls), true);
    (*root).add_child(before);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    ToolTipSettings settings = window.tool_tip_settings();
    settings.initial_delay = std::chrono::milliseconds(0);
    settings.reshow_delay = std::chrono::milliseconds(0);
    settings.auto_pop_delay = std::chrono::milliseconds(0);
    window.set_tool_tip_settings(settings);
    window.flush();
    require((*host).started(), "the host starts");
    require(wait_for_content_tag(*(*host).live_surface(), 1U), "the sub-surface is configured");

    // Hover: one request, shown below the pointer once the presenter wakes.
    static_cast<void>(window.dispatch_pointer(pointer_move(Point{20.0 + 15.0, 40.0 + 10.0})));
    require(drain_until_requests(window, *host, ReceivedAtLeast{1U}), "the sub-surface asks for the hover tip");
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    require(window.tool_tip_visible(), "the window shows the requested tooltip");
    require(window.tool_tip_target() == host, "on the host");
    require(window.tool_tip_text() == "hover tip", "with the sub-surface's text");
    require(starts_with(window.tool_tip_item_id(), "sub-surface-request."), "as one of the host's items");
    require(wait_for_outcomes(*host, 1U), "the sub-surface hears it was shown");
    require((*host).request_statistics().shown == 1U, "counted shown");

    // The pointer leaves the host: the host revokes, the bubble closes.
    static_cast<void>(window.dispatch_pointer(pointer_move(Point{5.0, 90.0})));
    require(!window.tool_tip_visible(), "leaving the host closes the tooltip");
    require((*host).request_statistics().revoked == 1U, "the host revoked the pointer's request");
    require(wait_for_outcomes(*host, 2U), "the sub-surface hears it was revoked");

    // Keyboard: Tab onto the host enters at the focus tip, which is asked
    // for and shown below its anchor.
    require(window.request_focus(before), "focus starts before the host");
    require(window.dispatch_key(key_down(PhysicalKey::tab)), "Tab moves onto the host");
    require(window.focused_control() == host, "the host has the window's focus");
    const std::uint64_t asked = (*host).request_statistics().received;
    require(drain_until_requests(window, *host, ReceivedAtLeast{asked + 1U}), "the sub-surface asks for the focus tip");
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    require(window.tool_tip_visible() && window.tool_tip_text() == "focus tip", "the keyboard's tooltip shows");
    // Focus leaves the host: revoked and closed.
    require(window.request_focus(before), "focus leaves the host");
    require(!window.tool_tip_visible(), "focus leaving the host closes the tooltip");
    require((*host).request_statistics().revoked == 2U, "the host revoked the keyboard's request");

    // The program: shown at once, then withdrawn by the sub-surface.
    std::weak_ptr<Control> hover;
    {
        const std::lock_guard<std::mutex> lock((*controls).mutex);
        hover = (*controls).hover;
    }
    const std::uint64_t before_program = (*host).request_statistics().received;
    static_cast<void>((*host).dispatcher().begin_invoke(ProgramTip(hover, true)));
    require(drain_until_requests(window, *host, ReceivedAtLeast{before_program + 1U}), "the program asks");
    require(window.tool_tip_visible() && window.tool_tip_text() == "hover tip", "a program request shows at once");
    const std::uint64_t withdrawn = (*host).request_statistics().withdrawn;
    static_cast<void>((*host).dispatcher().begin_invoke(ProgramTip(hover, false)));
    require(drain_until_requests(window, *host, WithdrawnAtLeast{withdrawn + 1U}), "the program withdraws it");
    require(!window.tool_tip_visible(), "withdrawing closes it");

    // The window's switch: the hover request is declined, nothing shows.
    window.set_tool_tips_shown(false);
    const std::uint64_t before_declined = (*host).request_statistics().received;
    static_cast<void>(window.dispatch_pointer(pointer_move(Point{20.0 + 16.0, 40.0 + 11.0})));
    require(drain_until_requests(window, *host, ReceivedAtLeast{before_declined + 1U}), "asked again");
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    require((*host).request_statistics().declined == 1U, "the window declined");
    require(!window.tool_tip_visible(), "nothing shows while tooltips are off");

    static_cast<void>((*root).remove_child((*host).runtime_id()));
    require(!(*host).started(), "leaving the window stops the thread");
}

// Collects a request's answers on the requesting root's thread.
class CollectAnswers final : public SurfaceRequestAnswer {
public:
    void request_answered(const SurfaceRequestOutcome& outcome) override {
        const std::lock_guard<std::mutex> lock(mutex);
        outcomes.push_back(outcome);
    }
    [[nodiscard]] bool has(std::uint64_t id, SurfaceRequestResult result) {
        const std::lock_guard<std::mutex> lock(mutex);
        for (const SurfaceRequestOutcome& outcome : outcomes) {
            if (outcome.id == id && outcome.result == result) return true;
        }
        return false;
    }
    std::mutex mutex;
    std::vector<SurfaceRequestOutcome> outcomes;
};

// A window handles a request itself: shown at once below its anchor and
// answered through the dispatcher; a newer request replaces it (revoked),
// withdrawing closes it unanswered, and an owner that cannot show one now
// is declined.
void window_handles_tool_tip_requests() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("request.root"));
    std::shared_ptr<PlainStop> owner = std::make_shared<PlainStop>(StableId("request.owner"));
    (*owner).set_requested_bounds(Rect{10.0, 10.0, 40.0, 20.0});
    (*root).add_child(owner);
    Window window(root, Size{200.0, 100.0});
    window.flush();
    std::shared_ptr<CollectAnswers> answers = std::make_shared<CollectAnswers>();
    ToolTipRequest request;
    request.text = "asked";
    request.anchor = Rect{12.0, 12.0, 10.0, 10.0};
    request.duration = std::chrono::milliseconds(0);
    const std::uint64_t first = window.request_tool_tip(owner, request, answers);
    require(first == 1U, "the first request is 1");
    require(window.tool_tip_visible() && window.tool_tip_text() == "asked", "the window shows it at once");
    require(window.tool_tip_target() == owner && window.tool_tip_item_id() == "request.1", "as the owner's part");
    static_cast<void>(window.drain_posted_work());
    require((*answers).has(first, SurfaceRequestResult::shown), "answered shown through the dispatcher");
    request.text = "newer";
    const std::uint64_t second = window.request_tool_tip(owner, request, answers);
    static_cast<void>(window.drain_posted_work());
    require((*answers).has(first, SurfaceRequestResult::revoked), "a newer request replaces the first");
    require(window.tool_tip_text() == "newer", "and shows");
    window.withdraw_tool_tip_request(second);
    require(!window.tool_tip_visible(), "withdrawing closes it");
    (*owner).visible = false;
    const std::uint64_t hidden = window.request_tool_tip(owner, request, answers);
    static_cast<void>(window.drain_posted_work());
    require((*answers).has(hidden, SurfaceRequestResult::declined), "a hidden owner is declined");
    bool refused = false;
    request.text.clear();
    try {
        static_cast<void>(window.request_tool_tip(owner, request, answers));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "empty text is refused");
}

// A sub-surface forwards a request to its parent, which shows it and
// answers; the answer reaches the request's target on the sub-surface's
// thread; withdrawing closes it in the window.
class RequestTip final {
public:
    RequestTip(std::weak_ptr<Control> control, std::shared_ptr<CollectAnswers> answers, bool withdraw) noexcept
        : control_(std::move(control)), answers_(std::move(answers)), withdraw_(withdraw) {}
    void operator()() const {
        const Control::Ptr control = control_.lock();
        if (!control) return;
        SurfaceRoot* const root = (*control).attached_surface_root();
        if (root == nullptr) return;
        if (withdraw_) {
            (*root).withdraw_tool_tip_request(1U);
            return;
        }
        ToolTipRequest request;
        request.text = "from inside";
        request.anchor = Rect{10.0, 5.0, 30.0, 15.0};
        request.duration = std::chrono::milliseconds(0);
        static_cast<void>((*root).request_tool_tip(control, request, answers_));
    }

private:
    std::weak_ptr<Control> control_;
    std::shared_ptr<CollectAnswers> answers_;
    bool withdraw_{};
};

[[nodiscard]] bool wait_for_answer(CollectAnswers& answers, std::uint64_t id, SurfaceRequestResult result) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (answers.has(id, result)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void sub_surface_forwards_tool_tip_requests() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("forward.window"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("forward.host"));
    (*host).set_requested_bounds(Rect{20.0, 40.0, 100.0, 30.0});
    std::shared_ptr<TipControls> controls = std::make_shared<TipControls>();
    (*host).set_content(std::make_unique<TipContent>(controls), true);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require(wait_for_content_tag(*(*host).live_surface(), 1U), "the sub-surface is configured");
    std::weak_ptr<Control> hover;
    {
        const std::lock_guard<std::mutex> lock((*controls).mutex);
        hover = (*controls).hover;
    }
    std::shared_ptr<CollectAnswers> answers = std::make_shared<CollectAnswers>();
    static_cast<void>((*host).dispatcher().begin_invoke(RequestTip(hover, answers, false)));
    require(drain_until_requests(window, *host, ReceivedAtLeast{1U}), "the request reaches the parent");
    require(window.tool_tip_visible() && window.tool_tip_text() == "from inside", "the parent shows it");
    require(window.tool_tip_target() == host, "on the host");
    require(wait_for_answer(*answers, 1U, SurfaceRequestResult::shown), "the answer reaches the target");
    static_cast<void>((*host).dispatcher().begin_invoke(RequestTip(hover, answers, true)));
    require(drain_until_requests(window, *host, WithdrawnAtLeast{1U}), "the withdrawal reaches the parent");
    require(!window.tool_tip_visible(), "withdrawing closes it in the window");
    static_cast<void>((*root).remove_child((*host).runtime_id()));
}

// Stage S4: effects on a sub-surface (design note section 14).

// What an ambient control on a sub-surface saw, written on its thread.
struct AmbientLog final {
    std::mutex mutex;
    std::vector<FrameTime> ticks;
    // Paints while ambient motion was suppressed: the still pose.
    std::uint64_t still_paints{};
};

// Enlists itself on its root's motion clock whenever it paints, as a
// control hosting an effect program does, and records each frame's time.
class AmbientProbe final : public Control {
public:
    AmbientProbe(StableId stable_id, std::shared_ptr<AmbientLog> log)
        : Control(std::move(stable_id)), log_(std::move(log)) {}
    void on_paint(Painter& painter, Rect local_damage) override {
        Control::on_paint(painter, local_damage);
        painted_ = true;
        SurfaceRoot* const root = attached_surface_root();
        if (root == nullptr) return;
        MotionClock& clock = (*root).motion_clock();
        if (clock.ambient_suppressed()) {
            const std::lock_guard<std::mutex> lock((*log_).mutex);
            ++(*log_).still_paints;
            return;
        }
        static_cast<void>(clock.enlist_control_ambient(*this, std::chrono::milliseconds(4)));
    }
    bool on_ambient_frame(FrameTime now) override {
        const bool shown = painted_;
        painted_ = false;
        {
            const std::lock_guard<std::mutex> lock((*log_).mutex);
            (*log_).ticks.push_back(now);
        }
        invalidate(Dirty::paint);
        return shown;
    }

private:
    std::shared_ptr<AmbientLog> log_;
    bool painted_{};
};

class AmbientContent final : public SubSurfaceContent {
public:
    explicit AmbientContent(std::shared_ptr<AmbientLog> log) : log_(std::move(log)) {}
    Control::Ptr build_root() override {
        return std::make_shared<AmbientProbe>(StableId("ambient.probe"), log_);
    }

private:
    std::shared_ptr<AmbientLog> log_;
};

[[nodiscard]] std::size_t ticks_of(AmbientLog& log) {
    const std::lock_guard<std::mutex> lock(log.mutex);
    return log.ticks.size();
}

[[nodiscard]] bool wait_for_ticks(AmbientLog& log, std::size_t ticks) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (ticks_of(log) >= ticks) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Measurement M3's test: the UI thread blocks for 200 ms and the
// sub-surface's ambient control keeps ticking; once the window reports
// display timing, the ticks fall on its vertical blanks; reduced motion
// from the window stills it (it paints its still pose and stops ticking),
// and turning it off resumes the ticks.
void ambient_effect_ticks_off_the_ui_thread() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("ambient.window"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("ambient.host"));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 60.0, 30.0});
    std::shared_ptr<AmbientLog> log = std::make_shared<AmbientLog>();
    (*host).set_content(std::make_unique<AmbientContent>(log), true);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require(wait_for_ticks(*log, 3U), "the ambient control ticks on the sub-surface");

    // The UI thread blocks; the sub-surface goes on.
    const std::size_t before_block = ticks_of(*log);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(ticks_of(*log) >= before_block + 3U, "ticks continue while the UI thread is blocked");

    // Display timing: a 10 ms refresh phased at `phase`. Later ticks are on
    // its vertical blanks.
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    LiveDisplayTiming timing;
    timing.refresh_period = std::chrono::duration_cast<FrameInterval>(std::chrono::milliseconds(10));
    timing.last_vblank = FrameClock::now();
    timing.source = LiveTimingSource::display_phased;
    (*surface).notify_presented((*surface).snapshot().published_generation, timing);
    const std::size_t after_timing = ticks_of(*log) + 2U;
    require(wait_for_ticks(*log, after_timing + 3U), "ticks continue with display timing");
    {
        const std::lock_guard<std::mutex> lock((*log).mutex);
        for (std::size_t index = after_timing; index < (*log).ticks.size(); ++index) {
            const FrameInterval offset = (*log).ticks[index] - timing.last_vblank;
            require(offset.count() >= 0 && offset % timing.refresh_period == FrameInterval::zero(),
                    "an ambient frame's time is a vertical blank of the window's display");
        }
    }

    // Reduced motion: still pose, no more ticks.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    window.flush();
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    bool stilled = false;
    while (!stilled && std::chrono::steady_clock::now() < limit) {
        {
            const std::lock_guard<std::mutex> lock((*log).mutex);
            stilled = (*log).still_paints > 0U;
        }
        if (!stilled) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(stilled, "reduced motion paints the still pose on the sub-surface");
    const std::size_t stilled_ticks = ticks_of(*log);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    require(ticks_of(*log) == stilled_ticks, "no ambient frame runs while motion is reduced");

    // Motion back: the ticks resume.
    settings.reduced_motion = false;
    window.set_presentation_settings(settings);
    window.flush();
    require(wait_for_ticks(*log, stilled_ticks + 3U), "turning reduced motion off resumes the ticks");
    static_cast<void>((*root).remove_child((*host).runtime_id()));
}

// What a sub-surface's control saw of its root's environment, written on
// the sub-surface's thread.
struct EnvironmentLog final {
    std::mutex mutex;
    // The root's user theme rules at the first paint (an address compared,
    // never followed, on the test's thread).
    std::uintptr_t user_rules{};
    bool painted{};
    // MetricsSnapshot::engine_input_changes at the last pointer move.
    std::uint64_t engine_input_changes{};
    std::uint64_t pointer_moves{};
};

class EnvironmentProbe final : public Control {
public:
    EnvironmentProbe(StableId stable_id, std::shared_ptr<EnvironmentLog> log)
        : Control(std::move(stable_id)), log_(std::move(log)) {}
    void on_paint(Painter& painter, Rect local_damage) override {
        Control::on_paint(painter, local_damage);
        const SurfaceRoot* const root = attached_surface_root();
        if (root == nullptr) return;
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        if ((*log_).painted) return;
        (*log_).painted = true;
        (*log_).user_rules = reinterpret_cast<std::uintptr_t>((*root).user_theme_rules());
    }
    void on_pointer(PointerEvent& event) override {
        Control::on_pointer(event);
        if (event.action != PointerAction::move) return;
        const SurfaceRoot* const root = attached_surface_root();
        if (root == nullptr) return;
        // The root noted the position for its engine parts before routing.
        const std::uint64_t changes = (*root).metrics_snapshot().engine_input_changes;
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        (*log_).engine_input_changes = changes;
        ++(*log_).pointer_moves;
    }

private:
    std::shared_ptr<EnvironmentLog> log_;
};

class EnvironmentContent final : public SubSurfaceContent {
public:
    explicit EnvironmentContent(std::shared_ptr<EnvironmentLog> log) : log_(std::move(log)) {}
    Control::Ptr build_root() override {
        return std::make_shared<EnvironmentProbe>(StableId("environment.probe"), log_);
    }

private:
    std::shared_ptr<EnvironmentLog> log_;
};

[[nodiscard]] std::shared_ptr<const Theme> user_theme_of_test() {
    std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).rule_set = ThemeRuleSet::user;
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    return theme;
}

// Gap 3: the window's layered user theme is the sub-surface's, the same
// immutable theme and so the same rules. Gap 5: pointer input the window
// forwards reaches the sub-surface's engine inputs (the position its
// themed parts' expressions read).
void user_theme_and_engine_input_reach_the_sub_surface() {
    const std::shared_ptr<const Theme> user = user_theme_of_test();
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    (*resources).set_user_theme(user);
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("environment.window"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("environment.host"));
    (*host).set_requested_bounds(Rect{20.0, 40.0, 100.0, 30.0});
    std::shared_ptr<EnvironmentLog> log = std::make_shared<EnvironmentLog>();
    (*host).set_content(std::make_unique<EnvironmentContent>(log), true);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0}, resources);
    window.set_framebuffer_painter(&raster);
    window.flush();
    require(wait_for_content_tag(*(*host).live_surface(), 1U), "the sub-surface is configured");
    const std::chrono::steady_clock::time_point painted_limit = std::chrono::steady_clock::now() + wait_limit;
    bool painted = false;
    while (!painted && std::chrono::steady_clock::now() < painted_limit) {
        {
            const std::lock_guard<std::mutex> lock((*log).mutex);
            painted = (*log).painted;
        }
        if (!painted) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(painted, "the sub-surface's control paints");
    {
        const std::lock_guard<std::mutex> lock((*log).mutex);
        require((*log).user_rules == reinterpret_cast<std::uintptr_t>(&(*user).engine().rules()),
                "the window's layered user theme rules are the sub-surface's");
    }

    // Moves waiting together coalesce (the newest wins), so each is sent
    // once the last arrived.
    std::uint64_t changes = 0U;
    std::uint64_t moves = 0U;
    const Point positions[2] = {Point{20.0 + 10.0, 40.0 + 10.0}, Point{20.0 + 30.0, 40.0 + 12.0}};
    for (std::uint64_t index = 0U; index < 2U; ++index) {
        // Each move is posted once, the entering one too (its enter is not
        // forwarded as a second move).
        const std::uint64_t received = (*host).statistics().inputs_received;
        static_cast<void>(window.dispatch_pointer(pointer_move(positions[index])));
        require((*host).statistics().inputs_received == received + 1U, "each move is posted to the sub-surface once");
        const std::chrono::steady_clock::time_point moved_limit = std::chrono::steady_clock::now() + wait_limit;
        while (moves <= index && std::chrono::steady_clock::now() < moved_limit) {
            {
                const std::lock_guard<std::mutex> lock((*log).mutex);
                changes = (*log).engine_input_changes;
                moves = (*log).pointer_moves;
            }
            if (moves <= index) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    require(moves == 2U, "both pointer moves reach the sub-surface's control");
    // The count is the root's activity since its metrics were last reset,
    // so only the move just noted is certain to be in it.
    require(changes >= 1U, "a forwarded move changes the sub-surface's engine pointer input");
    static_cast<void>((*root).remove_child((*host).runtime_id()));
}

// Stage S4.6: alerts on the request API (design note section 15).

// The host's message dialog, scripted: records each request and answers
// the next scripted result (cancelled without a choice when none is left).
struct AlertScript final {
    std::vector<HostDialogRequest> requests{};
    std::deque<HostMessageDialogResult> answers{};
};

class ScriptedAlerts final {
public:
    explicit ScriptedAlerts(AlertScript* script) noexcept : script_(script) {}
    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices&) const {
        (*script_).requests.push_back(request);
        HostDialogResult result;
        result.request_id = request.request_id;
        HostMessageDialogResult message{HostDialogOutcome::cancelled, HostDialogChoice::none};
        if (!(*script_).answers.empty()) {
            message = (*script_).answers.front();
            (*script_).answers.pop_front();
        }
        result.payload = message;
        return result;
    }

private:
    AlertScript* script_{};
};

// The answer `answers` received for alert `id`, if any.
[[nodiscard]] std::optional<SurfaceRequestOutcome> alert_answer(CollectAnswers& answers, std::uint64_t id) {
    const std::lock_guard<std::mutex> lock(answers.mutex);
    for (const SurfaceRequestOutcome& outcome : answers.outcomes) {
        if (outcome.id == id) return outcome;
    }
    return std::nullopt;
}

[[nodiscard]] std::size_t answers_for(CollectAnswers& answers, std::uint64_t id) {
    const std::lock_guard<std::mutex> lock(answers.mutex);
    std::size_t count = 0U;
    for (const SurfaceRequestOutcome& outcome : answers.outcomes) {
        if (outcome.id == id) ++count;
    }
    return count;
}

struct RaiseWithDefault final {
    Window* window{};
    Control::Ptr owner{};
    void operator()() const {
        AlertRequest request;
        request.text = "Wrong default";
        request.default_choice = HostDialogChoice::retry;
        static_cast<void>((*window).raise_alert(owner, request));
    }
};

struct RaiseEmpty final {
    Window* window{};
    Control::Ptr owner{};
    void operator()() const { static_cast<void>((*window).raise_alert(owner, AlertRequest{})); }
};

template <typename Action>
[[nodiscard]] bool throws_invalid(const Action& action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// A window shows its alerts through the host's message dialog, one at a
// time and in order, from its dispatcher; each is answered once (closed
// with the choice; declined without a host; revoked when withdrawn or its
// owner left before it showed), and bad content changes nothing.
void window_shows_alerts() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("alerts.window"));
    std::shared_ptr<PlainStop> owner = std::make_shared<PlainStop>(StableId("alerts.owner"));
    std::shared_ptr<PlainStop> leaving = std::make_shared<PlainStop>(StableId("alerts.leaving"));
    (*root).add_child(owner);
    (*root).add_child(leaving);
    Window window(root, Size{200.0, 100.0});
    std::shared_ptr<CollectAnswers> answers = std::make_shared<CollectAnswers>();

    AlertRequest saved;
    saved.text = "Saved.";
    const std::uint64_t unhosted = window.raise_alert(owner, saved, answers);
    require(unhosted == 1U, "the first alert is 1");
    require(!alert_answer(*answers, unhosted).has_value(), "raising returns before the alert is decided");
    static_cast<void>(window.drain_posted_work());
    static_cast<void>(window.drain_posted_work());
    const std::optional<SurfaceRequestOutcome> declined = alert_answer(*answers, unhosted);
    require(declined && (*declined).result == SurfaceRequestResult::declined,
            "without a host the alert is declined");

    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    AlertScript script;
    services.set_dialog_handler(ScriptedAlerts(&script));
    script.answers.push_back(HostMessageDialogResult{HostDialogOutcome::accepted, HostDialogChoice::no});

    AlertRequest discard;
    discard.text = "Discard the changes?";
    discard.caption = "Evidence";
    discard.buttons = HostMessageButtons::yes_no_cancel;
    discard.icon = HostMessageIcon::question;
    discard.default_choice = HostDialogChoice::no;
    const std::uint64_t first = window.raise_alert(owner, discard, answers);
    AlertRequest withdrawn;
    withdrawn.text = "Withdrawn";
    const std::uint64_t second = window.raise_alert(owner, withdrawn, answers);
    AlertRequest third;
    third.text = "Closed without a choice";
    third.buttons = HostMessageButtons::ok_cancel;
    const std::uint64_t later = window.raise_alert(owner, third, answers);
    AlertRequest orphan;
    orphan.text = "Its owner leaves";
    const std::uint64_t orphaned = window.raise_alert(leaving, orphan, answers);
    window.withdraw_alert(second);
    static_cast<void>((*root).remove_child((*leaving).runtime_id()));
    for (int round = 0; round < 8; ++round) static_cast<void>(window.drain_posted_work());

    require(script.requests.size() == 2U, "the two alerts still raised are shown, one at a time");
    const HostMessageDialogRequest* const shown =
        std::get_if<HostMessageDialogRequest>(&script.requests[0].payload);
    require(shown != nullptr && (*shown).message == "Discard the changes?" && (*shown).title == "Evidence" &&
                (*shown).buttons == HostMessageButtons::yes_no_cancel && (*shown).icon == HostMessageIcon::question &&
                (*shown).default_choice == HostDialogChoice::no,
            "the first alert's content reaches the host's dialog");
    require(script.requests[0].owner_id == "alerts.window" && script.requests[0].request_id != 0U &&
                script.requests[1].request_id != script.requests[0].request_id,
            "each dialog names the window's root and a fresh identity");
    const HostMessageDialogRequest* const next =
        std::get_if<HostMessageDialogRequest>(&script.requests[1].payload);
    require(next != nullptr && (*next).message == "Closed without a choice" &&
                (*next).default_choice == HostDialogChoice::ok,
            "the next one follows, defaulting to its first button");
    const std::optional<SurfaceRequestOutcome> chose = alert_answer(*answers, first);
    require(chose && (*chose).result == SurfaceRequestResult::closed && (*chose).choice == HostDialogChoice::no,
            "the user's choice is the answer");
    const std::optional<SurfaceRequestOutcome> revoked = alert_answer(*answers, second);
    require(revoked && (*revoked).result == SurfaceRequestResult::revoked, "a withdrawn alert is revoked");
    const std::optional<SurfaceRequestOutcome> dismissed = alert_answer(*answers, later);
    require(dismissed && (*dismissed).result == SurfaceRequestResult::closed &&
                (*dismissed).choice == HostDialogChoice::cancel,
            "a box closed without a choice answers cancel");
    const std::optional<SurfaceRequestOutcome> gone = alert_answer(*answers, orphaned);
    require(gone && (*gone).result == SurfaceRequestResult::revoked, "an alert whose owner left is revoked");
    require(answers_for(*answers, first) == 1U && answers_for(*answers, later) == 1U, "each alert is answered once");

    require(throws_invalid(RaiseWithDefault{&window, owner}), "a default choice outside the buttons is refused");
    require(throws_invalid(RaiseEmpty{&window, owner}), "an alert without text is refused");
    require(throws_invalid(RaiseEmpty{&window, leaving}), "an owner outside the root is refused");
    static_cast<void>(window.drain_posted_work());
    require(script.requests.size() == 2U, "refused alerts show nothing");
}

// Posted to the sub-surface's thread: raises an alert about `control`
// through its root, or withdraws alert 1.
class RaiseAlertInside final {
public:
    RaiseAlertInside(std::weak_ptr<Control> control, std::shared_ptr<CollectAnswers> answers, bool withdraw) noexcept
        : control_(std::move(control)), answers_(std::move(answers)), withdraw_(withdraw) {}
    void operator()() const {
        const Control::Ptr control = control_.lock();
        if (!control) return;
        SurfaceRoot* const root = (*control).attached_surface_root();
        if (root == nullptr) return;
        if (withdraw_) {
            (*root).withdraw_alert(1U);
            return;
        }
        AlertRequest request;
        request.text = "From inside";
        request.buttons = HostMessageButtons::retry_cancel;
        request.icon = HostMessageIcon::warning;
        static_cast<void>((*root).raise_alert(control, request, answers_));
    }

private:
    std::weak_ptr<Control> control_;
    std::shared_ptr<CollectAnswers> answers_;
    bool withdraw_{};
};

struct AlertsAtLeast final {
    std::uint64_t count{};
    [[nodiscard]] bool operator()(const SubSurfaceRequestStatistics& statistics) const noexcept {
        return statistics.alerts >= count;
    }
};

// A sub-surface forwards its alert; the parent raises it on the host and
// shows it over the window, and the one answer reaches the target on the
// sub-surface's thread, with the choice. Withdrawn while it waits at the
// parent, it is revoked and never shown.
void sub_surface_forwards_alerts() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("forward-alert.window"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("forward-alert.host"));
    (*host).set_requested_bounds(Rect{20.0, 40.0, 100.0, 30.0});
    std::shared_ptr<TipControls> controls = std::make_shared<TipControls>();
    (*host).set_content(std::make_unique<TipContent>(controls), true);
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    AlertScript script;
    services.set_dialog_handler(ScriptedAlerts(&script));
    script.answers.push_back(HostMessageDialogResult{HostDialogOutcome::accepted, HostDialogChoice::retry});
    window.flush();
    require(wait_for_content_tag(*(*host).live_surface(), 1U), "the sub-surface is configured");
    std::weak_ptr<Control> hover;
    {
        const std::lock_guard<std::mutex> lock((*controls).mutex);
        hover = (*controls).hover;
    }
    std::shared_ptr<CollectAnswers> answers = std::make_shared<CollectAnswers>();
    static_cast<void>((*host).dispatcher().begin_invoke(RaiseAlertInside(hover, answers, false)));
    require(drain_until_requests(window, *host, AlertsAtLeast{1U}), "the alert reaches the parent");
    for (int round = 0; round < 4; ++round) static_cast<void>(window.drain_posted_work());
    require(script.requests.size() == 1U, "the parent shows it");
    const HostMessageDialogRequest* const shown =
        std::get_if<HostMessageDialogRequest>(&script.requests[0].payload);
    require(shown != nullptr && (*shown).message == "From inside" &&
                (*shown).buttons == HostMessageButtons::retry_cancel && (*shown).icon == HostMessageIcon::warning,
            "with the sub-surface's content");
    require(wait_for_answer(*answers, 1U, SurfaceRequestResult::closed), "the answer reaches the target");
    const std::optional<SurfaceRequestOutcome> chose = alert_answer(*answers, 1U);
    require(chose && (*chose).choice == HostDialogChoice::retry, "with the user's choice");

    // A second alert, withdrawn before the parent shows it.
    static_cast<void>((*host).dispatcher().begin_invoke(RaiseAlertInside(hover, answers, false)));
    require(drain_until_requests(window, *host, AlertsAtLeast{2U}), "the second alert reaches the parent");
    // The parent shows it from its next drain; the sub-surface withdraws it
    // first (alert 2 is the root's; withdraw_alert(1) is a no-op now).
    static_cast<void>(window.raise_alert(host, AlertRequest{.text = "Parent's own"}, answers));
    require(script.requests.size() == 1U, "nothing more shown yet");
    static_cast<void>((*root).remove_child((*host).runtime_id()));
    for (int round = 0; round < 4; ++round) static_cast<void>(window.drain_posted_work());
    require(script.requests.size() == 1U, "a host that leaves withdraws its waiting alerts, and its own go too");
}

// A sub-surface made on the test's thread, without a SubSurfaceThread: its
// tooltip API works and asks nowhere (it used to reach a window's absent
// presenter).
void tool_tips_without_a_parent_ask_nowhere() {
    std::shared_ptr<Panel> tree = std::make_shared<Panel>(StableId("alone.root"));
    std::shared_ptr<TipControl> tip = std::make_shared<TipControl>(StableId("alone.tip"), "tip", true);
    (*tree).add_child(tip);
    SubSurface surface(tree, std::make_shared<ApplicationResources>());
    (*tip).tool_tip_text = "changed";
    surface.show_tool_tip(tip);
    require(!surface.tool_tip_visible(), "a sub-surface shows no tooltip itself");
    surface.hide_tool_tip();
    require(!surface.tool_tip_visible(), "and hiding changes nothing");
}

// The failing check's message goes to stderr: an uncaught exception would
// end the process without it.
int run_all() {
    thread_produces_tagged_frames();
    content_failure_is_reported();
    coalesced_first_configuration_keeps_its_framebuffer();
    new_epochs_are_produced_without_pacing();
    start_stop_stress();
    host_in_window();
    host_notices_stops_and_missed_pairings();
    caret_falls_back_to_left_edge();
    input_reaches_sub_surface_in_order();
    host_forwards_input_and_focus();
    tool_tips_are_asked_of_the_parent();
    tool_tips_without_a_parent_ask_nowhere();
    window_handles_tool_tip_requests();
    sub_surface_forwards_tool_tip_requests();
    ambient_effect_ticks_off_the_ui_thread();
    user_theme_and_engine_input_reach_the_sub_surface();
    window_shows_alerts();
    sub_surface_forwards_alerts();
    thread_publishes_semantics();
    host_stitches_semantics();
    return 0;
}

} // namespace

int main() {
    try {
        return run_all();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "gui_forms_sub_surface_tests: %s\n", error.what());
        return 1;
    }
}
