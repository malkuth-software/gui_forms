#include "winforms_project.hpp"

#include "csharp_lexer.hpp"
#include "designer_reader.hpp"
#include "resx_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace gui_forms::winforms_converter {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view designer_suffix = ".Designer.cs";

void report(ConversionDiagnosticList& diagnostics, ConversionCode code, std::string_view file, std::string detail) {
    ConversionDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(file);
    diagnostic.detail = std::move(detail);
    diagnostics.add(std::move(diagnostic));
}

[[nodiscard]] bool skipped_directory(const fs::path& path) {
    const std::string name = path.filename().string();
    const bool skipped = name == "bin" || name == "obj" || (!name.empty() && name.front() == '.');
    return skipped;
}

struct FormBelow final {
    [[nodiscard]] bool operator()(const ProjectForm& left, const ProjectForm& right) const noexcept {
        const bool below = left.designer_file < right.designer_file;
        return below;
    }
};

} // namespace

const ProjectForm* WinFormsProject::find_form(std::string_view name) const& noexcept {
    for (const ProjectForm& form : forms) {
        if (form.name == name || form.designer_file == name) return &form;
    }
    return nullptr;
}

bool read_source_file(std::string_view file, std::string& text, ConversionDiagnosticList& diagnostics) {
    const fs::path path{std::string(file)};
    std::error_code error{};
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        report(diagnostics, ConversionCode::file_unreadable, file, "cannot be read: " + error.message());
        return false;
    }
    if (size > maximum_source_bytes) {
        report(diagnostics, ConversionCode::file_too_large, file,
               "larger than the converter reads (" + std::to_string(maximum_source_bytes) + " bytes)");
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        report(diagnostics, ConversionCode::file_unreadable, file, "cannot be opened");
        return false;
    }
    std::string read(static_cast<std::size_t>(size), '\0');
    stream.read(read.data(), static_cast<std::streamsize>(read.size()));
    if (static_cast<std::uintmax_t>(stream.gcount()) != size) {
        report(diagnostics, ConversionCode::file_unreadable, file, "ended before its size");
        return false;
    }
    text = std::move(read);
    return true;
}

bool write_text_file(std::string_view file, std::string_view text, ConversionDiagnosticList& diagnostics) {
    const fs::path path{std::string(file)};
    std::error_code error{};
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), error);
    if (error) {
        report(diagnostics, ConversionCode::file_unreadable, file, "its directory cannot be made: " + error.message());
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) {
        report(diagnostics, ConversionCode::file_unreadable, file, "cannot be written");
        return false;
    }
    return true;
}

ProjectOutcome find_winforms_project(std::string_view path, WinFormsProject& project,
                                     ConversionDiagnosticList& diagnostics) {
    fs::path root{std::string(path)};
    std::error_code error{};
    if (fs::is_regular_file(root, error) && root.extension() == ".csproj") root = root.parent_path();
    if (root.empty()) root = ".";
    if (!fs::is_directory(root, error)) {
        report(diagnostics, ConversionCode::file_unreadable, path, "neither a project directory nor a .csproj");
        return ProjectOutcome::refused;
    }
    WinFormsProject found{};
    found.root = root.string();
    fs::recursive_directory_iterator walk(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end{};
    std::size_t visited = 0U;
    bool limited = false;
    while (!error && walk != end) {
        visited = visited + 1U;
        if (visited > maximum_project_entries) {
            limited = true;
            break;
        }
        const fs::directory_entry& entry = *walk;
        if (entry.is_directory(error)) {
            if (skipped_directory(entry.path())) walk.disable_recursion_pending();
        } else if (entry.is_regular_file(error) && entry.path().extension() == ".cs") {
            if (found.csharp_files.size() >= maximum_project_files) {
                limited = true;
                break;
            }
            found.csharp_files.push_back(entry.path().generic_string());
        }
        walk.increment(error);
    }
    if (limited) {
        report(diagnostics, ConversionCode::limit_reached, path,
               "more files than the converter lists; the rest are not read");
    }
    std::sort(found.csharp_files.begin(), found.csharp_files.end());
    for (const std::string& file : found.csharp_files) {
        if (!std::string_view(file).ends_with(designer_suffix)) continue;
        ProjectForm form{};
        form.designer_file = file;
        const std::string stem = file.substr(0U, file.size() - designer_suffix.size());
        const std::size_t separator = stem.find_last_of('/');
        form.name = separator == std::string::npos ? stem : stem.substr(separator + 1U);
        const fs::path resx{stem + ".resx"};
        if (fs::is_regular_file(resx, error)) form.resx_file = resx.generic_string();
        found.forms.push_back(std::move(form));
    }
    std::sort(found.forms.begin(), found.forms.end(), FormBelow{});
    if (found.forms.empty()) {
        report(diagnostics, ConversionCode::project_empty, path, "no *.Designer.cs file: no form to convert");
        return ProjectOutcome::refused;
    }
    project = std::move(found);
    return ProjectOutcome::found;
}

void read_project_classes(const WinFormsProject& project, CSharpClassIndex& index,
                          ConversionDiagnosticList& diagnostics) {
    for (const std::string& file : project.csharp_files) {
        CSharpSource source{};
        source.file = file;
        if (!read_source_file(file, source.text, diagnostics)) continue;
        read_csharp_classes(source, index, diagnostics);
    }
}

ConversionOutcome convert_project_form(const ProjectForm& form, const CSharpClassIndex& classes,
                                       const form_language::FormSchema& schema, const ConversionOptions& options,
                                       ConvertedForm& converted, ConversionDiagnosticList& diagnostics) {
    CSharpSource source{};
    source.file = form.designer_file;
    if (!read_source_file(form.designer_file, source.text, diagnostics)) return ConversionOutcome::refused;
    DesignerDocument designer{};
    if (read_designer_file(source, designer, diagnostics) != DesignerReadOutcome::read) {
        return ConversionOutcome::refused;
    }
    ResxDocument resx{};
    bool has_resx = false;
    if (!form.resx_file.empty()) {
        std::string text{};
        if (read_source_file(form.resx_file, text, diagnostics)) {
            has_resx = read_resx(form.resx_file, text, resx, diagnostics) == ResxReadOutcome::read;
        }
    }
    const ConversionOutcome outcome = convert_winforms_form(designer, has_resx ? &resx : nullptr, classes, schema,
                                                            options, converted, diagnostics);
    return outcome;
}

} // namespace gui_forms::winforms_converter
