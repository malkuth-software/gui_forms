#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

#include "../../resources/image_registry/image_registry_access.hpp"
#include "../../theme_language/compiler/theme_compiler.hpp"
#include "../../theme_language/package/theme_package.hpp"
#include "../theme/theme_access.hpp"
#include "control_inline_style.hpp"
#include "theme_image_lease.hpp"
#include "theme_source_record.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms {

// The images compiled themes hold, by registry and by the hash of their
// bytes, so that themes loading the same resource into one registry share
// one image. Entries whose themes or registry are gone are dropped.
struct ThemeImageShares final {
    struct Entry final {
        detail::Sha256Digest digest{};
        std::weak_ptr<detail::ThemeImageLease> lease{};
    };
    std::vector<Entry> entries{};
};

struct ThemeLibrary::State final {
    theme_language::InstalledThemeIndex index{};
    ThemeImageShares images{};
};

struct ThemeLibraryAccess final {
    [[nodiscard]] static const theme_language::InstalledThemeIndex& index(const ThemeLibrary& library) noexcept {
        return (*library.state_).index;
    }
    // The library's shared images, or null for a moved-from library. The
    // record of what is loaded changes under a const library.
    [[nodiscard]] static ThemeImageShares* images(const ThemeLibrary& library) noexcept {
        if (library.state_ == nullptr) return nullptr;
        return &(*library.state_).images;
    }
};

namespace {

using theme_language::DiagnosticCode;
using theme_language::ThemeDiagnostic;
using theme_language::VerifiedThemePack;

// The name a closure's combined sheet gives its diagnostics before they
// are placed in their own sheets.
constexpr std::string_view closure_file = "<closure>";

ThemeCompileDiagnostic public_diagnostic(const ThemeDiagnostic& diagnostic) {
    ThemeCompileDiagnostic made{};
    made.code = theme_language::diagnostic_code_label(diagnostic.code);
    made.name = std::string(theme_language::diagnostic_code_name(diagnostic.code));
    const bool error = theme_language::diagnostic_severity(diagnostic.code) == theme_language::DiagnosticSeverity::error;
    made.severity = error ? ThemeDiagnosticSeverity::error : ThemeDiagnosticSeverity::warning;
    made.file = diagnostic.file;
    // A package's finding without a place has line 0.
    made.line = diagnostic.position.line.value;
    made.column = made.line == 0U ? 0U : diagnostic.position.column.value;
    made.rule = diagnostic.rule;
    made.property = diagnostic.property;
    made.detail = diagnostic.detail;
    return made;
}

ThemeCompileResult refusal(const ThemeDiagnostic& problem) {
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    diagnostics.push_back(public_diagnostic(problem));
    return ThemeCompileResult::refused(std::move(diagnostics));
}

ThemeDiagnostic problem_of(DiagnosticCode code, std::string file, std::string detail) {
    ThemeDiagnostic problem{};
    problem.code = code;
    problem.file = std::move(file);
    problem.position.line = theme_language::LineNumber{0U};
    problem.position.column = theme_language::ColumnNumber{0U};
    problem.detail = std::move(detail);
    return problem;
}

std::string sheet_file(const VerifiedThemePack& pack) {
    return pack.manifest.name + "/" + pack.manifest.sheet.path;
}

std::string manifest_file(const VerifiedThemePack& pack) {
    return pack.manifest.name + "/" + std::string(theme_language::theme_manifest_file);
}

// One sheet of the combined closure: its pack and where its bytes start.
struct ClosureSegment final {
    const VerifiedThemePack* pack{};
    std::size_t start{};
};

// The closure's sheets in cascade order as one text, each `@import` rule
// (and a leading byte order mark) replaced by as many spaces, so that every
// byte keeps its offset within its sheet; one line feed between sheets.
std::string combined_text(std::span<const VerifiedThemePack* const> order, std::vector<ClosureSegment>& segments) {
    std::string text{};
    segments.clear();
    for (const VerifiedThemePack* const pack : order) {
        if (!text.empty()) text.push_back('\n');
        const std::size_t start = text.size();
        segments.push_back(ClosureSegment{pack, start});
        text += (*pack).sheet;
        for (const theme_language::ThemeSheetImport& imported : (*pack).imports) {
            for (std::size_t offset = imported.first.value; offset < imported.end.value; ++offset) {
                text[start + offset] = ' ';
            }
        }
        if ((*pack).sheet.starts_with("\xEF\xBB\xBF")) text.replace(start, 3U, "   ");
    }
    return text;
}

// Places a diagnostic of the combined text in its own sheet.
void place(ThemeDiagnostic& diagnostic, std::span<const ClosureSegment> segments) {
    if (diagnostic.file != closure_file || segments.empty()) return;
    const std::size_t offset = diagnostic.position.offset.value;
    std::size_t found = 0U;
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        if (segments[index].start <= offset) found = index;
    }
    const VerifiedThemePack& pack = *segments[found].pack;
    diagnostic.file = sheet_file(pack);
    const std::size_t within = std::min(offset - segments[found].start, pack.sheet.size());
    theme_language::LineStartTable lines{};
    lines.reset(pack.sheet);
    diagnostic.position = lines.position(pack.sheet, theme_language::ByteOffset{static_cast<std::uint32_t>(within)});
}

// The coverage names no control type of `controls` has (W106).
void check_coverage(const VerifiedThemePack& pack, std::span<const ControlAnatomy> controls,
                    std::vector<ThemeCompileDiagnostic>& diagnostics) {
    for (const std::string& name : pack.manifest.coverage) {
        const std::string registered = theme_language::registered_name_from_kebab(name);
        bool known = false;
        for (const ControlAnatomy& anatomy : controls) {
            if (!registered.empty() && anatomy.control_id == registered) known = true;
        }
        if (known) continue;
        diagnostics.push_back(
            public_diagnostic(problem_of(DiagnosticCode::unknown_selector_name, manifest_file(pack), name)));
    }
}

bool same_registry(const std::weak_ptr<detail::ImageRegistryLiveness>& left,
                   const std::weak_ptr<detail::ImageRegistryLiveness>& right) noexcept {
    const bool same = !left.owner_before(right) && !right.owner_before(left);
    return same;
}

// The lease of an image of `encoded` in `images`: one a theme already
// holds, or a newly loaded one, recorded in `shares`; null with `error`
// set when the registry does not admit it.
std::shared_ptr<detail::ThemeImageLease> lease_image(const std::string& encoded, ImageRegistry& images,
                                                     ThemeImageShares& shares, ImageResourceError& error) {
    const std::weak_ptr<detail::ImageRegistryLiveness> registry = detail::ImageRegistryAccess::liveness(images);
    const detail::Sha256Digest digest = theme_language::package_sha256(encoded);
    std::size_t kept = 0U;
    std::shared_ptr<detail::ThemeImageLease> found{};
    for (std::size_t index = 0U; index < shares.entries.size(); ++index) {
        std::shared_ptr<detail::ThemeImageLease> held = shares.entries[index].lease.lock();
        if (held == nullptr || (*held).registry().expired()) continue;
        if (found == nullptr && shares.entries[index].digest == digest && same_registry((*held).registry(), registry)) {
            found = held;
        }
        shares.entries[kept] = shares.entries[index];
        kept = kept + 1U;
    }
    shares.entries.resize(kept);
    if (found != nullptr) return found;
    const ImageLoadResult image = images.load_png(std::as_bytes(std::span<const char>(encoded)));
    if (!image) {
        error = image.error;
        return nullptr;
    }
    const std::optional<ImageResourceView> view = images.find(image.image);
    if (!view) {
        // Not reached: a loaded image is found until the next mutation.
        static_cast<void>(images.remove(image.image));
        error = ImageResourceError::invalid_signature;
        return nullptr;
    }
    const Size pixel_size{static_cast<double>((*view).metadata.width), static_cast<double>((*view).metadata.height)};
    std::shared_ptr<detail::ThemeImageLease> made =
        std::make_shared<detail::ThemeImageLease>(registry, image.image, pixel_size);
    shares.entries.push_back(ThemeImageShares::Entry{digest, made});
    return made;
}

ThemeCompileResult compile_closure(const VerifiedThemePack& root, const theme_language::InstalledThemeIndex& index,
                                   ImageRegistry& images, ThemeImageShares& shares,
                                   const ThemeCompileOptions& options) {
    std::vector<const VerifiedThemePack*> order{};
    ThemeDiagnostic problem{};
    if (theme_language::resolve_theme_imports(root, index, order, problem) != theme_language::ThemeImportOutcome::resolved) {
        return refusal(problem);
    }
    // The published values every pack lists, each one the application
    // publishes (E606); the engine bound, the highest of the closure's.
    std::vector<std::string> published{};
    EngineVersion requires_engine{};
    for (const VerifiedThemePack* const pack : order) {
        requires_engine = std::max(requires_engine, (*pack).manifest.engine);
        for (const std::string& name : (*pack).manifest.published_values) {
            bool offered = false;
            for (const std::string& application : options.published_values) {
                if (application == name) offered = true;
            }
            if (!offered) {
                return refusal(problem_of(DiagnosticCode::published_value_invalid, manifest_file(*pack), name));
            }
            if (std::find(published.begin(), published.end(), name) == published.end()) published.push_back(name);
        }
    }
    std::vector<ClosureSegment> segments{};
    const std::string text = combined_text(order, segments);
    if (text.size() > theme_language::maximum_sheet_bytes) {
        return refusal(problem_of(DiagnosticCode::sheet_too_large, sheet_file(root),
                                  std::to_string(theme_language::maximum_sheet_bytes)));
    }
    // Each resource, the later pack's where two share a name, loaded as an
    // image.
    std::vector<std::string_view> names{};
    std::vector<const std::string*> bytes{};
    std::vector<const VerifiedThemePack*> owners{};
    for (const VerifiedThemePack* const pack : order) {
        for (std::size_t position = 0U; position < (*pack).manifest.resources.size(); ++position) {
            const std::string_view name = (*pack).manifest.resources[position].name;
            const std::vector<std::string_view>::iterator same = std::find(names.begin(), names.end(), name);
            if (same == names.end()) {
                names.push_back(name);
                bytes.push_back(&(*pack).resources[position]);
                owners.push_back(pack);
                continue;
            }
            const std::size_t earlier = static_cast<std::size_t>(same - names.begin());
            bytes[earlier] = &(*pack).resources[position];
            owners[earlier] = pack;
        }
    }
    // The theme owns its images through their leases: a refusal drops
    // them, and with them every image no other theme holds.
    std::vector<theme_language::ThemeImageResource> loaded{};
    std::vector<std::shared_ptr<const detail::ThemeImageLease>> held{};
    for (std::size_t position = 0U; position < names.size(); ++position) {
        ImageResourceError error{ImageResourceError::none};
        const std::shared_ptr<detail::ThemeImageLease> lease = lease_image(*bytes[position], images, shares, error);
        if (lease == nullptr) {
            const std::string detail = std::string(names[position]) + ": " + std::string(image_resource_error_name(error));
            return refusal(problem_of(DiagnosticCode::manifest_invalid, manifest_file(*owners[position]), detail));
        }
        theme_language::ThemeImageResource resource{};
        resource.resource = names[position];
        resource.image = (*lease).image();
        resource.pixel_size = (*lease).pixel_size();
        loaded.push_back(resource);
        held.push_back(lease);
    }
    const theme_language::ThemeSource source{.file = closure_file, .text = text};
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = registry,
                                                  .vocabulary = theme_language::SelectorVocabulary{options.controls},
                                                  .published_values = published,
                                                  .resources = names,
                                                  .images = loaded,
                                                  .requires_engine = requires_engine};
    std::unique_ptr<ThemeDefinition> definition(new ThemeDefinition(windows_professional_theme_definition()));
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, (*definition).rules, diagnostics);
    std::vector<ThemeCompileDiagnostic> found{};
    for (const VerifiedThemePack* const pack : order) check_coverage(*pack, options.controls, found);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) {
        ThemeDiagnostic placed = diagnostic;
        place(placed, segments);
        found.push_back(public_diagnostic(placed));
    }
    if (outcome != theme_language::CompileOutcome::compiled) return ThemeCompileResult::refused(std::move(found));
    (*definition).id = root.manifest.name;
    // The source the theme keeps for inline styles compiled against it
    // (P12b).
    std::shared_ptr<detail::ThemeSourceRecord> record = std::make_shared<detail::ThemeSourceRecord>();
    (*record).text = text;
    (*record).file = sheet_file(root);
    (*record).published_values = published;
    for (const theme_language::ThemeImageResource& resource : loaded) {
        (*record).images.push_back(detail::ThemeSourceImage{.resource = std::string(resource.resource),
                                                            .image = resource.image,
                                                            .pixel_size = resource.pixel_size});
    }
    (*record).controls.assign(options.controls.begin(), options.controls.end());
    (*record).requires_engine = requires_engine;
    std::shared_ptr<const detail::ThemeSourceRecord> kept(std::move(record));
    std::shared_ptr<const Theme> theme =
        detail::ThemeAccess::create_holding(*definition, std::move(held), std::move(kept));
    return ThemeCompileResult::compiled(std::move(theme), std::move(found));
}

} // namespace

ThemeLibrary::ThemeLibrary() : state_(std::make_unique<State>()) {}
ThemeLibrary::~ThemeLibrary() = default;
ThemeLibrary::ThemeLibrary(ThemeLibrary&&) noexcept = default;
ThemeLibrary& ThemeLibrary::operator=(ThemeLibrary&&) noexcept = default;

std::vector<ThemeCompileDiagnostic> ThemeLibrary::install(const ThemePackage& package) {
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    if (state_ == nullptr) state_ = std::make_unique<State>();
    theme_language::ThemePackageBytes bytes{};
    bytes.manifest = package.manifest;
    for (const ThemePackageFile& file : package.files) {
        bytes.files.push_back(theme_language::ThemePackageEntry{file.path, file.bytes});
    }
    VerifiedThemePack pack{};
    ThemeDiagnostic problem{};
    if (theme_language::verify_theme_package(bytes, current_engine_version, pack, problem) !=
        theme_language::ThemePackageOutcome::verified) {
        diagnostics.push_back(public_diagnostic(problem));
        return diagnostics;
    }
    const std::string name = pack.manifest.name;
    if (!(*state_).index.install(std::move(pack))) {
        diagnostics.push_back(public_diagnostic(
            problem_of(DiagnosticCode::manifest_invalid, std::string(theme_language::theme_manifest_file),
                       "the library holds " + std::to_string(theme_language::maximum_installed_themes) +
                           " packages; " + name + " is not installed")));
    }
    return diagnostics;
}

bool ThemeLibrary::remove(std::string_view name) noexcept {
    return state_ != nullptr && (*state_).index.remove(name);
}

bool ThemeLibrary::contains(std::string_view name) const noexcept {
    return state_ != nullptr && (*state_).index.find(name) != nullptr;
}

std::size_t ThemeLibrary::size() const noexcept {
    return state_ == nullptr ? 0U : (*state_).index.size();
}

ThemeCompileResult compile_theme_package(const ThemePackage& package, const ThemeLibrary& library,
                                         ImageRegistry& images, const ThemeCompileOptions& options) {
    theme_language::ThemePackageBytes bytes{};
    bytes.manifest = package.manifest;
    for (const ThemePackageFile& file : package.files) {
        bytes.files.push_back(theme_language::ThemePackageEntry{file.path, file.bytes});
    }
    VerifiedThemePack root{};
    ThemeDiagnostic problem{};
    if (theme_language::verify_theme_package(bytes, current_engine_version, root, problem) !=
        theme_language::ThemePackageOutcome::verified) {
        return refusal(problem);
    }
    const theme_language::InstalledThemeIndex empty{};
    const theme_language::InstalledThemeIndex& index =
        library.size() == 0U ? empty : ThemeLibraryAccess::index(library);
    ThemeImageShares unshared{};
    ThemeImageShares* const shares = ThemeLibraryAccess::images(library);
    return compile_closure(root, index, images, shares == nullptr ? unshared : *shares, options);
}

ThemeCompileResult compile_installed_theme(std::string_view name, const ThemeLibrary& library, ImageRegistry& images,
                                           const ThemeCompileOptions& options) {
    const VerifiedThemePack* const root =
        library.size() == 0U ? nullptr : ThemeLibraryAccess::index(library).find(name);
    if (root == nullptr) {
        return refusal(problem_of(DiagnosticCode::import_not_installed, std::string{},
                                  theme_language::clipped_theme_detail(name)));
    }
    return compile_closure(*root, ThemeLibraryAccess::index(library), images, *ThemeLibraryAccess::images(library),
                           options);
}

ThemeCompileResult::ThemeCompileResult(std::variant<Compiled, Refused> state) noexcept : state_(std::move(state)) {}

ThemeCompileResult ThemeCompileResult::compiled(std::shared_ptr<const Theme> theme,
                                                std::vector<ThemeCompileDiagnostic> warnings) noexcept {
    return ThemeCompileResult(Compiled{.theme = std::move(theme), .warnings = std::move(warnings)});
}

ThemeCompileResult ThemeCompileResult::refused(std::vector<ThemeCompileDiagnostic> diagnostics) noexcept {
    return ThemeCompileResult(Refused{.diagnostics = std::move(diagnostics)});
}

bool ThemeCompileResult::succeeded() const noexcept { return std::holds_alternative<Compiled>(state_); }

std::shared_ptr<const Theme> ThemeCompileResult::theme() const noexcept {
    const Compiled* const compiled = std::get_if<Compiled>(&state_);
    if (compiled == nullptr) return nullptr;
    return (*compiled).theme;
}

std::span<const ThemeCompileDiagnostic> ThemeCompileResult::diagnostics() const noexcept {
    const Compiled* const compiled = std::get_if<Compiled>(&state_);
    if (compiled != nullptr) return (*compiled).warnings;
    const Refused* const refused = std::get_if<Refused>(&state_);
    if (refused != nullptr) return (*refused).diagnostics;
    return {};
}

InlineStyleResult::InlineStyleResult(std::variant<Applied, Refused> state) noexcept : state_(std::move(state)) {}

InlineStyleResult InlineStyleResult::applied(std::vector<ThemeCompileDiagnostic> warnings) noexcept {
    return InlineStyleResult(Applied{.warnings = std::move(warnings)});
}

InlineStyleResult InlineStyleResult::refused(std::vector<ThemeCompileDiagnostic> diagnostics) noexcept {
    return InlineStyleResult(Refused{.diagnostics = std::move(diagnostics)});
}

bool InlineStyleResult::succeeded() const noexcept { return std::holds_alternative<Applied>(state_); }

std::span<const ThemeCompileDiagnostic> InlineStyleResult::diagnostics() const noexcept {
    const Applied* const applied = std::get_if<Applied>(&state_);
    if (applied != nullptr) return (*applied).warnings;
    const Refused* const refused = std::get_if<Refused>(&state_);
    if (refused != nullptr) return (*refused).diagnostics;
    return {};
}

namespace {

// Whether `rule`, lowered from a theme compiled again for an inline style,
// is one of the theme's rules that the style's definitions vary (P12b): a
// rule at the theme's levels, reading a name in `defined` (sorted), whose
// selector can match the control `stable_id` of type `control_type`. The
// segment's own definitions, at the inline levels, are the style's and
// are not taken again.
bool theme_reader_taken(const ThemeRule& rule, std::span<const std::string> defined, std::string_view control_type,
                        std::string_view stable_id) noexcept {
    if (rule.level == CascadeLevel::inline_style || rule.level == CascadeLevel::important_inline_style) return false;
    if (rule.source_position == 0U) return false;
    if (!rule.selector.control_type.empty() && rule.selector.control_type != control_type) return false;
    if (!rule.selector.stable_id.empty() && rule.selector.stable_id != stable_id) return false;
    for (const std::string& name : rule.reads) {
        if (std::binary_search(defined.begin(), defined.end(), name)) return true;
    }
    return false;
}

// Compiles the theme of `record` again with the inline style `text`
// (compiled by `compiler`, defining the names `defined`, sorted) written
// after it as sheet rules, and appends to `sheet` the theme's rules the
// style's definitions vary (theme_reader_taken). The theme's own
// diagnostics are its warnings again and are not repeated; every other
// diagnostic is appended to `diagnostics`, naming the theme's sheet or the
// style. False, with `sheet` unchanged, where an error refuses the style.
bool append_theme_readers(std::string_view text, const theme_language::ThemeCompiler& compiler,
                          std::span<const std::string> defined, const ControlAnatomy& anatomy,
                          std::string_view stable_id, std::string_view file, const detail::ThemeSourceRecord& record,
                          ThemeRuleSheet& sheet, std::vector<ThemeCompileDiagnostic>& diagnostics) {
    std::string combined = record.text;
    combined += "\n";
    const std::size_t from = combined.size();
    compiler.append_inline_as_sheet(text, combined);
    if (combined.size() > theme_language::maximum_sheet_bytes) {
        diagnostics.push_back(public_diagnostic(problem_of(DiagnosticCode::sheet_too_large, std::string(file),
                                                           std::to_string(theme_language::maximum_sheet_bytes))));
        return false;
    }
    // The theme's control types, and the style's own where the theme names
    // none of its kind.
    std::vector<ControlAnatomy> controls = record.controls;
    bool listed = false;
    for (const ControlAnatomy& control : controls) {
        if (control.control_id == anatomy.control_id) listed = true;
    }
    if (!listed) controls.push_back(anatomy);
    // Borrows of the record's strings, which outlive the compilation.
    std::vector<std::string_view> names{};
    std::vector<theme_language::ThemeImageResource> images{};
    names.reserve(record.images.size());
    images.reserve(record.images.size());
    for (const detail::ThemeSourceImage& image : record.images) {
        names.push_back(image.resource);
        theme_language::ThemeImageResource resource{};
        resource.resource = image.resource;
        resource.image = image.image;
        resource.pixel_size = image.pixel_size;
        images.push_back(resource);
    }
    const theme_language::ThemeSource source{.file = record.file, .text = combined};
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = builtin_primitive_registry(),
                                                  .vocabulary = theme_language::SelectorVocabulary{controls},
                                                  .published_values = record.published_values,
                                                  .resources = names,
                                                  .images = images,
                                                  .requires_engine = record.requires_engine,
                                                  .form = theme_language::ThemeSheetForm::sheet,
                                                  .inline_definitions_from = static_cast<std::uint32_t>(from)};
    ThemeRuleSheet lowered{};
    theme_language::ThemeCompiler again{};
    theme_language::ThemeDiagnosticList found{};
    const theme_language::CompileOutcome outcome = again.compile(input, lowered, found);
    for (const ThemeDiagnostic& diagnostic : found.records()) {
        const bool in_style = diagnostic.position.offset.value >= from;
        const bool error = theme_language::diagnostic_severity(diagnostic.code) == theme_language::DiagnosticSeverity::error;
        if (!in_style && !error) continue;
        ThemeDiagnostic placed = diagnostic;
        if (in_style) placed.file = std::string(file);
        diagnostics.push_back(public_diagnostic(placed));
    }
    if (outcome != theme_language::CompileOutcome::compiled) return false;
    for (ThemeRule& rule : lowered.rules) {
        if (theme_reader_taken(rule, defined, anatomy.control_id, stable_id)) sheet.rules.push_back(std::move(rule));
    }
    return true;
}

} // namespace

std::string detail::inline_restyle_refusal(std::string_view change,
                                           std::span<const ThemeCompileDiagnostic> diagnostics) {
    std::string message = std::string(change);
    message += " refused: an inline style defining custom properties does not compile against it";
    for (const ThemeCompileDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != ThemeDiagnosticSeverity::error) continue;
        message += " (" + diagnostic.code + " " + diagnostic.name + " in " + diagnostic.file + ":" +
                   std::to_string(diagnostic.line) + ":" + std::to_string(diagnostic.column) + ")";
        break;
    }
    return message;
}

std::shared_ptr<const detail::ControlInlineStyle> detail::compile_inline_style(
    std::string_view text, const ControlAnatomy& anatomy, std::string_view stable_id, std::string_view file,
    const Theme& theme, std::vector<ThemeCompileDiagnostic>& diagnostics) {
    const theme_language::ThemeSource source{.file = file, .text = text};
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const theme_language::ThemeCompileInput input{
        .source = source,
        .registry = registry,
        .vocabulary = theme_language::SelectorVocabulary{std::span<const ControlAnatomy>(&anatomy, 1U)},
        .published_values = {},
        .resources = {},
        .images = {},
        .requires_engine = current_engine_version,
        .form = theme_language::ThemeSheetForm::inline_style};
    ThemeRuleSheet sheet{};
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList found{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, sheet, found);
    for (const ThemeDiagnostic& diagnostic : found.records()) diagnostics.push_back(public_diagnostic(diagnostic));
    if (outcome != theme_language::CompileOutcome::compiled) return nullptr;
    // A style defining custom properties is compiled against the theme in
    // effect (P12b): the theme's rules reading them are lowered again with
    // the style's definitions. A theme without a kept source (the built-in
    // default) has no such rules.
    std::vector<std::string> defined{};
    compiler.append_defined_names(defined);
    std::sort(defined.begin(), defined.end());
    const detail::ThemeSourceRecord* const record = ThemeAccess::source(theme);
    if (!defined.empty() && record != nullptr) {
        if (!append_theme_readers(text, compiler, defined, anatomy, stable_id, file, *record, sheet, diagnostics)) {
            return nullptr;
        }
    }
    const CompiledThemeRules* const against = defined.empty() ? nullptr : &theme.engine().rules();
    std::shared_ptr<ControlInlineStyle> style = std::make_shared<ControlInlineStyle>();
    (*style).text = std::string(text);
    (*style).defines = !defined.empty();
    (*style).rules = CompiledThemeRules::compile_inline(sheet, registry, against);
    std::shared_ptr<const ControlInlineStyle> result(std::move(style));
    return result;
}

} // namespace gui_forms
