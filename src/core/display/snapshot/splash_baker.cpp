#include "splash_baker.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>

namespace gui_forms::detail {

namespace {

// The installed baker; UI thread only (SplashBakerInstallation).
SplashBakerSlot installed_baker{};
std::uint64_t installations{};

} // namespace

SplashBaker::SplashBaker(std::unique_ptr<SplashBakeRasterFactory> factory) : factory_(std::move(factory)) {
    if (!factory_) throw std::invalid_argument("SplashBaker requires a raster factory");
    // Started last, when every member it reads exists.
    worker_ = std::thread(WorkerEntry{this});
}

SplashBaker::~SplashBaker() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void SplashBaker::WorkerEntry::operator()() const {
    (*baker).run();
}

SplashBakeTicket SplashBaker::submit(std::shared_ptr<const DisplaySnapshot> snapshot) {
    if (!snapshot) throw std::invalid_argument("SplashBaker::submit requires a snapshot");
    const SplashBakeTicket ticket{next_ticket_};
    ++next_ticket_;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (waiting_.size() >= maximum_waiting_bakes) {
            refused_.push_back(waiting_.front().ticket);
            waiting_.pop_front();
        }
        Job job{};
        job.ticket = ticket;
        job.snapshot = std::move(snapshot);
        waiting_.push_back(std::move(job));
    }
    wake_.notify_one();
    return ticket;
}

SplashBakeOutcome SplashBaker::take(const SplashBakeTicket ticket) {
    SplashBakeOutcome outcome{};
    const std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t index = 0U; index < finished_.size(); ++index) {
        if (!(finished_[index].ticket == ticket)) continue;
        outcome.raster = std::move(finished_[index].raster);
        outcome.state = outcome.raster ? SplashBakeState::finished : SplashBakeState::refused;
        finished_.erase(finished_.begin() + static_cast<std::ptrdiff_t>(index));
        return outcome;
    }
    for (std::size_t index = 0U; index < refused_.size(); ++index) {
        if (!(refused_[index] == ticket)) continue;
        refused_.erase(refused_.begin() + static_cast<std::ptrdiff_t>(index));
        outcome.state = SplashBakeState::refused;
        return outcome;
    }
    if (painting_ == ticket.value) {
        outcome.state = SplashBakeState::pending;
        return outcome;
    }
    for (const Job& job : waiting_) {
        if (!(job.ticket == ticket)) continue;
        outcome.state = SplashBakeState::pending;
        return outcome;
    }
    return outcome;
}

void SplashBaker::publish_locked(Done done) {
    if (finished_.size() >= maximum_finished_bakes) finished_.erase(finished_.begin());
    finished_.push_back(std::move(done));
}

std::shared_ptr<const PartRaster> SplashBaker::paint(SplashBakeRaster* raster, const Job& job) const {
    if (raster == nullptr) return nullptr;
    const DisplaySnapshot& snapshot = *job.snapshot;
    std::shared_ptr<PartRaster> made = PartRaster::create(snapshot.device_extent(), snapshot.scale());
    if (!made) return nullptr;
    // The worker is a subsystem boundary: a renderer failure refuses this
    // bake and leaves the worker running.
    bool painted = false;
    try {
        painted = (*raster).bake(snapshot, (*made).view());
    } catch (const std::exception&) {
        painted = false;
    }
    if (!painted) return nullptr;
    std::shared_ptr<const PartRaster> published = std::move(made);
    return published;
}

void SplashBaker::run() {
    // The warm standby: the raster (its canvas and fonts) is made before any
    // job waits on it. Made, used and destroyed on this thread only.
    std::unique_ptr<SplashBakeRaster> raster;
    try {
        raster = (*factory_).make();
    } catch (const std::exception&) {
        raster.reset();
    }
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        while (!stopping_ && waiting_.empty()) wake_.wait(lock);
        if (stopping_) break;
        Job job = std::move(waiting_.front());
        waiting_.pop_front();
        painting_ = job.ticket.value;
        lock.unlock();
        std::shared_ptr<const PartRaster> baked = paint(raster.get(), job);
        // The snapshot is released here, off the lock.
        job.snapshot.reset();
        lock.lock();
        painting_ = 0U;
        Done done{};
        done.ticket = job.ticket;
        done.raster = std::move(baked);
        publish_locked(std::move(done));
    }
    // Dropped work reads refused; nothing waits on it after the join.
    waiting_.clear();
    lock.unlock();
    raster.reset();
}

SplashBakerSlot current_splash_baker() noexcept {
    return installed_baker;
}

SplashBakerInstallation::SplashBakerInstallation(SplashBaker& baker) noexcept : previous_(installed_baker) {
    ++installations;
    installed_baker.baker = &baker;
    installed_baker.run = installations;
}

SplashBakerInstallation::~SplashBakerInstallation() {
    installed_baker = previous_;
}

} // namespace gui_forms::detail
