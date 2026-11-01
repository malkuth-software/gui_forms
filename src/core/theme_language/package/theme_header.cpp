#include "theme_header.hpp"

#include "theme_manifest.hpp"

#include <string_view>

namespace gui_forms::theme_language {
namespace {

// Which descriptor a header declaration names.
enum class HeaderDescriptor : std::uint8_t { version, engine, author, provenance, unknown };

HeaderDescriptor descriptor_named(std::string_view name) noexcept {
    if (equals_ascii_folded(name, "version")) return HeaderDescriptor::version;
    if (equals_ascii_folded(name, "engine")) return HeaderDescriptor::engine;
    if (equals_ascii_folded(name, "author")) return HeaderDescriptor::author;
    if (equals_ascii_folded(name, "provenance")) return HeaderDescriptor::provenance;
    return HeaderDescriptor::unknown;
}

std::string_view text_of(const ThemeSource& source, const Token& token) noexcept {
    const std::string_view text = source.text.substr(token.offset.value, token.length.value);
    return text;
}

bool trivia(TokenKind kind) noexcept {
    const bool skipped = kind == TokenKind::whitespace || kind == TokenKind::comment;
    return skipped;
}

void report(const ThemeSource& source, SourcePositionCursor& cursor, DiagnosticCode code, const Token& token,
            std::string_view property, std::string detail, ThemeDiagnosticList& diagnostics) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source.file);
    diagnostic.position = cursor.locate(token.offset);
    diagnostic.rule = "@theme";
    diagnostic.property = std::string(property);
    diagnostic.detail = std::move(detail);
    diagnostics.add(std::move(diagnostic));
}

} // namespace

ThemeHeaderOutcome read_theme_header(const ThemeSource& source, const ThemeTokenBuffer& tokens,
                                     const LineStartTable& lines, const ThemeSyntaxTree& tree, ThemeHeader& header,
                                     ThemeDiagnosticList& diagnostics) {
    header = ThemeHeader{};
    const std::vector<SyntaxNode>& nodes = tree.nodes;
    // The header is the first item when present (the parser refuses it
    // anywhere else).
    std::size_t at = 0U;
    for (std::size_t index = 1U; index < nodes.size() && at == 0U; index = nodes[index].subtree_end.value) {
        if (nodes[index].kind == SyntaxNodeKind::theme_header) at = index;
    }
    if (at == 0U) return ThemeHeaderOutcome::read;
    header.present = true;
    SourcePositionCursor cursor(lines, source.text);
    const std::vector<Token>& all = tokens.tokens;
    for (std::size_t index = at + 1U; index < nodes[at].subtree_end.value; index = nodes[index].subtree_end.value) {
        const SyntaxNode& node = nodes[index];
        if (node.kind != SyntaxNodeKind::declaration) continue;
        const Token& name_token = all[node.tokens.first.value];
        const std::string_view name = text_of(source, name_token);
        const HeaderDescriptor descriptor = descriptor_named(name);
        if (descriptor == HeaderDescriptor::unknown) {
            report(source, cursor, DiagnosticCode::unknown_property, name_token, name, std::string(name), diagnostics);
            continue;
        }
        // The value: one string, with trivia around it.
        std::uint32_t value = node.content.first.value;
        std::uint32_t end = node.content.end.value;
        while (value < end && trivia(all[value].kind)) ++value;
        while (end > value && trivia(all[end - 1U].kind)) --end;
        const bool one_string = end == value + 1U && all[value].kind == TokenKind::string &&
                                (node.flags & syntax_flag_important) == 0U;
        if (!one_string) {
            const Token& at_fault = value < end ? all[value] : name_token;
            report(source, cursor, DiagnosticCode::value_syntax, at_fault, name, "a descriptor's value is one string",
                   diagnostics);
            return ThemeHeaderOutcome::refused;
        }
        const std::string_view quoted = text_of(source, all[value]);
        const std::string_view written = quoted.substr(1U, quoted.size() - 2U);
        switch (descriptor) {
        case HeaderDescriptor::version: header.version = std::string(written); break;
        case HeaderDescriptor::author: header.author = std::string(written); break;
        case HeaderDescriptor::provenance: header.provenance = std::string(written); break;
        case HeaderDescriptor::engine: {
            const std::optional<EngineVersion> bound = parse_engine_bound(written);
            if (!bound) {
                report(source, cursor, DiagnosticCode::value_syntax, all[value], name, "an engine bound as \">=0.3\"",
                       diagnostics);
                return ThemeHeaderOutcome::refused;
            }
            header.engine = bound;
            break;
        }
        case HeaderDescriptor::unknown:
        default: break; // Reported above; not reached.
        }
    }
    return ThemeHeaderOutcome::read;
}

} // namespace gui_forms::theme_language
