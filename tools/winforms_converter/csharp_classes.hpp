#pragma once

// The classes a WinForms project's C# files declare (stage MCP G7): for
// each class its namespace, its first base type and its methods' names and
// parameter types. The converter uses them to know a form's base (Form or
// UserControl), the project's own control classes and the bases they
// derive from, and which event handlers a form class really declares. It
// reads declarations only; no body is interpreted.

#include "conversion_diagnostics.hpp"
#include "csharp_lexer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

// The most classes one index holds, and the most bases a chain is followed
// through (a cycle in broken source ends there too).
inline constexpr std::size_t maximum_csharp_classes = 100000U;
inline constexpr std::size_t maximum_base_chain = 32U;

struct CSharpMethod final {
    std::string name{};
    // As written, `object` and `System.EventArgs` for a handler.
    std::vector<std::string> parameter_types{};
    std::uint32_t line{};
};

struct CSharpClass final {
    std::string name{};
    std::string namespace_name{};
    // The first type of its base list, as written; empty when it has none.
    std::string base{};
    bool partial{};
    std::vector<CSharpMethod> methods{};
    std::string file{};
    std::uint32_t line{};
};

// Caller-owned; read_csharp_classes appends.
struct CSharpClassIndex final {
    std::vector<CSharpClass> classes{};

    // A class named `name` (its last segment when dotted); a partial class
    // declared in several files is several records, and the first with a
    // base is preferred. Null when none.
    [[nodiscard]] const CSharpClass* find(std::string_view name) const& noexcept;
    const CSharpClass* find(std::string_view name) const&& = delete;
    // Whether any record of the class `class_name` declares a method `name`.
    [[nodiscard]] bool declares_method(std::string_view class_name, std::string_view name) const noexcept;
    // The base of `class_name` (any of its records), or empty.
    [[nodiscard]] std::string base_of(std::string_view class_name) const;
};

// Lexes `source` and appends the classes it declares. A file the lexer
// refuses adds its error and nothing else.
void read_csharp_classes(CSharpSource& source, CSharpClassIndex& index, ConversionDiagnosticList& diagnostics);

} // namespace gui_forms::winforms_converter
