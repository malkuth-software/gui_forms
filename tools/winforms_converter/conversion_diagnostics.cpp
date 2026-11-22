#include "conversion_diagnostics.hpp"

#include <cstdio>
#include <utility>

namespace gui_forms::winforms_converter {

void ConversionDiagnosticList::add(ConversionDiagnostic diagnostic) {
    const ConversionSeverity severity = conversion_severity(diagnostic.code);
    if (severity == ConversionSeverity::error) {
        errors_ = errors_ + 1U;
    } else if (severity == ConversionSeverity::warning) {
        warnings_ = warnings_ + 1U;
    } else {
        notes_ = notes_ + 1U;
    }
    if (records_.size() >= maximum_conversion_diagnostics) {
        dropped_ = dropped_ + 1U;
        return;
    }
    if (diagnostic.detail.size() > maximum_conversion_detail_bytes) {
        // Cut on a UTF-8 boundary: back up over continuation bytes.
        std::size_t cut = maximum_conversion_detail_bytes - 3U;
        while (cut > 0U && (static_cast<unsigned char>(diagnostic.detail[cut]) & 0xC0U) == 0x80U) cut = cut - 1U;
        diagnostic.detail.resize(cut);
        diagnostic.detail.append("...");
    }
    records_.push_back(std::move(diagnostic));
}

void ConversionDiagnosticList::clear() noexcept {
    records_.clear();
    errors_ = 0U;
    warnings_ = 0U;
    notes_ = 0U;
    dropped_ = 0U;
}

ConversionSeverity conversion_severity(ConversionCode code) noexcept {
    switch (code) {
    case ConversionCode::file_unreadable:
    case ConversionCode::file_too_large:
    case ConversionCode::project_empty:
    case ConversionCode::form_not_found:
    case ConversionCode::csharp_lexical:
    case ConversionCode::initialize_component_missing:
    case ConversionCode::resx_invalid:
        return ConversionSeverity::error;
    case ConversionCode::statement_skipped:
    case ConversionCode::expression_too_deep:
    case ConversionCode::limit_reached:
    case ConversionCode::type_unmapped:
    case ConversionCode::property_unmapped:
    case ConversionCode::property_not_admitted:
    case ConversionCode::value_unconverted:
    case ConversionCode::event_unmapped:
    case ConversionCode::resource_missing:
    case ConversionCode::resource_binary:
    case ConversionCode::binding_unconverted:
    case ConversionCode::handler_missing:
        return ConversionSeverity::warning;
    case ConversionCode::type_converted_as_base:
    case ConversionCode::property_to_code:
    case ConversionCode::presentation_to_theme:
    case ConversionCode::identity_respelled:
    case ConversionCode::known_divergence:
    case ConversionCode::call_to_code:
    case ConversionCode::auto_scale_dropped:
    case ConversionCode::layout_approximated:
        return ConversionSeverity::note;
    }
    // A value outside the enumeration counts as an error: it was made by
    // nothing this module writes.
    return ConversionSeverity::error;
}

std::string conversion_code_label(ConversionCode code) {
    char label[8]{};
    const int written = std::snprintf(label, sizeof(label), "WF%03u", static_cast<unsigned>(code));
    std::string text = written > 0 ? std::string(label) : std::string("WF???");
    return text;
}

std::string_view conversion_code_name(ConversionCode code) noexcept {
    switch (code) {
    case ConversionCode::file_unreadable: return "file_unreadable";
    case ConversionCode::file_too_large: return "file_too_large";
    case ConversionCode::project_empty: return "project_empty";
    case ConversionCode::form_not_found: return "form_not_found";
    case ConversionCode::csharp_lexical: return "csharp_lexical";
    case ConversionCode::initialize_component_missing: return "initialize_component_missing";
    case ConversionCode::statement_skipped: return "statement_skipped";
    case ConversionCode::expression_too_deep: return "expression_too_deep";
    case ConversionCode::resx_invalid: return "resx_invalid";
    case ConversionCode::limit_reached: return "limit_reached";
    case ConversionCode::type_unmapped: return "type_unmapped";
    case ConversionCode::type_converted_as_base: return "type_converted_as_base";
    case ConversionCode::property_unmapped: return "property_unmapped";
    case ConversionCode::property_not_admitted: return "property_not_admitted";
    case ConversionCode::property_to_code: return "property_to_code";
    case ConversionCode::presentation_to_theme: return "presentation_to_theme";
    case ConversionCode::value_unconverted: return "value_unconverted";
    case ConversionCode::event_unmapped: return "event_unmapped";
    case ConversionCode::resource_missing: return "resource_missing";
    case ConversionCode::resource_binary: return "resource_binary";
    case ConversionCode::binding_unconverted: return "binding_unconverted";
    case ConversionCode::identity_respelled: return "identity_respelled";
    case ConversionCode::known_divergence: return "known_divergence";
    case ConversionCode::handler_missing: return "handler_missing";
    case ConversionCode::call_to_code: return "call_to_code";
    case ConversionCode::auto_scale_dropped: return "auto_scale_dropped";
    case ConversionCode::layout_approximated: return "layout_approximated";
    }
    return "unknown";
}

std::string_view conversion_severity_name(ConversionSeverity severity) noexcept {
    switch (severity) {
    case ConversionSeverity::error: return "error";
    case ConversionSeverity::warning: return "warning";
    case ConversionSeverity::note: return "note";
    }
    return "error";
}

std::string conversion_diagnostic_line(const ConversionDiagnostic& diagnostic) {
    std::string line = diagnostic.file;
    line.push_back(':');
    line.append(std::to_string(diagnostic.line));
    line.push_back(':');
    line.append(std::to_string(diagnostic.column));
    line.append(": ");
    line.append(conversion_severity_name(conversion_severity(diagnostic.code)));
    line.append(": ");
    line.append(conversion_code_label(diagnostic.code));
    line.push_back(' ');
    line.append(conversion_code_name(diagnostic.code));
    line.append(": ");
    if (!diagnostic.subject.empty()) {
        line.append(diagnostic.subject);
        line.append(": ");
    }
    line.append(diagnostic.detail);
    return line;
}

} // namespace gui_forms::winforms_converter
