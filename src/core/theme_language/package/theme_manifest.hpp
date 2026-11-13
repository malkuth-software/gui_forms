#pragma once

// A theme package's manifest, `theme.json` (stage T3, PLAN.md P10, owner
// decisions of 2026-10-28). The manifest is untrusted input, read with the
// bounded JSON reader:
//
//   {"format": "gui-forms-theme/1", "name": "harbour-dark",
//    "version": "1.4.0", "engine": ">=0.3",
//    "author": "...", "provenance": "...",
//    "sheet": {"path": "theme.theme.css", "sha256": "<hex>"},
//    "imports": {"harbour-base": "<SHA-256 of its theme.json>"},
//    "resources": [{"name": "icon.check", "path": "images/check.png",
//                   "sha256": "<hex>"}],
//    "published_values": ["progress"],
//    "coverage": ["button", "check-box"]}
//
// `format`, `name`, `version`, `engine` and `sheet` are required; the rest
// may be left out. One sheet a pack (decision 1a); further sheets arrive by
// `@import`, each import pinned by the SHA-256 of its manifest (2a). The
// engine bound is read first, so a manifest for a newer engine is refused
// with engine_too_old (E607, C8) before any key it may add is judged; then
// an unknown key, a missing one, or one of the wrong kind or form is
// manifest_invalid (E601, 3a). Coverage is informational (5a): the names
// are checked here for form only, and against the vocabulary by the
// caller (W106). Encoding (6): a hash is 64 lowercase hexadecimal digits;
// a path is relative, separated by `/`, with no `.`, `..` or empty
// segment, no backslash and no leading `/`, of at most 255 bytes and 8
// segments (nor, so that no path names a drive or a device, a control
// character or `:`); a name (the theme's, an import's, a published value's or a
// coverage entry's) is `[a-z0-9][a-z0-9-]{0,63}`; a resource's name may
// also hold `.` and `_` after its first character; `version` is
// MAJOR.MINOR.PATCH and informational; `author` and `provenance` are at
// most 256 bytes each. A published value ill-formed or listed twice is
// published_value_invalid (E606), and more than maximum_imports imports
// is too_many_imports (E506).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/integrity/sha256/sha256.hpp"
#include "../../json/json_reader/json_reader.hpp"
#include "../diagnostics/theme_diagnostics.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "gui_forms/engine/types/engine_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

inline constexpr std::string_view theme_manifest_format = "gui-forms-theme/1";
inline constexpr std::string_view theme_manifest_file = "theme.json";
// The most bytes one manifest takes.
inline constexpr std::size_t maximum_manifest_bytes = 64U * 1024U;
inline constexpr std::size_t maximum_manifest_text_bytes = 256U;
inline constexpr std::size_t maximum_package_path_bytes = 255U;
inline constexpr std::size_t maximum_package_path_segments = 8U;
inline constexpr std::size_t maximum_package_name_bytes = 64U;
inline constexpr std::size_t maximum_manifest_resources = 256U;
inline constexpr std::size_t maximum_manifest_published_values = 64U;
inline constexpr std::size_t maximum_manifest_coverage = 256U;
// A file of the package and the hash its bytes must have.
struct ThemePackageFile final {
    std::string path{};
    Sha256Digest sha256{};

    friend bool operator==(const ThemePackageFile& left, const ThemePackageFile& right) noexcept {
        return left.path == right.path && left.sha256 == right.sha256;
    }
};

// An installed theme the sheet may `@import`, pinned by the SHA-256 of its
// manifest's bytes.
struct ThemeManifestImport final {
    std::string name{};
    Sha256Digest manifest_sha256{};

    friend bool operator==(const ThemeManifestImport& left, const ThemeManifestImport& right) noexcept {
        return left.name == right.name && left.manifest_sha256 == right.manifest_sha256;
    }
};

// An image the sheet reads as `url("resource:<name>")`.
struct ThemeManifestResource final {
    std::string name{};
    ThemePackageFile file{};

    friend bool operator==(const ThemeManifestResource& left, const ThemeManifestResource& right) noexcept {
        return left.name == right.name && left.file == right.file;
    }
};

struct ThemeManifest final {
    std::string name{};
    std::string version{};
    EngineVersion engine{};
    std::string author{};
    std::string provenance{};
    ThemePackageFile sheet{};
    // In the manifest's order.
    std::vector<ThemeManifestImport> imports{};
    std::vector<ThemeManifestResource> resources{};
    std::vector<std::string> published_values{};
    std::vector<std::string> coverage{};

    friend bool operator==(const ThemeManifest& left, const ThemeManifest& right) noexcept {
        return left.name == right.name && left.version == right.version && left.engine == right.engine &&
               left.author == right.author && left.provenance == right.provenance && left.sheet == right.sheet &&
               left.imports == right.imports && left.resources == right.resources &&
               left.published_values == right.published_values && left.coverage == right.coverage;
    }
};

// Why a manifest was refused: the code, a detail (the key at fault, as
// `sheet.path`; the JSON reader's error name; or, for engine_too_old, the
// sentence naming the version needed), and where the JSON reader found
// its error (all zero for any other).
struct ThemeManifestProblem final {
    DiagnosticCode code{DiagnosticCode::manifest_invalid};
    std::string detail{};
    detail::JsonSourcePosition position{};
};

enum class ThemeManifestOutcome : std::uint8_t { read, refused };

// Reads `bytes` as a manifest for the `running` engine. On `read`,
// `manifest` holds it; on `refused`, `problem` says why and `manifest` is
// left in an unspecified but valid state.
[[nodiscard]] ThemeManifestOutcome read_theme_manifest(std::string_view bytes, EngineVersion running,
                                                       ThemeManifest& manifest, ThemeManifestProblem& problem);

// The SHA-256 of `bytes` (main's first-party implementation).
[[nodiscard]] GUI_FORMS_API Sha256Digest package_sha256(std::string_view bytes) noexcept;
// A digest as the manifest writes it: 64 lowercase hexadecimal digits.
[[nodiscard]] GUI_FORMS_API std::string package_sha256_hex(const Sha256Digest& digest);

// `>=X.Y`: X and Y decimal, without a sign or a leading zero, each at most
// 65535; none for anything else (C8: a lower bound only).
[[nodiscard]] std::optional<EngineVersion> parse_engine_bound(std::string_view text) noexcept;
// The sentence engine_too_old gives: "this theme needs engine >= X.Y;
// upgrade to run it".
[[nodiscard]] std::string engine_too_old_detail(EngineVersion needed);
// `[a-z0-9][a-z0-9-]{0,63}`.
[[nodiscard]] bool package_name_valid(std::string_view name) noexcept;
// A resource's name: a package name that may also hold `.` and `_` after
// its first character.
[[nodiscard]] bool package_resource_name_valid(std::string_view name) noexcept;
// A relative package path, as the encoding rules above state.
[[nodiscard]] bool package_path_valid(std::string_view path) noexcept;

} // namespace gui_forms::theme_language
