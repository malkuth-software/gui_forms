// RTF conversion under allocation failure (audit N114): import_rtf and
// export_rtf report limit_exceeded instead of raising, wherever an
// allocation fails, reader and writer included. The test replaces the global
// allocation functions with a countdown that fails the nth allocation.

#include "gui_forms/text/markdown/rtf_conversion.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Allocations left before the next one fails; negative while disarmed. One
// thread only (the test's).
std::int64_t allocations_before_failure = -1;
std::size_t failures_injected = 0U;

void* allocate(std::size_t bytes) {
    if (allocations_before_failure == 0) {
        allocations_before_failure = -1;
        ++failures_injected;
        throw std::bad_alloc();
    }
    if (allocations_before_failure > 0) --allocations_before_failure;
    void* const memory = std::malloc(bytes == 0U ? 1U : bytes);
    if (memory == nullptr) throw std::bad_alloc();
    return memory;
}

} // namespace

void* operator new(std::size_t bytes) {
    return allocate(bytes);
}

void* operator new[](std::size_t bytes) {
    return allocate(bytes);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "rtf conversion failure tests failed: " << message << '\n';
    std::exit(1);
}

constexpr std::string_view sample_rtf =
    "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\f0\\fs20 Hello \\b bold\\b0  world.\\par "
    "Second paragraph with \\i some\\i0  emphasis.\\par}";

constexpr std::string_view sample_markdown = "Hello **bold** world.\n\nSecond paragraph with *some* emphasis.\n";

// Fails each allocation of one import in turn, from the first, until an
// import runs without meeting the failure.
std::size_t import_under_failures() {
    const std::span<const std::byte> rtf(reinterpret_cast<const std::byte*>(sample_rtf.data()), sample_rtf.size());
    std::size_t failed_runs = 0U;
    for (std::int64_t countdown = 0; countdown < 100000; ++countdown) {
        std::string markdown{};
        const std::size_t before = failures_injected;
        allocations_before_failure = countdown;
        bool raised = false;
        try {
            const gui_forms::RtfConversionResult result = gui_forms::import_rtf(rtf, markdown);
            allocations_before_failure = -1;
            if (failures_injected == before) {
                require(result.succeeded() && !markdown.empty(), "an import without a failure succeeds");
                return failed_runs;
            }
            // The library may absorb a failed allocation (a cache it does
            // without) and succeed; otherwise the failure is limit_exceeded.
            if (!result.succeeded()) {
                const int error = static_cast<int>(result.error().value_or(gui_forms::RtfConversionError::malformed));
                if (result.error() != gui_forms::RtfConversionError::limit_exceeded) {
                    std::cerr << "import at allocation " << countdown << ": error " << error << '\n';
                }
                require(result.error() == gui_forms::RtfConversionError::limit_exceeded,
                        "an import that met an allocation failure reports limit_exceeded");
                ++failed_runs;
            }
        } catch (...) {
            raised = true;
        }
        allocations_before_failure = -1;
        require(!raised, "an import never raises on allocation failure");
    }
    require(false, "the import ends within the countdown bound");
    return failed_runs;
}

std::size_t export_under_failures() {
    std::size_t failed_runs = 0U;
    for (std::int64_t countdown = 0; countdown < 100000; ++countdown) {
        std::vector<std::byte> rtf{};
        const std::size_t before = failures_injected;
        allocations_before_failure = countdown;
        bool raised = false;
        try {
            const gui_forms::RtfConversionResult result = gui_forms::export_rtf(sample_markdown, rtf);
            allocations_before_failure = -1;
            if (failures_injected == before) {
                require(result.succeeded() && !rtf.empty(), "an export without a failure succeeds");
                return failed_runs;
            }
            if (!result.succeeded()) {
                require(result.error() == gui_forms::RtfConversionError::limit_exceeded,
                        "an export that met an allocation failure reports limit_exceeded");
                ++failed_runs;
            }
        } catch (...) {
            raised = true;
        }
        allocations_before_failure = -1;
        require(!raised, "an export never raises on allocation failure");
    }
    require(false, "the export ends within the countdown bound");
    return failed_runs;
}

} // namespace

int main() {
    const std::size_t imports = import_under_failures();
    const std::size_t exports = export_under_failures();
    require(imports > 0U && exports > 0U, "some conversions met an injected failure");
    std::cout << "rtf conversion failure tests passed (" << imports << " import and " << exports
              << " export allocation failures reported)\n";
    return 0;
}
