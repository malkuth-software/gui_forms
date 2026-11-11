#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include "gui_forms/integrity/sha256/sha256.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include "../../image/resource_pixels.hpp"
#include "../../resources/image_registry/image_registry_access.hpp"
#include "../../theme_language/compiler/theme_compiler.hpp"
#include "../../theme_language/package/theme_header.hpp"
#include "../../theme_language/package/theme_package.hpp"
#include "../theme/floor_sheet.hpp"
#include "../familiar_themes/windows_professional_floor.hpp"
#include "../familiar_themes/windows_professional_sheets.hpp"
#include "../theme/theme_access.hpp"
#include "built_in_theme.hpp"
#include "control_inline_style.hpp"
#include "theme_image_lease.hpp"
#include "theme_source_record.hpp"

#include <algorithm>
#include <array>
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
        Sha256Digest digest{};
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

// Whether a rule of `sheet` holds only under `prefers-color-scheme: dark`.
bool sheet_has_dark_rules(const ThemeRuleSheet& sheet) noexcept {
    for (const ThemeRule& rule : sheet.rules) {
        if (rule.media.dark_scheme == true) return true;
    }
    return false;
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

// A package's sheet as `<theme>/<path>`; a user theme's sheet, whose pack
// has no sheet path, by its own name (`harbour.css`, `harbour/editor.css`).
std::string sheet_file(const VerifiedThemePack& pack) {
    if (pack.manifest.sheet.path.empty()) return pack.manifest.name;
    return pack.manifest.name + "/" + pack.manifest.sheet.path;
}

// A package's manifest; a user theme's sheet stands for its implied one.
std::string manifest_file(const VerifiedThemePack& pack) {
    if (pack.manifest.sheet.path.empty()) return pack.manifest.name;
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

// Places each rule of the combined text (provenance file closure_file) in
// its own sheet (T4 cascade spec, item 5): the sheet's file, the line and
// column within it, its pack's name and manifest hash, and its layer:
// GUI.Forms' base sheet and the packages `roots` import are library packs,
// the roots themselves the project theme; a user theme's rules keep the
// user layer. `segments` are in increasing order of start.
void place_rules(ThemeRuleSheet& sheet, std::span<const ClosureSegment> segments,
                 std::span<const VerifiedThemePack* const> roots, const VerifiedThemePack* base, ThemeRuleSet set) {
    if (segments.empty()) return;
    std::vector<theme_language::LineStartTable> lines(segments.size());
    for (std::size_t index = 0U; index < segments.size(); ++index) lines[index].reset((*segments[index].pack).sheet);
    for (ThemeRule& rule : sheet.rules) {
        RuleProvenance& provenance = rule.provenance;
        if (provenance.file != closure_file) continue;
        const std::size_t offset = provenance.offset;
        std::size_t found = 0U;
        for (std::size_t index = 0U; index < segments.size(); ++index) {
            if (segments[index].start <= offset) found = index;
        }
        const VerifiedThemePack& pack = *segments[found].pack;
        const std::size_t within = std::min(offset - segments[found].start, pack.sheet.size());
        const theme_language::SourcePosition position =
            lines[found].position(pack.sheet, theme_language::ByteOffset{static_cast<std::uint32_t>(within)});
        provenance.file = sheet_file(pack);
        provenance.line = position.line.value;
        provenance.column = position.column.value;
        provenance.offset = position.offset.value;
        provenance.pack = pack.manifest.name;
        const bool own_base = &pack == base;
        if (!own_base) provenance.pack_hash = theme_language::package_sha256_hex(pack.identity);
        if (set == ThemeRuleSet::user) continue;
        const bool root = std::find(roots.begin(), roots.end(), &pack) != roots.end();
        provenance.layer = root && !own_base ? ThemeLayer::project_theme : ThemeLayer::library_pack;
    }
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
    const Sha256Digest digest = theme_language::package_sha256(encoded);
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

// A user theme's icon override, its identifier one the program lets it
// replace (compile_user_theme); `bytes` borrows the folder's file.
struct UserIcon final {
    std::string identifier{};
    std::string file{};
    const std::string* bytes{};
};

constexpr std::uint32_t maximum_icon_side = 1024U;
constexpr std::size_t maximum_icon_bytes = std::size_t{1} << 20U;

// Whether any pixel of `pixels` (premultiplied BGRA rows) is not fully
// transparent: in premultiplied pixels a zero alpha means a zero pixel.
bool any_pixel_visible(std::span<const std::byte> pixels) noexcept {
    for (std::size_t alpha = 3U; alpha < pixels.size(); alpha += 4U) {
        if (pixels[alpha] != std::byte{0}) return true;
    }
    return false;
}

// Leases `icon`'s image and checks it (USER_THEMES.md section 6): null,
// with `refused` saying why, where the registry does not admit it, it is
// larger than 1024 by 1024 pixels, it does not decode or every pixel is
// transparent. `decoder` and `pixels` are the caller's, kept across icons;
// `pixels` grows to the largest icon decoded.
std::shared_ptr<detail::ThemeImageLease> checked_icon(const UserIcon& icon, ImageRegistry& images,
                                                      ThemeImageShares& shares, image::ResourcePixelDecoder& decoder,
                                                      std::vector<std::byte>& pixels, std::string& refused) {
    ImageResourceError error{ImageResourceError::none};
    std::shared_ptr<detail::ThemeImageLease> lease = lease_image(*icon.bytes, images, shares, error);
    if (lease == nullptr) {
        refused = std::string(image_resource_error_name(error));
        return nullptr;
    }
    const std::optional<ImageResourceView> view = images.find((*lease).image());
    if (!view) {
        refused = "not in the registry";
        return nullptr;
    }
    const std::uint32_t width = (*view).metadata.width;
    const std::uint32_t height = (*view).metadata.height;
    if (width > maximum_icon_side || height > maximum_icon_side) {
        refused = "larger than 1024 by 1024 pixels";
        return nullptr;
    }
    // At most 1024 * 4 bytes a row and 4 MiB in all: no overflow.
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    const std::size_t needed = row_bytes * static_cast<std::size_t>(height);
    if (pixels.size() < needed) pixels.resize(needed);
    const std::span<std::byte> target_pixels(pixels.data(), needed);
    const image::PixelTarget target{image::PixelOrder::bgra, image::PixelAlpha::premultiplied};
    const image::ResourcePixelResult decoded = decoder.decode(*view, target, target_pixels, row_bytes);
    if (decoded.error != image::ResourcePixelError::none) {
        refused = "does not decode";
        return nullptr;
    }
    if (!any_pixel_visible(target_pixels)) {
        refused = "fully transparent";
        return nullptr;
    }
    return lease;
}

// Whether `order` already holds a pack named `name`.
bool order_names(std::span<const VerifiedThemePack* const> order, std::string_view name) noexcept {
    for (const VerifiedThemePack* const pack : order) {
        if ((*pack).manifest.name == name) return true;
    }
    return false;
}

// Compiles `roots`, in order, each with its imports among `index`'s packs,
// as one cascade at the levels of `set`, into a theme named `id` (the
// base's own id where `id` is empty). Every pack appears once, where it is
// first reached: a pack a later root imports again keeps its earlier
// place. Two roots of one name are refused (E601). With no roots the theme
// is the base alone.
// Whether `controls` holds an anatomy for `control_id`.
bool names_control(std::span<const ControlAnatomy> controls, std::string_view control_id) {
    for (const ControlAnatomy& control : controls) {
        if (control.control_id == control_id) return true;
    }
    return false;
}

// The anatomies a closure's selectors resolve against: the caller's, then,
// onto windows-professional, each stock control the caller did not name,
// since that base's parts sheet speaks the stock controls' vocabulary
// whatever the caller compiles for. Fills `storage` only in that case.
std::span<const ControlAnatomy> closure_vocabulary(const ThemeCompileOptions& options,
                                                   std::vector<ControlAnatomy>& storage) {
    if (options.base == ThemeBase::gui_forms) return options.controls;
    const std::span<const ControlAnatomy> stock = stock_control_anatomies();
    storage.reserve(options.controls.size() + stock.size());
    storage.insert(storage.end(), options.controls.begin(), options.controls.end());
    for (const ControlAnatomy& control : stock) {
        if (!names_control(options.controls, control.control_id)) storage.push_back(control);
    }
    const std::span<const ControlAnatomy> merged(storage);
    return merged;
}

ThemeCompileResult compile_closure(std::span<const VerifiedThemePack* const> roots,
                                   const theme_language::InstalledThemeIndex& index, ImageRegistry& images,
                                   ThemeImageShares& shares, const ThemeCompileOptions& options, ThemeRuleSet set,
                                   std::string_view id, std::span<const UserIcon> icons) {
    // The base's sheet as the first sheet of the cascade: GUI.Forms' own
    // theme's light sheet, or windows-professional's parts sheet (owner,
    // 2026-11-10). A pack with no manifest entries of its own, held here
    // for the whole compilation (`order` borrows it); it also names the
    // theme's findings when there is no root.
    const bool onto_gui_forms = options.base == ThemeBase::gui_forms;
    VerifiedThemePack base_pack{};
    base_pack.manifest.name = onto_gui_forms ? "gui-forms" : "windows-professional";
    base_pack.manifest.sheet.path = onto_gui_forms ? "light.css" : "parts.css";
    base_pack.sheet = onto_gui_forms ? std::string(gui_forms_light_sheet())
                                     : std::string(detail::windows_professional_parts_sheet);
    std::vector<const VerifiedThemePack*> order{};
    order.push_back(&base_pack);
    std::vector<const VerifiedThemePack*> reached{};
    ThemeDiagnostic problem{};
    for (std::size_t position = 0U; position < roots.size(); ++position) {
        const VerifiedThemePack& root = *roots[position];
        for (std::size_t earlier = 0U; earlier < position; ++earlier) {
            if ((*roots[earlier]).manifest.name == root.manifest.name) {
                return refusal(problem_of(DiagnosticCode::manifest_invalid, manifest_file(root), "listed twice"));
            }
        }
        if (theme_language::resolve_theme_imports(root, index, reached, problem) !=
            theme_language::ThemeImportOutcome::resolved) {
            return refusal(problem);
        }
        for (const VerifiedThemePack* const pack : reached) {
            if (!order_names(order, (*pack).manifest.name)) order.push_back(pack);
        }
    }
    // The pack the theme's own findings name: the last root, which the
    // cascade lets override the others, else the base.
    const VerifiedThemePack& root = roots.empty() ? base_pack : *roots.back();
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
    // A layered user theme publishes no values (CompiledThemeRules::
    // compile_user): its expressions read the engine inputs alone.
    if (set == ThemeRuleSet::user && !published.empty()) {
        return refusal(problem_of(DiagnosticCode::published_value_invalid, sheet_file(root),
                                  "a user theme reads no published value: " + published[0]));
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
    // Packages compiled onto the GUI.Forms base: the base sheet is CSS's
    // user-agent origin under them (selector step 1). Alone, it is the
    // program's own sheet.
    std::optional<std::uint32_t> base_origin_end{};
    if (set == ThemeRuleSet::program && segments.size() > 1U) {
        base_origin_end = static_cast<std::uint32_t>(segments[1].start);
    }
    const theme_language::ThemeSource source{.file = closure_file, .text = text};
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    std::vector<ControlAnatomy> vocabulary_storage{};
    const std::span<const ControlAnatomy> vocabulary = closure_vocabulary(options, vocabulary_storage);
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = registry,
                                                  .vocabulary = theme_language::SelectorVocabulary{vocabulary},
                                                  .published_values = published,
                                                  .resources = names,
                                                  .images = loaded,
                                                  .requires_engine = requires_engine,
                                                  .user_theme = set == ThemeRuleSet::user,
                                                  .base_origin_end = base_origin_end,
                                                  .system_colours = options.system_colours,
                                                  .colour_authority = onto_gui_forms && roots.empty(),
                                                  .effects = options.effects};
    // The base's recipes, some 100 KB held out of line.
    std::unique_ptr<ThemeDefinition> definition(new ThemeDefinition(
        onto_gui_forms ? gui_forms_theme_definition() : detail::windows_professional_floor_definition()));
    (*definition).rule_set = set;
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
    place_rules((*definition).rules, segments, roots, &base_pack, set);
    // A program theme with rules for the dark scheme but no dark variant
    // (T4 theme variants): a dark window shows GUI.Forms' own dark theme,
    // so the rules apply nowhere. A user theme is layered, and has none.
    const bool dark_declared = (*definition).rules.colour_schemes && (*(*definition).rules.colour_schemes).dark;
    if (set == ThemeRuleSet::program && !dark_declared && sheet_has_dark_rules((*definition).rules)) {
        found.push_back(public_diagnostic(
            problem_of(DiagnosticCode::scheme_rules_without_variant, sheet_file(root), "color-scheme: light dark")));
    }
    if (!id.empty()) (*definition).id = std::string(id);
    // The source the theme keeps for inline styles compiled against it
    // (P12b).
    std::shared_ptr<detail::ThemeSourceRecord> record = std::make_shared<detail::ThemeSourceRecord>();
    (*record).text = text;
    (*record).file = closure_file;
    for (const ClosureSegment& segment : segments) {
        (*record).sheets.push_back(detail::ThemeSourceSheet{
            .file = sheet_file(*segment.pack), .start = segment.start, .length = (*segment.pack).sheet.size()});
    }
    compiler.append_defined_names((*record).defined_names);
    std::sort((*record).defined_names.begin(), (*record).defined_names.end());
    (*record).published_values = published;
    for (const theme_language::ThemeImageResource& resource : loaded) {
        (*record).images.push_back(detail::ThemeSourceImage{.resource = std::string(resource.resource),
                                                            .image = resource.image,
                                                            .pixel_size = resource.pixel_size});
    }
    // The icon overrides, each checked and held as the images are; one
    // refused is a warning and the program's icon stays.
    image::ResourcePixelDecoder decoder{};
    std::vector<std::byte> pixels{};
    for (const UserIcon& icon : icons) {
        std::string refused{};
        const std::shared_ptr<detail::ThemeImageLease> lease =
            checked_icon(icon, images, shares, decoder, pixels, refused);
        if (lease == nullptr) {
            found.push_back(public_diagnostic(problem_of(DiagnosticCode::icon_refused, icon.file, refused)));
            continue;
        }
        (*record).icons.push_back(detail::ThemeSourceImage{
            .resource = icon.identifier, .image = (*lease).image(), .pixel_size = (*lease).pixel_size()});
        held.push_back(lease);
    }
    (*record).controls.assign(vocabulary.begin(), vocabulary.end());
    (*record).requires_engine = requires_engine;
    std::shared_ptr<const detail::ThemeSourceRecord> kept(std::move(record));
    std::shared_ptr<const Theme> theme =
        detail::ThemeAccess::create_holding(*definition, std::move(held), std::move(kept));
    return ThemeCompileResult::compiled(std::move(theme), std::move(found));
}


// -- user themes (T3, USER_THEMES.md) -------------------------------------------

// What one file of a user theme's folder is.
enum class UserThemeFileKind : std::uint8_t { sub_sheet, resource, icon, other };

UserThemeFileKind user_file_kind(std::string_view path) noexcept {
    const bool top_level = path.find('/') == std::string_view::npos;
    if (top_level && path.ends_with(".css")) return UserThemeFileKind::sub_sheet;
    if (top_level && path.ends_with(".png")) return UserThemeFileKind::resource;
    constexpr std::string_view icons = "icons/";
    const bool in_icons = path.starts_with(icons) && path.find('/', icons.size()) == std::string_view::npos;
    if (in_icons && path.ends_with(".png")) return UserThemeFileKind::icon;
    return UserThemeFileKind::other;
}

// One sheet of a user theme: its name in the closure (`harbour.css` for
// the main file, `harbour/editor.css` for a sub-sheet), its path in the
// theme's folder (empty for the main file), its bytes and their hash.
struct UserSheet final {
    std::string name{};
    std::string path{};
    const std::string* text{};
    Sha256Digest identity{};
};

// The first error of `diagnostics`, or a manifest_invalid problem in `file`.
ThemeDiagnostic first_error(const theme_language::ThemeDiagnosticList& diagnostics, std::string_view file) {
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) {
        if (theme_language::diagnostic_severity(diagnostic.code) == theme_language::DiagnosticSeverity::error) {
            return diagnostic;
        }
    }
    return problem_of(DiagnosticCode::manifest_invalid, std::string(file), {});
}

// `sheet` made a pack in `pack`: its `@import`s read, a sub-sheet's
// (`@import "editor.css";`) named as `<theme>/<path>` and pinned to its
// hash, any other pinned to `index`'s package of that name; its `@theme`
// header, when `header` is given, read into it with its warnings appended
// to `warnings`, and blanked from the pack's text (every byte but line
// breaks a space), since the closure's combined text holds it after other
// sheets. False with `problem` set at the first error.
bool read_user_sheet(const UserSheet& sheet, std::string_view theme, std::span<const UserSheet> subs,
                     const theme_language::InstalledThemeIndex& index, VerifiedThemePack& pack,
                     theme_language::ThemeHeader* header, std::vector<ThemeCompileDiagnostic>& warnings,
                     ThemeDiagnostic& problem) {
    const theme_language::ThemeSource source{.file = sheet.name, .text = *sheet.text};
    theme_language::ThemeTokenBuffer tokens{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeSyntaxTree tree{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::TokenizeOutcome tokenized =
        theme_language::tokenize_theme_sheet(source, tokens, lines, diagnostics, theme_language::NameEscapes::in_names);
    if (tokenized == theme_language::TokenizeOutcome::tokens_cover_sheet && diagnostics.error_count() == 0U) {
        static_cast<void>(theme_language::parse_theme_sheet(source, tokens, lines, tree, diagnostics));
    }
    if (diagnostics.error_count() != 0U) {
        problem = first_error(diagnostics, sheet.name);
        return false;
    }
    pack = VerifiedThemePack{};
    pack.manifest.name = sheet.name;
    pack.identity = sheet.identity;
    pack.sheet = *sheet.text;
    for (std::size_t index_of = 1U; index_of < tree.nodes.size(); index_of = tree.nodes[index_of].subtree_end.value) {
        const theme_language::SyntaxNode& node = tree.nodes[index_of];
        const theme_language::Token& last = tokens.tokens[node.tokens.end.value - 1U];
        const std::size_t first = tokens.tokens[node.tokens.first.value].offset.value;
        const std::size_t end = last.offset.value + last.length.value;
        if (node.kind == theme_language::SyntaxNodeKind::theme_header) {
            for (std::size_t offset = first; offset < end; ++offset) {
                if (pack.sheet[offset] != '\n' && pack.sheet[offset] != '\r') pack.sheet[offset] = ' ';
            }
            continue;
        }
        if (node.kind != theme_language::SyntaxNodeKind::import_rule) continue;
        const theme_language::Token& written = tokens.tokens[node.content.first.value];
        const std::string_view quoted = std::string_view(*sheet.text).substr(written.offset.value, written.length.value);
        const std::string_view name = quoted.substr(1U, quoted.size() - 2U);
        theme_language::ThemeSheetImport imported{};
        imported.position = lines.position(source.text, written.offset);
        imported.first = theme_language::ByteOffset{static_cast<std::uint32_t>(first)};
        imported.end = theme_language::ByteOffset{static_cast<std::uint32_t>(end)};
        theme_language::ThemeManifestImport pin{};
        bool pinned = false;
        if (name.ends_with(".css")) {
            for (const UserSheet& sub : subs) {
                if (sub.path != name) continue;
                pin.name = sub.name;
                pin.manifest_sha256 = sub.identity;
                pinned = true;
            }
            if (!pinned) {
                problem = problem_of(DiagnosticCode::import_not_installed, sheet.name,
                                     theme_language::clipped_theme_detail(name));
                problem.position = imported.position;
                return false;
            }
        } else {
            pin.name = std::string(name);
            const VerifiedThemePack* const installed = index.find(name);
            if (installed != nullptr) {
                pin.manifest_sha256 = (*installed).identity;
                pinned = true;
            }
        }
        imported.name = pin.name;
        if (pinned) pack.manifest.imports.push_back(pin);
        pack.imports.push_back(std::move(imported));
    }
    if (header == nullptr) return true;
    if (theme_language::read_theme_header(source, tokens, lines, tree, *header, diagnostics) !=
        theme_language::ThemeHeaderOutcome::read) {
        problem = first_error(diagnostics, sheet.name);
        return false;
    }
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) warnings.push_back(public_diagnostic(diagnostic));
    return true;
}

// Sub-sheets by the byte order of their paths.
struct UserSheetNameOrder final {
    bool operator()(const UserSheet& left, const UserSheet& right) const noexcept { return left.path < right.path; }
};

// Whether any of `packs` imports the sheet named `name`.
bool imported_by(std::span<const VerifiedThemePack> packs, const VerifiedThemePack& main, std::string_view name) {
    for (const theme_language::ThemeSheetImport& imported : main.imports) {
        if (imported.name == name) return true;
    }
    for (const VerifiedThemePack& pack : packs) {
        for (const theme_language::ThemeSheetImport& imported : pack.imports) {
            if (imported.name == name) return true;
        }
    }
    return false;
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
    const VerifiedThemePack* const roots[] = {&root};
    return compile_closure(roots, index, images, shares == nullptr ? unshared : *shares, options, ThemeRuleSet::program,
                           root.manifest.name, std::span<const UserIcon>{});
}

ThemeValidation validate_theme_packages(std::span<const ThemePackage> packages, const ThemeLibrary& library,
                                        const ThemeCompileOptions& options) {
    // The registry outlives the compiled theme, whose leases remove its
    // images from it when the theme is released at the end of this scope.
    ImageRegistry images{};
    ThemeValidation validation{};
    {
        const ThemeCompileResult result = compile_theme_packages(packages, library, images, options);
        validation.valid = result.succeeded();
        const std::span<const ThemeCompileDiagnostic> found = result.diagnostics();
        validation.diagnostics.assign(found.begin(), found.end());
    }
    return validation;
}

std::string format_theme_diagnostic(const ThemeCompileDiagnostic& diagnostic) {
    std::string line{};
    if (!diagnostic.file.empty()) {
        line += diagnostic.file;
        if (diagnostic.line != 0U) {
            line += ':' + std::to_string(diagnostic.line) + ':' + std::to_string(diagnostic.column);
        }
        line += ": ";
    }
    line += diagnostic.severity == ThemeDiagnosticSeverity::error ? "error " : "warning ";
    line += diagnostic.code;
    if (!diagnostic.name.empty()) line += ' ' + diagnostic.name;
    if (!diagnostic.detail.empty()) line += ": " + diagnostic.detail;
    if (!diagnostic.rule.empty()) line += " (in " + diagnostic.rule;
    if (!diagnostic.rule.empty() && !diagnostic.property.empty()) line += ", " + diagnostic.property;
    if (!diagnostic.rule.empty()) line += ')';
    return line;
}

ThemeCompileResult compile_theme_packages(std::span<const ThemePackage> packages, const ThemeLibrary& library,
                                          ImageRegistry& images, const ThemeCompileOptions& options) {
    if (packages.size() > maximum_theme_packages) {
        return refusal(problem_of(DiagnosticCode::manifest_invalid, std::string(theme_language::theme_manifest_file),
                                  "more than " + std::to_string(maximum_theme_packages) + " packages"));
    }
    // Every package verified whole before any is compiled; `roots` borrows
    // from `verified`, which is sized once and never grows after.
    std::vector<VerifiedThemePack> verified(packages.size());
    std::vector<const VerifiedThemePack*> roots{};
    roots.reserve(packages.size());
    theme_language::ThemePackageBytes bytes{};
    ThemeDiagnostic problem{};
    for (std::size_t position = 0U; position < packages.size(); ++position) {
        const ThemePackage& package = packages[position];
        bytes.manifest = package.manifest;
        bytes.files.clear();
        for (const ThemePackageFile& file : package.files) {
            bytes.files.push_back(theme_language::ThemePackageEntry{file.path, file.bytes});
        }
        if (theme_language::verify_theme_package(bytes, current_engine_version, verified[position], problem) !=
            theme_language::ThemePackageOutcome::verified) {
            return refusal(problem);
        }
        roots.push_back(&verified[position]);
    }
    const theme_language::InstalledThemeIndex empty{};
    const theme_language::InstalledThemeIndex& index =
        library.size() == 0U ? empty : ThemeLibraryAccess::index(library);
    ThemeImageShares unshared{};
    ThemeImageShares* const shares = ThemeLibraryAccess::images(library);
    const std::string_view id = roots.empty() ? std::string_view{} : std::string_view((*roots.back()).manifest.name);
    ThemeCompileResult result = compile_closure(roots, index, images, shares == nullptr ? unshared : *shares, options,
                                                ThemeRuleSet::program, id, std::span<const UserIcon>{});
    return result;
}

std::shared_ptr<const Theme> default_compiled_theme() {
    // Built once, on the first call's thread (C++ makes the initialization
    // thread-safe), and never recompiled.
    static const std::shared_ptr<const Theme> theme =
        default_theme_base == ThemeBase::gui_forms ? gui_forms_theme() : default_theme();
    return theme;
}

namespace {

// `text` as the inside of a JSON string: a quote, a backslash and the
// control characters escaped; every other byte as it is (UTF-8 passes
// through).
std::string json_string_body(std::string_view text) {
    static constexpr char hex_digits[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            escaped.push_back('\\');
            escaped.push_back(character);
        } else if (byte < 0x20U) {
            escaped.append("\\u00");
            escaped.push_back(hex_digits[byte >> 4U]);
            escaped.push_back(hex_digits[byte & 0x0FU]);
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

} // namespace

ThemePackage theme_sheet_package(std::string_view sheet, std::string_view name) {
    std::array<char, sha256_hex_digits> digest{};
    format_sha256_hex(sha256_of_text(sheet), digest);
    const std::string_view digest_text(digest.data(), digest.size());
    ThemePackage package{};
    package.manifest.append("{\"format\": \"gui-forms-theme/1\", \"name\": \"");
    package.manifest.append(json_string_body(name));
    package.manifest.append("\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", "
                            "\"sheet\": {\"path\": \"sheet.theme.css\", \"sha256\": \"");
    package.manifest.append(digest_text);
    package.manifest.append("\"}}");
    ThemePackageFile file{};
    file.path = "sheet.theme.css";
    file.bytes = std::string(sheet);
    package.files.push_back(std::move(file));
    return package;
}

ThemeCompileResult compile_installed_theme(std::string_view name, const ThemeLibrary& library, ImageRegistry& images,
                                           const ThemeCompileOptions& options) {
    const VerifiedThemePack* const root =
        library.size() == 0U ? nullptr : ThemeLibraryAccess::index(library).find(name);
    if (root == nullptr) {
        return refusal(problem_of(DiagnosticCode::import_not_installed, std::string{},
                                  theme_language::clipped_theme_detail(name)));
    }
    const VerifiedThemePack* const roots[] = {root};
    return compile_closure(roots, ThemeLibraryAccess::index(library), images, *ThemeLibraryAccess::images(library),
                           options, ThemeRuleSet::program, (*root).manifest.name, std::span<const UserIcon>{});
}

ThemeCompileResult compile_user_theme(const UserThemeFiles& files, const ThemeLibrary& program, ImageRegistry& images,
                                      const ThemeCompileOptions& options, ThemeRuleSet set, UserThemeHeader* header) {
    const std::string main_name = files.name + ".css";
    // The folder, checked whole before anything is read.
    if (!theme_language::package_name_valid(files.name)) {
        return refusal(problem_of(DiagnosticCode::manifest_invalid, main_name,
                                  "name: " + theme_language::clipped_theme_detail(files.name)));
    }
    if (files.files.size() > maximum_user_theme_files) {
        return refusal(problem_of(DiagnosticCode::manifest_invalid, main_name,
                                  "the folder holds more than " + std::to_string(maximum_user_theme_files) + " files"));
    }
    std::size_t total = 0U;
    std::vector<UserSheet> subs{};
    std::vector<const ThemePackageFile*> resources{};
    std::vector<const ThemePackageFile*> icon_files{};
    for (std::size_t index = 0U; index < files.files.size(); ++index) {
        const ThemePackageFile& file = files.files[index];
        const std::string where = files.name + "/" + file.path;
        if (!theme_language::package_path_valid(file.path)) {
            return refusal(problem_of(DiagnosticCode::manifest_invalid, where, "a path the package rules refuse"));
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (files.files[earlier].path == file.path) {
                return refusal(problem_of(DiagnosticCode::manifest_invalid, where, "listed twice"));
            }
        }
        total = total + file.bytes.size();
        if (total > maximum_user_theme_bytes) {
            return refusal(problem_of(DiagnosticCode::manifest_invalid, main_name,
                                      "the folder holds more than 16 MiB"));
        }
        const UserThemeFileKind kind = user_file_kind(file.path);
        if (kind == UserThemeFileKind::other) {
            return refusal(problem_of(DiagnosticCode::manifest_invalid, where, "not a sheet, an image or an icon"));
        }
        if (kind == UserThemeFileKind::resource && !theme_language::package_resource_name_valid(file.path)) {
            return refusal(problem_of(DiagnosticCode::manifest_invalid, where, "a resource name the rules refuse"));
        }
        if (kind == UserThemeFileKind::resource) resources.push_back(&file);
        if (kind == UserThemeFileKind::icon) icon_files.push_back(&file);
        if (kind == UserThemeFileKind::sub_sheet) {
            subs.push_back(UserSheet{.name = where,
                                     .path = file.path,
                                     .text = &file.bytes,
                                     .identity = theme_language::package_sha256(file.bytes)});
        }
    }
    // Sub-sheets in filename order: the byte order of their names.
    std::sort(subs.begin(), subs.end(), UserSheetNameOrder{});
    const theme_language::InstalledThemeIndex empty{};
    const theme_language::InstalledThemeIndex& installed =
        program.size() == 0U ? empty : ThemeLibraryAccess::index(program);
    // Each sheet a pack; the main file's header read.
    std::vector<ThemeCompileDiagnostic> warnings{};
    std::vector<VerifiedThemePack> packs(subs.size());
    ThemeDiagnostic problem{};
    for (std::size_t index = 0U; index < subs.size(); ++index) {
        if (!read_user_sheet(subs[index], files.name, subs, installed, packs[index], nullptr, warnings, problem)) {
            return refusal(problem);
        }
    }
    const UserSheet main_sheet{.name = main_name,
                               .path = {},
                               .text = &files.main_sheet,
                               .identity = theme_language::package_sha256(files.main_sheet)};
    VerifiedThemePack main{};
    theme_language::ThemeHeader read{};
    if (!read_user_sheet(main_sheet, files.name, subs, installed, main, &read, warnings, problem)) {
        return refusal(problem);
    }
    if (header != nullptr) {
        (*header).present = read.present;
        (*header).version = read.version;
        (*header).author = read.author;
        (*header).provenance = read.provenance;
    }
    if (!read.present && !files.header_implied) {
        return refusal(problem_of(DiagnosticCode::manifest_invalid, main_name, "no @theme header: not a theme"));
    }
    if (read.engine && current_engine_version < *read.engine) {
        return refusal(problem_of(DiagnosticCode::engine_too_old, main_name,
                                  theme_language::engine_too_old_detail(*read.engine)));
    }
    main.manifest.engine = read.engine.value_or(EngineVersion{});
    main.manifest.version = read.version;
    main.manifest.author = read.author;
    main.manifest.provenance = read.provenance;
    for (const ThemePackageFile* const file : resources) {
        theme_language::ThemeManifestResource resource{};
        resource.name = (*file).path;
        resource.file.path = (*file).path;
        main.manifest.resources.push_back(resource);
        main.resources.push_back((*file).bytes);
    }
    // The sub-sheets no sheet imports load first, in filename order, as if
    // the main file imported them before its own imports.
    std::vector<std::uint8_t> implied(subs.size(), 0U);
    for (std::size_t position = 0U; position < subs.size(); ++position) {
        implied[position] = imported_by(packs, main, subs[position].name) ? 0U : 1U;
    }
    // The program's packages and the sub-sheets, one index for the closure.
    theme_language::InstalledThemeIndex index = installed;
    for (VerifiedThemePack& pack : packs) {
        if (!index.install(std::move(pack))) {
            return refusal(problem_of(DiagnosticCode::too_many_imports, main_name,
                                      std::to_string(theme_language::maximum_imports)));
        }
    }
    // A sub-sheet only sheets that never load import (sub-sheets importing
    // each other alone) is implied too, so that its cycle is found (E605).
    const std::vector<theme_language::ThemeSheetImport> written = main.imports;
    std::vector<const VerifiedThemePack*> reached{};
    for (std::size_t round = 0U; round < 2U; ++round) {
        main.imports.clear();
        main.manifest.imports.clear();
        for (std::size_t position = 0U; position < subs.size(); ++position) {
            if (implied[position] == 0U) continue;
            theme_language::ThemeSheetImport imported{};
            imported.name = subs[position].name;
            main.imports.push_back(imported);
            main.manifest.imports.push_back(
                theme_language::ThemeManifestImport{subs[position].name, subs[position].identity});
        }
        main.imports.insert(main.imports.end(), written.begin(), written.end());
        for (const theme_language::ThemeSheetImport& imported : written) {
            const VerifiedThemePack* const target = index.find(imported.name);
            if (target != nullptr) {
                main.manifest.imports.push_back(theme_language::ThemeManifestImport{imported.name, (*target).identity});
            }
        }
        if (round == 1U) break;
        ThemeDiagnostic unused{};
        if (theme_language::resolve_theme_imports(main, index, reached, unused) !=
            theme_language::ThemeImportOutcome::resolved) {
            break;
        }
        bool all = true;
        for (std::size_t position = 0U; position < subs.size(); ++position) {
            const VerifiedThemePack* const sub = index.find(subs[position].name);
            if (std::find(reached.begin(), reached.end(), sub) != reached.end()) continue;
            implied[position] = 1U;
            all = false;
        }
        if (all) break;
    }
    // The icon overrides, each naming an icon the program lets it replace;
    // any other is a warning and is ignored.
    std::vector<UserIcon> icons{};
    for (const ThemePackageFile* const file : icon_files) {
        const std::string where = files.name + "/" + (*file).path;
        constexpr std::string_view icons_folder = "icons/";
        constexpr std::string_view png = ".png";
        const std::string_view path = (*file).path;
        const std::string_view identifier =
            path.substr(icons_folder.size(), path.size() - icons_folder.size() - png.size());
        const IconDeclaration* declared = nullptr;
        for (const IconDeclaration& declaration : options.icons) {
            if (declaration.identifier == identifier) declared = &declaration;
        }
        if (declared == nullptr) {
            warnings.push_back(public_diagnostic(
                problem_of(DiagnosticCode::icon_unknown, where, theme_language::clipped_theme_detail(identifier))));
            continue;
        }
        if ((*declared).policy == IconOverride::fixed) {
            warnings.push_back(public_diagnostic(
                problem_of(DiagnosticCode::icon_not_overridable, where, std::string(identifier))));
            continue;
        }
        if ((*file).bytes.size() > maximum_icon_bytes) {
            warnings.push_back(public_diagnostic(problem_of(DiagnosticCode::icon_refused, where, "larger than 1 MiB")));
            continue;
        }
        icons.push_back(UserIcon{.identifier = std::string(identifier), .file = where, .bytes = &(*file).bytes});
    }
    ThemeImageShares unshared{};
    ThemeImageShares* const shares = ThemeLibraryAccess::images(program);
    const VerifiedThemePack* const roots[] = {&main};
    ThemeCompileResult compiled = compile_closure(roots, index, images, shares == nullptr ? unshared : *shares, options,
                                                  set, files.name, icons);
    if (!compiled.succeeded()) return compiled;
    if (warnings.empty()) return compiled;
    for (const ThemeCompileDiagnostic& later : compiled.diagnostics()) warnings.push_back(later);
    return ThemeCompileResult::compiled(compiled.theme(), std::move(warnings));
}

namespace {

// `text` as the body of a CSS string a header descriptor admits: a quote,
// a backslash or a control character (none of which a string may hold
// unescaped, and the language admits no escapes, E105) becomes a space.
std::string header_string(std::string_view text) {
    std::string made{};
    made.reserve(text.size());
    for (const char character : text) {
        const bool control = static_cast<unsigned char>(character) < 0x20U || character == '\x7f';
        const bool replaced = control || character == '"' || character == '\\';
        made.push_back(replaced ? ' ' : character);
    }
    return made;
}

struct PackNameOrder final {
    bool operator()(const ThemePackageExport& left, const ThemePackageExport& right) const noexcept {
        return left.files.name < right.files.name;
    }
};

} // namespace

std::vector<ThemePackageExport> export_theme_packages(const ThemeLibrary& library) {
    std::vector<ThemePackageExport> exports{};
    if (library.size() == 0U) return exports;
    const std::span<const VerifiedThemePack> packs = ThemeLibraryAccess::index(library).packs();
    exports.reserve(packs.size());
    for (const VerifiedThemePack& pack : packs) {
        ThemePackageExport made{};
        made.files.name = pack.manifest.name;
        std::string header = "@theme { version: \"" + header_string(pack.manifest.version) + "\"; author: \"" +
                             header_string(pack.manifest.author) + "\"; provenance: \"" +
                             header_string(pack.manifest.provenance) + "\"";
        const EngineVersion engine = pack.manifest.engine;
        if (!(engine == EngineVersion{})) {
            header += "; engine: \">=" + std::to_string(engine.major) + "." + std::to_string(engine.minor) + "\"";
        }
        header += "; }\n";
        made.files.main_sheet = header + pack.sheet;
        for (std::size_t position = 0U; position < pack.manifest.resources.size(); ++position) {
            const std::string& name = pack.manifest.resources[position].name;
            if (!std::string_view(name).ends_with(".png")) {
                made.omitted.push_back(name);
                continue;
            }
            made.files.files.push_back(ThemePackageFile{name, pack.resources[position]});
        }
        exports.push_back(std::move(made));
    }
    std::sort(exports.begin(), exports.end(), PackNameOrder{});
    return exports;
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
// segment's own rules, at the inline levels, are the style's and are taken
// apart (style_rule).
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

bool style_rule(const ThemeRule& rule) noexcept {
    return rule.level == CascadeLevel::inline_style || rule.level == CascadeLevel::important_inline_style;
}

// Whether `rule`, lowered as above, is one of the theme's definitions of a
// name in `read` (sorted), the names the style's own rules read, whose
// selector can match the control (P12b): the style holds them, so that
// where none of its own definitions holds for a part, the theme's does.
bool theme_definition_taken(const ThemeRule& rule, std::span<const std::string> read, std::string_view control_type,
                            std::string_view stable_id) noexcept {
    if (style_rule(rule) || rule.source_position == 0U) return false;
    if (!rule.selector.control_type.empty() && rule.selector.control_type != control_type) return false;
    if (!rule.selector.stable_id.empty() && rule.selector.stable_id != stable_id) return false;
    for (const ScopedDefinition& definition : rule.scoped_definitions) {
        if (std::binary_search(read.begin(), read.end(), definition.name)) return true;
    }
    return false;
}

// Places a diagnostic of a theme compiled again for an inline style (P12b)
// in the text it came from: at or after `from` in the style `text`
// (through `pieces`, named `file`), else in the theme's own sheet.
void place_recompiled(ThemeDiagnostic& diagnostic, std::size_t from, std::string_view text, std::string_view file,
                      std::span<const theme_language::InlineSheetPiece> pieces,
                      const detail::ThemeSourceRecord& record) {
    const std::size_t offset = diagnostic.position.offset.value;
    if (offset >= from) {
        // The last piece starting at or before the offset; a written byte
        // between pieces (the `*`, braces, `;`) is placed at its piece's
        // end.
        std::size_t within = 0U;
        for (const theme_language::InlineSheetPiece& piece : pieces) {
            if (piece.written > offset) break;
            within = piece.style + std::min(offset - piece.written, piece.length);
        }
        within = std::min(within, text.size());
        theme_language::LineStartTable lines{};
        lines.reset(text);
        diagnostic.file = std::string(file);
        diagnostic.position = lines.position(text, theme_language::ByteOffset{static_cast<std::uint32_t>(within)});
        return;
    }
    if (record.sheets.empty()) return;
    std::size_t found = 0U;
    for (std::size_t index = 0U; index < record.sheets.size(); ++index) {
        if (record.sheets[index].start <= offset) found = index;
    }
    const detail::ThemeSourceSheet& sheet = record.sheets[found];
    const std::string_view own = std::string_view(record.text).substr(sheet.start, sheet.length);
    const std::size_t within = std::min(offset - sheet.start, own.size());
    theme_language::LineStartTable lines{};
    lines.reset(own);
    diagnostic.file = sheet.file;
    diagnostic.position = lines.position(own, theme_language::ByteOffset{static_cast<std::uint32_t>(within)});
}

// Whether `diagnostic` repeats one of `earlier`: the style's own
// compilation reported it already.
bool reported(const ThemeCompileDiagnostic& diagnostic, std::span<const ThemeCompileDiagnostic> earlier) noexcept {
    for (const ThemeCompileDiagnostic& one : earlier) {
        if (one.code == diagnostic.code && one.file == diagnostic.file && one.line == diagnostic.line &&
            one.column == diagnostic.column) {
            return true;
        }
    }
    return false;
}

// What compiling a theme again for an inline style takes (P12b): the style
// `text` (compiled by `compiler`, defining the names `defined`, sorted),
// whether its own declarations are lowered there too (`declarations`: it
// reads a name the theme defines), and where the style's own diagnostics
// begin in the output list (`own_first`).
struct ThemeRecompile final {
    std::string_view text;
    const theme_language::ThemeCompiler& compiler;
    std::span<const std::string> defined;
    bool declarations{};
    std::size_t own_first{};
};

// Compiles the theme of `record` again with the inline style written after
// it as sheet rules, and puts in `sheet` the theme's rules the style's
// definitions vary (theme_reader_taken), after the style's own rules: those
// already in `sheet`, or, with `declarations`, the style's rules as lowered
// there, which replace them. The theme's own diagnostics are its warnings
// again and are not repeated, nor are the style's own; every other
// diagnostic is appended to `diagnostics`, placed in the theme's sheet or
// the style. False, with `sheet` unchanged, where an error refuses the
// style.
bool append_theme_readers(const ThemeRecompile& recompile, const ControlAnatomy& anatomy, std::string_view stable_id,
                          std::string_view file, const detail::ThemeSourceRecord& record, ThemeRuleSheet& sheet,
                          std::vector<ThemeCompileDiagnostic>& diagnostics) {
    std::string combined = record.text;
    combined += "\n";
    const std::size_t from = combined.size();
    std::vector<theme_language::InlineSheetPiece> pieces{};
    recompile.compiler.append_inline_as_sheet(recompile.text, combined, pieces);
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
                                                  .inline_definitions_from = static_cast<std::uint32_t>(from),
                                                  .inline_declarations = recompile.declarations};
    ThemeRuleSheet lowered{};
    theme_language::ThemeCompiler again{};
    theme_language::ThemeDiagnosticList found{};
    const theme_language::CompileOutcome outcome = again.compile(input, lowered, found);
    const std::size_t own_end = diagnostics.size();
    for (const ThemeDiagnostic& diagnostic : found.records()) {
        const bool in_style = diagnostic.position.offset.value >= from;
        const bool error = theme_language::diagnostic_severity(diagnostic.code) == theme_language::DiagnosticSeverity::error;
        if (!in_style && !error) continue;
        ThemeDiagnostic placed = diagnostic;
        place_recompiled(placed, from, recompile.text, file, pieces, record);
        const ThemeCompileDiagnostic published = public_diagnostic(placed);
        const std::span<const ThemeCompileDiagnostic> own =
            std::span<const ThemeCompileDiagnostic>(diagnostics).subspan(recompile.own_first, own_end - recompile.own_first);
        if (in_style && reported(published, own)) continue;
        diagnostics.push_back(published);
    }
    if (outcome != theme_language::CompileOutcome::compiled) return false;
    if (recompile.declarations) {
        // The style's rules as lowered here replace its own compilation's;
        // the theme's definitions of the scoped names they read come first,
        // below them.
        std::vector<std::string> read{};
        for (const ThemeRule& rule : lowered.rules) {
            if (!style_rule(rule)) continue;
            for (const ScopedCondition& condition : rule.scoped_conditions) read.push_back(condition.name);
        }
        std::sort(read.begin(), read.end());
        sheet.rules.clear();
        for (ThemeRule& rule : lowered.rules) {
            if (theme_definition_taken(rule, read, anatomy.control_id, stable_id)) sheet.rules.push_back(rule);
        }
        for (ThemeRule& rule : lowered.rules) {
            if (style_rule(rule)) sheet.rules.push_back(std::move(rule));
        }
    }
    for (ThemeRule& rule : lowered.rules) {
        if (theme_reader_taken(rule, recompile.defined, anatomy.control_id, stable_id)) {
            sheet.rules.push_back(std::move(rule));
        }
    }
    return true;
}

} // namespace

namespace {

// ` (<code> <name> in <file>:<line>:<column>)` for the first error of
// `diagnostics`; empty without one.
std::string first_error_text(std::span<const ThemeCompileDiagnostic> diagnostics) {
    std::string text{};
    for (const ThemeCompileDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != ThemeDiagnosticSeverity::error) continue;
        text = " (" + diagnostic.code + " " + diagnostic.name + " in " + diagnostic.file + ":" +
               std::to_string(diagnostic.line) + ":" + std::to_string(diagnostic.column) + ")";
        break;
    }
    return text;
}

} // namespace

std::string detail::inline_restyle_refusal(std::string_view change,
                                           std::span<const ThemeCompileDiagnostic> diagnostics) {
    std::string message = std::string(change);
    message += " refused: an inline style using custom properties does not compile against it";
    message += first_error_text(diagnostics);
    return message;
}

std::string detail::inline_restyle_kept(std::string_view file, std::span<const ThemeCompileDiagnostic> diagnostics) {
    std::string message = std::string(file);
    message += ": inline style kept as it was on attachment: it does not compile against the theme in effect";
    message += first_error_text(diagnostics);
    message += "; the theme's definitions apply to the theme's rules";
    return message;
}

std::shared_ptr<const detail::ControlInlineStyle> detail::compile_inline_style(
    std::string_view text, const ControlAnatomy& anatomy, std::string_view stable_id, std::string_view file,
    const Theme& theme, std::vector<ThemeCompileDiagnostic>& diagnostics) {
    const theme_language::ThemeSource source{.file = file, .text = text};
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const detail::ThemeSourceRecord& record = ThemeAccess::source(theme);
    const theme_language::ThemeCompileInput input{
        .source = source,
        .registry = registry,
        .vocabulary = theme_language::SelectorVocabulary{std::span<const ControlAnatomy>(&anatomy, 1U)},
        .published_values = {},
        .resources = {},
        .images = {},
        .requires_engine = current_engine_version,
        .form = theme_language::ThemeSheetForm::inline_style,
        .theme_names = record.defined_names};
    ThemeRuleSheet sheet{};
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList found{};
    const std::size_t own_first = diagnostics.size();
    const theme_language::CompileOutcome outcome = compiler.compile(input, sheet, found);
    for (const ThemeDiagnostic& diagnostic : found.records()) diagnostics.push_back(public_diagnostic(diagnostic));
    if (outcome != theme_language::CompileOutcome::compiled) return nullptr;
    // A style defining custom properties, or reading one the theme in
    // effect defines, is compiled against that theme (P12b): the theme's
    // rules reading the style's names are lowered again with its
    // definitions, and where it reads the theme's names its own rules are
    // lowered there too. Every theme keeps a source; a theme made from a
    // definition alone (the default) keeps the empty sheet's.
    std::vector<std::string> defined{};
    compiler.append_defined_names(defined);
    std::sort(defined.begin(), defined.end());
    const bool reads_theme = compiler.reads_theme_names();
    const bool bound = !defined.empty() || reads_theme;
    if (bound) {
        const ThemeRecompile recompile{.text = text,
                                       .compiler = compiler,
                                       .defined = defined,
                                       .declarations = reads_theme,
                                       .own_first = own_first};
        if (!append_theme_readers(recompile, anatomy, stable_id, file, record, sheet, diagnostics)) return nullptr;
    }
    const CompiledThemeRules* const against = bound ? &theme.engine().rules() : nullptr;
    std::shared_ptr<ControlInlineStyle> style = std::make_shared<ControlInlineStyle>();
    (*style).text = std::string(text);
    (*style).theme_bound = bound;
    (*style).rules = CompiledThemeRules::compile_inline(sheet, registry, against);
    std::shared_ptr<const ControlInlineStyle> result(std::move(style));
    return result;
}

detail::BuiltInThemeCompilation detail::compile_built_in_theme(const ThemeDefinition& floor, std::string_view file,
                                                               std::string_view sheet,
                                                               std::span<const ControlAnatomy> controls) {
    BuiltInThemeCompilation result{};
    const theme_language::ThemeSource source{.file = file, .text = sheet};
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = registry,
                                                  .vocabulary = theme_language::SelectorVocabulary{controls},
                                                  .colour_authority = true};
    // The definition is some 100 KB of recipes held out of line; it is
    // copied once, onto the heap, as compile_closure's is.
    std::unique_ptr<ThemeDefinition> definition(new ThemeDefinition(floor));
    (*definition).rule_set = ThemeRuleSet::program;
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, (*definition).rules, diagnostics);
    result.operations = compiler.work().operations();
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) {
        result.diagnostics.push_back(public_diagnostic(diagnostic));
    }
    if (outcome != theme_language::CompileOutcome::compiled) return result;
    std::shared_ptr<ThemeSourceRecord> record = std::make_shared<ThemeSourceRecord>();
    (*record).text = std::string(sheet);
    (*record).file = std::string(file);
    compiler.append_defined_names((*record).defined_names);
    std::sort((*record).defined_names.begin(), (*record).defined_names.end());
    (*record).controls.assign(controls.begin(), controls.end());
    (*record).requires_engine = current_engine_version;
    std::shared_ptr<const ThemeSourceRecord> kept(std::move(record));
    result.theme = ThemeAccess::create_holding(*definition, {}, std::move(kept));
    return result;
}

detail::FloorSheetEvaluation detail::evaluate_floor_sheet(std::string_view file, std::string_view sheet,
                                                          const ControlVisualRecipe& structure,
                                                          ThemeDefinition& definition) {
    FloorSheetEvaluation result{};
    BuiltInThemeCompilation compiled = compile_built_in_theme(definition, file, sheet, stock_control_anatomies());
    result.diagnostics = std::move(compiled.diagnostics);
    result.operations = compiled.operations;
    if (compiled.theme == nullptr) return result;
    const CompiledThemeRules& rules = (*compiled.theme).engine().rules();
    for (std::size_t index = 0U; index < control_visual_role_count; ++index) {
        const ControlVisualRole role = static_cast<ControlVisualRole>(index);
        fill_family_recipes(rules, role, structure, definition.roles[index]);
    }
    result.evaluated = true;
    return result;
}

} // namespace gui_forms
