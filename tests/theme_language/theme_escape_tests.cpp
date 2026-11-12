// CSS backslash escapes in theme selectors (owner 2026-11-04): a theme
// sheet's tokenizer admits escapes in names, so that a selector can name
// the Gallery's dotted stable IDs (`#gallery\.typography\.heading`); the
// compiler refuses an escape anywhere but a selector, and escapes of 0, a
// surrogate or a value past U+10FFFF; a form document or layout style
// (NameEscapes::refused, the default) refuses every backslash, as before.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/token_box.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using theme_language::CompileOutcome;
using theme_language::DiagnosticCode;
using theme_language::LineStartTable;
using theme_language::NameEscapes;
using theme_language::SelectorVocabulary;
using theme_language::ThemeCompileInput;
using theme_language::ThemeCompiler;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;
using theme_language::ThemeTokenBuffer;
using theme_language::Token;
using theme_language::TokenKind;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The tokens of `text` that are neither whitespace nor comments, and the
// diagnostics.
struct Tokens final {
    std::vector<Token> tokens{};
    std::vector<std::string> names{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

Tokens tokenize(std::string_view text, NameEscapes escapes) {
    ThemeSource source{};
    source.file = "escapes.theme.css";
    source.text = text;
    ThemeTokenBuffer buffer{};
    LineStartTable lines{};
    ThemeDiagnosticList diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, buffer, lines, diagnostics, escapes));
    Tokens result{};
    for (const Token& token : buffer.tokens) {
        if (token.kind == TokenKind::whitespace || token.kind == TokenKind::comment) continue;
        result.tokens.push_back(token);
        std::string name{};
        theme_language::append_decoded_name(text.substr(token.offset.value, token.length.value), name);
        result.names.push_back(name);
    }
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    return result;
}

bool refused_escape(const Tokens& tokens) {
    const bool refused = tokens.diagnostics.size() == 1U &&
                         tokens.diagnostics[0].code == DiagnosticCode::escape_not_admitted;
    return refused;
}

void test_tokenizing() {
    const Tokens dotted = tokenize("#gallery\\.typography\\.heading", NameEscapes::in_names);
    require(dotted.diagnostics.empty() && dotted.tokens.size() == 1U && dotted.tokens[0].kind == TokenKind::hash,
            "an escaped ID is one hash");
    require((dotted.tokens[0].flags & theme_language::token_flag_escaped) != 0U &&
                (dotted.tokens[0].flags & theme_language::token_flag_hash_identifier) != 0U,
            "flagged escaped, and a name");
    require(dotted.names[0] == "#gallery.typography.heading", "its name has the dots");

    const Tokens hex = tokenize("\\31 st \\E9t\\00E9 x", NameEscapes::in_names);
    require(hex.diagnostics.empty() && hex.tokens.size() == 2U && hex.tokens[0].kind == TokenKind::ident,
            "hex escapes, each ended by one space");
    require(hex.names.size() == 2U && hex.names[0] == "1st" && hex.names[1] == "\xC3\xA9t\xC3\xA9x",
            "decode to their code points");
    const Tokens crlf = tokenize("a\\41\r\nb", NameEscapes::in_names);
    require(crlf.tokens.size() == 1U && crlf.names[0] == "aAb", "CR LF after a hex escape is one whitespace");
    const Tokens plain = tokenize("plain", NameEscapes::in_names);
    require(plain.tokens.size() == 1U && plain.tokens[0].flags == 0U, "a name without escapes is not flagged");

    require(refused_escape(tokenize("#gallery\\.heading", NameEscapes::refused)), "refused where not admitted");
    const char* const invalid[] = {"a\\0 b", "a\\D800 b", "a\\110000 b"};
    for (const char* const text : invalid) {
        require(refused_escape(tokenize(text, NameEscapes::in_names)), std::string("escape refused: ") + text);
    }
    const Tokens lone = tokenize("a \\\nb", NameEscapes::in_names);
    require(refused_escape(lone), "a backslash before a line break is no escape");
}

struct Compilation final {
    CompileOutcome outcome{CompileOutcome::refused};
    ThemeRuleSheet sheet{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

Compilation compile(std::string_view text) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeSource source{};
    source.file = "escapes.theme.css";
    source.text = text;
    const ThemeCompileInput input{.source = source,
                                  .registry = registry,
                                  .vocabulary = SelectorVocabulary{},
                                  .published_values = {},
                                  .resources = {},
                                  .images = {},
                                  .requires_engine = current_engine_version};
    ThemeCompiler compiler{};
    ThemeDiagnosticList diagnostics{};
    Compilation result{};
    result.outcome = compiler.compile(input, result.sheet, diagnostics);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    return result;
}

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
};

void test_selectors() {
    const Compilation dotted = compile("#gallery\\.typography\\.heading, .wide\\-row { opacity: 0.5 }");
    require(dotted.outcome == CompileOutcome::compiled && dotted.diagnostics.empty() &&
                dotted.sheet.rules.size() == 2U,
            "escaped names compile in selectors");
    if (dotted.sheet.rules.size() == 2U) {
        require(dotted.sheet.rules[0].selector.stable_id == "gallery.typography.heading", "the ID is decoded");
        require(dotted.sheet.rules[1].selector.classes.size() == 1U &&
                    dotted.sheet.rules[1].selector.classes[0] == "wide-row",
                "and the class");
    }
    const char* const outside[] = {
        "#pad { opacity: \\30 .5 }",
        "#pad { op\\61 city: 0.5 }",
        "@me\\64 ia (prefers-color-scheme: dark) { #pad { opacity: 0.5 } }",
    };
    for (const char* const text : outside) {
        const Compilation compiled = compile(text);
        const bool refused = compiled.outcome == CompileOutcome::refused && !compiled.diagnostics.empty() &&
                             compiled.diagnostics.back().code == DiagnosticCode::escape_not_admitted;
        require(refused, std::string("an escape outside a selector is refused: ") + text);
    }

    // The rule reaches the control whose ID has the dots.
    const std::unique_ptr<ThemeDefinition> definition =
        std::make_unique<ThemeDefinition>(windows_professional_theme_definition());
    (*definition).id = "escapes";
    (*definition).rules = compile("#gallery\\.typography\\.heading { outline: 3px solid }").sheet;
    const std::shared_ptr<const Theme> theme = Theme::create(*definition);
    const std::shared_ptr<Pad> heading = make_control<Pad>(StableId("gallery.typography.heading"));
    const std::shared_ptr<Pad> other = make_control<Pad>(StableId("gallery.typography.body"));
    const ResolvedControlRecipe matched = (*theme).resolve(*heading, ControlVisualRole::button, ControlVisualContext{});
    const ResolvedControlRecipe unmatched = (*theme).resolve(*other, ControlVisualRole::button, ControlVisualContext{});
    require(matched.recipe().focus_width == 3.0 && unmatched.recipe().focus_width != 3.0,
            "the escaped ID selects the dotted control alone");
}

} // namespace

int main() {
    test_tokenizing();
    test_selectors();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme escape tests passed\n";
    return 0;
}
