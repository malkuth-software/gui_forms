#include "gui_forms/plots/series_ring_channel/series_ring_channel.hpp"

#include <algorithm>
#include <stdexcept>

namespace gui_forms::plots {

SeriesRingChannel::SeriesRingChannel(std::size_t capacity, std::size_t stream_count)
    : capacity_(capacity), stream_count_(stream_count) {
    if (capacity == 0U) throw std::invalid_argument("SeriesRingChannel capacity must be at least 1");
    if (stream_count == 0U || stream_count > maximum_channel_streams) {
        throw std::invalid_argument("SeriesRingChannel holds 1 to 4 streams");
    }
    if (capacity > static_cast<std::size_t>(-1) / stream_count) {
        throw std::invalid_argument("SeriesRingChannel capacity overflows");
    }
    slots_.assign(capacity * stream_count, 0.0);
}

std::unique_ptr<SeriesWriteLease> SeriesRingChannel::acquire_write_lease() {
    bool expected = false;
    if (!leased_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return nullptr;
    std::unique_ptr<SeriesWriteLease> lease(new SeriesWriteLease(*this));
    return lease;
}

void SeriesRingChannel::end_lease() noexcept {
    leased_.store(false, std::memory_order_release);
}

void SeriesRingChannel::write(std::span<const double> samples) noexcept {
    const std::size_t whole = samples.size() / stream_count_;
    if (whole == 0U) return;
    // A block larger than the ring keeps its newest samples; the older ones
    // still advance the sequence, as if written and overwritten at once.
    const std::size_t kept = std::min(whole, capacity_);
    const std::size_t skipped = whole - kept;
    const std::uint64_t start = published_.load(std::memory_order_relaxed);
    const std::uint64_t end = start + whole;
    writing_.store(end, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t sample = 0U; sample < kept; ++sample) {
        const std::uint64_t sequence = start + skipped + sample;
        const std::size_t slot = static_cast<std::size_t>(sequence % capacity_) * stream_count_;
        const std::size_t source = (skipped + sample) * stream_count_;
        for (std::size_t stream = 0U; stream < stream_count_; ++stream) {
            std::atomic_ref<double> target(slots_[slot + stream]);
            target.store(samples[source + stream], std::memory_order_relaxed);
        }
    }
    published_.store(end, std::memory_order_release);
}

std::size_t SeriesRingChannel::consume(std::span<SeriesColumn<double>* const> columns, std::span<double> scratch) {
    if (columns.size() != stream_count_) return 0U;
    if (scratch.size() < capacity_ * stream_count_) return 0U;
    const std::uint64_t published = published_.load(std::memory_order_acquire);
    if (published == consumed_through_) return 0U;
    // Samples older than one ring behind the publication are gone already.
    std::uint64_t first = consumed_through_;
    if (published - first > capacity_) {
        const std::uint64_t lost = published - capacity_ - first;
        overrun_.fetch_add(lost, std::memory_order_relaxed);
        first = published - capacity_;
    }
    const std::size_t count = static_cast<std::size_t>(published - first);
    for (std::size_t sample = 0U; sample < count; ++sample) {
        const std::uint64_t sequence = first + sample;
        const std::size_t slot = static_cast<std::size_t>(sequence % capacity_) * stream_count_;
        for (std::size_t stream = 0U; stream < stream_count_; ++stream) {
            // Scratch is stream-major (stream s at s * capacity), so each
            // stream reaches its column as one span.
            std::atomic_ref<double> source(slots_[slot + stream]);
            scratch[stream * capacity_ + sample] = source.load(std::memory_order_relaxed);
        }
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t writing = writing_.load(std::memory_order_relaxed);
    // A sample older than writing - capacity may have been overwritten
    // while it was copied: it is discarded.
    std::size_t torn = 0U;
    if (writing > capacity_ && writing - capacity_ > first) {
        torn = static_cast<std::size_t>(std::min<std::uint64_t>(writing - capacity_ - first, count));
    }
    if (torn != 0U) overrun_.fetch_add(torn, std::memory_order_relaxed);
    const std::size_t intact = count - torn;
    std::size_t taken = intact;
    for (std::size_t stream = 0U; stream < stream_count_; ++stream) {
        const std::span<const double> values(scratch.data() + stream * capacity_ + torn, intact);
        const SeriesEditOutcome outcome = (*columns[stream]).push_overwrite(values);
        // A column refusing the block (a monotone column going back) loses
        // it; the streams' columns may then differ in length until the
        // caller clears them.
        if (outcome != SeriesEditOutcome::applied) taken = 0U;
    }
    if (taken != intact) overrun_.fetch_add(intact, std::memory_order_relaxed);
    consumed_through_ = published;
    consumed_.fetch_add(taken, std::memory_order_relaxed);
    return taken;
}

SeriesChannelStatistics SeriesRingChannel::statistics() const noexcept {
    SeriesChannelStatistics statistics{};
    statistics.published = published_.load(std::memory_order_acquire);
    statistics.consumed = consumed_.load(std::memory_order_relaxed);
    statistics.overrun = overrun_.load(std::memory_order_relaxed);
    return statistics;
}

// -- SeriesWriteLease ----------------------------------------------------------

SeriesWriteLease::~SeriesWriteLease() {
    (*channel_).end_lease();
}

void SeriesWriteLease::write(std::span<const double> samples) noexcept {
    (*channel_).write(samples);
}

} // namespace gui_forms::plots
