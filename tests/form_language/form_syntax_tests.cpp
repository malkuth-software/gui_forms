// Form documents' syntax (stage FD, phase FD1; XML since cp34): the
// bounded XML reader's located read, through read_form_syntax. Gate: every
// corpus document reads to the tree read_xml gives, with each element and
// attribute located at its bytes; every reader defect is one located E821
// that leaves nothing behind.

#include "../../src/core/form_language/form_xml/form_xml.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/xml/types/xml_types.hpp"
#include "gui_forms/xml/xml_reader/xml_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef GUI_FORMS_FORM_CORPUS_DIR
#error "GUI_FORMS_FORM_CORPUS_DIR names the corpus directory"
#endif

namespace {

using gui_forms::XmlAttribute;
using gui_forms::XmlElement;
using gui_forms::XmlError;
using gui_forms::XmlReadResult;
using gui_forms::detail::XmlAttributeLocation;
using gui_forms::detail::XmlElementLocation;
using gui_forms::form_language::FormSyntax;
using gui_forms::form_language::FormSyntaxOutcome;
using gui_forms::form_language::form_xml_limits;
using gui_forms::form_language::read_form_syntax;
using gui_forms::theme_language::ByteOffset;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::SourcePosition;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;
using gui_forms::theme_language::maximum_sheet_bytes;
using gui_forms::theme_language::parse_theme_sheet;
using gui_forms::theme_language::tokenize_theme_sheet;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The caller-owned storage one read of a document fills. `text` is declared
// before the syntax, whose locations are offsets into it.
struct Workspace final {
    std::string text{};
    FormSyntax syntax{};
    ThemeDiagnosticList diagnostics{};
};

FormSyntaxOutcome read(Workspace& workspace, std::string_view text) {
    workspace.text = std::string(text);
    workspace.diagnostics.clear();
    ThemeSource source{};
    source.file = "test.form";
    source.text = workspace.text;
    const FormSyntaxOutcome outcome = read_form_syntax(source, workspace.syntax, workspace.diagnostics);
    return outcome;
}

// One element of the read tree with its position in the locations.
struct WalkedElement final {
    const XmlElement* element{};
    std::size_t index{};
};

// The tree's elements in pre-order: the order of the locations.
std::vector<WalkedElement> pre_order(const XmlElement& root) {
    std::vector<WalkedElement> order{};
    std::vector<const XmlElement*> pending{};
    pending.push_back(&root);
    while (!pending.empty()) {
        const XmlElement* current = pending.back();
        pending.pop_back();
        WalkedElement walked{};
        walked.element = current;
        walked.index = order.size();
        order.push_back(walked);
        const std::vector<XmlElement>& children = (*current).children;
        for (std::size_t remaining = children.size(); remaining > 0U; --remaining) {
            pending.push_back(&children[remaining - 1U]);
        }
    }
    return order;
}

// Whether each element and attribute is located at its own bytes: the
// start tag's '<' and name, the end tag or "/>", each attribute's name and
// quoted value; a value written without references is its decoded value.
bool located_at_bytes(const Workspace& workspace) {
    const std::string_view text = workspace.text;
    const std::vector<WalkedElement> order = pre_order(workspace.syntax.root);
    const std::vector<XmlElementLocation>& locations = workspace.syntax.locations.elements;
    if (order.size() != locations.size()) return false;
    for (const WalkedElement& walked : order) {
        const XmlElement& element = *walked.element;
        const XmlElementLocation& where = locations[walked.index];
        if (text.substr(where.start_offset, 1U) != "<") return false;
        if (text.substr(where.start_offset + 1U, element.name.size()) != element.name) return false;
        if (text[where.start_tag_end - 1U] != '>') return false;
        if (where.empty_element_tag) {
            if (text.substr(where.start_tag_end - 2U, 2U) != "/>") return false;
            if (where.end_tag_offset != where.start_tag_end || where.end_offset != where.start_tag_end) return false;
        } else {
            const std::string end_tag = "</" + element.name;
            if (text.substr(where.end_tag_offset, end_tag.size()) != end_tag) return false;
            if (text[where.end_offset - 1U] != '>') return false;
        }
        if (where.attributes.size() != element.attributes.size()) return false;
        for (std::size_t index = 0U; index < where.attributes.size(); ++index) {
            const XmlAttribute& attribute = element.attributes[index];
            const XmlAttributeLocation& spot = where.attributes[index];
            if (text.substr(spot.name_offset, attribute.name.size()) != attribute.name) return false;
            const char quote = text[spot.value_offset - 1U];
            if ((quote != '"' && quote != '\'') || text[spot.value_end] != quote) return false;
            const std::string_view raw = text.substr(spot.value_offset, spot.value_end - spot.value_offset);
            if (raw.find('&') == std::string_view::npos && raw != attribute.value) return false;
        }
    }
    return true;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return text;
}

// One small document: each offset where the bytes say.
void test_structure() {
    const std::string_view text = "<form xmlns=\"urn:gui-forms:form:1\" id=\"app.main\">\n"
                                  "  <!-- a comment -->\n"
                                  "  <button id=\"app.ok\" text=\"OK\"/>\n"
                                  "  <panel id=\"app.side\"></panel>\n"
                                  "</form>\n";
    Workspace workspace{};
    require(read(workspace, text) == FormSyntaxOutcome::read, "structure: read");
    require(workspace.diagnostics.records().empty(), "structure: no diagnostics");
    const std::vector<XmlElementLocation>& locations = workspace.syntax.locations.elements;
    require(locations.size() == 3U, "structure: three elements located");
    if (locations.size() != 3U) return;
    require(locations[0].start_offset == 0U && locations[0].end_tag_offset == text.find("</form>") &&
                locations[0].end_offset == text.size() - 1U && !locations[0].empty_element_tag,
            "structure: the root's start and end tags");
    require(locations[1].start_offset == text.find("<button") && locations[1].empty_element_tag &&
                locations[1].attributes.size() == 2U && locations[1].attributes[1].name_offset == text.find("text=") &&
                locations[1].attributes[1].value_offset == text.find("OK") &&
                locations[1].attributes[1].value_end == text.find("OK") + 2U,
            "structure: an empty-element tag and its attributes");
    require(locations[2].start_tag_end == locations[2].end_tag_offset && !locations[2].empty_element_tag,
            "structure: an element with no content");
    require(located_at_bytes(workspace), "structure: every location at its bytes");
    require(workspace.syntax.lines.line_count() == 6U, "structure: the line table covers the document");
}

// Every corpus document reads to read_xml's tree, each location at its
// bytes.
void test_corpus() {
    const std::filesystem::path directory(GUI_FORMS_FORM_CORPUS_DIR);
    std::size_t documents = 0U;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".form") continue;
        ++documents;
        const std::string name = entry.path().filename().string();
        Workspace workspace{};
        const FormSyntaxOutcome outcome = read(workspace, read_file(entry.path()));
        require(outcome == FormSyntaxOutcome::read, "corpus: " + name + " reads");
        const XmlReadResult plain = gui_forms::read_xml(workspace.text, form_xml_limits());
        require(static_cast<bool>(plain) && plain.root == workspace.syntax.root,
                "corpus: " + name + " reads to read_xml's tree");
        require(located_at_bytes(workspace), "corpus: " + name + " located at its bytes");
    }
    require(documents >= 3U, "corpus: the corpus is present");
}

// CR LF line ends: the line table and the reader count lines alike.
void test_line_endings() {
    const std::string_view text = "<form xmlns=\"urn:gui-forms:form:1\" id=\"a.b\">\r\n"
                                  "  <label id=\"a.c\"\r\n"
                                  "      text=\"x\"/>\r\n"
                                  "</form>\r\n";
    Workspace workspace{};
    require(read(workspace, text) == FormSyntaxOutcome::read, "line endings: read");
    require(located_at_bytes(workspace), "line endings: located at the bytes");
    if (workspace.syntax.locations.elements.size() != 2U) return;
    const std::size_t attribute = workspace.syntax.locations.elements[1].attributes[1].name_offset;
    const SourcePosition position =
        workspace.syntax.lines.position(workspace.text, ByteOffset{static_cast<std::uint32_t>(attribute)});
    require(position.line.value == 3U && position.column.value == 7U, "line endings: an attribute's line and column");
}

// A reader defect: what it is and where (line, or 0 to leave it unchecked).
struct Defect final {
    std::string_view text{};
    XmlError error{};
    std::uint32_t line{};
};

// Each defect is one E821 whose detail is the reader's XmlError name, and
// the refused read leaves the syntax empty.
void test_defects() {
    const std::array<Defect, 10> defects{{
        {"", XmlError::missing_root_element, 0U},
        {"<form id=\"a\">", XmlError::unexpected_end, 1U},
        {"<a b=\"1\" b=\"2\"/>", XmlError::duplicate_attribute, 1U},
        {"<a><![CDATA[x]]></a>", XmlError::cdata_refused, 1U},
        {"<!DOCTYPE a><a/>", XmlError::doctype_refused, 1U},
        {"<a>\n  <b>\n  </c>\n</a>", XmlError::mismatched_end_tag, 3U},
        {"<a>\n  x<b/>\n</a>", XmlError::mixed_content, 0U},
        {"<a/>\n<b/>", XmlError::content_after_root, 2U},
        {"<a b=\"&nbsp;\"/>", XmlError::unknown_entity, 1U},
        {"<?php echo?><a/>", XmlError::processing_instruction_refused, 1U},
    }};
    for (const Defect& defect : defects) {
        Workspace workspace{};
        // A good read first, so that the refusal is seen to clear it.
        static_cast<void>(read(workspace, "<form xmlns=\"urn:gui-forms:form:1\" id=\"a.b\"/>"));
        const FormSyntaxOutcome outcome = read(workspace, defect.text);
        const std::string label = "defects: " + std::string(gui_forms::xml_error_name(defect.error));
        require(outcome == FormSyntaxOutcome::refused, label + " refused");
        const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
        const bool one = records.size() == 1U && records[0].code == DiagnosticCode::form_xml_invalid &&
                         records[0].detail == gui_forms::xml_error_name(defect.error) &&
                         records[0].file == "test.form";
        require(one, label + " is one E821 naming the reader's error");
        if (one && defect.line != 0U) require(records[0].position.line.value == defect.line, label + " located");
        require(workspace.syntax.root.name.empty() && workspace.syntax.locations.elements.empty() &&
                    workspace.syntax.lines.line_count() == 0U,
                label + " leaves the syntax empty");
    }
}

// Elements nest as deep as XmlLimits::maximum_depth, and no deeper; a
// document over a theme sheet's byte bound is refused before reading.
void test_limits() {
    const std::uint32_t depth = form_xml_limits().maximum_depth;
    std::string within = "<form xmlns=\"urn:gui-forms:form:1\" id=\"a\">";
    for (std::uint32_t level = 1U; level < depth; ++level) within += "<panel id=\"p\">";
    for (std::uint32_t level = 1U; level < depth; ++level) within += "</panel>";
    within += "</form>";
    Workspace workspace{};
    require(read(workspace, within) == FormSyntaxOutcome::read, "limits: the deepest admitted nesting reads");
    require(workspace.syntax.locations.elements.size() == depth, "limits: every level located");

    std::string beyond = "<form xmlns=\"urn:gui-forms:form:1\" id=\"a\">";
    for (std::uint32_t level = 0U; level < depth; ++level) beyond += "<panel id=\"p\">";
    for (std::uint32_t level = 0U; level < depth; ++level) beyond += "</panel>";
    beyond += "</form>";
    require(read(workspace, beyond) == FormSyntaxOutcome::refused, "limits: one level more is refused");
    require(workspace.diagnostics.records().size() == 1U &&
                workspace.diagnostics.records()[0].detail == gui_forms::xml_error_name(XmlError::depth_exceeded),
            "limits: depth_exceeded");

    require(form_xml_limits().maximum_input_bytes == maximum_sheet_bytes, "limits: a sheet's byte bound");
    std::string large = "<form xmlns=\"urn:gui-forms:form:1\" id=\"a\">";
    large.append(maximum_sheet_bytes, ' ');
    large += "</form>";
    require(read(workspace, large) == FormSyntaxOutcome::refused, "limits: an oversized document is refused");
    require(workspace.diagnostics.records().size() == 1U &&
                workspace.diagnostics.records()[0].detail == gui_forms::xml_error_name(XmlError::input_too_large),
            "limits: input_too_large");
}

// A theme sheet still does not admit nesting: forms left the theme parser
// whole.
void test_sheet_unchanged() {
    const std::string text = "button { label a.b { } }";
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = text;
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeSyntaxTree tree{};
    ThemeDiagnosticList diagnostics{};
    static_cast<void>(tokenize_theme_sheet(source, tokens, lines, diagnostics));
    const ParseOutcome outcome = parse_theme_sheet(source, tokens, lines, tree, diagnostics);
    require(outcome == ParseOutcome::stopped_at_error, "sheet: nesting still stops a theme sheet");
}

} // namespace

int main() {
    test_structure();
    test_corpus();
    test_line_endings();
    test_defects();
    test_limits();
    test_sheet_unchanged();
    if (failures != 0) {
        std::cerr << failures << " form syntax checks failed\n";
        return 1;
    }
    std::cout << "form syntax: XML structure, located corpus, line endings, reader defects and limits passed\n";
    return 0;
}
