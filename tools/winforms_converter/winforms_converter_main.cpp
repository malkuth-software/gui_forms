// gui_forms_winforms_converter (stage MCP G7): the converter's tools as a
// program, one subcommand per tool, for people and for scripts.
//
//   gui_forms_winforms_converter list <project>
//   gui_forms_winforms_converter convert <project> <form> [--out DIR] [--form-id ID] [--namespace NS]
//   gui_forms_winforms_converter validate <file.form>
//   gui_forms_winforms_converter diff <before.form> <after.form>
//   gui_forms_winforms_converter preview <file.form> --png OUT.png [--original IN.png] [--scale S]
//   gui_forms_winforms_converter tools
//
// `--json` before the subcommand prints the tool's structured reply
// instead of its text. Exit status: 0 done, 1 refused or not valid, 2 a
// usage error.

#include "converter_tools.hpp"
#include "form_preview.hpp"
#include "winforms_project.hpp"

#include "../form_compiler/form_catalog.hpp"

#include <cstdio>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::winforms_converter::ConverterToolReply;
using gui_forms::winforms_converter::ConverterToolSpec;

int usage() {
    std::fputs("usage: gui_forms_winforms_converter [--json] list|convert|validate|diff|preview|tools ...\n"
               "  list <project>\n"
               "  convert <project> <form> [--out DIR] [--form-id ID] [--namespace NS]\n"
               "  validate <file.form>\n"
               "  diff <before.form> <after.form>\n"
               "  preview <file.form> --png OUT.png [--original IN.png] [--scale S]\n"
               "  tools\n",
               stderr);
    return 2;
}

// The value after `flag` among `arguments` from `first`, or empty.
std::string option(const std::vector<std::string_view>& arguments, std::size_t first, std::string_view flag) {
    for (std::size_t index = first; index + 1U < arguments.size(); ++index) {
        if (arguments[index] == flag) return std::string(arguments[index + 1U]);
    }
    return std::string();
}

bool read_file(std::string_view file, std::string& text) {
    gui_forms::winforms_converter::ConversionDiagnosticList diagnostics{};
    const bool read = gui_forms::winforms_converter::read_source_file(file, text, diagnostics);
    for (const gui_forms::winforms_converter::ConversionDiagnostic& record : diagnostics.records()) {
        const std::string line = gui_forms::winforms_converter::conversion_diagnostic_line(record);
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    return read;
}

int print(const ConverterToolReply& reply, bool json) {
    const std::string& shown = json ? reply.json : reply.text;
    std::fwrite(shown.data(), 1U, shown.size(), stdout);
    if (json) std::fputc('\n', stdout);
    const int status = reply.succeeded ? 0 : 1;
    return status;
}

int print_specs() {
    const std::span<const ConverterToolSpec> specs = gui_forms::winforms_converter::converter_tool_specs();
    std::string out = "[";
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        const ConverterToolSpec& spec = specs[index];
        if (index != 0U) out.push_back(',');
        out.append("{\"name\":" + gui_forms::winforms_converter::json_string(spec.name) +
                   ",\"title\":" + gui_forms::winforms_converter::json_string(spec.title) +
                   ",\"description\":" + gui_forms::winforms_converter::json_string(spec.description) +
                   ",\"inputSchema\":" + std::string(spec.input_schema) + ",\"annotations\":{\"readOnlyHint\":" +
                   (spec.read_only ? "true" : "false") + ",\"destructiveHint\":" +
                   (spec.destructive ? "true" : "false") + "}}");
    }
    out.append("]\n");
    std::fwrite(out.data(), 1U, out.size(), stdout);
    return 0;
}

int run(const std::vector<std::string_view>& arguments) {
    std::size_t at = 0U;
    bool json = false;
    if (at < arguments.size() && arguments[at] == "--json") {
        json = true;
        at = at + 1U;
    }
    if (at >= arguments.size()) return usage();
    const std::string_view command = arguments[at];
    const std::size_t rest = at + 1U;
    const std::size_t count = arguments.size() - rest;
    if (command == "tools") return print_specs();
    if (command == "list" && count >= 1U) {
        gui_forms::winforms_converter::ListFormsRequest request{};
        request.project = std::string(arguments[rest]);
        return print(gui_forms::winforms_converter::run_list_forms(request), json);
    }
    const gui_forms::form_language::FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    if (command == "convert" && count >= 2U) {
        gui_forms::winforms_converter::ConvertRequest request{};
        request.project = std::string(arguments[rest]);
        request.form = std::string(arguments[rest + 1U]);
        request.output_directory = option(arguments, rest, "--out");
        request.options.form_id = option(arguments, rest, "--form-id");
        request.options.csharp_namespace = option(arguments, rest, "--namespace");
        return print(gui_forms::winforms_converter::run_convert(request, schema), json);
    }
    if (command == "validate" && count >= 1U) {
        gui_forms::winforms_converter::ValidateRequest request{};
        request.file = std::string(arguments[rest]);
        if (!read_file(request.file, request.document)) return 1;
        return print(gui_forms::winforms_converter::run_validate(request, schema), json);
    }
    if (command == "diff" && count >= 2U) {
        gui_forms::winforms_converter::DiffRequest request{};
        if (!read_file(arguments[rest], request.before) || !read_file(arguments[rest + 1U], request.after)) return 1;
        return print(gui_forms::winforms_converter::run_diff(request), json);
    }
    if (command == "preview" && count >= 1U) {
        gui_forms::winforms_converter::PreviewRequest request{};
        request.file = std::string(arguments[rest]);
        request.output_png = option(arguments, rest, "--png");
        request.original_png = option(arguments, rest, "--original");
        const std::string scale = option(arguments, rest, "--scale");
        if (!scale.empty()) request.scale = std::stod(scale);
        if (request.output_png.empty()) return usage();
        if (!read_file(request.file, request.document)) return 1;
        return print(gui_forms::winforms_converter::run_preview(request, schema), json);
    }
    return usage();
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> arguments{};
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    try {
        return run(arguments);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "gui_forms_winforms_converter: %s\n", error.what());
        return 2;
    }
}
