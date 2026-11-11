#include "gui_forms/application/user_themes/user_themes.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include "../../core/theme_language/package/theme_manifest.hpp"
#include "theme_files.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
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

constexpr std::string_view theme_option = "--theme";
constexpr std::string_view export_option = "--export-themes";
// The name a sheet requested by its path compiles under; its findings name
// this file until they are written with the sheet's own path.
constexpr std::string_view requested_sheet_name = "theme-sheet";
constexpr std::string_view requested_sheet_file = "theme-sheet.css";

// Whether an option's next argument is its value: there is one, and it is
// not itself an option (read_theme_command_line's rule).
bool value_follows(std::span<const std::string_view> arguments, std::size_t index) noexcept {
    const bool follows = index + 1U < arguments.size() && !arguments[index + 1U].starts_with("--");
    return follows;
}

// Whether a --theme value names a sheet by its path: it holds a path
// separator, or ends in `.css` in any case.
bool names_sheet(std::string_view value) noexcept {
    if (value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos) return true;
    if (value.size() < sheet_suffix.size()) return false;
    const std::string_view tail = value.substr(value.size() - sheet_suffix.size());
    for (std::size_t index = 0U; index < tail.size(); ++index) {
        const char character = tail[index];
        const bool upper = character >= 'A' && character <= 'Z';
        const char lower = upper ? static_cast<char>(character - 'A' + 'a') : character;
        if (lower != sheet_suffix[index]) return false;
    }
    return true;
}

ThemeRequest request_of(ThemeRequestSource source, std::string_view value) {
    ThemeRequest request{};
    request.source = source;
    request.value = std::string(value);
    if (value == built_in_theme_name) {
        request.kind = ThemeRequestKind::built_in;
    } else if (names_sheet(value)) {
        request.kind = ThemeRequestKind::sheet;
    } else {
        request.kind = ThemeRequestKind::named;
    }
    return request;
}

// How the request is written in its log lines: as it was given.
std::string request_text(const ThemeRequest& request) {
    std::string text{};
    if (request.source == ThemeRequestSource::environment) {
        text = std::string(theme_sheet_environment_variable) + "=" + request.value;
    } else {
        text = std::string(theme_option) + " " + request.value;
    }
    return text;
}

// Writes `line` to stderr and keeps it in the report.
void report_line(ThemeRequestReport& report, std::string line) {
    std::fprintf(stderr, "GUI.Forms: %s\n", line.c_str());
    report.log.push_back(std::move(line));
}

// Writes each finding and keeps it, a finding on the requested sheet's
// implied file named by the sheet's path.
void report_findings(ThemeRequestReport& report, std::span<const ThemeCompileDiagnostic> diagnostics,
                     std::string_view sheet_path) {
    for (const ThemeCompileDiagnostic& diagnostic : diagnostics) {
        ThemeCompileDiagnostic shown = diagnostic;
        if (!sheet_path.empty() && shown.file == requested_sheet_file) shown.file = std::string(sheet_path);
        report_line(report, format_theme_diagnostic(shown));
        report.findings.push_back(std::move(shown));
    }
}

// The compile options of `context`, borrowing it.
ThemeCompileOptions options_of(const ThemeRequestContext& context) {
    ThemeCompileOptions options{};
    options.controls = context.controls.empty() ? stock_control_anatomies()
                                                : std::span<const ControlAnatomy>(context.controls);
    options.published_values = std::span<const std::string>(context.published_values);
    options.base = context.base;
    options.effects = context.effects;
    return options;
}

// A named request: the theme of that name among those the context's folders
// hold, compiled by the scan.
void apply_named(ApplicationResources& resources, const ThemeRequest& request, const ThemeRequestContext& context,
                 const ThemeLibrary& library, const ThemeCompileOptions& options, ThemeRequestReport& report) {
    const UserThemeScan scan = scan_user_themes(context.folders, resources, library, options);
    for (const std::string& line : scan.log) report_line(report, line);
    for (const UserThemeCandidate& candidate : scan.themes) {
        if (candidate.name != request.value) continue;
        report_findings(report, candidate.diagnostics, std::string_view{});
        if (candidate.theme == nullptr) {
            report.outcome = ThemeRequestOutcome::refused;
            report_line(report, request_text(request) + " ignored: " + candidate.file +
                                    " does not compile; the program's own theme stays");
            return;
        }
        resources.set_user_theme(candidate.theme);
        report.outcome = ThemeRequestOutcome::applied;
        return;
    }
    report.outcome = ThemeRequestOutcome::unknown_name;
    report_line(report, request_text(request) + " ignored: no user theme of that name in `" +
                            context.folders.beside_program + "` or `" + context.folders.per_user + "`");
}

// A sheet requested by its path, read and compiled as a user theme whose
// header is implied.
void apply_sheet(ApplicationResources& resources, const ThemeRequest& request, const ThemeLibrary& library,
                 const ThemeCompileOptions& options, ThemeRequestReport& report) {
    UserThemeFiles sheet{};
    sheet.name = std::string(requested_sheet_name);
    sheet.header_implied = true;
    const files::FileOutcome read =
        files::read_named_file(request.value, maximum_main_sheet_bytes, sheet.main_sheet);
    if (read != files::FileOutcome::done) {
        report.outcome = ThemeRequestOutcome::unreadable;
        report_line(report, request_text(request) + " ignored: the sheet is " + outcome_text(read) +
                                " (at most 1 MiB is read); the program's own theme stays");
        return;
    }
    const ThemeCompileResult compiled = resources.compile_user_theme(sheet, library, options);
    report_findings(report, compiled.diagnostics(), request.value);
    if (!compiled.succeeded()) {
        report.outcome = ThemeRequestOutcome::refused;
        report_line(report,
                    request_text(request) + " ignored: the sheet does not compile; the program's own theme stays");
        return;
    }
    resources.set_user_theme(compiled.theme());
    report.outcome = ThemeRequestOutcome::applied;
}

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

ThemeRequest read_theme_request(std::span<const std::string_view> arguments, std::string_view environment_sheet) {
    const ThemeCommandLine line = read_theme_command_line(arguments);
    if (!line.error.empty()) {
        ThemeRequest misformed{};
        misformed.source = ThemeRequestSource::command_line;
        misformed.error = line.error;
        return misformed;
    }
    if (line.theme.kind == ThemeChoiceKind::built_in) {
        ThemeRequest request = request_of(ThemeRequestSource::command_line, built_in_theme_name);
        return request;
    }
    if (line.theme.kind == ThemeChoiceKind::named) {
        ThemeRequest request = request_of(ThemeRequestSource::command_line, line.theme.name);
        return request;
    }
    if (environment_sheet.empty()) return ThemeRequest{};
    ThemeRequest request = request_of(ThemeRequestSource::environment, environment_sheet);
    return request;
}

std::vector<std::string> process_arguments() {
    std::vector<std::string> arguments = files::process_arguments();
    return arguments;
}

ThemeRequest process_theme_request() {
    const std::vector<std::string> arguments = files::process_arguments();
    std::vector<std::string_view> views{};
    views.reserve(arguments.size());
    for (const std::string& argument : arguments) views.push_back(argument);
    const std::string environment_sheet = files::environment_value(theme_sheet_environment_variable);
    ThemeRequest request = read_theme_request(views, environment_sheet);
    return request;
}

std::vector<std::string_view> program_arguments(std::span<const std::string_view> arguments) {
    std::vector<std::string_view> kept{};
    kept.reserve(arguments.size());
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        const bool theme_options = argument == theme_option || argument == export_option;
        if (!theme_options) {
            kept.push_back(argument);
            continue;
        }
        if (value_follows(arguments, index)) index = index + 1U;
    }
    return kept;
}

UserThemeFolders process_theme_folders() {
    UserThemeFolders folders{};
    const std::string executable = files::executable_path();
    const std::size_t separator = executable.find_last_of("/\\");
    if (executable.empty() || separator == std::string::npos) return folders;
    const std::string_view folder = std::string_view(executable).substr(0U, separator);
    const std::string_view file = std::string_view(executable).substr(separator + 1U);
    const std::size_t dot = file.find_last_of('.');
    const std::string_view program = dot == std::string_view::npos || dot == 0U ? file : file.substr(0U, dot);
    folders.beside_program = files::join_path(folder, "themes");
    if (!program.empty()) folders.per_user = per_user_theme_folder(program);
    return folders;
}

ThemeRequestReport apply_theme_request(ApplicationResources& resources, const ThemeRequest& request,
                                       const ThemeRequestContext& context) {
    if (resources.window_count() != 0U) throw std::logic_error("a theme request is settled before the first window");
    ThemeRequestReport report{};
    report.request = request;
    if (!request.error.empty()) {
        report.outcome = ThemeRequestOutcome::malformed;
        report_line(report, "the command line's theme is ignored: " + request.error);
        return report;
    }
    if (request.kind == ThemeRequestKind::none) return report;
    if (resources.user_theme_policy() == UserThemePolicy::refused) {
        report.outcome = ThemeRequestOutcome::ignored_by_policy;
        report_line(report, request_text(request) + " ignored: this program refuses user themes");
        return report;
    }
    if (request.kind == ThemeRequestKind::built_in) {
        resources.set_user_theme(nullptr);
        report.outcome = ThemeRequestOutcome::built_in;
        return report;
    }
    const ThemeCompileOptions options = options_of(context);
    const ThemeLibrary no_library{};
    const ThemeLibrary& library = context.library != nullptr ? *context.library : no_library;
    if (request.kind == ThemeRequestKind::named) {
        apply_named(resources, request, context, library, options, report);
    } else {
        apply_sheet(resources, request, library, options, report);
    }
    return report;
}

void hold_process_theme_request(ApplicationResources& resources, ThemeRequestContext context) {
    ThemeRequest request = process_theme_request();
    const bool asked = request.kind != ThemeRequestKind::none || !request.error.empty();
    if (!asked) return;
    if (context.folders.beside_program.empty() && context.folders.per_user.empty()) {
        context.folders = process_theme_folders();
    }
    std::unique_ptr<DeferredThemeRequest> held = std::make_unique<DeferredThemeRequest>();
    (*held).request = std::move(request);
    (*held).context = std::move(context);
    resources.hold_theme_request(std::move(held));
}

} // namespace gui_forms
