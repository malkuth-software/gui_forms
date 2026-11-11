#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms {

// Where a host found the bundled font directory. A host looks in its own order and takes the
// first directory that holds every required face (BundledFaceUse::required):
// - environment: the directory GUI_FORMS_FONT_DIR names;
// - executable: "fonts" beside the running executable;
// - library_module: "fonts" beside the module that holds GUI.Forms (the shared library a .NET
//   process or a plug-in host loaded, or the executable when GUI.Forms is linked into it);
// - install_share: an installed "share/GUIForms/fonts" next to the executable's or the
//   library's directory (Linux);
// - application_bundle: the application bundle's Resources/fonts (macOS).
// `none` means no candidate held the faces: the window then has no bundled text.
enum class BundledFontSource : std::uint8_t {
    none,
    environment,
    executable,
    library_module,
    install_share,
    application_bundle,
};

// The source's name in diagnostics: "none", "environment", "executable", "library_module",
// "install_share" or "application_bundle".
[[nodiscard]] std::string_view bundled_font_source_name(BundledFontSource source) noexcept;

// What the hosts' font lookups found in this process, for diagnostics and the windows' metrics
// (MetricsSnapshot::bundled_font_lookups_without_source). A host looks once per application run
// and once per window it opens. The first lookup that finds no source also writes one line to
// stderr, the refusal bundled_font_refusal returns; later ones are only counted. Any thread.
struct BundledFontDiagnostics final {
    std::uint64_t lookups{};
    std::uint64_t lookups_without_source{};
    // The most recent lookup's answer; none before the first lookup.
    BundledFontSource last_source{BundledFontSource::none};
    // The bundled fallback faces (the Noto faces for CJK, emoji, Arabic, Hebrew, Devanagari,
    // Bengali and Gurmukhi) that the most recent lookup's directory holds, of how many the
    // library knows. They are optional: an install or app package may leave out their
    // component (fallback_fonts, GUI_FORMS_INSTALL_FALLBACK_FONTS), and text only they cover
    // then falls back as for any missing face (missing-glyph boxes). Found is 0 when the
    // lookup found no directory; bundled_font_notice names the absent faces.
    std::uint32_t fallback_faces_found{};
    std::uint32_t fallback_faces_expected{};
};
[[nodiscard]] BundledFontDiagnostics bundled_font_diagnostics() noexcept;

// The most recent lookup's refusal: which directories it tried and why text is missing, as one
// line of UTF-8. Empty when that lookup found a source, or before the first lookup. A copy; any
// thread.
[[nodiscard]] std::string bundled_font_refusal();

// The most recent lookup's note on absent fallback faces: which of them its directory lacks
// and what that means for text, as one line of UTF-8. Empty when every fallback face is
// present, when that lookup found no directory (bundled_font_refusal says why), or before the
// first lookup. Only recorded, never written to stderr, since leaving the faces out is an
// install's or a package's choice. A copy; any thread.
[[nodiscard]] std::string bundled_font_notice();

} // namespace gui_forms
