// gui_forms_d15_rewrite: the D15 vocabulary rewriter (README.md).
//
// Main sequence: read the options; choose the translation units; run the
// plan phase over every unit in parallel (entities and edges); close the
// plan (the converted set); run the rewrite phase over every unit (edits
// and residue); resolve and apply or check the edits; scan the files no
// configuration compiled; write the report.

#include "analysis.hpp"
#include "closure.hpp"
#include "edits.hpp"
#include "model.hpp"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

llvm::cl::OptionCategory rewrite_category("GUI.Forms D15 rewriter options");

llvm::cl::opt<std::string> stage_option("stage", llvm::cl::Required,
    llvm::cl::desc("The stage: V1 (optional), V2 (text views), V3 (owned text), V4 (sequences), V5 (ownership)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> mode_option("mode", llvm::cl::init("check"),
    llvm::cl::desc("check (report, exit 1 when files would change), apply (write), or plan (write the plan only)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> source_root_option("source-root", llvm::cl::Required,
    llvm::cl::desc("Absolute path of the gui_forms checkout"), llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> seed_option("seed",
    llvm::cl::desc("A directory whose declarations seed the stage (repeatable; default include/gui_forms)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> seed_exclude_option("seed-exclude",
    llvm::cl::desc("A directory never seeded (repeatable; default include/gui_forms/c)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> scope_option("scope",
    llvm::cl::desc("A directory the rewriter may change (repeatable; default every first-party directory)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> exclude_option("exclude",
    llvm::cl::desc("A directory the rewriter never changes (repeatable; added to the defaults)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> evidence_option("evidence",
    llvm::cl::desc("V2: a directory outside the scope whose units the plan still reads, for refusals only "
                   "(repeatable; default tests; `none` reads none)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> units_option("units",
    llvm::cl::desc("Only parse translation units under this directory (repeatable; a quick partial run)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> only_option("only",
    llvm::cl::desc("Seed only declarations whose qualified name matches this regular expression"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> kinds_option("kinds",
    llvm::cl::desc("Seeded entity kinds: field,variable,parameter,return,alias,shared_base (default per stage)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> propagate_option("propagate-locals", llvm::cl::init("default"),
    llvm::cl::desc("Convert locals that exchange values with converted entities: on, off or default"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> flow_option("flow-edges", llvm::cl::init("default"),
    llvm::cl::desc("Convert first-party declarations that exchange values with converted ones: on, off or default"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> returns_option("allocating-returns", llvm::cl::init("plain"),
    llvm::cl::desc("V3: plain (Utf8String returns) or result (Q1: OperationResult<Utf8String> returns)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<bool> drop_includes_option("drop-standard-includes", llvm::cl::init(false),
    llvm::cl::desc("Remove a standard header a rewritten file no longer names"), llvm::cl::cat(rewrite_category));
llvm::cl::opt<unsigned> jobs_option("jobs", llvm::cl::init(0U),
    llvm::cl::desc("Translation units parsed at once (0: every core)"), llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> report_option("report", llvm::cl::init(""),
    llvm::cl::desc("Write the JSON report here"), llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> residue_option("residue", llvm::cl::init(""),
    llvm::cl::desc("Write the residue, one line per site, here"), llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> plan_output_option("plan-output", llvm::cl::init(""),
    llvm::cl::desc("Write the plan (entities and edges) here"), llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> plan_input_option("plan-input",
    llvm::cl::desc("Read a plan instead of running the plan phase (repeatable: plans of several configurations "
                   "of the same tree are merged)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<std::string> edits_output_option("edits-output", llvm::cl::init(""),
    llvm::cl::desc("Write this configuration's unresolved edits, residue and seen files here"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> edits_input_option("edits-input",
    llvm::cl::desc("Merge edits files instead of parsing (repeatable); with --plan-input the uncompiled scan runs too"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> surface_headers_option("surface-headers",
    llvm::cl::desc("A file listing the consumer surface's headers (or directories), one relative path per line "
                   "(repeatable): only their declarations are seeded, and value flows do not convert other "
                   "non-local declarations"),
    llvm::cl::cat(rewrite_category));
llvm::cl::list<std::string> surface_exports_option("surface-exports",
    llvm::cl::desc("A module interface whose `using gui_forms::Name;` lines name the consumer surface "
                   "(modules/gui_forms.cppm, tools/module_exports.py; repeatable)"),
    llvm::cl::cat(rewrite_category));
llvm::cl::opt<bool> quiet_option("quiet", llvm::cl::init(false),
    llvm::cl::desc("Print only the summary"), llvm::cl::cat(rewrite_category));

const char* const default_scope[] = {"include", "src", "tests", "examples", "tools", "demo", "lab",
                                     "benchmarks", "fuzz", "file_manager_demoboard"};
const char* const default_exclusions[] = {"third_party", "tools/d15_rewrite", "build", ".build",
                                          "src/platform/stxlib_bridge.hpp"};

// Names too common to search for in files no configuration compiled.
const char* const common_names[] = {"value", "values", "text", "name", "size", "data", "items", "index", "count",
                                    "first", "second", "result", "state", "kind", "type", "label", "title",
                                    "color", "colour", "width", "height", "bounds", "position", "source", "target"};

[[nodiscard]] bool parse_stage(llvm::StringRef text, d15::Stage& stage) {
    if (text.equals_insensitive("V1")) stage = d15::Stage::v1_optional;
    else if (text.equals_insensitive("V2")) stage = d15::Stage::v2_text_view;
    else if (text.equals_insensitive("V3")) stage = d15::Stage::v3_owned_text;
    else if (text.equals_insensitive("V4")) stage = d15::Stage::v4_sequences;
    else if (text.equals_insensitive("V5")) stage = d15::Stage::v5_ownership;
    else return false;
    return true;
}

[[nodiscard]] bool parse_kind(llvm::StringRef text, d15::EntityKind& kind) {
    if (text == "field") kind = d15::EntityKind::field;
    else if (text == "variable") kind = d15::EntityKind::variable;
    else if (text == "parameter") kind = d15::EntityKind::parameter;
    else if (text == "return") kind = d15::EntityKind::function_return;
    else if (text == "alias") kind = d15::EntityKind::alias;
    else if (text == "shared_base") kind = d15::EntityKind::shared_base;
    else return false;
    return true;
}

[[nodiscard]] std::string trimmed_directory(llvm::StringRef text) {
    llvm::StringRef value = text.trim();
    while (value.ends_with("/")) value = value.drop_back();
    while (value.starts_with("./")) value = value.drop_front(2U);
    const std::string result(value);
    return result;
}

[[nodiscard]] bool parse_switch(llvm::StringRef text, const bool fallback, bool& value) {
    if (text == "default") {
        value = fallback;
        return true;
    }
    if (text == "on") {
        value = true;
        return true;
    }
    if (text == "off") {
        value = false;
        return true;
    }
    return false;
}

// Reads a list file: one entry per line, blank lines and `#` comments
// skipped. False when the file cannot be read.
[[nodiscard]] bool read_list(const std::string& path, std::vector<std::string>& entries) {
    std::string text;
    if (!d15::read_file(path, text)) return false;
    llvm::SmallVector<llvm::StringRef, 64U> lines;
    llvm::StringRef(text).split(lines, '\n', -1, false);
    for (const llvm::StringRef line : lines) {
        const llvm::StringRef entry = line.trim();
        if (entry.empty() || entry.starts_with("#")) continue;
        entries.push_back(trimmed_directory(entry));
    }
    return true;
}

// Reads the names a module interface exports by using-declaration
// (`using gui_forms::Name;`, as tools/module_exports.py writes them), as
// qualified names. False when the file cannot be read.
[[nodiscard]] bool read_exports(const std::string& path, std::set<std::string>& names) {
    std::string text;
    if (!d15::read_file(path, text)) return false;
    llvm::SmallVector<llvm::StringRef, 256U> lines;
    llvm::StringRef(text).split(lines, '\n', -1, false);
    for (const llvm::StringRef line : lines) {
        llvm::StringRef entry = line.trim();
        if (!entry.consume_front("using ")) continue;
        if (!entry.consume_back(";")) continue;
        entry = entry.trim();
        entry.consume_front("::");
        if (entry.empty() || entry.contains('=') || entry.starts_with("namespace ")) continue;
        names.insert(std::string(entry));
    }
    return true;
}

[[nodiscard]] bool build_options(d15::Options& options, std::string& error) {
    if (!parse_stage(stage_option, options.stage)) {
        error = "unknown --stage " + stage_option;
        return false;
    }
    llvm::SmallString<512U> root(source_root_option);
    llvm::sys::fs::make_absolute(root);
    llvm::sys::path::remove_dots(root, true);
    options.source_root = std::string(root.str());
    while (options.source_root.size() > 1U && options.source_root.back() == '/') options.source_root.pop_back();

    for (const std::string& value : seed_option) options.seed_directories.push_back(trimmed_directory(value));
    if (options.seed_directories.empty()) options.seed_directories.push_back("include/gui_forms");
    for (const std::string& value : seed_exclude_option) options.seed_exclusions.push_back(trimmed_directory(value));
    if (seed_exclude_option.empty()) options.seed_exclusions.push_back("include/gui_forms/c");
    for (const std::string& value : scope_option) options.scope_directories.push_back(trimmed_directory(value));
    if (options.scope_directories.empty()) {
        for (const char* directory : default_scope) options.scope_directories.push_back(directory);
    }
    for (const char* directory : default_exclusions) options.scope_exclusions.push_back(directory);
    for (const std::string& value : exclude_option) options.scope_exclusions.push_back(trimmed_directory(value));
    bool no_evidence = false;
    for (const std::string& value : evidence_option) {
        if (value == "none") {
            no_evidence = true;
            continue;
        }
        options.evidence_directories.push_back(trimmed_directory(value));
    }
    if (evidence_option.empty()) options.evidence_directories.push_back("tests");
    if (no_evidence) options.evidence_directories.clear();
    options.only_pattern = only_option;

    // Per-stage defaults (README.md, "Stages").
    bool propagate = true;
    bool flows = true;
    std::vector<d15::EntityKind> kinds = {d15::EntityKind::field, d15::EntityKind::variable,
                                          d15::EntityKind::parameter, d15::EntityKind::function_return,
                                          d15::EntityKind::alias};
    switch (options.stage) {
    case d15::Stage::v2_text_view:
        propagate = false;
        flows = false;
        kinds = {d15::EntityKind::parameter};
        break;
    case d15::Stage::v3_owned_text:
    case d15::Stage::v4_sequences:
        propagate = false;
        flows = false;
        break;
    case d15::Stage::v5_ownership:
        kinds.push_back(d15::EntityKind::shared_base);
        break;
    default:
        break;
    }
    if (!parse_switch(propagate_option, propagate, options.propagate_locals)) {
        error = "--propagate-locals takes on, off or default";
        return false;
    }
    if (!parse_switch(flow_option, flows, options.flow_edges)) {
        error = "--flow-edges takes on, off or default";
        return false;
    }
    if (!kinds_option.empty()) {
        kinds.clear();
        llvm::SmallVector<llvm::StringRef, 8U> parts;
        llvm::StringRef(kinds_option).split(parts, ',', -1, false);
        for (const llvm::StringRef part : parts) {
            d15::EntityKind kind = d15::EntityKind::field;
            if (!parse_kind(part.trim(), kind)) {
                error = "unknown entity kind in --kinds: " + std::string(part);
                return false;
            }
            kinds.push_back(kind);
        }
    }
    options.seed_kinds.insert(kinds.begin(), kinds.end());
    if (returns_option != "plain" && returns_option != "result") {
        error = "--allocating-returns takes plain or result";
        return false;
    }
    options.allocating_returns_result = returns_option == "result";
    options.drop_standard_includes = drop_includes_option;
    for (const std::string& list : surface_headers_option) {
        if (!read_list(list, options.surface_headers)) {
            error = "cannot read --surface-headers " + list;
            return false;
        }
        options.surface_mode = true;
    }
    for (const std::string& module : surface_exports_option) {
        if (!read_exports(module, options.surface_names)) {
            error = "cannot read --surface-exports " + module;
            return false;
        }
        options.surface_mode = true;
    }
    if (options.surface_mode && options.surface_headers.empty() && options.surface_names.empty()) {
        error = "the consumer surface given is empty";
        return false;
    }
    return true;
}

[[nodiscard]] bool within(const std::string& relative, const std::string& directory) {
    if (relative == directory) return true;
    const bool result = relative.size() > directory.size() && relative.compare(0U, directory.size(), directory) == 0 &&
        relative[directory.size()] == '/';
    return result;
}

[[nodiscard]] bool within_any(const std::string& relative, const std::vector<std::string>& directories) {
    for (const std::string& directory : directories) {
        if (within(relative, directory)) return true;
    }
    return false;
}

[[nodiscard]] bool relative_to_root(const d15::Options& options, llvm::StringRef absolute, std::string& relative) {
    llvm::SmallString<512U> path(absolute);
    llvm::sys::path::remove_dots(path, true);
    const llvm::StringRef normal = path.str();
    const std::string& root = options.source_root;
    if (normal.size() <= root.size() + 1U || !normal.starts_with(root) || normal[root.size()] != '/') return false;
    relative = std::string(normal.drop_front(root.size() + 1U));
    return true;
}

// Counts a unit's errors and keeps the first, without printing them.
class CountingDiagnostics final : public clang::DiagnosticConsumer {
public:
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& information) override {
        clang::DiagnosticConsumer::HandleDiagnostic(level, information);
        if (level < clang::DiagnosticsEngine::Error) return;
        ++errors_;
        if (!first_.empty()) return;
        llvm::SmallString<256U> text;
        information.FormatDiagnostic(text);
        std::string location;
        if (information.hasSourceManager() && information.getLocation().isValid()) {
            const clang::PresumedLoc presumed =
                information.getSourceManager().getPresumedLoc(information.getLocation());
            if (presumed.isValid()) {
                location = std::string(presumed.getFilename()) + ":" + std::to_string(presumed.getLine()) + ": ";
            }
        }
        first_ = location + std::string(text.str());
    }

    [[nodiscard]] unsigned errors() const noexcept { return errors_; }
    [[nodiscard]] const std::string& first() const& noexcept { return first_; }
    const std::string& first() const&& = delete;

private:
    unsigned errors_{0U};
    std::string first_;
};

struct ParseFailure final {
    std::string source;
    unsigned errors{0U};
    std::string first;
};

// Everything the phases produce, merged under one lock. Owned by main.
class Collector final {
public:
    void add(d15::TranslationUnitHarvest& harvest) {
        const std::lock_guard<std::mutex> hold(lock_);
        entities.insert(entities.end(), harvest.entities.begin(), harvest.entities.end());
        edges.insert(edges.end(), harvest.edges.begin(), harvest.edges.end());
        residue.insert(residue.end(), harvest.residue.begin(), harvest.residue.end());
        files_seen.insert(harvest.files_seen.begin(), harvest.files_seen.end());
        edits.merge(harvest);
        if (harvest.errors > 0U) {
            ParseFailure failure;
            failure.source = harvest.source;
            failure.errors = harvest.errors;
            failure.first = harvest.first_error;
            failures.push_back(failure);
        }
        ++units;
    }

    std::vector<d15::EntityFact> entities;
    std::vector<d15::EdgeFact> edges;
    std::vector<d15::Residue> residue;
    std::set<std::string> files_seen;
    d15::EditPlan edits;
    std::vector<ParseFailure> failures;
    std::size_t units{0U};

private:
    std::mutex lock_;
};

// True when the driver would read a precompiled form of `header` given as
// `-include header` (GCC's convention, which clang's driver follows).
[[nodiscard]] bool has_precompiled_sibling(const std::string& header) {
    const bool result = llvm::sys::fs::exists(header + ".pch") || llvm::sys::fs::exists(header + ".gch");
    return result;
}

// The compile command without precompiled headers (defect 6 of the
// 2026-11-16 V1 run): an apply makes the build tree's PCH stale, and a
// unit read through it reports the PCH's errors, not the code's. Drops
// `-include-pch FILE` (plain or as `-Xclang` pairs, CMake's form) and
// turns a plain `-include HEADER` that has a precompiled sibling into the
// cc1 form, so the header is read as text (the PCH's prefix header, which
// CMake also passes, keeps every unit's force-included declarations).
// An ArgumentsAdjuster.
clang::tooling::CommandLineArguments without_precompiled_headers(const clang::tooling::CommandLineArguments& arguments,
                                                                 llvm::StringRef file) {
    (void)file;
    clang::tooling::CommandLineArguments result;
    result.reserve(arguments.size());
    const std::size_t count = arguments.size();
    std::size_t index = 0U;
    while (index < count) {
        const std::string& argument = arguments[index];
        const bool next_exists = index + 1U < count;
        if (argument == "-include-pch") {
            index += 2U;
            continue;
        }
        if (llvm::StringRef(argument).starts_with("-include-pch=")) {
            index += 1U;
            continue;
        }
        if (argument == "-Xclang" && next_exists && arguments[index + 1U] == "-include-pch") {
            index += 2U;
            if (index < count && arguments[index] == "-Xclang") index += 1U;
            index += 1U;
            continue;
        }
        if (argument == "-include" && next_exists && has_precompiled_sibling(arguments[index + 1U])) {
            result.push_back("-Xclang");
            result.push_back("-include");
            result.push_back("-Xclang");
            result.push_back(arguments[index + 1U]);
            index += 2U;
            continue;
        }
        result.push_back(argument);
        index += 1U;
    }
    return result;
}

// One translation unit's analysis, run on a pool thread. Borrows the
// database, input and collector, which main keeps alive until the pool
// has finished.
class UnitTask final {
public:
    UnitTask(const clang::tooling::CompilationDatabase& database, std::string source, const d15::AnalysisInput& input,
             Collector& collector)
        : database_(&database), source_(std::move(source)), input_(&input), collector_(&collector) {}

    void operator()() const {
        d15::TranslationUnitHarvest harvest;
        harvest.source = source_;
        const std::vector<std::string> sources = {source_};
        clang::tooling::ClangTool tool(*database_, sources, std::make_shared<clang::PCHContainerOperations>(),
                                       llvm::vfs::createPhysicalFileSystem());
        // Clang 22's ClangTool has no setRestoreWorkingDir: it restores nothing to set.
        const clang::tooling::ArgumentsAdjuster precompiled_headers_removed(&without_precompiled_headers);
        tool.appendArgumentsAdjuster(precompiled_headers_removed);
        CountingDiagnostics diagnostics;
        tool.setDiagnosticConsumer(&diagnostics);
        std::unique_ptr<clang::tooling::FrontendActionFactory> factory =
            d15::make_action_factory(*input_, harvest);
        const int status = tool.run(factory.get());
        harvest.errors = diagnostics.errors();
        harvest.first_error = diagnostics.first();
        if (status != 0 && harvest.errors == 0U) {
            harvest.errors = 1U;
            harvest.first_error = "the tool could not run this unit";
        }
        (*collector_).add(harvest);
    }

private:
    const clang::tooling::CompilationDatabase* database_;
    std::string source_;
    const d15::AnalysisInput* input_;
    Collector* collector_;
};

void run_phase(const clang::tooling::CompilationDatabase& database, const std::vector<std::string>& sources,
               const d15::AnalysisInput& input, Collector& collector, const unsigned jobs) {
    llvm::ThreadPoolStrategy strategy = llvm::hardware_concurrency(jobs);
    llvm::DefaultThreadPool pool(strategy);
    for (const std::string& source : sources) {
        UnitTask task(database, source, input, collector);
        pool.async(task);
    }
    pool.wait();
}

[[nodiscard]] bool residue_less(const d15::Residue& left, const d15::Residue& right) noexcept {
    if (left.path != right.path) return left.path < right.path;
    if (left.line != right.line) return left.line < right.line;
    if (left.column != right.column) return left.column < right.column;
    return left.reason < right.reason;
}

[[nodiscard]] bool residue_equal(const d15::Residue& left, const d15::Residue& right) noexcept {
    const bool result = left.path == right.path && left.line == right.line && left.column == right.column &&
        left.reason == right.reason;
    return result;
}

[[nodiscard]] bool identifier_character(const char character) noexcept {
    const bool result = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_';
    return result;
}

[[nodiscard]] bool source_extension(llvm::StringRef path) {
    const llvm::StringRef extension = llvm::sys::path::extension(path);
    const bool result = extension == ".h" || extension == ".hpp" || extension == ".hh" || extension == ".inl" ||
        extension == ".ipp" || extension == ".cpp" || extension == ".cc" || extension == ".cxx" ||
        extension == ".mm" || extension == ".m";
    return result;
}

// Files in scope that no configuration compiled (the Windows host, for
// example): each line naming a converted member is residue to check by
// hand. A heuristic, by name only.
void scan_uncompiled(const d15::Options& options, const d15::Closure& closure, const std::set<std::string>& seen,
                     std::vector<d15::Residue>& residue) {
    std::set<std::string> names;
    for (const std::string& key : closure.converted) {
        std::map<std::string, d15::EntityFact>::const_iterator fact = closure.entities.find(key);
        if (fact == closure.entities.end()) continue;
        if ((*fact).second.kind == d15::EntityKind::local) continue;
        const std::string& name = (*fact).second.name;
        if (name.size() < 4U) continue;
        bool common = false;
        for (const char* word : common_names) common = common || name == word;
        if (!common) names.insert(name);
    }
    if (names.empty()) return;
    for (const std::string& directory : options.scope_directories) {
        llvm::SmallString<512U> start(options.source_root);
        llvm::sys::path::append(start, directory);
        std::error_code error;
        for (llvm::sys::fs::recursive_directory_iterator entry(start, error), end; entry != end && !error;
             entry.increment(error)) {
            const std::string absolute = (*entry).path();
            if (!source_extension(absolute)) continue;
            std::string relative;
            if (!relative_to_root(options, absolute, relative)) continue;
            if (within_any(relative, options.scope_exclusions)) continue;
            if (seen.find(relative) != seen.end()) continue;
            std::string text;
            if (!d15::read_file(absolute, text)) continue;
            std::size_t line_start = 0U;
            unsigned line_number = 1U;
            while (line_start <= text.size()) {
                const std::size_t newline = text.find('\n', line_start);
                const std::size_t line_end = newline == std::string::npos ? text.size() : newline;
                const llvm::StringRef line = llvm::StringRef(text).slice(line_start, line_end);
                std::string matched;
                for (const std::string& name : names) {
                    std::size_t at = line.find(name);
                    while (at != llvm::StringRef::npos) {
                        const bool left = at == 0U || !identifier_character(line[at - 1U]);
                        const std::size_t after = at + name.size();
                        const bool right = after >= line.size() || !identifier_character(line[after]);
                        if (left && right) {
                            if (!matched.empty()) matched += ", ";
                            matched += name;
                            break;
                        }
                        at = line.find(name, at + 1U);
                    }
                }
                if (!matched.empty()) {
                    d15::Residue item;
                    item.path = relative;
                    item.line = line_number;
                    item.column = 1U;
                    item.reason = std::string(d15::stage_name(options.stage)) +
                        ": not compiled in any configuration given; names converted " + matched + " (check by hand)";
                    item.snippet = std::string(line.trim());
                    residue.push_back(item);
                }
                if (newline == std::string::npos) break;
                line_start = newline + 1U;
                ++line_number;
            }
        }
    }
}

[[nodiscard]] bool write_residue(const std::string& path, const std::vector<d15::Residue>& residue) {
    std::error_code error;
    llvm::raw_fd_ostream output(path, error);
    if (error) return false;
    for (const d15::Residue& item : residue) {
        output << item.path << ':' << item.line << ':' << item.column << ": " << item.reason;
        if (!item.snippet.empty()) output << " | " << item.snippet;
        output << '\n';
    }
    return true;
}

[[nodiscard]] bool write_report(const std::string& path, const d15::Options& options, const Collector& collector,
                                const d15::Closure& closure, const std::vector<d15::FileOutcome>& outcomes,
                                const std::vector<d15::Residue>& residue, const std::size_t units) {
    std::error_code error;
    llvm::raw_fd_ostream output(path, error);
    if (error) return false;
    llvm::json::Array failure_records;
    for (const ParseFailure& failure : collector.failures) {
        llvm::json::Object record;
        record["source"] = failure.source;
        record["errors"] = static_cast<std::int64_t>(failure.errors);
        record["first"] = failure.first;
        failure_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array file_records;
    for (const d15::FileOutcome& outcome : outcomes) {
        llvm::json::Object record;
        record["path"] = outcome.path;
        record["edits"] = static_cast<std::int64_t>(outcome.edit_count);
        record["changed"] = outcome.changed;
        file_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array converted_records;
    for (const std::string& key : closure.converted) {
        std::map<std::string, d15::EntityFact>::const_iterator fact = closure.entities.find(key);
        if (fact == closure.entities.end()) continue;
        llvm::json::Object record;
        record["kind"] = d15::entity_kind_name((*fact).second.kind);
        record["name"] = (*fact).second.qualified_name;
        record["path"] = (*fact).second.path;
        record["line"] = static_cast<std::int64_t>((*fact).second.line);
        converted_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array residue_records;
    for (const d15::Residue& item : residue) {
        llvm::json::Object record;
        record["path"] = item.path;
        record["line"] = static_cast<std::int64_t>(item.line);
        record["column"] = static_cast<std::int64_t>(item.column);
        record["reason"] = item.reason;
        record["snippet"] = item.snippet;
        residue_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Object root;
    root["schema"] = "gui.forms.d15-rewrite-report/v1";
    root["stage"] = d15::stage_name(options.stage);
    root["mode"] = std::string(mode_option);
    root["translationUnits"] = static_cast<std::int64_t>(units);
    root["parseFailures"] = std::move(failure_records);
    root["entities"] = static_cast<std::int64_t>(closure.entities.size());
    root["components"] = static_cast<std::int64_t>(closure.components);
    root["convertingComponents"] = static_cast<std::int64_t>(closure.converting_components);
    root["converted"] = std::move(converted_records);
    root["files"] = std::move(file_records);
    root["residue"] = std::move(residue_records);
    output << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    return true;
}

// Every value of a command-line option written `-name value`, `-name=value`,
// `--name value` or `--name=value`, in order. For the options clang's
// CommonOptionsParser registers itself (-p, --extra-arg, --extra-arg-before),
// whose values it does not hand back.
std::vector<std::string> argument_values(int argc, const char** argv, const std::string& name) {
    std::vector<std::string> values;
    const std::string single = "-" + name;
    const std::string doubled = "--" + name;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        const bool bare = argument == single || argument == doubled;
        if (bare && index + 1 < argc) {
            values.push_back(std::string(argv[index + 1]));
            ++index;
            continue;
        }
        const std::string single_equals = single + "=";
        const std::string doubled_equals = doubled + "=";
        if (argument.rfind(single_equals, 0) == 0) values.push_back(argument.substr(single_equals.size()));
        if (argument.rfind(doubled_equals, 0) == 0) values.push_back(argument.substr(doubled_equals.size()));
    }
    return values;
}

} // namespace

int main(int argc, const char** argv) {
    llvm::Expected<clang::tooling::CommonOptionsParser> parsed = clang::tooling::CommonOptionsParser::create(
        argc, argv, rewrite_category, llvm::cl::ZeroOrMore,
        "Rewrites GUI.Forms' standard vocabulary to STXlib's, one D15 stage at a time.");
    if (!parsed) {
        llvm::errs() << llvm::toString(parsed.takeError()) << '\n';
        return 2;
    }
    d15::Options options;
    std::string error;
    if (!build_options(options, error)) {
        llvm::errs() << "d15_rewrite: " << error << '\n';
        return 2;
    }
    if (mode_option != "check" && mode_option != "apply" && mode_option != "plan") {
        llvm::errs() << "d15_rewrite: --mode takes check, apply or plan\n";
        return 2;
    }
    // CommonOptionsParser loads no database when, as here, no source path is
    // named (its sources are ZeroOrMore): it is loaded from -p, with the
    // --extra-arg-before and --extra-arg values applied as the parser would.
    std::unique_ptr<clang::tooling::CompilationDatabase> loaded{};
    const clang::tooling::CompilationDatabase* database_pointer = nullptr;
    if ((*parsed).getSourcePathList().empty()) {
        const std::vector<std::string> build_paths = argument_values(argc, argv, "p");
        if (build_paths.empty()) {
            llvm::errs() << "d15_rewrite: -p names the directory of compile_commands.json\n";
            return 2;
        }
        std::string message;
        std::unique_ptr<clang::tooling::CompilationDatabase> detected =
            clang::tooling::CompilationDatabase::autoDetectFromDirectory(build_paths.back(), message);
        if (!detected) {
            llvm::errs() << "d15_rewrite: " << message << '\n';
            return 2;
        }
        std::unique_ptr<clang::tooling::ArgumentsAdjustingCompilations> adjusting =
            std::make_unique<clang::tooling::ArgumentsAdjustingCompilations>(std::move(detected));
        const std::vector<std::string> before = argument_values(argc, argv, "extra-arg-before");
        const std::vector<std::string> after = argument_values(argc, argv, "extra-arg");
        (*adjusting).appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster(before, clang::tooling::ArgumentInsertPosition::BEGIN));
        (*adjusting).appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster(after, clang::tooling::ArgumentInsertPosition::END));
        loaded = std::move(adjusting);
        database_pointer = loaded.get();
    } else {
        database_pointer = &(*parsed).getCompilations();
    }
    const clang::tooling::CompilationDatabase& database = *database_pointer;

    // The translation units: those named, else every first-party unit in
    // the database within the scope (C sources excluded).
    // V2's plan also reads the evidence units: outside the scope, in
    // --evidence, never rewritten (README, "Evidence").
    std::vector<std::string> units;
    std::vector<std::string> evidence_units;
    std::vector<std::string> named = (*parsed).getSourcePathList();
    const std::vector<std::string> candidates = named.empty() ? database.getAllFiles() : named;
    std::vector<std::string> unit_directories;
    for (const std::string& value : units_option) unit_directories.push_back(trimmed_directory(value));
    for (const std::string& candidate : candidates) {
        if (!named.empty()) {
            units.push_back(candidate);
            continue;
        }
        if (llvm::sys::path::extension(candidate) == ".c") continue;
        std::string relative;
        if (!relative_to_root(options, candidate, relative)) continue;
        if (within_any(relative, options.scope_exclusions)) continue;
        if (!unit_directories.empty() && !within_any(relative, unit_directories)) continue;
        if (within_any(relative, options.scope_directories)) {
            units.push_back(candidate);
            continue;
        }
        const bool evidence = options.stage == d15::Stage::v2_text_view &&
            within_any(relative, options.evidence_directories);
        if (evidence) evidence_units.push_back(candidate);
    }
    std::sort(units.begin(), units.end());
    units.erase(std::unique(units.begin(), units.end()), units.end());
    std::sort(evidence_units.begin(), evidence_units.end());
    evidence_units.erase(std::unique(evidence_units.begin(), evidence_units.end()), evidence_units.end());
    std::vector<std::string> plan_units = units;
    plan_units.insert(plan_units.end(), evidence_units.begin(), evidence_units.end());
    if (units.empty() && edits_input_option.empty()) {
        llvm::errs() << "d15_rewrite: no translation units in scope\n";
        return 2;
    }

    // Plan phase.
    Collector planning;
    std::vector<d15::EntityFact> facts;
    std::vector<d15::EdgeFact> edges;
    const bool merging = !edits_input_option.empty();
    if (!plan_input_option.empty()) {
        for (const std::string& plan : plan_input_option) {
            if (!d15::read_plan(plan, options.stage, facts, edges, error)) {
                llvm::errs() << "d15_rewrite: " << plan << ": " << error << '\n';
                return 2;
            }
        }
    } else if (!merging) {
        d15::AnalysisInput plan_input;
        plan_input.options = &options;
        plan_input.phase = d15::Phase::plan;
        run_phase(database, plan_units, plan_input, planning, jobs_option);
        facts = planning.entities;
        edges = planning.edges;
    }
    if (!plan_output_option.empty() && !d15::write_plan(plan_output_option, options.stage, facts, edges)) {
        llvm::errs() << "d15_rewrite: cannot write " << plan_output_option << '\n';
        return 2;
    }
    std::vector<d15::Residue> residue;
    d15::Closure closure;
    d15::close_plan(facts, edges, closure, residue);

    std::vector<d15::FileOutcome> outcomes;
    Collector rewriting;
    if (merging && mode_option != "plan") {
        for (const std::string& input : edits_input_option) {
            if (!rewriting.edits.load(input, residue, rewriting.files_seen, error)) {
                llvm::errs() << "d15_rewrite: " << error << '\n';
                return 2;
            }
        }
        rewriting.edits.resolve(options.source_root, residue);
        rewriting.edits.apply(options, mode_option == "apply", outcomes, residue);
        scan_uncompiled(options, closure, rewriting.files_seen, residue);
    } else if (mode_option != "plan") {
        d15::AnalysisInput rewrite_input;
        rewrite_input.options = &options;
        rewrite_input.phase = d15::Phase::rewrite;
        rewrite_input.converted = &closure.converted;
        run_phase(database, units, rewrite_input, rewriting, jobs_option);
        residue.insert(residue.end(), rewriting.residue.begin(), rewriting.residue.end());
        if (!edits_output_option.empty()) {
            std::set<std::string> seen_here = planning.files_seen;
            seen_here.insert(rewriting.files_seen.begin(), rewriting.files_seen.end());
            if (!rewriting.edits.save(edits_output_option, residue, seen_here)) {
                llvm::errs() << "d15_rewrite: cannot write " << edits_output_option << '\n';
                return 2;
            }
        }
        rewriting.edits.resolve(options.source_root, residue);
        rewriting.edits.apply(options, mode_option == "apply", outcomes, residue);
        std::set<std::string> seen = planning.files_seen;
        seen.insert(rewriting.files_seen.begin(), rewriting.files_seen.end());
        scan_uncompiled(options, closure, seen, residue);
    }
    std::sort(residue.begin(), residue.end(), residue_less);
    residue.erase(std::unique(residue.begin(), residue.end(), residue_equal), residue.end());

    std::size_t changed = 0U;
    for (const d15::FileOutcome& outcome : outcomes) {
        if (outcome.changed) ++changed;
    }
    const std::vector<ParseFailure>& failures = mode_option == "plan" ? planning.failures : rewriting.failures;
    if (!quiet_option) {
        for (const d15::Residue& item : residue) {
            llvm::outs() << item.path << ':' << item.line << ':' << item.column << ": " << item.reason << '\n';
        }
        for (const ParseFailure& failure : failures) {
            llvm::outs() << "parse errors (" << failure.errors << ") in " << failure.source << ": " << failure.first
                         << '\n';
        }
    }
    llvm::outs() << "d15_rewrite " << d15::stage_name(options.stage) << ' ' << mode_option << ": " << units.size()
                 << " units, " << closure.entities.size() << " entities, " << closure.converted.size()
                 << " converted, " << changed << " files " << (mode_option == "apply" ? "rewritten" : "to rewrite")
                 << ", " << residue.size() << " residue, " << failures.size() << " units with errors\n";

    if (!residue_option.empty() && !write_residue(residue_option, residue)) {
        llvm::errs() << "d15_rewrite: cannot write " << residue_option << '\n';
        return 2;
    }
    const Collector& reported = mode_option == "plan" ? planning : rewriting;
    if (!report_option.empty() &&
        !write_report(report_option, options, reported, closure, outcomes, residue, units.size())) {
        llvm::errs() << "d15_rewrite: cannot write " << report_option << '\n';
        return 2;
    }
    if (mode_option == "check" && changed > 0U) return 1;
    return EXIT_SUCCESS;
}
