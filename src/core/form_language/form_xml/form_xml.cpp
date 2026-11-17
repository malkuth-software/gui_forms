// A form document's XML front end (cp34).

#include "form_xml.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace gui_forms::form_language {

XmlLimits form_xml_limits() noexcept {
    XmlLimits limits{};
    limits.maximum_input_bytes = theme_language::maximum_sheet_bytes;
    return limits;
}

FormSyntaxOutcome read_form_syntax(const theme_language::ThemeSource& source, FormSyntax& syntax,
                                   theme_language::ThemeDiagnosticList& diagnostics) {
    const XmlLimits limits = form_xml_limits();
    XmlReadResult read = detail::read_xml_located(source.text, limits, syntax.locations);
    if (!read) {
        theme_language::ThemeDiagnostic diagnostic{};
        diagnostic.code = theme_language::DiagnosticCode::form_xml_invalid;
        diagnostic.file = std::string(source.file);
        diagnostic.position.offset = theme_language::ByteOffset{static_cast<std::uint32_t>(read.position.byte_offset)};
        diagnostic.position.line = theme_language::LineNumber{read.position.line};
        diagnostic.position.column = theme_language::ColumnNumber{read.position.column};
        diagnostic.detail = std::string(xml_error_name(read.error));
        diagnostics.add(std::move(diagnostic));
        syntax.root = XmlElement{};
        syntax.lines.clear();
        return FormSyntaxOutcome::refused;
    }
    // The reader admitted the input, so it is within maximum_sheet_bytes,
    // the line table's bound.
    syntax.lines.reset(source.text);
    syntax.root = std::move(read.root);
    return FormSyntaxOutcome::read;
}

} // namespace gui_forms::form_language
