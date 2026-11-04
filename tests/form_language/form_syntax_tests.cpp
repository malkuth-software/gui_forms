// Form documents' syntax (stage FD, phase FD1): the form grammar of the
// theme language's parser and its byte-preserving round trip. Gate: the
// corpus returns identical bytes; every structural defect is located.

#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifndef GUI_FORMS_FORM_CORPUS_DIR
#error "GUI_FORMS_FORM_CORPUS_DIR names the corpus directory"
#endif

namespace {

using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::SyntaxNode;
using gui_forms::theme_language::SyntaxNodeKind;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSheetForm;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;
using gui_forms::theme_language::TokenRange;
using gui_forms::theme_language::TokenizeOutcome;
using gui_forms::theme_language::WriteOutcome;
using gui_forms::theme_language::diagnostic_code_name;
using gui_forms::theme_language::maximum_declarations_per_rule;
using gui_forms::theme_language::maximum_nesting_depth;
using gui_forms::theme_language::parse_theme_sheet;
using gui_forms::theme_language::syntax_node_kind_name;
using gui_forms::theme_language::tokenize_theme_sheet;
using gui_forms::theme_language::write_theme_sheet;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The caller-owned storage one parse of a document fills.
struct Workspace final {
    std::string_view text{};
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeSyntaxTree tree{};
    ThemeDiagnosticList diagnostics{};
};

ParseOutcome parse(Workspace& workspace, std::string_view text) {
    workspace.text = text;
    workspace.diagnostics.clear();
    ThemeSource source{};
    source.file = "test.form";
    source.text = text;
    const TokenizeOutcome tokenized =
        tokenize_theme_sheet(source, workspace.tokens, workspace.lines, workspace.diagnostics);
    require(tokenized == TokenizeOutcome::tokens_cover_sheet, "the document tokenizes");
    const ParseOutcome outcome = parse_theme_sheet(source, workspace.tokens, workspace.lines, workspace.tree,
                                                   workspace.diagnostics, ThemeSheetForm::form_document);
    return outcome;
}

// The bytes of a token range.
std::string_view range_text(const Workspace& workspace, TokenRange range) {
    if (range.first.value >= range.end.value) return {};
    const gui_forms::theme_language::Token& first = workspace.tokens.tokens[range.first.value];
    const gui_forms::theme_language::Token& last = workspace.tokens.tokens[range.end.value - 1U];
    const std::size_t begin = first.offset.value;
    const std::size_t stop = static_cast<std::size_t>(last.offset.value) + last.length.value;
    return workspace.text.substr(begin, stop - begin);
}

bool writes_back(const Workspace& workspace) {
    std::string written{};
    const WriteOutcome outcome = write_theme_sheet(workspace.tree, workspace.tokens, workspace.text, written);
    return outcome == WriteOutcome::written && written == workspace.text;
}

struct ExpectedNode final {
    SyntaxNodeKind kind{};
    std::string_view content{};
    // The node's descendants, counted to its subtree's end.
    std::uint32_t descendants{};
};

void test_structure() {
    const std::string_view document = "form app.main {\n"
                                      "  text: \"Main\";\n"
                                      "  button app.ok { text: \"OK\"; on-click: accept }\n"
                                      "  style { color: red; :hover { opacity: 0.5 } }\n"
                                      "  panel app.side {}\n"
                                      "}\n";
    const std::array<ExpectedNode, 11> expected{{
        {SyntaxNodeKind::sheet, "", 11U},
        {SyntaxNodeKind::object_rule, "form app.main", 10U},
        {SyntaxNodeKind::declaration, "\"Main\"", 0U},
        {SyntaxNodeKind::object_rule, "button app.ok", 2U},
        {SyntaxNodeKind::declaration, "\"OK\"", 0U},
        {SyntaxNodeKind::declaration, "accept", 0U},
        {SyntaxNodeKind::style_block, "style", 4U},
        {SyntaxNodeKind::style_rule, "", 1U},
        {SyntaxNodeKind::declaration, "red", 0U},
        {SyntaxNodeKind::style_rule, ":hover", 1U},
        {SyntaxNodeKind::declaration, "0.5", 0U},
    }};
    Workspace workspace{};
    const ParseOutcome outcome = parse(workspace, document);
    require(outcome == ParseOutcome::tree_complete, "structure: complete");
    require(workspace.diagnostics.records().empty(), "structure: no diagnostics");
    const std::vector<SyntaxNode>& nodes = workspace.tree.nodes;
    // The last object, empty, follows the expected rows.
    require(nodes.size() == expected.size() + 1U, "structure: node count");
    for (std::size_t index = 0U; index < expected.size() && index < nodes.size(); ++index) {
        const SyntaxNode& node = nodes[index];
        const ExpectedNode& entry = expected[index];
        const std::string_view content = index == 0U ? std::string_view{} : range_text(workspace, node.content);
        const std::uint32_t descendants = node.subtree_end.value - static_cast<std::uint32_t>(index) - 1U;
        const bool matches = node.kind == entry.kind && content == entry.content && descendants == entry.descendants;
        if (!matches) {
            std::cerr << "  node " << index << ": " << syntax_node_kind_name(node.kind) << " '" << content
                      << "' " << descendants << '\n';
        }
        require(matches, "structure: node kind, content and subtree");
    }
    if (nodes.size() == expected.size() + 1U) {
        require(nodes.back().kind == SyntaxNodeKind::object_rule &&
                    range_text(workspace, nodes.back().content) == "panel app.side",
                "structure: the empty object");
    }
    require(writes_back(workspace), "structure: writes back");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return text;
}

// Every corpus document parses completely and without diagnostics, and
// gives back its bytes exactly.
void test_corpus() {
    const std::filesystem::path directory(GUI_FORMS_FORM_CORPUS_DIR);
    std::size_t documents = 0U;
    Workspace workspace{};
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".form") continue;
        ++documents;
        const std::string text = read_file(entry.path());
        const ParseOutcome outcome = parse(workspace, text);
        const bool clean = outcome == ParseOutcome::tree_complete && workspace.diagnostics.records().empty();
        if (!clean) {
            std::cerr << "  " << entry.path().filename().string() << '\n';
            for (const ThemeDiagnostic& diagnostic : workspace.diagnostics.records()) {
                std::cerr << "    " << diagnostic_code_name(diagnostic.code) << ' '
                          << diagnostic.position.line.value << ':' << diagnostic.position.column.value << ' '
                          << diagnostic.detail << '\n';
            }
        }
        require(clean, "corpus: parses completely and cleanly");
        require(writes_back(workspace), "corpus: writes back byte for byte");
    }
    require(documents >= 3U, "corpus: the documents are found");
}

// Line endings and a byte order mark pass through untouched.
void test_line_endings() {
    const std::string_view crlf = "form a.b {\r\n  text: \"x\";\r\n  label a.b.c { }\r\n}\r\n";
    Workspace workspace{};
    require(parse(workspace, crlf) == ParseOutcome::tree_complete, "line endings: CRLF parses");
    require(writes_back(workspace), "line endings: CRLF writes back");
    const std::string_view bom = "\xEF\xBB\xBF" "form a.b { }";
    require(parse(workspace, bom) == ParseOutcome::tree_complete, "line endings: BOM parses");
    require(writes_back(workspace), "line endings: BOM writes back");
    require(parse(workspace, "") == ParseOutcome::tree_complete, "line endings: an empty document parses");
    require(writes_back(workspace), "line endings: an empty document writes back");
}

struct Defect final {
    std::string_view text{};
    DiagnosticCode code{};
    std::uint32_t line{};
    std::uint32_t column{};
};

// Each structural defect stops the parse with one error at its place, and
// the stopped tree still writes back the document.
void test_defects() {
    const std::array<Defect, 16> defects{{
        // A declaration outside every object.
        {"text: \"x\";", DiagnosticCode::declaration_outside_rule, 1U, 1U},
        // A prelude without a stable ID, or with two.
        {"form {}", DiagnosticCode::unexpected_token, 1U, 6U},
        {"form a b {}", DiagnosticCode::unexpected_token, 1U, 8U},
        // A stable ID broken by whitespace, or against the segment rule.
        {"form a. b {}", DiagnosticCode::unexpected_token, 1U, 9U},
        {"form A.b {}", DiagnosticCode::unexpected_token, 1U, 6U},
        {"form a..b {}", DiagnosticCode::unexpected_token, 1U, 6U},
        // A type that is not an identifier.
        {"form a {\n  #x a.b {}\n}", DiagnosticCode::unexpected_token, 2U, 3U},
        // An object never closed, or a '}' that closes nothing.
        {"form a {\n  label a.b {\n", DiagnosticCode::unbalanced_block, 2U, 13U},
        {"form a {}\n}", DiagnosticCode::unbalanced_block, 2U, 1U},
        // At-rules are not admitted.
        {"@import \"x\";", DiagnosticCode::nesting_not_admitted, 1U, 1U},
        {"form a { @media x {} }", DiagnosticCode::nesting_not_admitted, 1U, 10U},
        // A bare word in a block.
        {"form a { visible; }", DiagnosticCode::unexpected_token, 1U, 17U},
        // A declaration's value opening a block.
        {"form a { text: x { } }", DiagnosticCode::nesting_not_admitted, 1U, 10U},
        // A style block's declaration after one of its blocks.
        {"form a { style { :hover {} color: red; } }", DiagnosticCode::declaration_outside_rule, 1U, 28U},
        // A stray closer.
        {"form a { ) }", DiagnosticCode::unbalanced_block, 1U, 10U},
        // The end of the document inside a declaration.
        {"form a { text: \"x\"", DiagnosticCode::unbalanced_block, 1U, 8U},
    }};
    Workspace workspace{};
    for (const Defect& defect : defects) {
        const ParseOutcome outcome = parse(workspace, defect.text);
        const std::vector<ThemeDiagnostic> records(workspace.diagnostics.records().begin(),
                                                   workspace.diagnostics.records().end());
        const bool located = outcome == ParseOutcome::stopped_at_error && records.size() == 1U &&
                             records[0].code == defect.code && records[0].position.line.value == defect.line &&
                             records[0].position.column.value == defect.column;
        if (!located) {
            std::cerr << "  '" << defect.text << "':";
            for (const ThemeDiagnostic& record : records) {
                std::cerr << ' ' << diagnostic_code_name(record.code) << ' ' << record.position.line.value << ':'
                          << record.position.column.value;
            }
            std::cerr << '\n';
        }
        require(located, "defects: one error at its place");
        require(writes_back(workspace), "defects: a stopped tree still writes back");
    }
}

// Objects nest as deep as the explicit stack holds, and no deeper.
void test_depth() {
    std::string within{};
    for (std::size_t level = 0U; level < maximum_nesting_depth; ++level) within += "panel a { ";
    for (std::size_t level = 0U; level < maximum_nesting_depth; ++level) within += "} ";
    Workspace workspace{};
    require(parse(workspace, within) == ParseOutcome::tree_complete, "depth: the deepest admitted nesting parses");
    require(writes_back(workspace), "depth: the deepest nesting writes back");

    std::string beyond{};
    for (std::size_t level = 0U; level <= maximum_nesting_depth; ++level) beyond += "panel a { ";
    for (std::size_t level = 0U; level <= maximum_nesting_depth; ++level) beyond += "} ";
    require(parse(workspace, beyond) == ParseOutcome::stopped_at_error, "depth: one more stops");
    const bool too_deep = workspace.diagnostics.records().size() == 1U &&
                          workspace.diagnostics.records()[0].code == DiagnosticCode::nesting_too_deep;
    require(too_deep, "depth: E504");
    require(writes_back(workspace), "depth: the stopped tree writes back");

    // A style block's rule inside the deepest object still writes back.
    std::string styled{};
    for (std::size_t level = 0U; level + 2U < maximum_nesting_depth; ++level) styled += "panel a { ";
    styled += "style { color: red; :hover { opacity: 1 } } ";
    for (std::size_t level = 0U; level + 2U < maximum_nesting_depth; ++level) styled += "} ";
    require(parse(workspace, styled) == ParseOutcome::tree_complete, "depth: a style block at depth parses");
    require(writes_back(workspace), "depth: a style block at depth writes back");
}

// The declaration limit counts per object; nested objects do not add to
// their parent's count.
void test_declaration_limit() {
    std::string full = "form a {\n";
    for (std::size_t index = 0U; index < maximum_declarations_per_rule; ++index) full += "  p: 1;\n";
    full += "  label a.b { p: 1; }\n}\n";
    Workspace workspace{};
    require(parse(workspace, full) == ParseOutcome::tree_complete, "limit: a full object parses");
    std::string over = "form a {\n";
    for (std::size_t index = 0U; index <= maximum_declarations_per_rule; ++index) over += "  p: 1;\n";
    over += "}\n";
    require(parse(workspace, over) == ParseOutcome::stopped_at_error, "limit: one more stops");
    const bool too_many = workspace.diagnostics.records().size() == 1U &&
                          workspace.diagnostics.records()[0].code == DiagnosticCode::too_many_declarations;
    require(too_many, "limit: E503");
}

// A theme sheet does not admit objects: the form grammar is only for form
// documents.
void test_sheet_unchanged() {
    const std::string_view text = "button { label a.b { } }";
    Workspace workspace{};
    workspace.text = text;
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = text;
    static_cast<void>(tokenize_theme_sheet(source, workspace.tokens, workspace.lines, workspace.diagnostics));
    const ParseOutcome outcome =
        parse_theme_sheet(source, workspace.tokens, workspace.lines, workspace.tree, workspace.diagnostics);
    require(outcome == ParseOutcome::stopped_at_error, "sheet: nesting still stops a theme sheet");
}

} // namespace

int main() {
    test_structure();
    test_corpus();
    test_line_endings();
    test_defects();
    test_depth();
    test_declaration_limit();
    test_sheet_unchanged();
    if (failures != 0) {
        std::cerr << failures << " form syntax checks failed\n";
        return 1;
    }
    std::cout << "form syntax: structure, corpus round trip, line endings, defects, depth and limits passed\n";
    return 0;
}
