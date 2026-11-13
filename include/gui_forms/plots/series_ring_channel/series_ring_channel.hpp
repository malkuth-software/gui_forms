#pragma once

// SeriesRingChannel: how a live plot's data crosses threads (IMGUI_IMPLOT_STUDY
// B.5). One producer thread writes samples through a SeriesWriteLease; one
// consumer (the live plot's frame, on its sub-surface thread) copies the
// samples it has not seen into its own ring columns once per frame. No
// series is ever mutated from two threads: the channel's slots are the only
// shared storage, and each slot is written by the producer only and read by
// the consumer only, through relaxed atomic references.
//
// Protocol (a sequence lock over a ring of samples):
// - the producer announces the sequence its block will reach (`writing_`,
//   relaxed, then a release fence), writes the block's slots, and then
//   publishes the same sequence (`published_`, release);
// - the consumer reads `published_` (acquire), copies the slots it has not
//   seen into scratch storage, then (after an acquire fence) reads
//   `writing_`: any copied sample older than `writing_ - capacity` may have
//   been overwritten while it copied, so it is discarded and counted as an
//   overrun. Only the remaining, intact samples reach the columns.
//
// A sample is `stream_count` doubles (x and y, or x and several y), stored
// interleaved. Capacity and stream count are fixed at construction.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms::plots {

// The most streams a channel's sample holds (x and three y).
inline constexpr std::size_t maximum_channel_streams = 4U;

// Counters of one channel, read by either thread.
struct GUI_FORMS_API SeriesChannelStatistics final {
    // Samples the producer published.
    std::uint64_t published{};
    // Samples the consumer took into its columns.
    std::uint64_t consumed{};
    // Samples lost: overwritten before the consumer reached them, or while
    // it copied them.
    std::uint64_t overrun{};
};

class SeriesWriteLease;

class GUI_FORMS_API SeriesRingChannel final {
public:
    // `capacity` samples of `stream_count` streams (1 to
    // maximum_channel_streams); allocates once. Throws
    // std::invalid_argument for a zero capacity or an invalid stream count.
    SeriesRingChannel(std::size_t capacity, std::size_t stream_count);
    SeriesRingChannel(const SeriesRingChannel&) = delete;
    SeriesRingChannel& operator=(const SeriesRingChannel&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t stream_count() const noexcept { return stream_count_; }

    // The producer's lease. Single producer: a second lease while one is
    // held is refused (null).
    [[nodiscard]] std::unique_ptr<SeriesWriteLease> acquire_write_lease();

    // Consumer side, once per frame: copies the samples published since the
    // last call into `columns` (one ring column per stream, in stream order;
    // push_overwrite), through `scratch`, reserved by the caller at
    // capacity() * stream_count() doubles. Returns the samples taken. A
    // block a column refuses (a monotone column going back) is counted as
    // overrun. Never allocates.
    std::size_t consume(std::span<SeriesColumn<double>* const> columns, std::span<double> scratch);

    // The sequence the producer has published, for an idle check: equal to
    // consumed_through() when the consumer has seen everything.
    [[nodiscard]] std::uint64_t published() const noexcept { return published_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t consumed_through() const noexcept { return consumed_through_; }
    [[nodiscard]] SeriesChannelStatistics statistics() const noexcept;

private:
    friend class SeriesWriteLease;

    // Producer side (SeriesWriteLease::write).
    void write(std::span<const double> samples) noexcept;
    void end_lease() noexcept;

    std::size_t capacity_{};
    std::size_t stream_count_{};
    // capacity_ * stream_count_ doubles, read and written only through
    // std::atomic_ref (relaxed).
    std::vector<double> slots_{};
    std::atomic<std::uint64_t> writing_{};
    std::atomic<std::uint64_t> published_{};
    std::atomic<bool> leased_{};
    // Consumer-thread state.
    std::uint64_t consumed_through_{};
    std::atomic<std::uint64_t> consumed_{};
    std::atomic<std::uint64_t> overrun_{};
};

// The single producer's right to write a channel (built like LiveSurface's
// write lease): writes whole samples, each `stream_count` doubles, and
// publishes them with release ordering. Owned by the producer's thread;
// destroying it ends the lease, so another may be acquired. The channel
// must outlive it.
class GUI_FORMS_API SeriesWriteLease final {
public:
    ~SeriesWriteLease();
    SeriesWriteLease(const SeriesWriteLease&) = delete;
    SeriesWriteLease& operator=(const SeriesWriteLease&) = delete;

    // Writes and publishes `samples` (whole samples, interleaved by stream;
    // a trailing partial sample is ignored). A block larger than the
    // channel keeps its newest capacity() samples. Never blocks or
    // allocates.
    void write(std::span<const double> samples) noexcept;

private:
    friend class SeriesRingChannel;
    explicit SeriesWriteLease(SeriesRingChannel& channel) noexcept : channel_(&channel) {}

    SeriesRingChannel* channel_{};
};

} // namespace gui_forms::plots
