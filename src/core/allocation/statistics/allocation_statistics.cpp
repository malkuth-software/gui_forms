#include "gui_forms/allocation/statistics/allocation_statistics.hpp"

#include <atomic>
#include <sstream>
#include <stdexcept>

namespace gui_forms {
namespace {

// One subsystem's process-wide counters, written by every thread.
struct SharedCounts final {
    std::atomic<std::uint64_t> allocations{0U};
    std::atomic<std::uint64_t> releases{0U};
    std::atomic<std::uint64_t> allocated_bytes{0U};
    std::atomic<std::uint64_t> released_bytes{0U};
};

// Zero-initialized before any dynamic initialization (constant
// initialization of atomics), so a counted allocation made by another
// translation unit's static constructor finds them ready.
std::array<SharedCounts, allocation_subsystem_count> shared_counts{};

// The calling thread's counters: trivially destructible, read and written
// only by its own thread.
thread_local AllocationStatistics thread_counts{};

[[nodiscard]] bool counted_subsystem(AllocationSubsystem subsystem) noexcept {
    const bool known = static_cast<std::size_t>(subsystem) < allocation_subsystem_count;
    return known;
}

// later - earlier, field by field. Each thread counter only grows, so no
// field goes below zero.
[[nodiscard]] AllocationCounts counts_since(const AllocationCounts& later, const AllocationCounts& earlier) noexcept {
    AllocationCounts difference{};
    difference.allocations = later.allocations - earlier.allocations;
    difference.releases = later.releases - earlier.releases;
    difference.allocated_bytes = later.allocated_bytes - earlier.allocated_bytes;
    difference.released_bytes = later.released_bytes - earlier.released_bytes;
    return difference;
}

void write_counts(std::ostringstream& output, const AllocationCounts& counts) {
    output << "{\"allocations\":" << counts.allocations << ",\"releases\":" << counts.releases
           << ",\"allocated_bytes\":" << counts.allocated_bytes << ",\"released_bytes\":" << counts.released_bytes
           << '}';
}

} // namespace

std::string_view allocation_subsystem_name(AllocationSubsystem subsystem) noexcept {
    switch (subsystem) {
    case AllocationSubsystem::image_resources: return "image_resources";
    case AllocationSubsystem::image_atlas: return "image_atlas";
    case AllocationSubsystem::effects: return "effects";
    case AllocationSubsystem::display_chunks: return "display_chunks";
    case AllocationSubsystem::damage_lists: return "damage_lists";
    case AllocationSubsystem::text_layout: return "text_layout";
    default: break;
    }
    return "unknown";
}

AllocationCounts AllocationStatistics::of(AllocationSubsystem subsystem) const noexcept {
    if (!counted_subsystem(subsystem)) return AllocationCounts{};
    const AllocationCounts counts = subsystems[static_cast<std::size_t>(subsystem)];
    return counts;
}

AllocationCounts AllocationStatistics::total() const noexcept {
    AllocationCounts sum{};
    for (const AllocationCounts& counts : subsystems) {
        sum.allocations += counts.allocations;
        sum.releases += counts.releases;
        sum.allocated_bytes += counts.allocated_bytes;
        sum.released_bytes += counts.released_bytes;
    }
    return sum;
}

std::string AllocationStatistics::to_json() const {
    std::ostringstream output;
    output << "{\"scope\":\"gui_forms_counted_paths\",\"total\":";
    write_counts(output, total());
    for (std::size_t index = 0U; index < allocation_subsystem_count; ++index) {
        const AllocationSubsystem subsystem = static_cast<AllocationSubsystem>(index);
        output << ",\"" << allocation_subsystem_name(subsystem) << "\":";
        write_counts(output, subsystems[index]);
    }
    output << '}';
    const std::string json = output.str();
    return json;
}

AllocationStatistics allocation_statistics() noexcept {
    AllocationStatistics statistics{};
    for (std::size_t index = 0U; index < allocation_subsystem_count; ++index) {
        const SharedCounts& shared = shared_counts[index];
        AllocationCounts& counts = statistics.subsystems[index];
        counts.allocations = shared.allocations.load(std::memory_order_relaxed);
        counts.releases = shared.releases.load(std::memory_order_relaxed);
        counts.allocated_bytes = shared.allocated_bytes.load(std::memory_order_relaxed);
        counts.released_bytes = shared.released_bytes.load(std::memory_order_relaxed);
    }
    return statistics;
}

AllocationStatistics thread_allocation_statistics() noexcept {
    const AllocationStatistics statistics = thread_counts;
    return statistics;
}

AllocationScope::AllocationScope() noexcept : thread_(std::this_thread::get_id()), start_(thread_counts) {}

AllocationStatistics AllocationScope::counted() const {
    if (std::this_thread::get_id() != thread_) {
        throw std::logic_error("GUI.Forms AllocationScope is read on the thread that made it");
    }
    const AllocationStatistics now = thread_counts;
    AllocationStatistics difference{};
    for (std::size_t index = 0U; index < allocation_subsystem_count; ++index) {
        difference.subsystems[index] = counts_since(now.subsystems[index], start_.subsystems[index]);
    }
    return difference;
}

void AllocationScope::restart() noexcept {
    thread_ = std::this_thread::get_id();
    start_ = thread_counts;
}

namespace detail {

void count_allocation(AllocationSubsystem subsystem, std::size_t bytes) noexcept {
    if (!counted_subsystem(subsystem)) return;
    const std::size_t index = static_cast<std::size_t>(subsystem);
    const std::uint64_t byte_count = static_cast<std::uint64_t>(bytes);
    SharedCounts& shared = shared_counts[index];
    shared.allocations.fetch_add(1U, std::memory_order_relaxed);
    shared.allocated_bytes.fetch_add(byte_count, std::memory_order_relaxed);
    AllocationCounts& own = thread_counts.subsystems[index];
    ++own.allocations;
    own.allocated_bytes += byte_count;
}

void count_release(AllocationSubsystem subsystem, std::size_t bytes) noexcept {
    if (!counted_subsystem(subsystem)) return;
    const std::size_t index = static_cast<std::size_t>(subsystem);
    const std::uint64_t byte_count = static_cast<std::uint64_t>(bytes);
    SharedCounts& shared = shared_counts[index];
    shared.releases.fetch_add(1U, std::memory_order_relaxed);
    shared.released_bytes.fetch_add(byte_count, std::memory_order_relaxed);
    AllocationCounts& own = thread_counts.subsystems[index];
    ++own.releases;
    own.released_bytes += byte_count;
}

} // namespace detail

} // namespace gui_forms
