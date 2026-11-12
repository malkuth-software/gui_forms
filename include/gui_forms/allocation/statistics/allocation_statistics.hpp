#pragma once

// GUI.Forms' own allocation counters (demonstrator gap A7).
//
// What is counted: the allocations GUI.Forms makes through its counted
// paths, and nothing else. Each counted path names its subsystem
// (AllocationSubsystem) and allocates through detail::CountedAllocator
// (allocation/counted_allocator), which calls the global operator new and
// operator delete with matched sizes and alignment and counts each call. The
// library does not replace or hook the global operator new, so the
// allocations of the program, of the standard library's own containers
// elsewhere in GUI.Forms, of Skia, FreeType, HarfBuzz and of the host
// toolkits are not counted. A figure here is never the process total: read
// a process figure from the platform for that.
//
// The counted paths today:
// - image_resources: the bytes an ImageRegistry holds for each image (a PNG
//   file's bytes or an image's BGRA pixels), allocated at load, replacement
//   and full update, released at replacement, removal and clear. The
//   registry's slot table is not counted.
// - image_atlas: an ImageAtlas's tables (slots, keys, users, free list), its
//   staging cell and the zeroed buffer its image is first loaded from (that
//   buffer is released before the constructor returns; the image itself is
//   then counted under image_resources).
// - effects: an effect instance's state storage (EffectInstance::attach),
//   allocated at the program's alignment.
// - display_chunks: the command lists of retained display chunks and of the
//   recording painter that records them (detail::DisplayCommandList), with
//   the copies a display snapshot makes. The commands' own members (shared
//   rasters, strings, prepared text) are not counted.
// - damage_lists: a DamageRegion's rectangles (every window's damage planes,
//   the damage hosts take) and a window's pending localized paint marks
//   (Control::invalidate(Rect)).
// - text_layout: reserved for the paragraph layout's reused scratch (the
//   public paragraph wrapping of branch claude/cp29-gap-paragraph-wrap);
//   nothing counts under it until that code allocates through
//   detail::CountedAllocator<..., AllocationSubsystem::text_layout>.
//
// Bytes are the bytes asked of operator new, without the allocator's own
// overhead or rounding. A release is counted by the thread that releases,
// with the bytes the allocation asked for.
//
// Threads: every counter is kept twice, process-wide (relaxed atomics,
// every thread) and per thread (the calling thread's own, read only by it).
// allocation_statistics() reads the process-wide counters one field at a
// time, so while other threads allocate its fields may come from slightly
// different moments; each field only grows. AllocationScope reads the
// calling thread's counters, which no other thread writes, so its figures
// are exact for the work that thread did inside the scope.

#include "gui_forms/export/application_export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace gui_forms {

// The library paths whose allocations are counted. Each value is an index
// into AllocationStatistics::subsystems.
enum class AllocationSubsystem : std::uint8_t {
    image_resources,
    image_atlas,
    effects,
    display_chunks,
    damage_lists,
    text_layout,
};

// The number of AllocationSubsystem values.
inline constexpr std::size_t allocation_subsystem_count = 6U;

// The name of `subsystem` as the JSON form spells it; "unknown" for a value
// outside the enumeration.
[[nodiscard]] GUI_FORMS_API std::string_view allocation_subsystem_name(AllocationSubsystem subsystem) noexcept;

// One subsystem's counts. allocated_bytes - released_bytes is the bytes the
// subsystem holds now, when the counts are process-wide and read from a
// quiet process; over a scope it is the net change on one thread.
struct GUI_FORMS_API AllocationCounts final {
    std::uint64_t allocations{};
    std::uint64_t releases{};
    std::uint64_t allocated_bytes{};
    std::uint64_t released_bytes{};
};

struct GUI_FORMS_API AllocationStatistics final {
    // Indexed by AllocationSubsystem.
    std::array<AllocationCounts, allocation_subsystem_count> subsystems{};

    // The counts of `subsystem`; zero counts for a value outside the
    // enumeration.
    [[nodiscard]] AllocationCounts of(AllocationSubsystem subsystem) const noexcept;
    // The sum over every subsystem: still only GUI.Forms' counted paths.
    [[nodiscard]] AllocationCounts total() const noexcept;
    // {"scope":"gui_forms_counted_paths","total":{...},"image_resources":{...},
    // "image_atlas":{...},"effects":{...},"display_chunks":{...},
    // "damage_lists":{...},"text_layout":{...}}: every subsystem by name, in
    // enumeration order, even at zero, with allocations, releases,
    // allocated_bytes and released_bytes in each object. The "scope" member names what is counted, so that a record
    // carrying it is not read as a process figure.
    [[nodiscard]] std::string to_json() const;
};

// The process-wide counters: every thread's counted allocations since the
// process started. Never reset.
[[nodiscard]] GUI_FORMS_API AllocationStatistics allocation_statistics() noexcept;

// The calling thread's counters since the thread started. Never reset.
[[nodiscard]] GUI_FORMS_API AllocationStatistics thread_allocation_statistics() noexcept;

// Counts what the constructing thread allocates through the counted paths
// from construction until counted() is read: an app wraps a workload (a
// frame, a scroll, a setup step) in one and reads it after. Other threads'
// allocations are not in it; read allocation_statistics() before and after
// for those. Scopes nest freely, as each one only remembers where the
// thread's counters stood. Holds no resources.
class GUI_FORMS_API AllocationScope final {
public:
    AllocationScope() noexcept;

    // What the constructing thread allocated and released through the
    // counted paths since construction. std::logic_error on another thread,
    // whose counters would answer for different work.
    [[nodiscard]] AllocationStatistics counted() const;
    // Moves the start to now, as a new scope would.
    void restart() noexcept;

private:
    std::thread::id thread_{};
    AllocationStatistics start_{};
};

namespace detail {

// The counted paths' entry points (CountedAllocator calls them). Each adds
// to the process-wide and the calling thread's counters; neither allocates
// or throws. A value outside the enumeration is not counted.
GUI_FORMS_API void count_allocation(AllocationSubsystem subsystem, std::size_t bytes) noexcept;
GUI_FORMS_API void count_release(AllocationSubsystem subsystem, std::size_t bytes) noexcept;

} // namespace detail

} // namespace gui_forms
