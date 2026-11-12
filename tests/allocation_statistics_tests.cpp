// GUI.Forms' own allocation counters (demonstrator gap A7): only the counted
// paths are counted, each under its subsystem, with exact counts and bytes;
// an AllocationScope sees only its own thread's work; the process-wide
// counters see every thread's; the JSON form names its scope.
#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"
#include "gui_forms/allocation/statistics/allocation_statistics.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "allocation statistics failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

using CountedBytes = std::vector<std::byte, detail::CountedAllocator<std::byte, AllocationSubsystem::effects>>;

// An over-aligned value, so the allocator takes its aligned path.
struct alignas(64) WideValue final {
    std::array<std::byte, 64> bytes{};
};

using CountedWide = std::vector<WideValue, detail::CountedAllocator<WideValue, AllocationSubsystem::image_atlas>>;

// A registry image of `width` x `height` BGRA pixels counts one allocation
// of exactly its pixel bytes under image_resources, and its removal one
// release of the same bytes; nothing else is counted.
void test_registry_counts_its_image_bytes() {
    ImageRegistry registry;
    const std::vector<std::byte> pixels(3U * 2U * 4U, std::byte{7});
    const AllocationScope load;
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(3U, 2U, 12U, std::span<const std::byte>(pixels));
    expect(static_cast<bool>(loaded), "the image loads");
    const AllocationStatistics after_load = load.counted();
    const AllocationCounts images = after_load.of(AllocationSubsystem::image_resources);
    expect(images.allocations == 1U && images.allocated_bytes == 24U && images.releases == 0U,
           "loading counts one allocation of the pixel bytes");
    expect(after_load.of(AllocationSubsystem::image_atlas).allocations == 0U &&
               after_load.of(AllocationSubsystem::effects).allocations == 0U,
           "no other subsystem is counted");

    const AllocationScope patch;
    const std::vector<std::byte> one_pixel(4U, std::byte{9});
    const ImageLoadResult patched = registry.patch_bgra32_premultiplied_in_place(
        loaded.image, 1U, 1U, 1U, 1U, 4U, std::span<const std::byte>(one_pixel));
    expect(patched && patched.image == loaded.image, "an in-place patch keeps the ImageId");
    expect(patch.counted().total().allocations == 0U, "a patch allocates nothing counted");

    const AllocationScope removal;
    expect(registry.remove(loaded.image), "the image is removed");
    const AllocationCounts removed = removal.counted().of(AllocationSubsystem::image_resources);
    expect(removed.releases == 1U && removed.released_bytes == 24U && removed.allocations == 0U,
           "removal counts one release of the same bytes");
}

// Containers and allocations outside the counted paths are not counted.
void test_other_allocations_are_not_counted() {
    const AllocationScope scope;
    std::vector<int> plain(1000U, 1);
    std::unique_ptr<std::string> text = std::make_unique<std::string>(200U, 'x');
    expect(plain.size() == 1000U && (*text).size() == 200U, "the plain allocations happened");
    expect(scope.counted().total().allocations == 0U, "allocations off the counted paths are not counted");
}

// The allocator counts each allocation and release with its bytes, on both
// its ordinary and its over-aligned path.
void test_counted_allocator() {
    const AllocationScope scope;
    {
        CountedBytes bytes;
        bytes.reserve(100U);
        CountedWide wide;
        wide.reserve(3U);
        expect(reinterpret_cast<std::uintptr_t>(wide.data()) % 64U == 0U, "over-aligned storage is aligned");
    }
    const AllocationStatistics counted = scope.counted();
    const AllocationCounts effects = counted.of(AllocationSubsystem::effects);
    expect(effects.allocations == 1U && effects.allocated_bytes == 100U && effects.releases == 1U &&
               effects.released_bytes == 100U,
           "the byte vector's storage is counted under its subsystem");
    const AllocationCounts atlas = counted.of(AllocationSubsystem::image_atlas);
    expect(atlas.allocations == 1U && atlas.allocated_bytes == 3U * sizeof(WideValue) && atlas.releases == 1U,
           "the over-aligned vector's storage is counted under its subsystem");
    const AllocationCounts total = counted.total();
    expect(total.allocations == 2U && total.releases == 2U && total.allocated_bytes == total.released_bytes,
           "the total sums the subsystems");
    const detail::CountedAllocator<int, AllocationSubsystem::effects> left{};
    const detail::CountedAllocator<char, AllocationSubsystem::effects> right{};
    expect(left == right && !(left != right), "instances of one subsystem are interchangeable");
}

// Allocates and releases `count` counted bytes on its own thread.
struct CountedWorker final {
    std::size_t count{};

    void operator()() const {
        CountedBytes bytes;
        bytes.reserve(count);
    }
};

// Reads a scope on another thread and records whether it was refused.
struct ScopeReader final {
    const AllocationScope* scope{};
    bool* refused{};

    void operator()() const {
        try {
            static_cast<void>((*scope).counted());
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

// A scope sees only its own thread; the process-wide counters see the other
// thread's work too; a scope is read on its own thread only.
void test_scope_is_per_thread() {
    const AllocationStatistics process_before = allocation_statistics();
    const AllocationScope scope;
    std::thread worker(CountedWorker{4096U});
    worker.join();
    expect(scope.counted().total().allocations == 0U, "another thread's allocation is not in this thread's scope");
    const AllocationStatistics process_after = allocation_statistics();
    const AllocationCounts before = process_before.of(AllocationSubsystem::effects);
    const AllocationCounts after = process_after.of(AllocationSubsystem::effects);
    expect(after.allocations - before.allocations >= 1U && after.allocated_bytes - before.allocated_bytes >= 4096U,
           "the process-wide counters include the other thread");

    bool refused = false;
    std::thread reader(ScopeReader{&scope, &refused});
    reader.join();
    expect(refused, "a scope read on another thread is refused");

    AllocationScope restarted;
    {
        CountedBytes bytes;
        bytes.reserve(8U);
    }
    restarted.restart();
    expect(restarted.counted().total().allocations == 0U, "restart moves the start to now");
}

// The JSON form names its scope and every subsystem.
void test_json_names_its_scope() {
    AllocationStatistics statistics{};
    statistics.subsystems[static_cast<std::size_t>(AllocationSubsystem::image_atlas)].allocations = 3U;
    const std::string json = statistics.to_json();
    expect(json.find("\"scope\":\"gui_forms_counted_paths\"") != std::string::npos, "the JSON names its scope");
    expect(json.find("\"image_resources\":") != std::string::npos && json.find("\"effects\":") != std::string::npos,
           "the JSON names every subsystem");
    expect(json.find("\"image_atlas\":{\"allocations\":3,") != std::string::npos, "the JSON carries the counts");
    expect(allocation_subsystem_name(static_cast<AllocationSubsystem>(200U)) == "unknown",
           "an unknown subsystem is named unknown");
    expect(statistics.of(static_cast<AllocationSubsystem>(200U)).allocations == 0U,
           "an unknown subsystem has no counts");
}

} // namespace

int main() {
    test_registry_counts_its_image_bytes();
    test_other_allocations_are_not_counted();
    test_counted_allocator();
    test_scope_is_per_thread();
    test_json_names_its_scope();
    std::cout << "allocation statistics tests passed\n";
    return EXIT_SUCCESS;
}
