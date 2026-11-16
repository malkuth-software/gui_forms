#pragma once

// Which configuration the library was built in (cmake/HighIntegrity.cmake).
//
// The high-integrity configuration (GUI_FORMS_HIGH_INTEGRITY) compiles out
// every entry that takes bytes from outside the program or reaches outside
// it: application fonts, the installed-face probes, environment variables,
// hosted content, the desktop services but (by allowance) clipboard text,
// and motion. The public headers declare fewer entries in that mode, so the
// library and every unit that includes them must be compiled alike. A header
// whose declarations differ between the modes includes this one, and each
// such unit refers to the link marker of its own mode, which only a library
// of that mode defines: a consumer compiled the other way fails at link time
// instead of misbehaving.

#include "gui_forms/export/application_export.hpp"
#include <cstdint>

#if defined(GUI_FORMS_HIGH_INTEGRITY)
extern "C" {
GUI_FORMS_API extern const int gui_forms_build_profile_high_integrity_v1;
}
#else
extern "C" {
GUI_FORMS_API extern const int gui_forms_build_profile_standard_v1;
}
#endif

namespace gui_forms {

// A build configuration.
enum class BuildProfile : std::uint8_t {
    // The ordinary build: every entry is compiled.
    standard = 1,
    // GUI_FORMS_HIGH_INTEGRITY: the entries that take outside bytes or reach
    // outside the program are not compiled.
    high_integrity = 2,
};

// What the library was built with: the profile and the allowances of the
// high-integrity configuration. In a standard build every allowance is true,
// since nothing is compiled out.
struct GUI_FORMS_API BuildProfileRecord final {
    BuildProfile profile{BuildProfile::standard};
    // HostServices::read_clipboard_text and write_clipboard_text are compiled
    // (GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT in high integrity).
    bool clipboard_text{true};
};

// The profile the including unit is compiled with. build_profile() reports
// the library's; the link markers above keep the two equal.
#if defined(GUI_FORMS_HIGH_INTEGRITY)
inline constexpr BuildProfile compiled_build_profile = BuildProfile::high_integrity;
#else
inline constexpr BuildProfile compiled_build_profile = BuildProfile::standard;
#endif

// The library's build profile and allowances. Constant for the process; any
// thread.
[[nodiscard]] GUI_FORMS_API BuildProfileRecord build_profile() noexcept;

namespace detail {

// Every unit that includes this header refers to its mode's marker, so the
// link needs a library of the same mode. `used` keeps the reference in an
// optimized unit that never reads it.
#if defined(__GNUC__) || defined(__clang__)
#if defined(GUI_FORMS_HIGH_INTEGRITY)
[[gnu::used]] static const int* const build_profile_link_marker = &gui_forms_build_profile_high_integrity_v1;
#else
[[gnu::used]] static const int* const build_profile_link_marker = &gui_forms_build_profile_standard_v1;
#endif
#endif

} // namespace detail

} // namespace gui_forms
