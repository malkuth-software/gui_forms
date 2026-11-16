// The build profile (cmake/HighIntegrity.cmake): build_profile() reports
// the profile this test was compiled with, and the allowances agree with the
// build's definitions. In the high-integrity configuration a font pack that
// does not open ends the process through the fail-fast path (mode
// damaged_pack, run by cmake/expect_fail_fast.cmake), and an animation
// timeline completes as soon as it starts.

#include "gui_forms/build_profile/build_profile.hpp"

#if defined(GUI_FORMS_HIGH_INTEGRITY)
#include "bundled_font_set.hpp"
#include "gui_forms/animation/animation_timeline/animation_timeline.hpp"
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_build_profile_tests: " << message << '\n';
    std::exit(1);
}

void test_profile_matches_compilation() {
    const BuildProfileRecord record = build_profile();
    require(record.profile == compiled_build_profile, "the library's profile differs from the unit's");
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    require(record.profile == BuildProfile::high_integrity, "a high-integrity build reports standard");
#if defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    require(record.clipboard_text, "the clipboard text allowance is not reported");
#else
    require(!record.clipboard_text, "clipboard text is reported without its allowance");
#endif
#else
    require(record.profile == BuildProfile::standard, "a standard build reports high integrity");
    require(record.clipboard_text, "a standard build reports clipboard text compiled out");
#endif
}

#if defined(GUI_FORMS_HIGH_INTEGRITY)
// Plain heap pages: the test needs storage, not sealing.
std::byte* heap_reserve(std::size_t bytes) noexcept {
    std::byte* const pages = new (std::nothrow) std::byte[bytes]();
    return pages;
}
bool heap_seal(std::byte*, std::size_t) noexcept {
    return true;
}
void heap_release(std::byte* pages, std::size_t) noexcept {
    delete[] pages;
}

// Motion is not compiled: a started timeline samples finished at its end
// at the moment it starts, and an infinite one rests at its start.
void test_timeline_completes_at_once() {
    AnimationSpec finite{};
    finite.duration = std::chrono::milliseconds(1000);
    finite.iterations = 2.0;
    AnimationTimeline timeline(finite);
    const FrameTime start{};
    timeline.start(start);
    const AnimationSample first = timeline.sample(start);
    require(first.finished, "a started timeline is not finished at once");
    require(!first.active && !first.delayed, "a finished timeline reports motion");
    require(first.iteration == 1U, "a finished timeline is not on its last iteration");
    require(first.progress == 1.0, "a finished timeline is not at its end");

    AnimationSpec endless{};
    endless.infinite = true;
    AnimationTimeline looping(endless);
    looping.start(start);
    const AnimationSample resting = looping.sample(start + std::chrono::milliseconds(500));
    require(resting.finished, "an infinite timeline moves");
    require(resting.progress == 0.0, "an infinite timeline does not rest at its start");
}

// A pack whose bytes are not a pack: the set's constructor must fail fast
// (record_pack_refusal) and never return.
void run_damaged_pack() {
    const detail::SealedPageFunctions pages{&heap_reserve, &heap_seal, &heap_release};
    std::array<std::byte, 64> damaged{};
    std::memset(damaged.data(), 0x5A, damaged.size());
    const detail::BundledFontSet set(std::span<const std::byte>(damaged.data(), damaged.size()), pages);
    static_cast<void>(set);
    std::cout << "gui_forms_build_profile_tests: survived a damaged font pack\n";
}
#endif

} // namespace

int main(int argument_count, char** arguments) {
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    if (argument_count > 1 && std::strcmp(arguments[1], "damaged_pack") == 0) {
        run_damaged_pack();
        return 0;
    }
#else
    static_cast<void>(argument_count);
    static_cast<void>(arguments);
#endif
    test_profile_matches_compilation();
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    test_timeline_completes_at_once();
#endif
    std::cout << "gui_forms_build_profile_tests: passed\n";
    return 0;
}
