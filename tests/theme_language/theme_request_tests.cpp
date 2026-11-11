// The theme a process is started with (gui_forms/application/user_themes:
// --theme <name|built-in|path.css> and GUI_FORMS_THEME_SHEET), in process:
// reading the request, the arguments a program keeps, the Windows command
// line's splitting, settling a request under each policy (a sheet, a name,
// built-in, a refused sheet, a missing file, a misformed command line, a
// misspelt effect name with and without an installed effect registry), and
// a request held by the resources until their first window, which wins
// over the program's own choice. The launched programs are
// theme_request_app.cpp and tests/abi/gf_abi_theme_request_app.c. Works in a
// folder of its own under the system's temporary one.
#include "../../src/runtime/user_themes/theme_files.hpp"
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::string_view good_sheet = "button::part(face) { background-color: #3366cc }\n";
// Refused on its second line: a comment never closed.
constexpr std::string_view bad_sheet = "button { opacity: 0.9 }\n/* never closed\n";
constexpr std::string_view named_sheet =
    "@theme { version: \"1.0.0\"; author: \"A. User\"; }\nbutton::part(face) { background-color: #aa3311 }\n";

std::string text_of(const std::filesystem::path& path) {
    const std::u8string units = path.generic_u8string();
    std::string made(reinterpret_cast<const char*>(units.data()), units.size());
    return made;
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "a fixture file is written");
}

// A user theme of the program's own choice (its saved setting), compiled
// for the resources' policy.
std::shared_ptr<const Theme> programs_choice(ApplicationResources& resources) {
    UserThemeFiles files{};
    files.name = "chosen";
    files.main_sheet = std::string(named_sheet);
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemeCompileResult compiled = resources.compile_user_theme(files, library, options);
    require(compiled.succeeded(), "the program's own user theme compiles");
    return compiled.theme();
}

ThemeRequest request(ThemeRequestKind kind, std::string_view value) {
    ThemeRequest made{};
    made.source = ThemeRequestSource::command_line;
    made.kind = kind;
    made.value = std::string(value);
    return made;
}

void test_reading() {
    const std::vector<std::string_view> sheet{"--size", "640x480", "--theme", "themes/Dx.CSS"};
    const ThemeRequest from_sheet = read_theme_request(sheet, std::string_view{});
    require(from_sheet.kind == ThemeRequestKind::sheet && from_sheet.value == "themes/Dx.CSS" &&
                from_sheet.source == ThemeRequestSource::command_line,
            "a value ending in .css in any case is a sheet's path");
    const std::vector<std::string_view> named{"--theme", "harbour"};
    require(read_theme_request(named, std::string_view{}).kind == ThemeRequestKind::named, "a bare word is a name");
    const std::vector<std::string_view> built_in{"--theme", "built-in"};
    require(read_theme_request(built_in, "dx.css").kind == ThemeRequestKind::built_in,
            "built-in is no user theme, and the command line wins over the environment");
    const std::vector<std::string_view> separator{"--theme", "C:\\themes\\dx"};
    require(read_theme_request(separator, std::string_view{}).kind == ThemeRequestKind::sheet,
            "a value with a path separator is a path");
    const std::vector<std::string_view> none{"--page", "buttons"};
    const ThemeRequest environment = read_theme_request(none, "/tmp/dx.css");
    require(environment.kind == ThemeRequestKind::sheet && environment.source == ThemeRequestSource::environment,
            "GUI_FORMS_THEME_SHEET names the sheet when the command line names none");
    require(read_theme_request(none, std::string_view{}).kind == ThemeRequestKind::none, "nothing asked, none");
    const std::vector<std::string_view> twice{"--theme", "a", "--theme", "b"};
    const ThemeRequest misformed = read_theme_request(twice, "dx.css");
    require(!misformed.error.empty() && misformed.kind == ThemeRequestKind::none,
            "--theme given twice is misformed, and the environment is not taken instead");
}

void test_program_arguments() {
    const std::vector<std::string_view> given{"--theme", "dx.css", "--page", "buttons", "--export-themes",
                                              "--automation", "--export-themes", "out"};
    const std::vector<std::string_view> kept = program_arguments(given);
    const std::vector<std::string_view> expected{"--page", "buttons", "--automation"};
    require(kept == expected, "the user-theme options and their values are passed over, the rest kept in order");
}

void test_windows_command_line() {
    namespace files = detail::theme_files;
    const std::vector<std::string> simple = files::split_windows_command_line("app.exe  --theme\tdx.css ");
    require(simple == std::vector<std::string>{"app.exe", "--theme", "dx.css"}, "spaces and tabs separate");
    const std::vector<std::string> quoted =
        files::split_windows_command_line("\"C:\\Program Files\\app.exe\" --theme \"C:\\My Themes\\dx.css\"");
    require(quoted == std::vector<std::string>{"C:\\Program Files\\app.exe", "--theme", "C:\\My Themes\\dx.css"},
            "quotes hold spaces, and backslashes before other characters are literal");
    const std::vector<std::string> escaped = files::split_windows_command_line("app a\\\\\\\"b \"c\\\\\" d");
    require(escaped == std::vector<std::string>{"app", "a\\\"b", "c\\", "d"},
            "2n+1 backslashes and a quote are n and a quote; 2n and a quote are n and a toggle");
    const std::vector<std::string> doubled = files::split_windows_command_line("app \"a\"\"b\" c");
    require(doubled == std::vector<std::string>{"app", "a\"b", "c"}, "a doubled quote inside quotes is a quote");
}

void test_sheet_layered(const std::filesystem::path& root) {
    const std::filesystem::path sheet = root / "good.css";
    write_file(sheet, good_sheet);
    ApplicationResources resources{};
    const ThemeRequestReport report =
        apply_theme_request(resources, request(ThemeRequestKind::sheet, text_of(sheet)), ThemeRequestContext{});
    require(report.outcome == ThemeRequestOutcome::applied, "a sheet with no header is applied as a user theme");
    require(resources.user_theme() != nullptr && resources.user_theme_rules() != nullptr,
            "under the layered policy the sheet is layered");
    require(resources.theme_ptr() == resources.program_theme_ptr(), "the program's theme stays in force");
}

void test_sheet_replacing(const std::filesystem::path& root) {
    const std::filesystem::path sheet = root / "good.css";
    ApplicationResources resources{};
    resources.set_user_theme_policy(UserThemePolicy::replaces_program_sheet);
    ThemeRequestContext context{};
    context.base = ThemeBase::windows_professional;
    const ThemeRequestReport report =
        apply_theme_request(resources, request(ThemeRequestKind::sheet, text_of(sheet)), context);
    require(report.outcome == ThemeRequestOutcome::applied, "a replacing sheet is applied");
    require(resources.user_theme() != nullptr && resources.theme_ptr() == resources.user_theme_ptr(),
            "under replaces_program_sheet the sheet is the windows' theme");
}

void test_refused_policy(const std::filesystem::path& root) {
    ApplicationResources resources{};
    resources.set_user_theme_policy(UserThemePolicy::refused);
    const ThemeRequestReport report = apply_theme_request(
        resources, request(ThemeRequestKind::sheet, text_of(root / "good.css")), ThemeRequestContext{});
    require(report.outcome == ThemeRequestOutcome::ignored_by_policy && !report.log.empty(),
            "a program refusing user themes ignores the request, with a warning");
    require(resources.user_theme() == nullptr, "nothing is selected under the refused policy");
}

void test_bad_sheet(const std::filesystem::path& root) {
    const std::filesystem::path sheet = root / "bad.css";
    write_file(sheet, bad_sheet);
    ApplicationResources resources{};
    const std::shared_ptr<const Theme> chosen = programs_choice(resources);
    resources.set_user_theme(chosen);
    const ThemeRequestReport report =
        apply_theme_request(resources, request(ThemeRequestKind::sheet, text_of(sheet)), ThemeRequestContext{});
    require(report.outcome == ThemeRequestOutcome::refused, "a sheet that does not compile is refused");
    require(!report.findings.empty(), "the refusal's findings are kept");
    if (!report.findings.empty()) {
        const ThemeCompileDiagnostic& finding = report.findings.front();
        require(finding.file == text_of(sheet) && finding.line == 2U,
                "the finding names the sheet's own path and its line");
    }
    require(resources.user_theme_ptr() == chosen, "a refused sheet leaves the program's own choice in force");
}

// A sheet naming an effect no registry holds: with no registry installed
// it is applied, its name checked for its form only, and the report says
// so; with the stock controls' registry installed it is refused with E417
// and the program's choice stays. Leaves none installed.
void test_effect_names_against_the_installed_registry(const std::filesystem::path& root) {
    const std::filesystem::path sheet = root / "misspelt-effect.css";
    write_file(sheet, "progress-bar::part(fill) { effect: progress-stirpes }\n");
    install_effect_registry(nullptr);
    {
        ApplicationResources resources{};
        const ThemeRequestReport report =
            apply_theme_request(resources, request(ThemeRequestKind::sheet, text_of(sheet)), ThemeRequestContext{});
        require(report.outcome == ThemeRequestOutcome::applied,
                "with no effect registry installed a misspelt effect name is not refused");
        bool noted = false;
        for (const std::string& line : report.log) {
            if (line.find("no effect registry is installed") != std::string::npos) noted = true;
        }
        require(noted, "the report says effect names were checked for their form only");
    }
    install_built_in_effect_registry();
    {
        ApplicationResources resources{};
        const ThemeRequestReport report =
            apply_theme_request(resources, request(ThemeRequestKind::sheet, text_of(sheet)), ThemeRequestContext{});
        require(report.outcome == ThemeRequestOutcome::refused,
                "with the built-in registry installed a misspelt effect name refuses the sheet");
        bool unknown = false;
        for (const ThemeCompileDiagnostic& finding : report.findings) {
            if (finding.code == "E417") unknown = true;
        }
        require(unknown, "the refusal is E417, at the name");
        require(resources.user_theme() == nullptr, "a refused sheet selects no user theme");
    }
    install_effect_registry(nullptr);
}

void test_missing_and_misformed(const std::filesystem::path& root) {
    ApplicationResources resources{};
    const ThemeRequestReport missing = apply_theme_request(
        resources, request(ThemeRequestKind::sheet, text_of(root / "absent.css")), ThemeRequestContext{});
    require(missing.outcome == ThemeRequestOutcome::unreadable, "a sheet that is not there is unreadable");
    ThemeRequest misformed{};
    misformed.source = ThemeRequestSource::command_line;
    misformed.error = "--theme names a theme, or built-in";
    const ThemeRequestReport refused = apply_theme_request(resources, misformed, ThemeRequestContext{});
    require(refused.outcome == ThemeRequestOutcome::malformed, "a misformed command line is reported");
    require(resources.user_theme() == nullptr, "neither selects anything");
}

void test_named_and_built_in(const std::filesystem::path& root) {
    write_file(root / "beside" / "harbour.css", named_sheet);
    ThemeRequestContext context{};
    context.folders.beside_program = text_of(root / "beside");
    ApplicationResources resources{};
    const ThemeRequestReport named =
        apply_theme_request(resources, request(ThemeRequestKind::named, "harbour"), context);
    require(named.outcome == ThemeRequestOutcome::applied && resources.user_theme() != nullptr,
            "a user theme found by its name is selected");
    const ThemeRequestReport unknown =
        apply_theme_request(resources, request(ThemeRequestKind::named, "dusk"), context);
    require(unknown.outcome == ThemeRequestOutcome::unknown_name && resources.user_theme() != nullptr,
            "an unknown name changes nothing");
    const ThemeRequestReport built_in =
        apply_theme_request(resources, request(ThemeRequestKind::built_in, "built-in"), context);
    require(built_in.outcome == ThemeRequestOutcome::built_in && resources.user_theme() == nullptr,
            "built-in selects no user theme");
}

void test_held_until_the_first_window(const std::filesystem::path& root) {
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    const std::shared_ptr<const Theme> chosen = programs_choice(*resources);
    std::unique_ptr<DeferredThemeRequest> held = std::make_unique<DeferredThemeRequest>();
    (*held).request = request(ThemeRequestKind::sheet, text_of(root / "good.css"));
    (*resources).hold_theme_request(std::move(held));
    // The program chooses after the request was held, before its window.
    (*resources).set_user_theme(chosen);
    require((*resources).theme_request_held() && (*resources).theme_request_report() == nullptr,
            "a held request waits for the first window");
    const Control::Ptr root_control = make_control<Control>(StableId(std::string("theme-request.root")));
    const Window window(root_control, Size{120.0, 80.0}, resources);
    const ThemeRequestReport* const report = (*resources).theme_request_report();
    require(!(*resources).theme_request_held() && report != nullptr &&
                (*report).outcome == ThemeRequestOutcome::applied,
            "the first window settles the request");
    require((*resources).user_theme() != nullptr && (*resources).user_theme_ptr() != chosen,
            "the request wins over the program's own choice for the run");
    bool refused = false;
    try {
        (*resources).hold_theme_request(std::make_unique<DeferredThemeRequest>());
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "a request held once a window shares the resources is misuse");
}

} // namespace

int main() {
    const std::string stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("gui_forms_theme_request_tests-" + stamp);
    std::error_code error{};
    std::filesystem::create_directories(root, error);
    require(!error, "the test's folder is made");
    test_reading();
    test_program_arguments();
    test_windows_command_line();
    test_sheet_layered(root);
    test_sheet_replacing(root);
    test_refused_policy(root);
    test_bad_sheet(root);
    test_missing_and_misformed(root);
    test_effect_names_against_the_installed_registry(root);
    test_named_and_built_in(root);
    test_held_until_the_first_window(root);
    // The test's own folder, which it made above.
    std::filesystem::remove_all(root, error);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "GUI.Forms theme request tests passed\n";
    return 0;
}
