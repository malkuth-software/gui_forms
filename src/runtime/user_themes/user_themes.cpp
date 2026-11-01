#include "gui_forms/application/user_themes/user_themes.hpp"

#include "../../core/theme_language/package/theme_manifest.hpp"
#include "theme_files.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

namespace files = detail::theme_files;

// A folder's top level is listed whole before its theme files are chosen;
// a folder with more entries than this is not read.
constexpr std::size_t maximum_folder_entries = 4096U;
// A theme's main file is at most a closure's bytes (1 MiB).
constexpr std::size_t maximum_main_sheet_bytes = std::size_t{1024} * 1024U;
// A theme folder's listing counts its folders as well as its files.
constexpr std::size_t maximum_theme_folder_entries = 2U * maximum_user_theme_files;

constexpr std::string_view sheet_suffix = ".css";

// A theme file found in one of the folders.
struct FoundTheme final {
    std::string name{};
    std::string folder{};
    UserThemeOrigin origin{UserThemeOrigin::beside_program};
};

struct FoundNameOrder final {
    bool operator()(const FoundTheme& left, const FoundTheme& right) const noexcept { return left.name < right.name; }
};

std::string outcome_text(files::FileOutcome outcome) {
    switch (outcome) {
    case files::FileOutcome::done: return "read";
    case files::FileOutcome::missing: return "missing";
    case files::FileOutcome::over_limit: return "over its limit";
    case files::FileOutcome::exists: return "already there";
    case files::FileOutcome::failed: return "could not be read";
    }
    return "could not be read";
}

// The theme files of `folder`'s top level into `found`, one per name, a
// later folder's replacing an earlier one's.
void find_themes(std::string_view folder, UserThemeOrigin origin, std::vector<FoundTheme>& found,
                 std::vector<std::string>& log) {
    if (folder.empty()) return;
    std::vector<files::FolderEntry> entries{};
    const files::FileOutcome listed = files::list_folder(folder, false, maximum_folder_entries, entries);
    // A folder that is not there holds no themes; the user may never make one.
    if (listed == files::FileOutcome::missing) return;
    if (listed != files::FileOutcome::done) {
        log.push_back(std::string(folder) + ": not read, " + outcome_text(listed));
        return;
    }
    std::size_t taken = 0U;
    for (const files::FolderEntry& entry : entries) {
        const std::string_view path = entry.path;
        if (!path.ends_with(sheet_suffix)) continue;
        const std::string where = files::join_path(folder, path);
        if (entry.kind != files::EntryKind::file) {
            log.push_back(where + ": skipped, not a regular file");
            continue;
        }
        const std::string name(path.substr(0U, path.size() - sheet_suffix.size()));
        if (!theme_language::package_name_valid(name)) {
            log.push_back(where + ": skipped, a name of lowercase letters, digits and dashes is a theme's");
            continue;
        }
        if (name == built_in_theme_name) {
            log.push_back(where + ": skipped, `built-in` names no user theme");
            continue;
        }
        if (taken == maximum_theme_folder_files) {
            log.push_back(where + ": skipped, a folder holds at most 64 themes");
            continue;
        }
        taken = taken + 1U;
        bool replaced = false;
        for (FoundTheme& earlier : found) {
            if (earlier.name != name) continue;
            log.push_back(files::join_path(earlier.folder, path) + ": replaced by " + where);
            earlier.folder = std::string(folder);
            earlier.origin = origin;
            replaced = true;
        }
        if (!replaced) found.push_back(FoundTheme{.name = name, .folder = std::string(folder), .origin = origin});
    }
}

// Reads `theme`'s main file and folder into `read`; false, with a log line,
// where a limit or the file system stops it.
bool read_theme(const FoundTheme& theme, UserThemeFiles& read, std::vector<std::string>& log) {
    read.name = theme.name;
    read.files.clear();
    const std::string main_file = files::join_path(theme.folder, theme.name + std::string(sheet_suffix));
    const files::FileOutcome main = files::read_file(main_file, maximum_main_sheet_bytes, read.main_sheet);
    if (main != files::FileOutcome::done) {
        log.push_back(main_file + ": skipped, " + outcome_text(main));
        return false;
    }
    const std::string folder = files::join_path(theme.folder, theme.name);
    std::vector<files::FolderEntry> entries{};
    const files::FileOutcome listed = files::list_folder(folder, true, maximum_theme_folder_entries, entries);
    if (listed == files::FileOutcome::missing) return true;
    if (listed != files::FileOutcome::done) {
        log.push_back(folder + ": skipped, " + outcome_text(listed));
        return false;
    }
    // The folder's sizes are checked whole before a file is read.
    std::uint64_t total = 0U;
    std::size_t file_count = 0U;
    for (const files::FolderEntry& entry : entries) {
        if (entry.kind == files::EntryKind::folder) continue;
        if (entry.kind != files::EntryKind::file) {
            log.push_back(files::join_path(folder, entry.path) + ": skipped the theme, not a regular file");
            return false;
        }
        file_count = file_count + 1U;
        total = total + entry.bytes;
    }
    if (file_count > maximum_user_theme_files || total > maximum_user_theme_bytes) {
        log.push_back(folder + ": skipped, a theme folder holds at most 256 files and 16 MiB");
        return false;
    }
    read.files.reserve(file_count);
    for (const files::FolderEntry& entry : entries) {
        if (entry.kind != files::EntryKind::file) continue;
        ThemePackageFile file{};
        file.path = entry.path;
        const std::string where = files::join_path(folder, entry.path);
        const files::FileOutcome outcome = files::read_file(where, maximum_user_theme_bytes, file.bytes);
        if (outcome != files::FileOutcome::done) {
            log.push_back(where + ": skipped the theme, " + outcome_text(outcome));
            return false;
        }
        read.files.push_back(std::move(file));
    }
    return true;
}

// Whether `result` refuses a main file only for having no theme header,
// which makes the file no theme rather than a refused one.
bool not_a_theme(const ThemeCompileResult& result, const UserThemeHeader& header) noexcept {
    if (result.succeeded() || header.present) return false;
    const std::span<const ThemeCompileDiagnostic> diagnostics = result.diagnostics();
    const bool header_missing = diagnostics.size() == 1U && diagnostics[0].code == "E601" && diagnostics[0].line == 0U;
    return header_missing;
}

void write_one(const std::string& path, std::string_view bytes, ThemeExportReport& report) {
    const files::FileOutcome outcome = files::write_new_file(path, bytes);
    if (outcome == files::FileOutcome::done) report.written.push_back(path);
    if (outcome == files::FileOutcome::exists) report.skipped.push_back(path);
    if (outcome != files::FileOutcome::done && outcome != files::FileOutcome::exists) report.failed.push_back(path);
}

constexpr std::string_view export_readme =
    "Themes for this program\n"
    "\n"
    "Each <name>.css here is a theme, with its images, icons and further\n"
    "sheets in the folder <name>/. These were exported from the program's own\n"
    "themes as a starting point: they change nothing until you edit them.\n"
    "Choose a theme in the program's settings, or start the program with\n"
    "--theme <name>. A theme is read when the program starts.\n"
    "\n"
    "Where the program keeps its own theme, it decides first: a declaration\n"
    "of yours applies where the program declares nothing, and one marked\n"
    "!important applies over the program's ordinary declarations. The\n"
    "program's own !important declarations stay the program's.\n"
    "\n"
    "An icon is replaced by <name>/icons/<identifier>.png, a PNG of at most\n"
    "1024 by 1024 pixels and 1 MiB that is not fully transparent.\n"
    "\n"
    "To go back to the program's built-in look, choose \"Built-in\" in the\n"
    "settings, or start the program with --theme built-in.\n";

} // namespace

std::string per_user_theme_folder(std::string_view program) {
    const std::string configuration = files::program_configuration_folder(program);
    if (configuration.empty()) return configuration;
    std::string folder = files::join_path(configuration, "themes");
    return folder;
}

UserThemeScan scan_user_themes(const UserThemeFolders& folders, ApplicationResources& resources,
                               const ThemeLibrary& program, const ThemeCompileOptions& options) {
    UserThemeScan scan{};
    if (resources.user_theme_policy() == UserThemePolicy::refused) return scan;
    std::vector<FoundTheme> found{};
    find_themes(folders.beside_program, UserThemeOrigin::beside_program, found, scan.log);
    find_themes(folders.per_user, UserThemeOrigin::per_user, found, scan.log);
    std::sort(found.begin(), found.end(), FoundNameOrder{});
    // One set of files, reused across the themes.
    UserThemeFiles read{};
    for (const FoundTheme& theme : found) {
        if (!read_theme(theme, read, scan.log)) continue;
        UserThemeHeader header{};
        ThemeCompileResult result = resources.compile_user_theme(read, program, options, &header);
        const std::string main_file = files::join_path(theme.folder, theme.name + std::string(sheet_suffix));
        if (not_a_theme(result, header)) {
            scan.log.push_back(main_file + ": not a theme, it does not open with an @theme header");
            continue;
        }
        UserThemeCandidate candidate{};
        candidate.name = theme.name;
        candidate.origin = theme.origin;
        candidate.file = main_file;
        candidate.header = std::move(header);
        const std::span<const ThemeCompileDiagnostic> diagnostics = result.diagnostics();
        candidate.diagnostics.assign(diagnostics.begin(), diagnostics.end());
        candidate.theme = result.theme();
        scan.themes.push_back(std::move(candidate));
    }
    return scan;
}

ThemeCommandLine read_theme_command_line(std::span<const std::string_view> arguments) {
    constexpr std::string_view theme_option = "--theme";
    constexpr std::string_view export_option = "--export-themes";
    ThemeCommandLine line{};
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1U < arguments.size() && !arguments[index + 1U].starts_with("--");
        if (argument == export_option) {
            line.export_themes = true;
            if (has_value) {
                index = index + 1U;
                line.export_folder = std::string(arguments[index]);
            }
            continue;
        }
        if (argument != theme_option) continue;
        if (!has_value || line.theme.kind != ThemeChoiceKind::unspecified) {
            ThemeCommandLine refused{};
            refused.error = has_value ? "--theme is given twice" : "--theme names a theme, or built-in";
            return refused;
        }
        index = index + 1U;
        const std::string_view value = arguments[index];
        if (value == built_in_theme_name) {
            line.theme.kind = ThemeChoiceKind::built_in;
            continue;
        }
        line.theme.kind = ThemeChoiceKind::named;
        line.theme.name = std::string(value);
    }
    return line;
}

ThemeSelection select_user_theme(ApplicationResources& resources, const UserThemeScan& scan,
                                 const ThemeChoice& choice) {
    if (choice.kind == ThemeChoiceKind::unspecified) return ThemeSelection::unchanged;
    if (choice.kind == ThemeChoiceKind::built_in) {
        resources.set_user_theme(nullptr);
        return ThemeSelection::built_in;
    }
    for (const UserThemeCandidate& candidate : scan.themes) {
        if (candidate.name != choice.name) continue;
        if (candidate.theme == nullptr) return ThemeSelection::refused;
        resources.set_user_theme(candidate.theme);
        return ThemeSelection::selected;
    }
    return ThemeSelection::unknown;
}

ThemeExportReport export_user_themes(const ThemeLibrary& program, const ApplicationResources& resources,
                                     std::string_view folder) {
    ThemeExportReport report{};
    const std::vector<ThemePackageExport> exports = export_theme_packages(program);
    // The overridable icons a PNG gives, each written into every theme.
    std::vector<std::string> icon_paths{};
    std::vector<std::string_view> icon_bytes{};
    for (const IconDeclaration& declaration : resources.icon_declarations()) {
        if (declaration.policy != IconOverride::overridable) continue;
        const std::string path = "icons/" + declaration.identifier + ".png";
        const std::optional<ImageId> image = resources.program_icon(declaration.identifier);
        const std::optional<ImageResourceView> view =
            image ? resources.images().find(*image) : std::optional<ImageResourceView>{};
        if (!view || (*view).encoding != ImageResourceEncoding::png) {
            report.omitted.push_back(path);
            continue;
        }
        const std::span<const std::byte> encoded = (*view).encoded;
        icon_paths.push_back(path);
        icon_bytes.push_back(std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
    }
    for (const ThemePackageExport& theme : exports) {
        write_one(files::join_path(folder, theme.files.name + std::string(sheet_suffix)), theme.files.main_sheet,
                  report);
        const std::string theme_folder = files::join_path(folder, theme.files.name);
        for (const ThemePackageFile& file : theme.files.files) {
            write_one(files::join_path(theme_folder, file.path), file.bytes, report);
        }
        for (std::size_t index = 0U; index < icon_paths.size(); ++index) {
            write_one(files::join_path(theme_folder, icon_paths[index]), icon_bytes[index], report);
        }
        for (const std::string& omitted : theme.omitted) report.omitted.push_back(theme.files.name + "/" + omitted);
    }
    write_one(files::join_path(folder, "README.txt"), export_readme, report);
    return report;
}

} // namespace gui_forms
