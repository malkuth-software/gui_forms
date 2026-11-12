// Form documents' editor (stage FD, phase FD4). Gate: a scripted edit of
// one property changes only that property's bytes; adding, removing and
// moving objects change only their own lines; an edit that would make an
// invalid document is refused and leaves the document as it was.

#include "../../src/core/form_language/form_editor/form_editor.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/user_control.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>

namespace {

using gui_forms::form_language::FormDocument;
using gui_forms::form_language::FormEditOutcome;
using gui_forms::form_language::FormSchema;
using gui_forms::form_language::add_form_object;
using gui_forms::form_language::form_control_type;
using gui_forms::form_language::form_root_type;
using gui_forms::form_language::move_form_object;
using gui_forms::form_language::open_form_document;
using gui_forms::form_language::quote_form_string;
using gui_forms::form_language::remove_form_object;
using gui_forms::form_language::reset_form_property;
using gui_forms::form_language::set_form_property;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::ThemeDiagnosticList;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

FormSchema sample_schema() {
    FormSchema schema{};
    schema.types.push_back(form_root_type());
    schema.types.push_back(form_control_type<gui_forms::UserControl>("user_control"));
    schema.types.push_back(form_control_type<gui_forms::Button>("button"));
    schema.types.push_back(form_control_type<gui_forms::CheckBox>("check_box"));
    return schema;
}

const std::string_view base = "form app.window {\n"
                              "  text: \"Main\";\n"
                              "  client-size: 400 300;\n"
                              "  user-control app.body {\n"
                              "    button app.ok { text: \"OK\"; }\n"
                              "    button app.cancel {\n"
                              "      text: \"Cancel\";\n"
                              "    }\n"
                              "  }\n"
                              "}\n";

FormDocument open(const FormSchema& schema, std::string_view text) {
    FormDocument document{};
    ThemeDiagnosticList diagnostics{};
    const FormEditOutcome opened = open_form_document(document, "test.form", std::string(text), schema, diagnostics);
    require(opened == FormEditOutcome::edited, "the document opens");
    return document;
}

// `text` with `expected` where `original` was: the bytes around it equal.
std::string replaced(std::string_view text, std::string_view original, std::string_view expected) {
    std::string result(text);
    const std::size_t at = result.find(original);
    require(at != std::string::npos, "the expected bytes are in the base");
    if (at != std::string::npos) result.replace(at, original.size(), expected);
    return result;
}

void check_text(const FormDocument& document, const std::string& expected, std::string_view label) {
    if (document.text != expected) std::cerr << "  " << label << ":\n" << document.text << "---\n" << expected;
    require(document.text == expected, label);
}

void test_properties() {
    const FormSchema schema = sample_schema();
    ThemeDiagnosticList diagnostics{};

    FormDocument changed = open(schema, base);
    require(set_form_property(changed, "app.cancel", "text", "\"Close\"", schema, diagnostics) ==
                FormEditOutcome::edited,
            "property: a written value replaced");
    check_text(changed, replaced(base, "\"Cancel\"", "\"Close\""), "property: only the value's bytes change");
    require(changed.model.properties.size() == 4U, "property: the model follows the text");

    FormDocument added = open(schema, base);
    require(set_form_property(added, "app.cancel", "tab-index", "2", schema, diagnostics) == FormEditOutcome::edited,
            "property: an unwritten one added");
    check_text(added, replaced(base, "      text: \"Cancel\";\n", "      text: \"Cancel\";\n      tab-index: 2;\n"),
               "property: a line of its own after the last declaration");

    FormDocument inline_added = open(schema, base);
    require(set_form_property(inline_added, "app.ok", "tab-index", "1", schema, diagnostics) ==
                FormEditOutcome::edited,
            "property: added to a one-line block");
    check_text(inline_added, replaced(base, "{ text: \"OK\"; }", "{ text: \"OK\"; tab-index: 1; }"),
               "property: kept on the block's line");

    FormDocument reset = open(schema, base);
    require(reset_form_property(reset, "app.cancel", "text", schema, diagnostics) == FormEditOutcome::edited,
            "property: reset");
    check_text(reset, replaced(base, "      text: \"Cancel\";\n", ""), "property: the reset takes its line");
    require(reset_form_property(reset, "app.cancel", "text", schema, diagnostics) == FormEditOutcome::unchanged,
            "property: resetting an unwritten property changes nothing");

    // Refusals leave the document as it was.
    FormDocument refused = open(schema, base);
    diagnostics.clear();
    require(set_form_property(refused, "app.ok", "text", "12", schema, diagnostics) == FormEditOutcome::refused,
            "property: a value of the wrong kind refused");
    require(diagnostics.records().size() == 1U &&
                diagnostics.records()[0].code == DiagnosticCode::form_value_invalid,
            "property: the refusal's diagnostic");
    require(set_form_property(refused, "app.ok", "text", "\"x\"; } evil x {", schema, diagnostics) ==
                FormEditOutcome::not_admitted,
            "property: a value that ends its declaration is not one value");
    require(set_form_property(refused, "app.ok", "Text", "\"x\"", schema, diagnostics) ==
                FormEditOutcome::not_admitted,
            "property: a name that is not kebab-case");
    require(set_form_property(refused, "app.none", "text", "\"x\"", schema, diagnostics) ==
                FormEditOutcome::object_missing,
            "property: an unknown object");
    check_text(refused, std::string(base), "property: the refused document is unchanged");

    // A text written through quote_form_string: its escapes are the value's
    // bytes, and the model reads the text back.
    FormDocument escaped = open(schema, base);
    const std::string two_lines = quote_form_string("Close\n\"now\"");
    require(set_form_property(escaped, "app.cancel", "text", two_lines, schema, diagnostics) ==
                FormEditOutcome::edited,
            "property: an escaped text written");
    check_text(escaped, replaced(base, "\"Cancel\"", "\"Close\\n\\\"now\\\"\""),
               "property: the escapes are written as the value's bytes");
    bool read_back = false;
    for (const gui_forms::form_language::FormProperty& property : escaped.model.properties) {
        const std::string* text = std::get_if<std::string>(&property.value);
        if (text != nullptr && *text == "Close\n\"now\"") read_back = true;
    }
    require(read_back, "property: the model reads the escaped text back");
    diagnostics.clear();
    require(set_form_property(escaped, "app.ok", "text", "\"bad \\q\"", schema, diagnostics) ==
                FormEditOutcome::refused,
            "property: a refused escape refuses the edit");
    require(diagnostics.records().size() == 1U &&
                diagnostics.records()[0].code == DiagnosticCode::form_escape_invalid,
            "property: the refused escape's diagnostic");

    // A binding is a value in its property's slot (owner rulings FD-6 R1 and
    // R6); an `on-` handler is refused, leaving the document as it was.
    FormDocument bound = open(schema, base);
    require(set_form_property(bound, "app.ok", "command", "bind(accept_)", schema, diagnostics) ==
                FormEditOutcome::edited,
            "binding: written in the Command slot");
    check_text(bound, replaced(base, "{ text: \"OK\"; }", "{ text: \"OK\"; command: bind(accept_); }"),
               "binding: kept on the block's line");
    bool binding_read = false;
    for (const gui_forms::form_language::FormProperty& property : bound.model.properties) {
        if (property.binding && (*property.binding).source == "accept_") binding_read = true;
    }
    require(binding_read, "binding: the model reads the binding");
    diagnostics.clear();
    require(set_form_property(bound, "app.ok", "on-clicked", "accept", schema, diagnostics) ==
                FormEditOutcome::refused,
            "binding: an on- handler refused");
    require(diagnostics.records().size() == 1U &&
                diagnostics.records()[0].code == DiagnosticCode::form_handler_removed,
            "binding: the handler's diagnostic, E816");
    check_text(bound, replaced(base, "{ text: \"OK\"; }", "{ text: \"OK\"; command: bind(accept_); }"),
               "binding: the refused document is unchanged");
}

void test_objects() {
    const FormSchema schema = sample_schema();
    ThemeDiagnosticList diagnostics{};

    FormDocument between = open(schema, base);
    require(add_form_object(between, "app.body", 1U, "check-box", "app.check", schema, diagnostics) ==
                FormEditOutcome::edited,
            "object: added between two");
    check_text(between, replaced(base, "    button app.cancel {\n", "    check-box app.check { }\n    button app.cancel {\n"),
               "object: a line of its own before the next");
    require(set_form_property(between, "app.check", "text", "\"Go\"", schema, diagnostics) == FormEditOutcome::edited,
            "object: a property in an empty block");
    check_text(between,
               replaced(base, "    button app.cancel {\n", "    check-box app.check { text: \"Go\"; }\n    button app.cancel {\n"),
               "object: the empty block's line keeps it");

    FormDocument appended = open(schema, base);
    require(add_form_object(appended, "app.body", 2U, "check-box", "app.check", schema, diagnostics) ==
                FormEditOutcome::edited,
            "object: appended");
    check_text(appended, replaced(base, "    }\n  }\n}\n", "    }\n    check-box app.check { }\n  }\n}\n"),
               "object: after the last child");
    require(add_form_object(appended, "app.body", 9U, "check-box", "app.more", schema, diagnostics) ==
                FormEditOutcome::not_admitted,
            "object: a position past the children");
    require(add_form_object(appended, "app.body", 0U, "check-box", "app.check", schema, diagnostics) ==
                FormEditOutcome::refused,
            "object: a repeated stable ID refused");
    require(add_form_object(appended, "app.body", 0U, "slider", "app.slide", schema, diagnostics) ==
                FormEditOutcome::refused,
            "object: an unknown type refused");

    FormDocument removed = open(schema, base);
    require(remove_form_object(removed, "app.ok", schema, diagnostics) == FormEditOutcome::edited, "object: removed");
    check_text(removed, replaced(base, "    button app.ok { text: \"OK\"; }\n", ""), "object: the removal takes its line");
    require(remove_form_object(removed, "app.window", schema, diagnostics) == FormEditOutcome::not_admitted,
            "object: the root stays");
    require(remove_form_object(removed, "app.body", schema, diagnostics) == FormEditOutcome::refused,
            "object: a form left without its control refused");

    FormDocument moved = open(schema, base);
    require(move_form_object(moved, "app.ok", "app.body", 1U, schema, diagnostics) == FormEditOutcome::edited,
            "object: moved");
    const std::string expected_move = "form app.window {\n"
                                      "  text: \"Main\";\n"
                                      "  client-size: 400 300;\n"
                                      "  user-control app.body {\n"
                                      "    button app.cancel {\n"
                                      "      text: \"Cancel\";\n"
                                      "    }\n"
                                      "    button app.ok { text: \"OK\"; }\n"
                                      "  }\n"
                                      "}\n";
    check_text(moved, expected_move, "object: moved after its sibling, its bytes kept");
    require(move_form_object(moved, "app.body", "app.ok", 0U, schema, diagnostics) == FormEditOutcome::not_admitted,
            "object: not into itself");
}

// A CRLF document's insertions use CRLF.
void test_line_endings() {
    const FormSchema schema = sample_schema();
    ThemeDiagnosticList diagnostics{};
    std::string crlf{};
    for (const char character : base) {
        if (character == '\n') crlf.push_back('\r');
        crlf.push_back(character);
    }
    FormDocument document = open(schema, crlf);
    require(set_form_property(document, "app.cancel", "tab-index", "2", schema, diagnostics) ==
                FormEditOutcome::edited,
            "line endings: added");
    check_text(document,
               replaced(crlf, "      text: \"Cancel\";\r\n", "      text: \"Cancel\";\r\n      tab-index: 2;\r\n"),
               "line endings: the document's own");
}

// Comments beside a value survive a property edit (audit P15): a
// declaration's value content is trimmed of trivia at both ends, so only the
// value's own tokens are spliced.
void test_comments() {
    const FormSchema schema = sample_schema();
    ThemeDiagnosticList diagnostics{};
    const std::string commented = replaced(base, "text: \"Cancel\";", "text: /* before */ \"Cancel\" /* after */;");
    FormDocument document = open(schema, commented);
    require(set_form_property(document, "app.cancel", "text", "\"Close\"", schema, diagnostics) ==
                FormEditOutcome::edited,
            "comments: a commented value replaced");
    check_text(document, replaced(commented, "\"Cancel\"", "\"Close\""), "comments: both comments kept");
}

} // namespace

int main() {
    test_properties();
    test_objects();
    test_line_endings();
    test_comments();
    if (failures != 0) {
        std::cerr << failures << " form editor checks failed\n";
        return 1;
    }
    std::cout << "form editor: properties set, added and reset, objects added, removed and moved, refusals, line "
                 "endings and comments passed\n";
    return 0;
}
