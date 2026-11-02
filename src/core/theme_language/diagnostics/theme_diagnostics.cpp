#include "theme_diagnostics.hpp"

#include <utility>

namespace gui_forms::theme_language {

namespace {

// Appends `field` to `line`, writing '%', '|' and control bytes as '%XX'.
void append_log_field(std::string& line, std::string_view field) {
    static constexpr char hex_digits[] = "0123456789ABCDEF";
    for (const char byte : field) {
        const unsigned char value = static_cast<unsigned char>(byte);
        const bool escaped = value == '%' || value == '|' || value < 0x20U || value == 0x7FU;
        if (!escaped) {
            line.push_back(byte);
            continue;
        }
        line.push_back('%');
        line.push_back(hex_digits[value >> 4U]);
        line.push_back(hex_digits[value & 0x0FU]);
    }
}

} // namespace

DiagnosticSeverity diagnostic_severity(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::invalid_utf8:
    case DiagnosticCode::nul_byte:
    case DiagnosticCode::unterminated_string:
    case DiagnosticCode::unterminated_comment:
    case DiagnosticCode::escape_not_admitted:
    case DiagnosticCode::bad_url:
    case DiagnosticCode::token_not_admitted:
    case DiagnosticCode::number_too_precise:
    case DiagnosticCode::number_out_of_range:
    case DiagnosticCode::unexpected_token:
    case DiagnosticCode::unbalanced_block:
    case DiagnosticCode::declaration_outside_rule:
    case DiagnosticCode::import_after_rules:
    case DiagnosticCode::important_in_keyframes:
    case DiagnosticCode::nesting_not_admitted:
    case DiagnosticCode::theme_header_misplaced:
    case DiagnosticCode::selector_syntax:
    case DiagnosticCode::combinator_not_admitted:
    case DiagnosticCode::pseudo_class_not_admitted:
    case DiagnosticCode::attribute_selector_not_admitted:
    case DiagnosticCode::value_syntax:
    case DiagnosticCode::value_wrong_kind:
    case DiagnosticCode::value_out_of_range:
    case DiagnosticCode::unit_not_admitted:
    case DiagnosticCode::value_form_not_admitted:
    case DiagnosticCode::keyword_unknown:
    case DiagnosticCode::too_many_layers:
    case DiagnosticCode::expression_invalid:
    case DiagnosticCode::variable_undefined:
    case DiagnosticCode::variable_cycle:
    case DiagnosticCode::keyframes_undefined:
    case DiagnosticCode::colour_role_undefined:
    case DiagnosticCode::variable_cannot_vary:
    case DiagnosticCode::resource_unknown:
    case DiagnosticCode::sheet_too_large:
    case DiagnosticCode::too_many_rules:
    case DiagnosticCode::too_many_declarations:
    case DiagnosticCode::nesting_too_deep:
    case DiagnosticCode::string_too_long:
    case DiagnosticCode::too_many_imports:
    case DiagnosticCode::expression_over_budget:
    case DiagnosticCode::too_many_variable_combinations:
    case DiagnosticCode::too_many_is_arguments:
    case DiagnosticCode::selector_too_long:
    case DiagnosticCode::too_many_custom_properties:
    case DiagnosticCode::substitution_too_long:
    case DiagnosticCode::lowered_sheet_too_large:
    case DiagnosticCode::too_many_keyframes:
    case DiagnosticCode::too_many_scoped_variables:
    case DiagnosticCode::manifest_invalid:
    case DiagnosticCode::file_hash_mismatch:
    case DiagnosticCode::import_not_installed:
    case DiagnosticCode::import_hash_mismatch:
    case DiagnosticCode::import_cycle:
    case DiagnosticCode::published_value_invalid:
    case DiagnosticCode::engine_too_old:
    case DiagnosticCode::reserved_input_set:
    case DiagnosticCode::not_yet_built:
        return DiagnosticSeverity::error;
    case DiagnosticCode::unknown_property:
    case DiagnosticCode::unknown_primitive:
    case DiagnosticCode::unknown_function:
    case DiagnosticCode::unknown_at_rule:
    case DiagnosticCode::unknown_media_feature:
    case DiagnosticCode::unknown_selector_name:
    case DiagnosticCode::property_not_admitted:
    case DiagnosticCode::primitive_over_budget:
    case DiagnosticCode::primitive_cost_unbounded:
    case DiagnosticCode::needs_newer_engine:
    case DiagnosticCode::keyword_from_newer_engine:
    case DiagnosticCode::literal_colour_unremappable:
    case DiagnosticCode::inline_with_theme:
    case DiagnosticCode::icon_unknown:
    case DiagnosticCode::icon_not_overridable:
    case DiagnosticCode::icon_refused:
        return DiagnosticSeverity::warning;
    }
    // A value outside the enumeration is treated as the stricter class.
    return DiagnosticSeverity::error;
}

std::string_view diagnostic_code_name(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::invalid_utf8: return "invalid_utf8";
    case DiagnosticCode::nul_byte: return "nul_byte";
    case DiagnosticCode::unterminated_string: return "unterminated_string";
    case DiagnosticCode::unterminated_comment: return "unterminated_comment";
    case DiagnosticCode::escape_not_admitted: return "escape_not_admitted";
    case DiagnosticCode::bad_url: return "bad_url";
    case DiagnosticCode::token_not_admitted: return "token_not_admitted";
    case DiagnosticCode::number_too_precise: return "number_too_precise";
    case DiagnosticCode::number_out_of_range: return "number_out_of_range";
    case DiagnosticCode::unexpected_token: return "unexpected_token";
    case DiagnosticCode::unbalanced_block: return "unbalanced_block";
    case DiagnosticCode::declaration_outside_rule: return "declaration_outside_rule";
    case DiagnosticCode::import_after_rules: return "import_after_rules";
    case DiagnosticCode::important_in_keyframes: return "important_in_keyframes";
    case DiagnosticCode::nesting_not_admitted: return "nesting_not_admitted";
    case DiagnosticCode::theme_header_misplaced: return "theme_header_misplaced";
    case DiagnosticCode::selector_syntax: return "selector_syntax";
    case DiagnosticCode::combinator_not_admitted: return "combinator_not_admitted";
    case DiagnosticCode::pseudo_class_not_admitted: return "pseudo_class_not_admitted";
    case DiagnosticCode::attribute_selector_not_admitted: return "attribute_selector_not_admitted";
    case DiagnosticCode::value_syntax: return "value_syntax";
    case DiagnosticCode::value_wrong_kind: return "value_wrong_kind";
    case DiagnosticCode::value_out_of_range: return "value_out_of_range";
    case DiagnosticCode::unit_not_admitted: return "unit_not_admitted";
    case DiagnosticCode::value_form_not_admitted: return "value_form_not_admitted";
    case DiagnosticCode::keyword_unknown: return "keyword_unknown";
    case DiagnosticCode::too_many_layers: return "too_many_layers";
    case DiagnosticCode::expression_invalid: return "expression_invalid";
    case DiagnosticCode::variable_undefined: return "variable_undefined";
    case DiagnosticCode::variable_cycle: return "variable_cycle";
    case DiagnosticCode::keyframes_undefined: return "keyframes_undefined";
    case DiagnosticCode::colour_role_undefined: return "colour_role_undefined";
    case DiagnosticCode::variable_cannot_vary: return "variable_cannot_vary";
    case DiagnosticCode::resource_unknown: return "resource_unknown";
    case DiagnosticCode::sheet_too_large: return "sheet_too_large";
    case DiagnosticCode::too_many_rules: return "too_many_rules";
    case DiagnosticCode::too_many_declarations: return "too_many_declarations";
    case DiagnosticCode::nesting_too_deep: return "nesting_too_deep";
    case DiagnosticCode::string_too_long: return "string_too_long";
    case DiagnosticCode::too_many_imports: return "too_many_imports";
    case DiagnosticCode::expression_over_budget: return "expression_over_budget";
    case DiagnosticCode::too_many_variable_combinations: return "too_many_variable_combinations";
    case DiagnosticCode::too_many_is_arguments: return "too_many_is_arguments";
    case DiagnosticCode::selector_too_long: return "selector_too_long";
    case DiagnosticCode::too_many_custom_properties: return "too_many_custom_properties";
    case DiagnosticCode::substitution_too_long: return "substitution_too_long";
    case DiagnosticCode::lowered_sheet_too_large: return "lowered_sheet_too_large";
    case DiagnosticCode::too_many_keyframes: return "too_many_keyframes";
    case DiagnosticCode::too_many_scoped_variables: return "too_many_scoped_variables";
    case DiagnosticCode::manifest_invalid: return "manifest_invalid";
    case DiagnosticCode::file_hash_mismatch: return "file_hash_mismatch";
    case DiagnosticCode::import_not_installed: return "import_not_installed";
    case DiagnosticCode::import_hash_mismatch: return "import_hash_mismatch";
    case DiagnosticCode::import_cycle: return "import_cycle";
    case DiagnosticCode::published_value_invalid: return "published_value_invalid";
    case DiagnosticCode::engine_too_old: return "engine_too_old";
    case DiagnosticCode::reserved_input_set: return "reserved_input_set";
    case DiagnosticCode::not_yet_built: return "not_yet_built";
    case DiagnosticCode::unknown_property: return "unknown_property";
    case DiagnosticCode::unknown_primitive: return "unknown_primitive";
    case DiagnosticCode::unknown_function: return "unknown_function";
    case DiagnosticCode::unknown_at_rule: return "unknown_at_rule";
    case DiagnosticCode::unknown_media_feature: return "unknown_media_feature";
    case DiagnosticCode::unknown_selector_name: return "unknown_selector_name";
    case DiagnosticCode::property_not_admitted: return "property_not_admitted";
    case DiagnosticCode::primitive_over_budget: return "primitive_over_budget";
    case DiagnosticCode::primitive_cost_unbounded: return "primitive_cost_unbounded";
    case DiagnosticCode::needs_newer_engine: return "needs_newer_engine";
    case DiagnosticCode::keyword_from_newer_engine: return "keyword_from_newer_engine";
    case DiagnosticCode::literal_colour_unremappable: return "literal_colour_unremappable";
    case DiagnosticCode::inline_with_theme: return "inline_with_theme";
    case DiagnosticCode::icon_unknown: return "icon_unknown";
    case DiagnosticCode::icon_not_overridable: return "icon_not_overridable";
    case DiagnosticCode::icon_refused: return "icon_refused";
    }
    return "unknown";
}

std::string clipped_theme_detail(std::string_view text) {
    if (text.size() <= maximum_detail_bytes) return std::string(text);
    std::size_t length = maximum_detail_bytes;
    while (length > 0U && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) --length;
    std::string detail(text.substr(0U, length));
    return detail;
}

std::string diagnostic_code_label(DiagnosticCode code) {
    const DiagnosticSeverity severity = diagnostic_severity(code);
    std::string label = severity == DiagnosticSeverity::error ? "E" : "W";
    // A warning's enumerator value carries 1000 above its number.
    const std::uint16_t value = static_cast<std::uint16_t>(code);
    const std::uint16_t number = static_cast<std::uint16_t>(value % 1000U);
    label += std::to_string(number);
    return label;
}

void ThemeDiagnosticList::add(ThemeDiagnostic diagnostic) {
    const DiagnosticSeverity severity = diagnostic_severity(diagnostic.code);
    if (severity == DiagnosticSeverity::error) {
        ++errors_;
    } else {
        if (warnings_muted_) return;
        ++warnings_;
    }
    if (records_.size() < maximum_diagnostic_records) {
        records_.push_back(std::move(diagnostic));
    } else {
        ++dropped_;
    }
}

void ThemeDiagnosticList::name_context(std::size_t first_record, std::string_view rule, std::string_view property) {
    for (std::size_t index = first_record; index < records_.size(); ++index) {
        ThemeDiagnostic& record = records_[index];
        if (record.rule.empty()) record.rule = std::string(rule);
        if (record.property.empty()) record.property = std::string(property);
    }
}

void ThemeDiagnosticList::clear() noexcept {
    records_.clear();
    errors_ = 0U;
    warnings_ = 0U;
    dropped_ = 0U;
    warnings_muted_ = false;
}

std::string theme_diagnostic_log_line(const ThemeDiagnostic& diagnostic) {
    const DiagnosticSeverity severity = diagnostic_severity(diagnostic.code);
    std::string line = "gui-forms-theme=";
    line += severity == DiagnosticSeverity::error ? "error" : "warning";
    line += "|code:";
    line += diagnostic_code_label(diagnostic.code);
    line += ' ';
    line += diagnostic_code_name(diagnostic.code);
    line += "|file:";
    append_log_field(line, diagnostic.file);
    line += "|line:";
    line += std::to_string(diagnostic.position.line.value);
    line += "|column:";
    line += std::to_string(diagnostic.position.column.value);
    line += "|rule:";
    append_log_field(line, diagnostic.rule);
    line += "|property:";
    append_log_field(line, diagnostic.property);
    line += "|detail:";
    append_log_field(line, diagnostic.detail);
    return line;
}

void write_theme_diagnostics(const ThemeDiagnosticList& diagnostics, std::FILE* stream) {
    const std::span<const ThemeDiagnostic> records = diagnostics.records();
    for (const ThemeDiagnostic& diagnostic : records) {
        const std::string line = theme_diagnostic_log_line(diagnostic);
        std::fputs(line.c_str(), stream);
        std::fputc('\n', stream);
    }
    if (diagnostics.dropped_count() != 0U) {
        const std::string count = std::to_string(diagnostics.dropped_count());
        std::fputs("gui-forms-theme=dropped|count:", stream);
        std::fputs(count.c_str(), stream);
        std::fputc('\n', stream);
    }
}

} // namespace gui_forms::theme_language
