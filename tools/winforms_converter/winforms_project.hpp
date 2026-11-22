#pragma once

// A Windows Forms project on disk (stage MCP G7): its C# files, and its
// forms, each a `*.Designer.cs` with the `.resx` beside it. The only part
// of the converter that touches the file system.
//
// A project is a directory (or the directory of a `.csproj` named), read
// recursively; `bin`, `obj` and dot-directories are skipped, as are files
// beyond the limits below, each with a diagnostic.

#include "conversion_diagnostics.hpp"
#include "csharp_classes.hpp"
#include "winforms_conversion.hpp"

#include "../../src/core/form_language/form_model/form_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

// The most C# files a project lists; the rest are reported (WF106).
inline constexpr std::size_t maximum_project_files = 20000U;
// The most directory entries the walk visits.
inline constexpr std::size_t maximum_project_entries = 200000U;

struct ProjectForm final {
    std::string designer_file{};
    // Empty when no `.resx` sits beside the designer file.
    std::string resx_file{};
    // The designer file's name less `.Designer.cs`: Form1 for Form1.Designer.cs.
    std::string name{};
};

struct WinFormsProject final {
    std::string root{};
    // Sorted. Designer files are among them.
    std::vector<std::string> csharp_files{};
    // Sorted by designer file.
    std::vector<ProjectForm> forms{};

    // The form named `name` (Form1, or its designer file's path), or null.
    [[nodiscard]] const ProjectForm* find_form(std::string_view name) const& noexcept;
    const ProjectForm* find_form(std::string_view name) const&& = delete;
};

enum class ProjectOutcome : std::uint8_t {
    found,
    // Not a readable directory or project file, or no form in it: one
    // error, and `project` unchanged.
    refused,
};

[[nodiscard]] ProjectOutcome find_winforms_project(std::string_view path, WinFormsProject& project,
                                                   ConversionDiagnosticList& diagnostics);

// The whole of `file` into `text`; false, with WF001 or WF002, when it
// cannot be read or exceeds maximum_source_bytes.
[[nodiscard]] bool read_source_file(std::string_view file, std::string& text, ConversionDiagnosticList& diagnostics);

// Writes `text` to `file` whole, making its directory; false, with WF001,
// when it cannot.
[[nodiscard]] bool write_text_file(std::string_view file, std::string_view text,
                                   ConversionDiagnosticList& diagnostics);

// The classes of every C# file of `project`, into `index` (appended).
void read_project_classes(const WinFormsProject& project, CSharpClassIndex& index,
                          ConversionDiagnosticList& diagnostics);

// Reads one form of `project` (its designer file and resources) and
// converts it against `classes` (read_project_classes) and `schema`.
[[nodiscard]] ConversionOutcome convert_project_form(const ProjectForm& form, const CSharpClassIndex& classes,
                                                     const form_language::FormSchema& schema,
                                                     const ConversionOptions& options, ConvertedForm& converted,
                                                     ConversionDiagnosticList& diagnostics);

} // namespace gui_forms::winforms_converter
