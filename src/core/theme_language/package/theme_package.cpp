#include "theme_package.hpp"

#include "../source_text/theme_source_text.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "../tokenizer/theme_tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

ThemeDiagnostic package_problem(DiagnosticCode code, std::string_view file, std::string detail) {
    ThemeDiagnostic problem{};
    problem.code = code;
    problem.file = std::string(file);
    // Line 0: a finding with no place in the file, until one is set.
    problem.position.line = LineNumber{0U};
    problem.position.column = ColumnNumber{0U};
    problem.detail = std::move(detail);
    return problem;
}

// The entry of `bytes` at `path`, or null; a path given twice is found
// by `twice`.
const ThemePackageEntry* entry_at(const ThemePackageBytes& bytes, std::string_view path, bool& twice) noexcept {
    const ThemePackageEntry* found = nullptr;
    twice = false;
    for (const ThemePackageEntry& entry : bytes.files) {
        if (entry.path != path) continue;
        if (found != nullptr) twice = true;
        found = &entry;
    }
    return found;
}

// Whether the manifest lists `path`, as the sheet or a resource.
bool listed(const ThemeManifest& manifest, std::string_view path) noexcept {
    if (manifest.sheet.path == path) return true;
    for (const ThemeManifestResource& resource : manifest.resources) {
        if (resource.file.path == path) return true;
    }
    return false;
}

// The bytes the manifest lists at `file`, checked against its hash, into
// `into`; false with `problem` set otherwise.
bool listed_bytes(const ThemePackageBytes& bytes, const ThemePackageFile& file, std::string& into,
                  ThemeDiagnostic& problem) {
    bool twice = false;
    const ThemePackageEntry* const entry = entry_at(bytes, file.path, twice);
    if (twice) {
        problem = package_problem(DiagnosticCode::manifest_invalid, file.path, clipped_theme_detail(file.path));
        return false;
    }
    if (entry == nullptr || package_sha256((*entry).bytes) != file.sha256) {
        problem = package_problem(DiagnosticCode::file_hash_mismatch, file.path, clipped_theme_detail(file.path));
        return false;
    }
    into = (*entry).bytes;
    return true;
}

// The sheet's `@import`s into `pack.imports`; false with `problem` set at
// the sheet's first error, or at an import the manifest does not list.
bool read_sheet_imports(VerifiedThemePack& pack, ThemeDiagnostic& problem) {
    const ThemeSource source{.file = pack.manifest.sheet.path, .text = pack.sheet};
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeSyntaxTree tree{};
    ThemeDiagnosticList diagnostics{};
    const TokenizeOutcome tokenized = tokenize_theme_sheet(source, tokens, lines, diagnostics, NameEscapes::in_names);
    if (tokenized == TokenizeOutcome::tokens_cover_sheet && diagnostics.error_count() == 0U) {
        static_cast<void>(parse_theme_sheet(source, tokens, lines, tree, diagnostics));
    }
    if (diagnostics.error_count() != 0U) {
        for (const ThemeDiagnostic& diagnostic : diagnostics.records()) {
            if (diagnostic_severity(diagnostic.code) != DiagnosticSeverity::error) continue;
            problem = diagnostic;
            return false;
        }
        problem = package_problem(DiagnosticCode::manifest_invalid, source.file, {});
        return false;
    }
    pack.imports.clear();
    for (const SyntaxNode& node : tree.nodes) {
        if (node.kind != SyntaxNodeKind::import_rule) continue;
        const Token& written = tokens.tokens[node.content.first.value];
        const std::string_view quoted = pack.sheet;
        const std::string_view text = quoted.substr(written.offset.value + 1U, written.length.value - 2U);
        ThemeSheetImport imported{};
        imported.name = std::string(text);
        imported.position = lines.position(source.text, written.offset);
        const Token& last = tokens.tokens[node.tokens.end.value - 1U];
        imported.first = tokens.tokens[node.tokens.first.value].offset;
        imported.end = ByteOffset{last.offset.value + last.length.value};
        bool pinned = false;
        for (const ThemeManifestImport& listed_import : pack.manifest.imports) {
            if (listed_import.name == imported.name) pinned = true;
        }
        if (!pinned) {
            problem = package_problem(DiagnosticCode::import_not_installed, source.file, clipped_theme_detail(text));
            problem.position = imported.position;
            return false;
        }
        pack.imports.push_back(std::move(imported));
    }
    return true;
}

} // namespace

ThemePackageOutcome verify_theme_package(const ThemePackageBytes& bytes, EngineVersion running,
                                         VerifiedThemePack& pack, ThemeDiagnostic& problem) {
    ThemeManifestProblem manifest_problem{};
    const ThemeManifestOutcome read = read_theme_manifest(bytes.manifest, running, pack.manifest, manifest_problem);
    if (read != ThemeManifestOutcome::read) {
        problem = package_problem(manifest_problem.code, theme_manifest_file, std::move(manifest_problem.detail));
        if (manifest_problem.position.line != 0U) {
            problem.position.offset = ByteOffset{static_cast<std::uint32_t>(manifest_problem.position.byte_offset)};
            problem.position.line = LineNumber{manifest_problem.position.line};
            problem.position.column = ColumnNumber{manifest_problem.position.column};
        }
        return ThemePackageOutcome::refused;
    }
    for (const ThemePackageEntry& entry : bytes.files) {
        if (listed(pack.manifest, entry.path)) continue;
        // A file the manifest does not list: a package carries only what
        // its hashes vouch for.
        problem = package_problem(DiagnosticCode::manifest_invalid, entry.path, clipped_theme_detail(entry.path));
        return ThemePackageOutcome::refused;
    }
    if (!listed_bytes(bytes, pack.manifest.sheet, pack.sheet, problem)) return ThemePackageOutcome::refused;
    pack.resources.assign(pack.manifest.resources.size(), std::string{});
    for (std::size_t index = 0U; index < pack.manifest.resources.size(); ++index) {
        const ThemePackageFile& file = pack.manifest.resources[index].file;
        if (!listed_bytes(bytes, file, pack.resources[index], problem)) return ThemePackageOutcome::refused;
    }
    if (pack.sheet.size() > maximum_sheet_bytes) {
        problem = package_problem(DiagnosticCode::sheet_too_large, pack.manifest.sheet.path,
                                  std::to_string(maximum_sheet_bytes));
        return ThemePackageOutcome::refused;
    }
    if (!read_sheet_imports(pack, problem)) return ThemePackageOutcome::refused;
    pack.identity = package_sha256(bytes.manifest);
    return ThemePackageOutcome::verified;
}

bool InstalledThemeIndex::install(VerifiedThemePack pack) {
    for (VerifiedThemePack& installed : packs_) {
        if (installed.manifest.name != pack.manifest.name) continue;
        installed = std::move(pack);
        return true;
    }
    if (packs_.size() >= maximum_installed_themes) return false;
    packs_.push_back(std::move(pack));
    return true;
}

bool InstalledThemeIndex::remove(std::string_view name) noexcept {
    for (std::size_t index = 0U; index < packs_.size(); ++index) {
        if (packs_[index].manifest.name != name) continue;
        packs_.erase(packs_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    return false;
}

const VerifiedThemePack* InstalledThemeIndex::find(std::string_view name) const& noexcept {
    for (const VerifiedThemePack& pack : packs_) {
        if (pack.manifest.name == name) return &pack;
    }
    return nullptr;
}

namespace {

struct ImportFrame final {
    const VerifiedThemePack* pack{};
    std::size_t next{};
};

const ThemeManifestImport* pin_of(const VerifiedThemePack& pack, std::string_view name) noexcept {
    for (const ThemeManifestImport& pinned : pack.manifest.imports) {
        if (pinned.name == name) return &pinned;
    }
    return nullptr;
}

} // namespace

ThemeImportOutcome resolve_theme_imports(const VerifiedThemePack& root, const InstalledThemeIndex& index,
                                         std::vector<const VerifiedThemePack*>& order, ThemeDiagnostic& problem) {
    order.clear();
    std::vector<ImportFrame> stack{};
    stack.reserve(maximum_imports + 1U);
    stack.push_back(ImportFrame{&root, 0U});
    std::size_t imported = 0U;
    while (!stack.empty()) {
        const VerifiedThemePack& importer = *stack.back().pack;
        if (stack.back().next == importer.imports.size()) {
            order.push_back(&importer);
            stack.pop_back();
            continue;
        }
        const ThemeSheetImport& written = importer.imports[stack.back().next];
        stack.back().next = stack.back().next + 1U;
        // A user theme's sheet has no sheet path and is named for itself.
        const std::string file = importer.manifest.sheet.path.empty()
                                     ? importer.manifest.name
                                     : importer.manifest.name + "/" + importer.manifest.sheet.path;
        const std::string detail = clipped_theme_detail(written.name);
        DiagnosticCode refusal = DiagnosticCode::manifest_invalid;
        bool refused = false;
        for (const ImportFrame& frame : stack) {
            if ((*frame.pack).manifest.name != written.name) continue;
            refusal = DiagnosticCode::import_cycle;
            refused = true;
        }
        // A pack reached before is checked against this importer's pin
        // too, and enters the closure once.
        const ThemeManifestImport* const pinned = pin_of(importer, written.name);
        const VerifiedThemePack* const installed = index.find(written.name);
        if (!refused && (pinned == nullptr || installed == nullptr)) {
            refusal = DiagnosticCode::import_not_installed;
            refused = true;
        }
        if (!refused && (*installed).identity != (*pinned).manifest_sha256) {
            refusal = DiagnosticCode::import_hash_mismatch;
            refused = true;
        }
        bool seen = false;
        for (const VerifiedThemePack* const done : order) {
            if (done == installed) seen = true;
        }
        if (!refused && !seen) {
            imported = imported + 1U;
            if (imported > maximum_imports) {
                refusal = DiagnosticCode::too_many_imports;
                refused = true;
            }
        }
        if (refused) {
            const std::string what = refusal == DiagnosticCode::too_many_imports ? std::to_string(maximum_imports)
                                                                                 : detail;
            problem = package_problem(refusal, file, what);
            problem.position = written.position;
            order.clear();
            return ThemeImportOutcome::refused;
        }
        if (seen) continue;
        stack.push_back(ImportFrame{installed, 0U});
    }
    return ThemeImportOutcome::resolved;
}

} // namespace gui_forms::theme_language
