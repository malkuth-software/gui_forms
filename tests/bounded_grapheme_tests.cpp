#include "unicode_grapheme.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0U;
std::size_t fail_at = 0U;

void* allocate(const std::size_t bytes) {
    if (enabled) {
        ++calls;
        if (calls == fail_at) throw std::bad_alloc();
    }
    const std::size_t requested = bytes == 0U ? 1U : bytes;
    void* result = std::malloc(requested);
    if (result == nullptr) throw std::bad_alloc();
    return result;
}
}

void* operator new(const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void* operator new[](const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void operator delete(void* address) noexcept { std::free(address); }
void operator delete[](void* address) noexcept { std::free(address); }
void operator delete(void* address, std::size_t) noexcept { std::free(address); }
void operator delete[](void* address, std::size_t) noexcept { std::free(address); }

namespace {
using gui_forms::detail::GraphemeBoundaryBuffer;
using gui_forms::detail::GraphemeStorageRequirement;
using gui_forms::detail::GraphemeStorageStatus;
using gui_forms::detail::bounded_grapheme_boundaries;
using gui_forms::detail::extended_grapheme_boundaries;
using gui_forms::detail::grapheme_storage_requirement;
using gui_forms::detail::GraphemeWorkspace;
using gui_forms::detail::grapheme_workspace_requirement;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void compare_existing(std::string_view text) {
    const gui_forms::Utf8ValidationResult validation = gui_forms::validate_utf8(text);
    require(validation.valid(), "valid comparison input");
    const std::vector<std::size_t> expected = extended_grapheme_boundaries(text, validation.scalar_count);
    const GraphemeStorageRequirement requirement = grapheme_storage_requirement(text);
    require(requirement.status == GraphemeStorageStatus::success, "valid storage calculation");
    require(requirement.peak_bytes == requirement.scalar_bytes + requirement.boundary_bytes,
            "all simultaneous arrays counted");
    GraphemeBoundaryBuffer bounded{};
    const GraphemeStorageStatus accepted = bounded_grapheme_boundaries(text, requirement.peak_bytes, bounded);
    require(accepted == GraphemeStorageStatus::success, "exact workspace limit accepted");
    const std::span<const std::size_t> actual = bounded.boundaries();
    require(actual.size() == expected.size(), "grapheme count preserved");
    require(std::equal(actual.begin(), actual.end(), expected.begin()), "grapheme boundaries preserved");
    require(bounded.capacity_bytes() == requirement.boundary_bytes, "exact retained array capacity");
    GraphemeWorkspace workspace{};
    const GraphemeStorageStatus prepared = workspace.prepare(validation.scalar_count, requirement.peak_bytes);
    require(prepared == GraphemeStorageStatus::success, "exact reusable workspace capacity");
    allocation_probe::calls = 0U;
    allocation_probe::enabled = true;
    const GraphemeStorageStatus filled = workspace.fill(text);
    allocation_probe::enabled = false;
    require(filled == GraphemeStorageStatus::success, "workspace fill accepted");
    require(allocation_probe::calls == 0U, "workspace fill does not allocate");
    const std::span<const std::size_t> reused = workspace.boundaries();
    require(reused.size() == expected.size(), "workspace boundary count");
    const bool same = std::equal(reused.begin(), reused.end(), expected.begin());
    require(same, "workspace shares Unicode segmentation results");
    GraphemeBoundaryBuffer refused{};
    const GraphemeStorageStatus insufficient = bounded_grapheme_boundaries(text, requirement.peak_bytes - 1U, refused);
    require(insufficient == GraphemeStorageStatus::budget_exceeded, "one byte below peak refused");
    require(refused.boundaries().empty(), "refusal did not allocate output");
}

void test_equivalence() {
    const std::array<std::string_view, 9> cases{
        "", "abc", "\r\n", "a\r\nb\nc", "a\xcc\x81",
        "\xd7\x90\xd7\x91", "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8",
        "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9",
        "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7"};
    for (std::size_t index = 0; index < cases.size(); ++index) compare_existing(cases[index]);
    std::string marks("a");
    marks.reserve(16'383);
    for (std::size_t index = 0; index < 8191; ++index) marks.append("\xcc\x81");
    compare_existing(marks);
    // A run of regional indicators pairs from its start (GB12, GB13), read
    // from parity carried forward (A-gf-core-12-F2): 4,097 flag letters
    // make 2,048 flags and a lone letter, also after a letter breaks a run.
    std::string flags("a");
    const std::string_view letter = "\xf0\x9f\x87\xa6";
    for (std::size_t index = 0; index < 4097; ++index) flags.append(letter);
    flags.append("b");
    flags.append(letter);
    flags.append(letter);
    flags.append(letter);
    compare_existing(flags);
    const gui_forms::Utf8ValidationResult validation = gui_forms::validate_utf8(flags);
    const std::vector<std::size_t> boundaries = extended_grapheme_boundaries(flags, validation.scalar_count);
    // a, 2,048 pairs, the lone letter, b, one pair, the lone last letter.
    require(boundaries.size() == 1U + 1U + 2048U + 1U + 1U + 1U + 1U, "regional indicators pair from each run's start");
    require(boundaries[2] == 1U + 8U, "the first flag is the first two letters");
    require(boundaries[2049] == 1U + 4096U * 4U, "the run's last letter stands alone");
    require(boundaries[boundaries.size() - 2U] == 1U + 4097U * 4U + 1U + 8U, "the second run pairs from its start");
}

void test_preservation_and_moves() {
    GraphemeBoundaryBuffer output{};
    const GraphemeStorageRequirement first = grapheme_storage_requirement("abc");
    const GraphemeStorageStatus initial = bounded_grapheme_boundaries("abc", first.peak_bytes, output);
    require(initial == GraphemeStorageStatus::success, "initial boundary output");
    const std::size_t retained = output.capacity_bytes();
    const std::span<const std::size_t> old = output.boundaries();
    const std::size_t* old_address = old.data();
    const GraphemeStorageRequirement replacement = grapheme_storage_requirement("xy");
    const GraphemeStorageStatus overlap_refused = bounded_grapheme_boundaries("xy",
        retained + replacement.peak_bytes - 1U, output);
    require(overlap_refused == GraphemeStorageStatus::budget_exceeded, "old output counted in replacement peak");
    require(output.boundaries().data() == old_address && output.boundaries().size() == 4,
            "failed replacement preserved old owner");
    const std::array<std::string_view, 4> malformed{
        "\x80", "\xf0\x9f\x98", "\xc0\xaf", "\xed\xa0\x80"};
    for (std::size_t index = 0; index < malformed.size(); ++index) {
        const GraphemeStorageStatus invalid = bounded_grapheme_boundaries(malformed[index],
            std::numeric_limits<std::size_t>::max(), output);
        require(invalid == GraphemeStorageStatus::invalid_utf8, "malformed input refused");
        require(output.boundaries().data() == old_address, "invalid input preserved output");
    }
    const GraphemeStorageStatus replaced = bounded_grapheme_boundaries("xy",
        retained + replacement.peak_bytes, output);
    require(replaced == GraphemeStorageStatus::success, "exact old plus new budget accepted");
    GraphemeBoundaryBuffer moved(std::move(output));
    require(output.boundaries().empty() && output.capacity_bytes() == 0, "move constructor emptied old owner");
    require(moved.boundaries().size() == 3 && moved.boundaries()[2] == 2, "move retained boundaries");
    output = std::move(moved);
    require(moved.boundaries().empty() && moved.capacity_bytes() == 0, "move assignment emptied old owner");
    require(output.boundaries().size() == 3, "move assignment retained boundaries");
}

void test_workspace_replacement() {
    GraphemeWorkspace workspace{};
    const GraphemeStorageRequirement first = grapheme_workspace_requirement(3U);
    GraphemeStorageStatus status = workspace.prepare(3U, first.peak_bytes);
    require(status == GraphemeStorageStatus::success, "initial workspace prepared");
    status = workspace.fill("abc");
    require(status == GraphemeStorageStatus::success, "initial workspace filled");
    const std::span<const std::size_t> original = workspace.boundaries();
    const std::size_t* original_address = original.data();
    const GraphemeStorageRequirement next = grapheme_workspace_requirement(6U);
    const std::size_t exact_peak = first.peak_bytes + next.peak_bytes;
    status = workspace.prepare(6U, exact_peak - 1U);
    require(status == GraphemeStorageStatus::budget_exceeded, "replacement charges both live owners");
    for (std::size_t failure = 1U; failure <= 2U; ++failure) {
        allocation_probe::calls = 0U;
        allocation_probe::fail_at = failure;
        allocation_probe::enabled = true;
        status = workspace.prepare(6U, exact_peak);
        allocation_probe::enabled = false;
        require(status == GraphemeStorageStatus::resource_failure, "each workspace allocation failure returned");
        const std::span<const std::size_t> retained = workspace.boundaries();
        require(retained.data() == original_address && retained.size() == 4U && retained[3] == 3U,
                "allocation failure preserves previous storage and values");
        require(workspace.capacity_bytes() == first.peak_bytes, "failed allocation preserves accounting");
    }
    allocation_probe::fail_at = 0U;
    status = workspace.fill("abcd");
    require(status == GraphemeStorageStatus::budget_exceeded, "fill cannot grow storage");
    status = workspace.fill("\x80");
    require(status == GraphemeStorageStatus::invalid_utf8, "invalid fill refused before mutation");
    const std::span<const std::size_t> preserved = workspace.boundaries();
    require(preserved.data() == original_address && preserved[3] == 3U, "failed fill preserved content");
    allocation_probe::calls = 0U;
    allocation_probe::enabled = true;
    status = workspace.prepare(2U, first.peak_bytes);
    allocation_probe::enabled = false;
    require(status == GraphemeStorageStatus::success && allocation_probe::calls == 0U,
            "sufficient storage reused without allocation");
    status = workspace.prepare(6U, exact_peak);
    require(status == GraphemeStorageStatus::success, "exact simultaneous replacement budget");
    const std::span<const std::size_t> reset = workspace.boundaries();
    require(reset.empty(), "preparation resets active result");
    require(workspace.capacity_bytes() == next.peak_bytes, "replacement owns only new arrays");
    status = workspace.fill("a\xcc\x81");
    require(status == GraphemeStorageStatus::success, "replacement fill succeeded");
    GraphemeWorkspace moved(std::move(workspace));
    require(workspace.capacity_bytes() == 0U, "workspace move empties source charge");
    status = workspace.fill("");
    require(status == GraphemeStorageStatus::budget_exceeded, "moved workspace has no prepared boundary slot");
    workspace = std::move(moved);
    require(moved.capacity_bytes() == 0U, "workspace move assignment empties source");
    const std::span<const std::size_t> result = workspace.boundaries();
    require(result.size() == 2U && result[1] == 3U, "move retains filled boundaries");
    const GraphemeStorageRequirement overflow = grapheme_workspace_requirement(std::numeric_limits<std::size_t>::max());
    require(overflow.status == GraphemeStorageStatus::budget_exceeded, "workspace capacity overflow refused");
}
} // namespace

int main() {
    try {
        test_equivalence();
        test_preservation_and_moves();
        test_workspace_replacement();
        std::cout << "Bounded grapheme allocation and equivalence fixtures passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
