// The GUI.Forms form compiler (stage FD, phase FD5; owner decision FD-4):
// generates a form document's C++ header with the same parser, model and
// checks the run-time loader uses. Shipped in the SDK.
//
//   gui_forms_form_compiler <document.form> --class <Name> --output <header>
//                           [--namespace <a::b>] [--type <description>]... [--check]
//
// Each --type adds an application type the document may name:
// "<name>,<C++ class>,<registry base type>,<header>" (add_application_type
// in form_catalog.hpp), for example
// "note_card,my_app::NoteCard,user_control,my_app/note_card.hpp".
//
// Writes the header only when its bytes change, so a build that runs the
// compiler on every configure rebuilds nothing. With --check it writes
// nothing and fails when the header differs from what it would write.
// Diagnostics go to standard error, one per line, located by file, line
// and column. Exit status: 0 done, 1 refused or stale, 2 usage or I/O.

#include "form_catalog.hpp"

#include "../../src/core/form_language/form_generator/form_generator.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"

#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::form_language::FormGenerateOutcome;
using gui_forms::form_language::FormGeneratorOptions;
using gui_forms::form_language::FormModel;
using gui_forms::form_language::FormModelOutcome;
using gui_forms::form_language::FormSchema;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSheetForm;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;

constexpr int exit_done = 0;
constexpr int exit_refused = 1;
constexpr int exit_usage = 2;

struct Arguments final {
    std::string input{};
    std::string output{};
    FormGeneratorOptions options{};
    // The --type descriptions, in command-line order.
    std::vector<std::string> types{};
    bool check{};
};

void print_usage() {
    std::cerr << "usage: gui_forms_form_compiler <document.form> --class <Name> --output <header>\n"
                 "                               [--namespace <a::b>] [--type <name>,<class>,<base>,<header>]...\n"
                 "                               [--derived] [--model <a::Type>] [--model-header <header>]\n"
                 "                               [--check]\n";
}

// Reads the command line; none when it is malformed.
std::optional<Arguments> read_arguments(std::span<char* const> words) {
    Arguments arguments{};
    for (std::size_t index = 1U; index < words.size(); ++index) {
        const std::string_view word = words[index];
        const bool has_value = index + 1U < words.size();
        if (word == "--check") {
            arguments.check = true;
        } else if (word == "--class" && has_value) {
            ++index;
            arguments.options.class_name = words[index];
        } else if (word == "--namespace" && has_value) {
            ++index;
            arguments.options.namespace_name = words[index];
        } else if (word == "--type" && has_value) {
            ++index;
            arguments.types.emplace_back(words[index]);
        } else if (word == "--derived") {
            arguments.options.derived = true;
        } else if (word == "--model" && has_value) {
            ++index;
            arguments.options.derived = true;
            arguments.options.model_type = words[index];
        } else if (word == "--model-header" && has_value) {
            ++index;
            arguments.options.model_header = words[index];
        } else if (word == "--output" && has_value) {
            ++index;
            arguments.output = words[index];
        } else if (!word.starts_with("--") && arguments.input.empty()) {
            arguments.input = std::string(word);
        } else {
            return std::nullopt;
        }
    }
    if (arguments.input.empty() || arguments.output.empty() || arguments.options.class_name.empty()) {
        return std::nullopt;
    }
    return arguments;
}

// The whole file's bytes, or none when it cannot be read.
std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::ostringstream contents{};
    contents << stream.rdbuf();
    if (stream.bad()) return std::nullopt;
    std::string text = contents.str();
    return text;
}

// The file name without its directories, as the generated header cites it.
std::string file_name(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    if (separator == std::string::npos) return path;
    std::string name = path.substr(separator + 1U);
    return name;
}

// One line per diagnostic, in the form compilers and editors read:
// `file:line:column: error: E812 form_generation_refused: rule property: detail`.
void print_diagnostics(const ThemeDiagnosticList& diagnostics) {
    for (const ThemeDiagnostic& record : diagnostics.records()) {
        const bool error = gui_forms::theme_language::diagnostic_severity(record.code) ==
                           gui_forms::theme_language::DiagnosticSeverity::error;
        std::cerr << record.file << ':' << record.position.line.value << ':' << record.position.column.value << ": "
                  << (error ? "error" : "warning") << ": "
                  << gui_forms::theme_language::diagnostic_code_label(record.code) << ' '
                  << gui_forms::theme_language::diagnostic_code_name(record.code) << ": " << record.rule;
        if (!record.property.empty()) std::cerr << ' ' << record.property;
        std::cerr << ": " << record.detail << '\n';
    }
}

// Parses, checks and generates; returns the exit status.
int compile(const Arguments& arguments) {
    const std::optional<std::string> text = read_file(arguments.input);
    if (!text) {
        std::cerr << arguments.input << ": cannot be read\n";
        return exit_usage;
    }
    FormGeneratorOptions options = arguments.options;
    options.source_name = file_name(arguments.input);
    ThemeSource source{};
    source.file = arguments.input;
    source.text = *text;
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeSyntaxTree tree{};
    ThemeDiagnosticList diagnostics{};
    static_cast<void>(gui_forms::theme_language::tokenize_theme_sheet(source, tokens, lines, diagnostics));
    const ParseOutcome parsed = gui_forms::theme_language::parse_theme_sheet(source, tokens, lines, tree, diagnostics,
                                                                             ThemeSheetForm::form_document);
    if (parsed != ParseOutcome::tree_complete || diagnostics.error_count() != 0U) {
        print_diagnostics(diagnostics);
        return exit_refused;
    }
    FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    for (const std::string& type : arguments.types) gui_forms::form_compiler::add_application_type(schema, type);
    FormModel model{};
    const FormModelOutcome built =
        gui_forms::form_language::build_form_model(source, tokens, lines, tree, schema, model, diagnostics);
    if (built != FormModelOutcome::model_built) {
        print_diagnostics(diagnostics);
        return exit_refused;
    }
    std::string generated{};
    const FormGenerateOutcome outcome = gui_forms::form_language::generate_form_cpp(
        source, tokens, lines, tree, schema, model, options, generated, diagnostics);
    print_diagnostics(diagnostics);
    if (outcome != FormGenerateOutcome::generated) return exit_refused;

    const std::optional<std::string> existing = read_file(arguments.output);
    const bool current = existing && *existing == generated;
    if (arguments.check) {
        if (current) return exit_done;
        std::cerr << arguments.output << ": stale; generate it again from " << arguments.input << '\n';
        return exit_refused;
    }
    if (current) return exit_done;
    std::ofstream stream(arguments.output, std::ios::binary | std::ios::trunc);
    stream << generated;
    stream.close();
    if (!stream) {
        std::cerr << arguments.output << ": cannot be written\n";
        return exit_usage;
    }
    return exit_done;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char* const> words(argv, static_cast<std::size_t>(argc));
    const std::optional<Arguments> arguments = read_arguments(words);
    if (!arguments) {
        print_usage();
        return exit_usage;
    }
    try {
        const int status = compile(*arguments);
        return status;
    } catch (const std::exception& failure) {
        // The program's boundary: a refused option, an application type
        // that cannot be added, or a prototype that could not be made.
        std::cerr << "gui_forms_form_compiler: " << failure.what() << '\n';
        return exit_usage;
    }
}
