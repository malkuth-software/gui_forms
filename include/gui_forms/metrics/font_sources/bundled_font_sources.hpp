#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms {

// Where the bundled faces come from. A shipped program reads them from the font pack compiled
// into the library (embedded_pack, font pack stage 4). A development build configured with
// GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY first offers its host's directories, in the host's own
// order, and takes the first that holds every required face (BundledFaceUse::required),
// falling back to the pack:
// - environment: the directory GUI_FORMS_FONT_DIR names;
// - executable: "fonts" beside the running executable;
// - library_module: "fonts" beside the module that holds GUI.Forms (the shared library a .NET
//   process or a plug-in host loaded, or the executable when GUI.Forms is linked into it);
// - install_share: an installed "share/GUIForms/fonts" next to the executable's or the
//   library's directory (Linux);
// - application_bundle: the application bundle's Resources/fonts (macOS);
// - embedded_pack: the font pack compiled into the library.
// `none` means the embedded pack did not open (and no development directory held the faces):
// the window then has no bundled text.
enum class BundledFontSource : std::uint8_t {
    none,
    environment,
    executable,
    library_module,
    install_share,
    application_bundle,
    embedded_pack,
};

// The source's name in diagnostics: "none", "environment", "executable", "library_module",
// "install_share", "application_bundle" or "embedded_pack".
[[nodiscard]] GUI_FORMS_API std::string_view bundled_font_source_name(BundledFontSource source) noexcept;

// What the hosts' font lookups found in this process, for diagnostics and the windows' metrics
// (MetricsSnapshot::bundled_font_lookups_without_source), and what decoding the pack refused.
// A host looks once per application run and once per window it opens. The first lookup that
// finds no source also writes one line to stderr, the refusal bundled_font_refusal returns;
// later ones are only counted. The first refused pack group is written to stderr the same way
// (bundled_font_pack_refusal). Any thread.
struct GUI_FORMS_API BundledFontDiagnostics final {
    std::uint64_t lookups{};
    std::uint64_t lookups_without_source{};
    // The most recent lookup's answer; none before the first lookup.
    BundledFontSource last_source{BundledFontSource::none};
    // The bundled fallback faces (the Noto faces for CJK, emoji, Arabic, Hebrew, Devanagari,
    // Bengali and Gurmukhi) that the most recent lookup's source holds, of how many the
    // library knows. They are optional: a build may leave them out of the pack
    // (GUI_FORMS_INSTALL_FALLBACK_FONTS), a development directory may lack them, and text
    // only they cover then falls back as for any missing face (missing-glyph boxes). Found is
    // 0 when the lookup found no source; bundled_font_notice names the absent faces.
    std::uint32_t fallback_faces_found{};
    std::uint32_t fallback_faces_expected{};
    // Pack groups (or whole packs) a font set refused when it decoded them: a face that fails
    // its CRC-32, a damaged stream, or no memory. Their faces are refused (FontSetError::
    // pack_refused) and draw as missing.
    std::uint64_t pack_refusals{};
};
[[nodiscard]] GUI_FORMS_API BundledFontDiagnostics bundled_font_diagnostics() noexcept;

// The most recent lookup's refusal: which directories it tried and why text is missing, as one
// line of UTF-8. Empty when that lookup found a source, or before the first lookup. A copy; any
// thread.
[[nodiscard]] GUI_FORMS_API std::string bundled_font_refusal();

// The most recent lookup's notes, as one line of UTF-8: which fallback faces its source lacks
// and what that means for text, and, in a development build, that no directory held the
// faces so the embedded pack was used. Empty when there is nothing to note, when that lookup
// found no source (bundled_font_refusal says why), or before the first lookup. Only recorded,
// never written to stderr, since leaving the faces out is a build's choice. A copy; any
// thread.
[[nodiscard]] GUI_FORMS_API std::string bundled_font_notice();

// The most recent pack refusal (BundledFontDiagnostics::pack_refusals): which group was
// refused, why (the loader's phrase, "bad check value" for a face whose CRC-32 differs), and
// which faces it held, as one line of UTF-8. Empty before the first refusal. A copy; any
// thread.
[[nodiscard]] GUI_FORMS_API std::string bundled_font_pack_refusal();

} // namespace gui_forms
