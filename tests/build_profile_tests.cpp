// The build profile (cmake/HighIntegrity.cmake): build_profile() reports
// the profile this test was compiled with, and the allowances agree with the
// build's definitions. In the high-integrity configuration, motion is
// compiled to complete at once (motion_clock.hpp): a value moved with a
// duration reaches its target in the call, no frame is requested, and the
// caret does not blink.

#include "gui_forms/build_profile/build_profile.hpp"

#include <cstdlib>
#include <iostream>

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

} // namespace

int main() {
    test_profile_matches_compilation();
    std::cout << "gui_forms_build_profile_tests: passed\n";
    return 0;
}
