#pragma once

#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// One string literal the GUI.Forms Clang plugin found passed to a localizable
// property (a parameter or field marked GUI_FORMS_LOCALIZABLE). Each literal
// is recorded once for each property and function it reaches.
struct LocalizableLiteral final {
    // The property the mark names: "text", "tool_tip", "title",
    // "placeholder", "accessible_name", "accessible_description", "header"
    // or "items".
    std::string property;
    // The variant name derived from the literal's bytes ("v-" and 12
    // lowercase hexadecimal digits of their 64-bit FNV-1a hash), the same at
    // every site that passes the same text.
    std::string name;
    // The file holding the literal, with forward slashes: relative to the
    // plugin's root= argument (the consuming project's source directory),
    // absolute outside it. For a named constant it is the constant's
    // definition.
    std::string file;
    // The 1-based line of the literal's first token.
    std::uint32_t line{};
    // The 1-based column, in bytes, of the literal's first token.
    std::uint32_t column{};
    // The qualified name of the function passing the literal on: its
    // template's name for an instantiation, "f::<lambda>" inside a lambda in
    // f, "<global>" at namespace scope.
    std::string function;
    // The literal's bytes after escapes and adjacent-literal concatenation,
    // without the terminating NUL.
    std::string text;
};

// The literals recorded in every image loaded into this process (the
// program and each shared library; on Windows the program's own image only),
// in image order and, within an image, in link order. Images or translation
// units compiled without the plugin contribute nothing; an image whose
// tables are malformed contributes nothing.
[[nodiscard]] std::vector<LocalizableLiteral> localizable_literals();

// Decodes the tables in one section's bytes, appending to `out`. Returns
// false, leaving `out` as it was, when the bytes are not a sequence of
// well-formed version 1 tables (zero padding between tables is allowed).
[[nodiscard]] bool parse_localizable_tables(std::span<const std::byte> section, std::vector<LocalizableLiteral>& out);

} // namespace gui_forms
