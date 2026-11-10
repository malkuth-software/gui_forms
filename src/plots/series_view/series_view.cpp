#include "gui_forms/plots/series_view/series_view.hpp"

#include <utility>

namespace gui_forms::plots {

// -- SeriesLeaseState ----------------------------------------------------------

// The pin is taken first and the release checked after, so that release()
// (which sets the flag first and then waits for readers) either sees this
// pin and waits for it, or this check sees the flag and backs out.
bool SeriesLeaseState::pin() noexcept {
    readers_.fetch_add(1U, std::memory_order_seq_cst);
    if (!released_.load(std::memory_order_seq_cst)) return true;
    unpin();
    refused_.fetch_add(1U, std::memory_order_relaxed);
    return false;
}

void SeriesLeaseState::unpin() noexcept {
    const std::uint32_t before = readers_.fetch_sub(1U, std::memory_order_seq_cst);
    if (before == 1U) readers_.notify_all();
}

void SeriesLeaseState::release() noexcept {
    released_.store(true, std::memory_order_seq_cst);
    std::uint32_t readers = readers_.load(std::memory_order_seq_cst);
    while (readers != 0U) {
        readers_.wait(readers, std::memory_order_seq_cst);
        readers = readers_.load(std::memory_order_seq_cst);
    }
}

// -- SeriesLease ---------------------------------------------------------------

SeriesLease::SeriesLease() : state_(std::make_shared<SeriesLeaseState>()) {}

SeriesLease::~SeriesLease() {
    release();
}

SeriesLease::SeriesLease(SeriesLease&& other) noexcept : state_(std::move(other.state_)) {}

SeriesLease& SeriesLease::operator=(SeriesLease&& other) noexcept {
    if (this == &other) return *this;
    release();
    state_ = std::move(other.state_);
    return *this;
}

void SeriesLease::release() noexcept {
    if (!state_) return;
    (*state_).release();
}

// -- SeriesPin -----------------------------------------------------------------

SeriesPin::SeriesPin(const std::shared_ptr<SeriesLeaseState>& lease) noexcept {
    if (!lease) return;
    lease_ = lease.get();
    held_ = (*lease_).pin();
}

SeriesPin::~SeriesPin() {
    if (held_) (*lease_).unpin();
}

} // namespace gui_forms::plots
