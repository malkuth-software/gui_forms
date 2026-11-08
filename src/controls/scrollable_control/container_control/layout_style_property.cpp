// The layout style properties' reading and refusal (layout_style_property.hpp).

#include "layout_style_property.hpp"

#include "../../../core/layout/style_reader/layout_style_reader.hpp"

#include <span>
#include <stdexcept>

namespace gui_forms::layout_style_property {

namespace {

// One diagnostic as a refusal's clause: "W101 unknown_property at 1:9 (float)".
void append_diagnostic(std::string& message, const theme_language::ThemeDiagnostic& diagnostic) {
    message.append(theme_language::diagnostic_code_name(diagnostic.code));
    message.append(" at ");
    message.append(std::to_string(diagnostic.position.line.value));
    message.push_back(':');
    message.append(std::to_string(diagnostic.position.column.value));
    if (!diagnostic.property.empty()) {
        message.append(" (");
        message.append(diagnostic.property);
        message.push_back(')');
    }
    if (!diagnostic.detail.empty()) {
        message.append(": ");
        message.append(diagnostic.detail);
    }
}

} // namespace

LayoutStyle read(std::string_view text, const LayoutStyle& base, std::string_view property) {
    const theme_language::ThemeSource source{property, text};
    theme_language::ThemeDiagnosticList diagnostics{};
    layout_style::LayoutStyleReader reader{};
    LayoutStyle style = base;
    const layout_style::StyleReadOutcome outcome = reader.read_inline_style(source, style, diagnostics);
    const std::span<const theme_language::ThemeDiagnostic> records = diagnostics.records();
    if (outcome == layout_style::StyleReadOutcome::applied && records.empty()) return style;
    std::string message(property);
    message.append(" refuses its style");
    for (const theme_language::ThemeDiagnostic& diagnostic : records) {
        message.append("; ");
        append_diagnostic(message, diagnostic);
    }
    throw std::invalid_argument(message);
}

void require_admitted(const LayoutStyle& read, const LayoutStyle& kept, std::string_view property,
                      std::string_view guidance) {
    if (read == kept) return;
    std::string message(property);
    message.append(" does not admit ");
    message.append(layout_style::describe_layout_style(read, kept));
    if (!guidance.empty()) {
        message.append("; ");
        message.append(guidance);
    }
    throw std::invalid_argument(message);
}

std::string describe(const LayoutStyle& style, const LayoutStyle& base) {
    std::string text = layout_style::describe_layout_style(style, base);
    return text;
}

BoxSides<bool> keep_auto_margins(const LayoutStyle& read, LayoutStyle& kept) {
    BoxSides<bool> automatic{};
    automatic.top = read.margin.top.automatic;
    automatic.right = read.margin.right.automatic;
    automatic.bottom = read.margin.bottom.automatic;
    automatic.left = read.margin.left.automatic;
    kept.margin = auto_margin_sides(automatic);
    return automatic;
}

BoxSides<AutoLengthPercentage> auto_margin_sides(const BoxSides<bool>& auto_margin) noexcept {
    BoxSides<AutoLengthPercentage> sides{};
    sides.top.automatic = auto_margin.top;
    sides.right.automatic = auto_margin.right;
    sides.bottom.automatic = auto_margin.bottom;
    sides.left.automatic = auto_margin.left;
    return sides;
}

} // namespace gui_forms::layout_style_property
