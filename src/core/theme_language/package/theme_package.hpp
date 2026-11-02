#pragma once

// Theme packages, the installed-pack index and `@import` closure (stage
// T3, PLAN.md P10, section 1.9, owner decisions D2 and those of 2026-10-28
// on theme.json).
//
// A package is its manifest's bytes and its files' bytes by path; nothing
// is read from a file system or a network. A package is verified whole
// before anything uses it: its manifest is read (theme_manifest.hpp);
// every file the manifest lists is present once and has its hash
// (file_hash_mismatch, E602, names the path, a resource's as well as the
// sheet's); a file it does not list, or one given twice, is
// manifest_invalid (E601); and the sheet's `@import`s are read, each one a
// theme the manifest's `imports` lists (else import_not_installed, E603).
// A verified pack's identity is the SHA-256 of its manifest's bytes.
//
// The installed index holds verified packs by name. An `@import` resolves
// only there: a name not installed is E603, an installed pack whose
// identity is not the importer's pin is import_hash_mismatch (E604), a
// pack importing itself through any chain is import_cycle (E605), and
// more than maximum_imports packs over the whole closure is
// too_many_imports (E506, R2-20). Each installed theme enters a closure
// once, the first time it is reached, and each pack comes after every
// pack it imports, as CSS places an imported sheet's rules before the
// importing sheet's.

#include "../diagnostics/theme_diagnostics.hpp"
#include "theme_manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The most packs one index holds.
inline constexpr std::size_t maximum_installed_themes = 256U;

// One file of a package: its path inside the package and its bytes.
struct ThemePackageEntry final {
    std::string path{};
    std::string bytes{};
};

// A package as its caller supplies it.
struct ThemePackageBytes final {
    std::string manifest{};
    std::vector<ThemePackageEntry> files{};
};

// One `@import` of a sheet: the theme it names, where its name is
// written, and the bytes of the whole rule, `@import` to `;`.
struct ThemeSheetImport final {
    std::string name{};
    SourcePosition position{};
    ByteOffset first{};
    ByteOffset end{};
};

// A verified package.
struct VerifiedThemePack final {
    ThemeManifest manifest{};
    // The SHA-256 of the manifest's bytes.
    detail::Sha256Digest identity{};
    std::string sheet{};
    // The resources' bytes, at the positions of manifest.resources.
    std::vector<std::string> resources{};
    // The sheet's `@import`s, in order.
    std::vector<ThemeSheetImport> imports{};
};

enum class ThemePackageOutcome : std::uint8_t { verified, refused };

// Verifies `bytes` for the `running` engine into `pack`. On `refused`,
// `problem` is the one error, named for the manifest (theme_manifest_file)
// or the file at fault, and `pack` is left in an unspecified but valid
// state. A sheet that does not tokenize or parse is refused with its
// first error, as the compiler would.
[[nodiscard]] ThemePackageOutcome verify_theme_package(const ThemePackageBytes& bytes, EngineVersion running,
                                                       VerifiedThemePack& pack, ThemeDiagnostic& problem);

// Verified packs by name. Copies of the index are independent.
class InstalledThemeIndex final {
public:
    // Installs `pack`, replacing an installed pack of the same name; false,
    // with nothing changed, when the index holds maximum_installed_themes
    // others.
    [[nodiscard]] bool install(VerifiedThemePack pack);
    // Removes the pack named `name`; whether there was one.
    bool remove(std::string_view name) noexcept;
    // The pack named `name`, or null; it borrows from the index.
    [[nodiscard]] const VerifiedThemePack* find(std::string_view name) const& noexcept;
    const VerifiedThemePack* find(std::string_view name) const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept { return packs_.size(); }
    // Every pack, in installation order; borrows from the index.
    [[nodiscard]] std::span<const VerifiedThemePack> packs() const& noexcept { return packs_; }
    std::span<const VerifiedThemePack> packs() const&& = delete;

private:
    std::vector<VerifiedThemePack> packs_{};
};

enum class ThemeImportOutcome : std::uint8_t { resolved, refused };

// The closure of `root`'s imports among `index`'s packs, in cascade order
// into `order` (cleared first): every pack it reaches, each after the packs
// it imports, then `root` last. The pointers borrow from `root` and
// `index`. On `refused`, `problem` is the error, at the `@import` it
// concerns, and `order` is empty. The walk is iterative: its stack holds at
// most maximum_imports + 1 packs.
[[nodiscard]] ThemeImportOutcome resolve_theme_imports(const VerifiedThemePack& root,
                                                       const InstalledThemeIndex& index,
                                                       std::vector<const VerifiedThemePack*>& order,
                                                       ThemeDiagnostic& problem);

} // namespace gui_forms::theme_language
