#pragma once

#include "display_snapshot.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gui_forms::detail {

// The raster a baker paints with: made on the baker's worker thread by its
// factory and used only there (it holds the renderer's own canvas and font
// registrations). The renderer implements it (render/skia); the core only
// replays snapshots into it, so the dispatch is the renderer seam, a runtime
// choice the core cannot make.
class SplashBakeRaster {
public:
    virtual ~SplashBakeRaster() = default;
    // Paints `snapshot` on a clear raster of its size at its scale and fills
    // `target` (the snapshot's device extent, tight BGRA8 rows) with the
    // premultiplied result. False, leaving `target` unspecified, when the
    // renderer could not.
    [[nodiscard]] virtual bool bake(const DisplaySnapshot& snapshot, PartRasterView target) = 0;
};

// Makes the worker's raster. make() runs once, on the worker thread, as the
// worker starts; the factory is then only kept.
class SplashBakeRasterFactory {
public:
    virtual ~SplashBakeRasterFactory() = default;
    // Null when the renderer cannot make one: every bake is then refused.
    [[nodiscard]] virtual std::unique_ptr<SplashBakeRaster> make() = 0;
};

// One bake's identity, unique for its baker's life.
struct SplashBakeTicket final {
    std::uint64_t value{};

    friend bool operator==(SplashBakeTicket left, SplashBakeTicket right) noexcept {
        return left.value == right.value;
    }
};

enum class SplashBakeState : std::uint8_t {
    // Queued or being painted.
    pending,
    // Painted: the outcome holds the raster, now the caller's.
    finished,
    // The worker could not paint it, or it was dropped (newer work pushed
    // it out, or the baker stopped).
    refused,
    // Not a ticket this baker holds: never issued, or already taken.
    unknown,
};

struct SplashBakeOutcome final {
    SplashBakeState state{SplashBakeState::unknown};
    std::shared_ptr<const PartRaster> raster{};
};

// The run's Splash baker (APP_WINDOW_RULES_DESIGN Part S, phase 2; owner
// ruling 2026-11-09 (a): Splash only, menus stay Popups). One worker thread,
// started warm with its raster made, paints display snapshots into part
// rasters while the UI thread goes on; the UI thread collects a finished
// raster with take(), which never waits on a bake.
//
// Execution contexts: submit, take and the destructor run on the UI thread
// that made the baker; the worker runs only run(). The mutex guards the
// waiting jobs, the finished bakes and the stop flag; nothing else is shared.
// A snapshot crosses as std::shared_ptr<const DisplaySnapshot> (immutable,
// with no window caches: DisplaySnapshot); a raster comes back as
// std::shared_ptr<const PartRaster>, filled on the worker before it is
// published under the mutex and never written again.
//
// Bounds: at most maximum_waiting_bakes wait; a submit past that drops the
// oldest waiting one (its ticket reads refused). At most
// maximum_finished_bakes stay untaken; a later finish drops the oldest.
//
// Shutdown: the destructor sets the stop flag, wakes the worker, and joins
// it. A bake being painted finishes first; waiting ones are dropped. The
// worker's raster is destroyed on the worker before it exits, so a renderer
// whose library shuts down later (shutdown_skia_library) is done with it.
class SplashBaker final {
public:
    static constexpr std::size_t maximum_waiting_bakes = 4U;
    static constexpr std::size_t maximum_finished_bakes = 8U;

    // Starts the worker, which makes its raster through `factory` first (the
    // warm standby). Null `factory` is std::invalid_argument.
    explicit SplashBaker(std::unique_ptr<SplashBakeRasterFactory> factory);
    ~SplashBaker();
    SplashBaker(const SplashBaker&) = delete;
    SplashBaker& operator=(const SplashBaker&) = delete;

    // Queues `snapshot` (not null) and returns its ticket.
    [[nodiscard]] SplashBakeTicket submit(std::shared_ptr<const DisplaySnapshot> snapshot);
    // A finished raster is moved out (later calls read unknown); a pending
    // ticket stays queued; a refused one reads refused once, then unknown.
    [[nodiscard]] SplashBakeOutcome take(SplashBakeTicket ticket);

private:
    struct Job final {
        SplashBakeTicket ticket{};
        std::shared_ptr<const DisplaySnapshot> snapshot{};
    };
    struct Done final {
        SplashBakeTicket ticket{};
        std::shared_ptr<const PartRaster> raster{};
    };
    // The worker thread's entry: runs run() on its baker.
    struct WorkerEntry final {
        SplashBaker* baker{};

        void operator()() const;
    };

    // The worker: makes its raster, then paints jobs until stopped.
    void run();
    // Paints one job on the worker; null when refused.
    [[nodiscard]] std::shared_ptr<const PartRaster> paint(SplashBakeRaster* raster, const Job& job) const;
    // Under the mutex: records `done`, dropping the oldest past the bound.
    void publish_locked(Done done);

    std::unique_ptr<SplashBakeRasterFactory> factory_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> waiting_;
    // The ticket being painted, or 0.
    std::uint64_t painting_{};
    std::vector<Done> finished_;
    // Tickets dropped or refused and not yet read.
    std::vector<SplashBakeTicket> refused_;
    bool stopping_{};
    // UI thread only.
    std::uint64_t next_ticket_{1U};
    // Declared last: started after every member above exists, joined first.
    std::thread worker_;
};

// The run's baker, as the host installs it for the length of a run (UI
// thread only): installed by SplashBakerInstallation, read by the window's
// tooltip presenter. `run` changes with each installation, so a ticket kept
// from an earlier baker is never taken from a later one.
struct SplashBakerSlot final {
    SplashBaker* baker{};
    std::uint64_t run{};
};
[[nodiscard]] SplashBakerSlot current_splash_baker() noexcept;

// Installs `baker` as the run's for this object's life; the previous
// installation (none in a host run) comes back when it goes. UI thread.
class SplashBakerInstallation final {
public:
    explicit SplashBakerInstallation(SplashBaker& baker) noexcept;
    ~SplashBakerInstallation();
    SplashBakerInstallation(const SplashBakerInstallation&) = delete;
    SplashBakerInstallation& operator=(const SplashBakerInstallation&) = delete;

private:
    SplashBakerSlot previous_{};
};

} // namespace gui_forms::detail
