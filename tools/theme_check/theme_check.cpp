// The GUI.Forms theme check (owner request 2026-11-09, the product ports):
// compiles a program's theme packages at build time, as
// ApplicationResources::compile_theme_packages will at run time, so a bad
// sheet fails the build instead of Application::create. Shipped in the SDK
// and run by gui_forms_add_theme_check (cmake/GUIFormsThemes.cmake).
//
//   gui_forms_theme_check [--base gui-forms|windows-professional]
//                         [--warnings-as-errors] [--stamp <file>]
//                         <package directory>...
//
// Each directory holds a package's theme.json and the files its manifest
// names (every "path" field); the packages are one cascade in command-line
// order, onto the base (gui-forms unless named), with the stock control
// types and the built-in effects. Every finding goes to standard error, one
// per line (format_theme_diagnostic). On success the stamp, when named, is
// written, so a build reruns the check only when a package changes. Exit
// status: 0 valid, 1 refused (or warned, with --warnings-as-errors), 2
// usage or I/O.

#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/theme.hpp"

#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::ThemeBase;
using gui_forms::ThemeCompileDiagnostic;
using gui_forms::ThemeCompileOptions;
using gui_forms::ThemeDiagnosticSeverity;
using gui_forms::ThemeLibrary;
using gui_forms::ThemePackage;
using gui_forms::ThemePackageFile;
using gui_forms::ThemeValidation;

constexpr int exit_valid = 0;
constexpr int exit_refused = 1;
constexpr int exit_usage = 2;

struct Arguments final {
    std::vector<std::string> packages{};
    ThemeBase base{gui_forms::default_theme_base};
    bool warnings_as_errors{};
    std::string stamp{};
};

void print_usage() {
    std::cerr << "usage: gui_forms_theme_check [--base gui-forms|windows-professional] [--warnings-as-errors]\n"
                 "                             [--stamp <file>] <package directory>...\n";
}

// Reads the command line; none when it is malformed.
std::optional<Arguments> read_arguments(std::span<char* const> words) {
    Arguments arguments{};
    for (std::size_t index = 1U; index < words.size(); ++index) {
        const std::string_view word = words[index];
        const bool has_value = index + 1U < words.size();
        if (word == "--warnings-as-errors") {
            arguments.warnings_as_errors = true;
        } else if (word == "--base" && has_value) {
            ++index;
            const std::string_view base = words[index];
            if (base == "gui-forms") {
                arguments.base = ThemeBase::gui_forms;
            } else if (base == "windows-professional") {
                arguments.base = ThemeBase::windows_professional;
            } else {
                return std::nullopt;
            }
        } else if (word == "--stamp" && has_value) {
            ++index;
            arguments.stamp = words[index];
        } else if (!word.starts_with("--")) {
            arguments.packages.emplace_back(word);
        } else {
            return std::nullopt;
        }
    }
    if (arguments.packages.empty()) return std::nullopt;
    return arguments;
}

// The bytes of `path`, or none when it cannot be read.
std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::ostringstream bytes{};
    bytes << stream.rdbuf();
    return bytes.str();
}

// Every "path" field's value in `manifest`, in order: the files the
// package names. The compiler verifies the manifest itself; this only finds
// which files to read, as gui_forms_add_theme's embedding does.
std::vector<std::string> manifest_paths(std::string_view manifest) {
    constexpr std::string_view key = "\"path\"";
    std::vector<std::string> paths{};
    std::size_t at = manifest.find(key);
    while (at != std::string_view::npos) {
        std::size_t cursor = at + key.size();
        while (cursor < manifest.size() && (manifest[cursor] == ' ' || manifest[cursor] == '\t' ||
                                            manifest[cursor] == '\r' || manifest[cursor] == '\n')) {
            ++cursor;
        }
        const bool colon = cursor < manifest.size() && manifest[cursor] == ':';
        std::size_t opening = std::string_view::npos;
        if (colon) opening = manifest.find('"', cursor + 1U);
        std::size_t closing = std::string_view::npos;
        if (opening != std::string_view::npos) closing = manifest.find('"', opening + 1U);
        if (closing != std::string_view::npos) {
            paths.emplace_back(manifest.substr(opening + 1U, closing - opening - 1U));
            at = manifest.find(key, closing + 1U);
        } else {
            at = manifest.find(key, cursor);
        }
    }
    return paths;
}

// The package in `directory`, or none after naming what could not be read.
std::optional<ThemePackage> read_package(const std::string& directory) {
    const std::string manifest_path = directory + "/theme.json";
    std::optional<std::string> manifest = read_file(manifest_path);
    if (!manifest) {
        std::cerr << manifest_path << ": cannot be read\n";
        return std::nullopt;
    }
    ThemePackage package{};
    for (const std::string& path : manifest_paths(*manifest)) {
        const std::string file_path = directory + "/" + path;
        std::optional<std::string> bytes = read_file(file_path);
        if (!bytes) {
            std::cerr << file_path << ": named by theme.json, cannot be read\n";
            return std::nullopt;
        }
        package.files.push_back(ThemePackageFile{.path = path, .bytes = std::move(*bytes)});
    }
    package.manifest = std::move(*manifest);
    return package;
}

int check(const Arguments& arguments) {
    std::vector<ThemePackage> packages{};
    packages.reserve(arguments.packages.size());
    for (const std::string& directory : arguments.packages) {
        std::optional<ThemePackage> package = read_package(directory);
        if (!package) return exit_usage;
        packages.push_back(std::move(*package));
    }
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = gui_forms::stock_control_anatomies();
    options.base = arguments.base;
    options.effects = &gui_forms::built_in_effect_registry();
    const ThemeValidation validation = gui_forms::validate_theme_packages(packages, library, options);
    bool warned = false;
    for (const ThemeCompileDiagnostic& diagnostic : validation.diagnostics) {
        std::cerr << gui_forms::format_theme_diagnostic(diagnostic) << '\n';
        if (diagnostic.severity == ThemeDiagnosticSeverity::warning) warned = true;
    }
    const bool passed = validation.valid && !(arguments.warnings_as_errors && warned);
    if (!passed) return exit_refused;
    if (!arguments.stamp.empty()) {
        std::ofstream stamp(arguments.stamp, std::ios::binary | std::ios::trunc);
        stamp << "checked\n";
        if (!stamp) {
            std::cerr << arguments.stamp << ": cannot be written\n";
            return exit_usage;
        }
    }
    return exit_valid;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char* const> words(argv, static_cast<std::size_t>(argc));
    const std::optional<Arguments> arguments = read_arguments(words);
    if (!arguments) {
        print_usage();
        return exit_usage;
    }
    try {
        const int status = check(*arguments);
        return status;
    } catch (const std::exception& failure) {
        // The program's boundary: an allocation or a stream that failed.
        std::cerr << "gui_forms_theme_check: " << failure.what() << '\n';
        return exit_usage;
    }
}
