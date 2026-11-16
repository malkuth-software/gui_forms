#include "analysis.hpp"

#include "families.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace d15 {

namespace {

// A byte range of one first-party file, from a source range that
// Lexer::makeFileCharRange could map to a file (a macro argument maps to
// its spelling; a macro body does not map, and is invalid).
struct Span final {
    bool valid{false};
    std::string path;          // relative to the source root
    clang::FileID file;
    unsigned begin{0U};
    unsigned end{0U};
};

// An expression that denotes an entity: a variable, parameter or field
// named directly, or a call of a function whose return is an entity
// (through std::move and, in V5, the pointer casts and Weak::lock).
struct Denoted final {
    bool found{false};
    std::string key;
    bool prvalue{false};
    bool result_call{false};   // V3 with Q1 Result returns: the call yields OperationResult<Utf8String>
};

// What a flow's source expression is.
enum class SourceKind : std::uint8_t {
    converted,          // denotes a converted entity
    owned,              // already of a stxlib type
    absent_literal,     // std::nullopt
    null_pointer,       // nullptr
    empty,              // {} or a default construction
    make_call,          // std::make_optional, std::make_shared, std::make_unique
    explicit_temporary, // std::optional<T>(x), std::optional<T>{x}
    conditional,
    standard,           // the standard family type, not converted
    literal,            // a string literal
    text_view,          // a stxlib::TextView value
    init_list,          // a non-empty braced list
    value,              // anything else
};

struct SourceShape final {
    SourceKind kind{SourceKind::value};
    const clang::Expr* expression{nullptr};   // the stripped source
    const clang::Expr* inner{nullptr};        // the argument of a converting construction or temporary
    Denoted denoted;
};

// Where a flow's value goes.
struct Destination final {
    bool known{false};
    std::string key;
    bool converted{false};     // the destination is, or becomes, the stxlib type
    Family family{Family::none};
    clang::QualType type;
    clang::QualType value_type;
    bool dependent{false};
    bool ambiguous_braces{false};
    bool result_return{false};
};

[[nodiscard]] bool directory_contains(const std::string& relative, const std::string& directory) {
    if (directory.empty()) return true;
    if (relative == directory) return true;
    const bool result = relative.size() > directory.size() && relative.compare(0U, directory.size(), directory) == 0 &&
        relative[directory.size()] == '/';
    return result;
}

[[nodiscard]] bool any_directory_contains(const std::string& relative, const std::vector<std::string>& directories) {
    for (const std::string& directory : directories) {
        if (directory_contains(relative, directory)) return true;
    }
    return false;
}

[[nodiscard]] bool identifier_character(const char character) noexcept {
    const bool result = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_';
    return result;
}

[[nodiscard]] unsigned skip_space_back(llvm::StringRef text, unsigned position) {
    while (position > 0U) {
        const char character = text[position - 1U];
        if (character != ' ' && character != '\t' && character != '\n' && character != '\r') break;
        --position;
    }
    return position;
}

// The start of a standard name's qualifier (`std::`, `::std::`,
// `std::__1::`) before the name at `name_begin`. `clean` is false when the
// name is qualified by something else (a namespace alias).
[[nodiscard]] unsigned qualifier_start(llvm::StringRef text, const unsigned name_begin, bool& clean) {
    clean = true;
    unsigned position = name_begin;
    for (unsigned guard = 0U; guard < 8U; ++guard) {
        const unsigned cursor = skip_space_back(text, position);
        if (cursor < 2U || text[cursor - 1U] != ':' || text[cursor - 2U] != ':') return position;
        const unsigned before_colons = skip_space_back(text, cursor - 2U);
        unsigned word_begin = before_colons;
        while (word_begin > 0U && identifier_character(text[word_begin - 1U])) --word_begin;
        const llvm::StringRef word = text.slice(word_begin, before_colons);
        if (word.empty()) return cursor - 2U;
        if (word == "std" || word.starts_with("__")) {
            position = word_begin;
            continue;
        }
        clean = false;
        return position;
    }
    return position;
}

// The type location that names the declared type's template or typedef,
// through cv-qualifiers, references and (before Clang 22) elaboration.
// Null for a pointer, an array or anything else.
[[nodiscard]] clang::TypeLoc core_type_loc(clang::TypeLoc location) {
    for (unsigned guard = 0U; guard < 32U && !location.isNull(); ++guard) {
        if (!location.getAs<clang::TemplateSpecializationTypeLoc>().isNull()) return location;
        if (!location.getAs<clang::TypedefTypeLoc>().isNull()) return location;
        if (!location.getAs<clang::AutoTypeLoc>().isNull()) return location;
        if (!location.getAs<clang::PointerTypeLoc>().isNull()) return clang::TypeLoc();
        if (!location.getAs<clang::ArrayTypeLoc>().isNull()) return clang::TypeLoc();
        const clang::QualifiedTypeLoc qualified = location.getAs<clang::QualifiedTypeLoc>();
        if (!qualified.isNull()) {
            location = qualified.getUnqualifiedLoc();
            continue;
        }
        const clang::ReferenceTypeLoc reference = location.getAs<clang::ReferenceTypeLoc>();
        if (!reference.isNull()) {
            location = reference.getPointeeLoc();
            continue;
        }
        location = location.getNextTypeLoc();
    }
    return location;
}

// The stxlib family a type already is, or Family::none.
[[nodiscard]] Family owned_family(const clang::QualType type) {
    const Family families[] = {Family::optional, Family::text_view, Family::owned_text, Family::vector,
                               Family::view, Family::mutable_view, Family::shared, Family::weak, Family::unique};
    for (const Family family : families) {
        if (is_owned_counterpart(type, family)) return family;
    }
    return Family::none;
}

[[nodiscard]] bool is_transparent_construction(const clang::CXXConstructExpr& construction, const Stage stage) {
    if (llvm::isa<clang::CXXTemporaryObjectExpr>(construction)) return false;
    if (construction.getNumArgs() != 1U) return false;
    const clang::CXXConstructorDecl* const constructor = construction.getConstructor();
    if (constructor != nullptr && (*constructor).isCopyOrMoveConstructor()) return true;
    const clang::QualType made = construction.getType();
    const clang::QualType from = (*construction.getArg(0U)).getType();
    const Family made_family = expression_family(made, stage);
    if (made_family != Family::none && made_family == expression_family(from, stage)) return true;
    const Family made_owned = owned_family(made);
    if (made_owned != Family::none && made_owned == owned_family(from)) return true;
    // V5: a Shared<Base> from a Shared<Derived>, a Weak from a Shared.
    if (stage == Stage::v5_ownership && made_family != Family::none &&
        expression_family(from, stage) != Family::none) {
        return true;
    }
    return false;
}

// Looks through the implicit layers around a value: casts, temporaries,
// full-expressions, parentheses, copies and same-family conversions.
[[nodiscard]] const clang::Expr* strip_value(const clang::Expr* expression, const Stage stage) {
    for (unsigned guard = 0U; guard < 64U && expression != nullptr; ++guard) {
        const clang::Expr* const implicit = (*expression).IgnoreImplicit();
        if (implicit != expression) {
            expression = implicit;
            continue;
        }
        const clang::ParenExpr* const parenthesised = llvm::dyn_cast<clang::ParenExpr>(expression);
        if (parenthesised != nullptr) {
            expression = (*parenthesised).getSubExpr();
            continue;
        }
        const clang::CXXConstructExpr* const construction = llvm::dyn_cast<clang::CXXConstructExpr>(expression);
        if (construction != nullptr && is_transparent_construction(*construction, stage)) {
            expression = (*construction).getArg(0U);
            continue;
        }
        break;
    }
    return expression;
}

// The offset of a `const` keyword (and the blanks after it) that directly
// precedes `begin` in `buffer`, or `begin` when none does.
[[nodiscard]] unsigned leading_const_start(const llvm::StringRef buffer, const unsigned begin) {
    unsigned position = begin;
    while (position > 0U && (buffer[position - 1U] == ' ' || buffer[position - 1U] == '\t')) --position;
    const llvm::StringRef keyword("const");
    const unsigned length = static_cast<unsigned>(keyword.size());
    if (position == begin || position < length) return begin;
    if (buffer.slice(position - length, position) != keyword) return begin;
    const unsigned start = position - length;
    if (start > 0U) {
        const char before = buffer[start - 1U];
        const bool identifier = (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
                                (before >= '0' && before <= '9') || before == '_';
        if (identifier) return begin;
    }
    return start;
}

// The type an expression has before the implicit layers above it: a
// method inherited from a base of std::optional (libc++'s has_value and
// reset) is reached through a derived-to-base cast, whose type is the base.
[[nodiscard]] clang::QualType written_type(const clang::Expr& expression) {
    const clang::Expr* current = &expression;
    for (unsigned guard = 0U; guard < 64U; ++guard) {
        const clang::Expr* const next = (*(*current).IgnoreImplicit()).IgnoreParens();
        if (next == current) break;
        current = next;
    }
    return (*current).getType();
}

[[nodiscard]] bool is_transparent_parent(const clang::Expr& parent, const Stage stage) {
    if (llvm::isa<clang::ImplicitCastExpr>(parent)) return true;
    if (llvm::isa<clang::ParenExpr>(parent)) return true;
    if (llvm::isa<clang::MaterializeTemporaryExpr>(parent)) return true;
    if (llvm::isa<clang::CXXBindTemporaryExpr>(parent)) return true;
    if (llvm::isa<clang::FullExpr>(parent)) return true;
    const clang::CXXConstructExpr* const construction = llvm::dyn_cast<clang::CXXConstructExpr>(&parent);
    if (construction != nullptr && is_transparent_construction(*construction, stage)) return true;
    return false;
}

[[nodiscard]] const char* pointer_cast_replacement(const clang::FunctionDecl* callee) {
    if (is_standard_entity(callee, "static_pointer_cast")) return "stxlib::static_shared_cast";
    if (is_standard_entity(callee, "dynamic_pointer_cast")) return "stxlib::dynamic_shared_cast";
    return nullptr;
}

class Analysis final : public clang::RecursiveASTVisitor<Analysis> {
public:
    Analysis(clang::ASTContext& context, const AnalysisInput& input, TranslationUnitHarvest& harvest)
        : context_(context), sources_(context.getSourceManager()), language_(context.getLangOpts()),
          input_(input), options_(*input.options), harvest_(harvest), plan_(input.phase == Phase::plan),
          stage_((*input.options).stage), only_((*input.options).only_pattern) {}

    void run() {
        TraverseDecl(context_.getTranslationUnitDecl());
    }

    [[nodiscard]] bool shouldVisitTemplateInstantiations() const { return false; }
    [[nodiscard]] bool shouldWalkTypesOfTypeLocs() const { return false; }

    bool TraverseDecl(clang::Decl* declaration) {
        if (declaration == nullptr) return true;
        if (!llvm::isa<clang::TranslationUnitDecl>(declaration)) {
            const clang::SourceLocation location = (*declaration).getLocation();
            if (location.isValid()) {
                if (sources_.isInSystemHeader(location)) return true;
                const clang::SourceLocation file_location = sources_.getFileLoc(location);
                std::string relative;
                if (!relative_path(sources_.getFileID(file_location), relative)) return true;
                if (!in_scope(relative)) return true;
                harvest_.files_seen.insert(relative);
            }
        }
        const bool result = clang::RecursiveASTVisitor<Analysis>::TraverseDecl(declaration);
        return result;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* function);
    bool VisitVarDecl(clang::VarDecl* variable);
    bool VisitFieldDecl(clang::FieldDecl* field);
    bool VisitTypedefNameDecl(clang::TypedefNameDecl* alias);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record);
    bool VisitCXXConstructorDecl(clang::CXXConstructorDecl* constructor);
    bool VisitReturnStmt(clang::ReturnStmt* statement);
    bool VisitCallExpr(clang::CallExpr* call);
    bool VisitCXXConstructExpr(clang::CXXConstructExpr* construction);
    bool VisitInitListExpr(clang::InitListExpr* list);
    bool VisitDeclRefExpr(clang::DeclRefExpr* reference);
    bool VisitMemberExpr(clang::MemberExpr* member);

private:
    // -- paths, spans and edits ---------------------------------------------

    bool relative_path(const clang::FileID file, std::string& relative) {
        const unsigned hash = file.getHashValue();
        std::map<unsigned, std::string>::const_iterator cached = paths_.find(hash);
        if (cached != paths_.end()) {
            relative = (*cached).second;
            return !relative.empty();
        }
        std::string result;
        const clang::OptionalFileEntryRef entry = sources_.getFileEntryRefForID(file);
        if (entry) {
            llvm::SmallString<512U> path((*entry).getName());
            sources_.getFileManager().makeAbsolutePath(path);
            llvm::sys::path::remove_dots(path, true);
            const llvm::StringRef absolute = path.str();
            const std::string& root = options_.source_root;
            if (absolute.size() > root.size() + 1U && absolute.starts_with(root) && absolute[root.size()] == '/') {
                result = std::string(absolute.drop_front(root.size() + 1U));
            }
        }
        paths_.emplace(hash, result);
        relative = result;
        return !relative.empty();
    }

    [[nodiscard]] bool in_scope(const std::string& relative) const {
        if (!any_directory_contains(relative, options_.scope_directories)) return false;
        if (any_directory_contains(relative, options_.scope_exclusions)) return false;
        return true;
    }

    [[nodiscard]] bool in_seed(const std::string& relative) const {
        if (!in_scope(relative)) return false;
        if (!any_directory_contains(relative, options_.seed_directories)) return false;
        if (any_directory_contains(relative, options_.seed_exclusions)) return false;
        return true;
    }

    // The private bridge (src/platform/stxlib_bridge.hpp) may be included
    // anywhere first-party except the public headers.
    [[nodiscard]] bool bridge_allowed(const std::string& relative) const {
        const bool result = in_scope(relative) && !directory_contains(relative, "include");
        return result;
    }

    [[nodiscard]] Span span_of(const clang::SourceRange range) {
        Span span;
        if (range.isInvalid()) return span;
        clang::CharSourceRange characters = clang::Lexer::makeFileCharRange(
            clang::CharSourceRange::getTokenRange(range), sources_, language_);
        if (characters.isInvalid()) characters = macro_argument_range(range);
        if (characters.isInvalid()) return span;
        const std::pair<clang::FileID, unsigned> begin = sources_.getDecomposedLoc(characters.getBegin());
        const std::pair<clang::FileID, unsigned> end = sources_.getDecomposedLoc(characters.getEnd());
        if (begin.first != end.first || end.second < begin.second) return span;
        if (!relative_path(begin.first, span.path)) return span;
        span.file = begin.first;
        span.begin = begin.second;
        span.end = end.second;
        span.valid = true;
        return span;
    }

    // A range lying wholly inside one macro argument whose text the macro
    // expands as written (REQUIRE(x.has_value())): the argument's spelling
    // in the file. A range that reaches into the macro's body, spans two
    // arguments, or whose spelling is not one contiguous run of a file
    // (token pasting, nested bodies) stays invalid.
    [[nodiscard]] clang::CharSourceRange macro_argument_range(const clang::SourceRange range) {
        const clang::CharSourceRange invalid;
        const clang::SourceLocation first = range.getBegin();
        const clang::SourceLocation last = range.getEnd();
        if (!first.isMacroID() || !last.isMacroID()) return invalid;
        if (!sources_.isMacroArgExpansion(first) || !sources_.isMacroArgExpansion(last)) return invalid;
        // Both ends must come from the same expansion of the same argument.
        const clang::SourceLocation first_expansion = sources_.getImmediateExpansionRange(first).getBegin();
        const clang::SourceLocation last_expansion = sources_.getImmediateExpansionRange(last).getBegin();
        if (first_expansion != last_expansion) return invalid;
        const clang::SourceLocation spelled_first = sources_.getSpellingLoc(first);
        const clang::SourceLocation spelled_last = sources_.getSpellingLoc(last);
        if (!spelled_first.isFileID() || !spelled_last.isFileID()) return invalid;
        if (sources_.getFileID(spelled_first) != sources_.getFileID(spelled_last)) return invalid;
        if (sources_.isWrittenInScratchSpace(spelled_first)) return invalid;
        // The spelling must lie in a macro invocation's argument list, not in
        // a #define: the argument is spelled where the macro is used.
        const clang::SourceLocation use = sources_.getExpansionLoc(first);
        if (sources_.getFileID(use) != sources_.getFileID(spelled_first)) return invalid;
        const clang::SourceLocation end = clang::Lexer::getLocForEndOfToken(spelled_last, 0U, sources_, language_);
        if (end.isInvalid() || sources_.getFileOffset(end) < sources_.getFileOffset(spelled_first)) return invalid;
        if (sources_.getFileOffset(spelled_first) < sources_.getFileOffset(use)) return invalid;
        const clang::CharSourceRange result = clang::CharSourceRange::getCharRange(spelled_first, end);
        return result;
    }

    [[nodiscard]] Span span_of(const clang::Expr* expression) {
        if (expression == nullptr) return Span();
        const Span result = span_of((*expression).getSourceRange());
        return result;
    }

    [[nodiscard]] Span token_span(const clang::SourceLocation location) {
        const Span result = span_of(clang::SourceRange(location, location));
        return result;
    }

    [[nodiscard]] llvm::StringRef buffer_of(const clang::FileID file) {
        bool invalid = false;
        const llvm::StringRef data = sources_.getBufferData(file, &invalid);
        if (invalid) return llvm::StringRef();
        return data;
    }

    [[nodiscard]] std::string text_of(const Span& span) {
        if (!span.valid) return std::string();
        const llvm::StringRef data = buffer_of(span.file);
        const std::string result(data.slice(span.begin, span.end));
        return result;
    }

    // Edits are recorded only in the rewrite phase and only in scope; an
    // edit that cannot be placed is residue at `where`.
    bool editable(const Span& span, const clang::SourceLocation where) {
        if (plan_) return false;
        if (!span.valid) {
            residue_at(where, "inside a macro body: the site cannot be rewritten");
            return false;
        }
        if (!in_scope(span.path)) return false;
        return true;
    }

    void add_edit(const std::string& path, const Edit& edit) {
        harvest_.edits[path].push_back(edit);
    }

    void replace_bytes(const Span& file, const unsigned begin, const unsigned end, const std::string& text) {
        Edit edit;
        edit.offset = begin;
        edit.length = end >= begin ? end - begin : 0U;
        edit.text = text;
        edit.role = EditRole::replace;
        add_edit(file.path, edit);
    }

    void replace_span(const Span& span, const std::string& text) {
        replace_bytes(span, span.begin, span.end, text);
    }

    void open_at(const Span& wrapped, const std::string& text) {
        Edit edit;
        edit.offset = wrapped.begin;
        edit.text = text;
        edit.role = EditRole::open;
        edit.span = wrapped.end - wrapped.begin;
        add_edit(wrapped.path, edit);
    }

    void close_at(const Span& wrapped, const std::string& text) {
        Edit edit;
        edit.offset = wrapped.end;
        edit.text = text;
        edit.role = EditRole::close;
        edit.span = wrapped.end - wrapped.begin;
        add_edit(wrapped.path, edit);
    }

    void wrap(const Span& wrapped, const std::string& opener, const std::string& closer) {
        open_at(wrapped, opener);
        close_at(wrapped, closer);
    }

    void need(const std::string& path, const std::uint32_t headers) {
        if (plan_) return;
        harvest_.includes[path] |= headers;
    }

    void residue_at(const clang::SourceLocation location, const std::string& reason) {
        if (plan_) return;
        if (location.isInvalid()) return;
        const clang::SourceLocation file_location = sources_.getFileLoc(location);
        const std::pair<clang::FileID, unsigned> decomposed = sources_.getDecomposedLoc(file_location);
        std::string relative;
        if (!relative_path(decomposed.first, relative)) return;
        if (!in_scope(relative)) return;
        Residue item;
        item.path = relative;
        item.line = sources_.getLineNumber(decomposed.first, decomposed.second);
        item.column = sources_.getColumnNumber(decomposed.first, decomposed.second);
        item.reason = std::string(stage_name(stage_)) + ": " + reason;
        const llvm::StringRef data = buffer_of(decomposed.first);
        if (!data.empty() && decomposed.second <= data.size()) {
            const std::size_t line_begin = data.rfind('\n', decomposed.second);
            const std::size_t start = line_begin == llvm::StringRef::npos ? 0U : line_begin + 1U;
            const std::size_t line_end = data.find('\n', decomposed.second);
            const llvm::StringRef line = data.slice(start, line_end == llvm::StringRef::npos ? data.size() : line_end);
            item.snippet = std::string(line.trim());
        }
        harvest_.residue.push_back(item);
    }

    // -- keys ---------------------------------------------------------------

    [[nodiscard]] std::string usr_of(const clang::Decl* declaration) const {
        llvm::SmallString<256U> buffer;
        if (clang::index::generateUSRForDecl(declaration, buffer)) return std::string();
        const std::string result(buffer.str());
        return result;
    }

    [[nodiscard]] std::string parameter_key(const clang::ParmVarDecl* parameter) {
        const clang::FunctionDecl* const function =
            llvm::dyn_cast<clang::FunctionDecl>((*parameter).getDeclContext());
        if (function == nullptr) return std::string();
        const std::string usr = usr_of(function);
        if (usr.empty()) return std::string();
        const std::string result = "P:" + usr + "#" + std::to_string((*parameter).getFunctionScopeIndex());
        return result;
    }

    [[nodiscard]] std::string return_key(const clang::FunctionDecl* function) {
        const std::string usr = usr_of(function);
        if (usr.empty()) return std::string();
        return "R:" + usr;
    }

    [[nodiscard]] std::string value_key(const clang::ValueDecl* declaration) {
        if (declaration == nullptr) return std::string();
        const clang::ParmVarDecl* const parameter = llvm::dyn_cast<clang::ParmVarDecl>(declaration);
        if (parameter != nullptr) return parameter_key(parameter);
        const clang::FieldDecl* const field = llvm::dyn_cast<clang::FieldDecl>(declaration);
        if (field != nullptr) {
            const std::string usr = usr_of(field);
            if (usr.empty()) return std::string();
            return "D:" + usr;
        }
        const clang::VarDecl* const variable = llvm::dyn_cast<clang::VarDecl>(declaration);
        if (variable == nullptr) return std::string();
        if ((*variable).isLocalVarDecl()) {
            const Span name = token_span((*variable).getLocation());
            if (!name.valid) return std::string();
            return "L:" + name.path + ":" + std::to_string(name.begin);
        }
        const std::string usr = usr_of(variable);
        if (usr.empty()) return std::string();
        return "V:" + usr;
    }

    [[nodiscard]] bool is_converted(const std::string& key) const {
        if (plan_ || key.empty() || input_.converted == nullptr) return false;
        const bool result = (*input_.converted).find(key) != (*input_.converted).end();
        return result;
    }

    // -- types ---------------------------------------------------------------

    [[nodiscard]] std::string strip_namespaces(const std::string& full, const clang::DeclContext* use) const {
        std::vector<std::string> names;
        for (const clang::DeclContext* context = use; context != nullptr; context = (*context).getParent()) {
            const clang::NamespaceDecl* const space = llvm::dyn_cast<clang::NamespaceDecl>(context);
            if (space == nullptr || (*space).isAnonymousNamespace() || (*space).isInline()) continue;
            names.insert(names.begin(), std::string((*space).getName()));
        }
        std::string result = full;
        for (const std::string& name : names) {
            const std::string prefix = name + "::";
            if (result.size() <= prefix.size() || result.compare(0U, prefix.size(), prefix) != 0) break;
            result = result.substr(prefix.size());
        }
        return result;
    }

    // A value type as it can be written at a use site in `use`: standard
    // typedefs keep their std:: spelling, everything else is fully
    // qualified and then shortened by the namespaces the use is inside.
    [[nodiscard]] std::string spell_type(const clang::QualType type, const clang::DeclContext* use) const {
        if (type.isNull()) return "void";
        const clang::Type* const plain = type.getTypePtr();
        const clang::TypedefType* const typedef_type = llvm::dyn_cast<clang::TypedefType>(plain);
        if (typedef_type != nullptr && (*(*typedef_type).getDecl()).isInStdNamespace()) {
            return "std::" + std::string((*(*typedef_type).getDecl()).getName());
        }
        const clang::UsingType* const using_type = llvm::dyn_cast<clang::UsingType>(plain);
        if (using_type != nullptr) {
            const clang::UsingShadowDecl* const shadow = (*using_type).getDecl();
            if (shadow != nullptr && (*shadow).isInStdNamespace()) {
                return "std::" + std::string((*shadow).getName());
            }
        }
        clang::PrintingPolicy policy(language_);
        policy.SuppressUnwrittenScope = true;
        policy.Bool = true;
        const std::string full = clang::TypeName::getFullyQualifiedName(type.getUnqualifiedType(), context_, policy,
                                                                       false);
        const std::string result = strip_namespaces(full, use);
        return result;
    }

    [[nodiscard]] std::string owned_type(const Family family, const clang::QualType value_type,
                                         const clang::DeclContext* use) const {
        switch (family) {
        case Family::text_view:
        case Family::text_reference:
        case Family::owned_text:
            return owned_name(family);
        case Family::view:
        case Family::mutable_view:
            return std::string(owned_name(family)) + "<" + spell_type(value_type.getUnqualifiedType(), use) + ">";
        default:
            break;
        }
        const std::string result = std::string(owned_name(family)) + "<" + spell_type(value_type, use) + ">";
        return result;
    }

    // The innermost declaration context of a statement: the function,
    // record or namespace a use is written in.
    [[nodiscard]] const clang::DeclContext* context_of(const clang::Stmt& statement) {
        clang::DynTypedNode node = clang::DynTypedNode::create(statement);
        for (unsigned guard = 0U; guard < 512U; ++guard) {
            const clang::DynTypedNodeList parents = context_.getParents(node);
            if (parents.empty()) break;
            const clang::Decl* const declaration = parents[0].get<clang::Decl>();
            if (declaration != nullptr) {
                const clang::DeclContext* const as_context = llvm::dyn_cast<clang::DeclContext>(declaration);
                if (as_context != nullptr) return as_context;
                return (*declaration).getDeclContext();
            }
            node = parents[0];
        }
        return context_.getTranslationUnitDecl();
    }

    [[nodiscard]] const clang::FunctionDecl* enclosing_function(const clang::Stmt& statement) {
        clang::DynTypedNode node = clang::DynTypedNode::create(statement);
        for (unsigned guard = 0U; guard < 512U; ++guard) {
            const clang::DynTypedNodeList parents = context_.getParents(node);
            if (parents.empty()) return nullptr;
            if (parents[0].get<clang::LambdaExpr>() != nullptr) return nullptr;
            const clang::FunctionDecl* const function = parents[0].get<clang::FunctionDecl>();
            if (function != nullptr) return function;
            if (parents[0].get<clang::Decl>() != nullptr) return nullptr;
            node = parents[0];
        }
        return nullptr;
    }

    [[nodiscard]] clang::DynTypedNode parent_of(const clang::DynTypedNode& node, bool& found) {
        const clang::DynTypedNodeList parents = context_.getParents(node);
        found = !parents.empty();
        if (!found) return clang::DynTypedNode();
        return parents[0];
    }

    // -- entities -------------------------------------------------------------

    [[nodiscard]] bool has_sibling_overload(const clang::FunctionDecl& function) {
        const clang::DeclContext* const context = (*function.getDeclContext()).getRedeclContext();
        const clang::DeclContextLookupResult found = (*context).lookup(function.getDeclName());
        for (const clang::NamedDecl* candidate : found) {
            const clang::FunctionDecl* other = llvm::dyn_cast<clang::FunctionDecl>(candidate);
            const clang::FunctionTemplateDecl* const pattern = llvm::dyn_cast<clang::FunctionTemplateDecl>(candidate);
            if (pattern != nullptr) other = (*pattern).getTemplatedDecl();
            if (other == nullptr) continue;
            if ((*other).getCanonicalDecl() == function.getCanonicalDecl()) continue;
            if ((*other).getNumParams() != function.getNumParams()) continue;
            return true;
        }
        return false;
    }

    // True when changing parameter `index` of `function` to the stxlib type
    // would give it the signature of an existing overload (the D15 queue's
    // deprecated-overload pairs).
    [[nodiscard]] bool would_collide(const clang::FunctionDecl& function, const unsigned index,
                                     const FamilyMatch& match) {
        const clang::DeclContext* const context = (*function.getDeclContext()).getRedeclContext();
        const clang::DeclContextLookupResult found = (*context).lookup(function.getDeclName());
        for (const clang::NamedDecl* candidate : found) {
            const clang::FunctionDecl* const other = llvm::dyn_cast<clang::FunctionDecl>(candidate);
            if (other == nullptr) continue;
            if ((*other).getCanonicalDecl() == function.getCanonicalDecl()) continue;
            if ((*other).getNumParams() != function.getNumParams()) continue;
            bool same = true;
            for (unsigned position = 0U; position < function.getNumParams() && same; ++position) {
                const clang::QualType mine = (*function.getParamDecl(position)).getType().getCanonicalType();
                const clang::QualType theirs = (*(*other).getParamDecl(position)).getType().getCanonicalType();
                if (position != index) {
                    same = mine == theirs;
                    continue;
                }
                if (!is_owned_counterpart(theirs, match.family)) {
                    same = false;
                    continue;
                }
                if (match.value_type.isNull()) continue;
                const clang::ClassTemplateSpecializationDecl* const owned =
                    llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                        (*theirs.getNonReferenceType()).getAsCXXRecordDecl());
                if (owned == nullptr || (*owned).getTemplateArgs().size() == 0U) continue;
                const clang::TemplateArgument& argument = (*owned).getTemplateArgs()[0];
                if (argument.getKind() != clang::TemplateArgument::Type) continue;
                same = argument.getAsType().getCanonicalType().getUnqualifiedType() ==
                    match.value_type.getCanonicalType().getUnqualifiedType();
            }
            if (same) return true;
        }
        return false;
    }

    [[nodiscard]] bool only_matches(const std::string& qualified) {
        if (options_.only_pattern.empty()) return true;
        const bool result = only_.match(qualified);
        return result;
    }

    // Records (plan) or rewrites (rewrite) one entity declaration.
    // `function` and `index` name a parameter's or return's function.
    void consider_entity(const clang::Decl& declaration, const std::string& key, const EntityKind kind,
                         const clang::QualType type, const clang::TypeLoc written, const clang::SourceLocation name,
                         const std::string& simple_name, const std::string& qualified,
                         const clang::FunctionDecl* function, const unsigned index) {
        if (key.empty() || declaration.isImplicit()) return;
        const FamilyMatch match = match_family(type, kind, stage_, context_);
        if (match.family == Family::none) return;
        const Span name_span = token_span(name);

        if (!plan_) {
            if (is_converted(key)) rewrite_declared_type(written, match, name);
            return;
        }

        EntityFact fact;
        fact.key = key;
        fact.kind = kind;
        fact.family = match.family;
        fact.name = simple_name;
        fact.qualified_name = qualified;
        if (name_span.valid) {
            fact.path = name_span.path;
            fact.line = sources_.getLineNumber(name_span.file, name_span.begin);
            fact.column = sources_.getColumnNumber(name_span.file, name_span.begin);
        }
        if (!name_span.valid) {
            fact.ineligible = "declared inside a macro";
        } else if (!in_scope(name_span.path)) {
            fact.ineligible = "outside the rewrite scope (" + name_span.path + ")";
        } else if (declaration.isTemplated()) {
            fact.ineligible = "inside a template: its dependent uses are not typed";
        } else if (!match.unsupported.empty()) {
            fact.ineligible = match.unsupported;
        } else if (written.isNull() && kind != EntityKind::shared_base) {
            fact.ineligible = "the type is not written where it is declared";
        }
        if (fact.ineligible.empty() && function != nullptr) {
            const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
            if (method != nullptr && (*(*method).getParent()).isLambda()) {
                fact.ineligible = "a lambda's parameter or return";
            } else if ((*function).hasAttr<clang::DeprecatedAttr>() && has_sibling_overload(*function)) {
                fact.ineligible = "a deprecated D15 compatibility overload (Q3), removed at V6";
            } else if (kind == EntityKind::parameter && would_collide(*function, index, match)) {
                fact.ineligible = "changing it would give the function the signature of an existing overload";
            }
        }
        const core_alias_result alias = first_party_alias(written);
        if (alias.alias != nullptr) {
            EdgeFact edge;
            edge.first = key;
            edge.second = "A:" + usr_of(alias.alias);
            edge.strict = true;
            harvest_.edges.push_back(edge);
        }
        fact.seeded = kind != EntityKind::local && name_span.valid && in_seed(name_span.path) &&
            options_.seed_kinds.find(kind) != options_.seed_kinds.end() && only_matches(qualified);
        harvest_.entities.push_back(fact);
    }

    struct core_alias_result final {
        const clang::TypedefNameDecl* alias{nullptr};
    };

    [[nodiscard]] core_alias_result first_party_alias(const clang::TypeLoc written) const {
        core_alias_result result;
        if (written.isNull()) return result;
        const clang::TypeLoc core = core_type_loc(written);
        if (core.isNull()) return result;
        const clang::TypedefTypeLoc typedef_loc = core.getAs<clang::TypedefTypeLoc>();
        if (typedef_loc.isNull()) return result;
        const clang::TypedefNameDecl* const alias = (*typedef_loc.getTypePtr()).getDecl();
        if (alias == nullptr || (*alias).isInStdNamespace()) return result;
        result.alias = alias;
        return result;
    }

    void rewrite_declared_type(const clang::TypeLoc whole, const FamilyMatch& match, const clang::SourceLocation where) {
        if (whole.isNull()) {
            residue_at(where, "the converted declaration's type is not written here");
            return;
        }
        if (match.family == Family::text_reference || (match.family == Family::text_view && match.reference)) {
            Span whole_span = span_of(whole.getSourceRange());
            if (!editable(whole_span, where)) return;
            // `const std::string& name` becomes `stxlib::TextView name`:
            // the view is passed by value, so the const before it goes too.
            whole_span.begin = leading_const_start(buffer_of(whole_span.file), whole_span.begin);
            replace_span(whole_span, "stxlib::TextView");
            need(whole_span.path, include_text_view);
            return;
        }
        const clang::TypeLoc core = core_type_loc(whole);
        if (core.isNull()) {
            residue_at(where, "the declared type is not spelled as the standard type");
            return;
        }
        if (!core.getAs<clang::AutoTypeLoc>().isNull()) return;
        const clang::TypedefTypeLoc typedef_loc = core.getAs<clang::TypedefTypeLoc>();
        if (!typedef_loc.isNull()) {
            const clang::TypedefNameDecl* const alias = (*typedef_loc.getTypePtr()).getDecl();
            if (alias == nullptr || !(*alias).isInStdNamespace()) return;
            const Span name = token_span(typedef_loc.getNameLoc());
            if (!editable(name, where)) return;
            bool clean = true;
            const unsigned start = qualifier_start(buffer_of(name.file), name.begin, clean);
            if (!clean) {
                residue_at(where, "the standard name is qualified by something other than std::");
                return;
            }
            replace_bytes(name, start, name.end, owned_name(match.family));
            need(name.path, family_includes(match.family));
            return;
        }
        const clang::TemplateSpecializationTypeLoc specialization = core.getAs<clang::TemplateSpecializationTypeLoc>();
        if (specialization.isNull()) {
            residue_at(where, "the declared type is not spelled as the standard template");
            return;
        }
        const Span name = token_span(specialization.getTemplateNameLoc());
        const Span closing = token_span(specialization.getRAngleLoc());
        if (!editable(name, where) || !closing.valid || closing.path != name.path) return;
        bool clean = true;
        const unsigned start = qualifier_start(buffer_of(name.file), name.begin, clean);
        if (!clean) {
            residue_at(where, "the standard name is qualified by something other than std::");
            return;
        }
        switch (match.family) {
        case Family::view:
        case Family::mutable_view: {
            if (specialization.getNumArgs() < 1U) {
                residue_at(where, "std::span without a written element type");
                return;
            }
            const clang::TemplateArgumentLoc argument = specialization.getArgLoc(0U);
            const clang::TypeSourceInfo* const information = argument.getTypeSourceInfo();
            if (information == nullptr) {
                residue_at(where, "std::span's element is not a written type");
                return;
            }
            clang::TypeLoc element = (*information).getTypeLoc();
            const clang::QualifiedTypeLoc qualified = element.getAs<clang::QualifiedTypeLoc>();
            if (!qualified.isNull()) element = qualified.getUnqualifiedLoc();
            const Span element_span = span_of(element.getSourceRange());
            if (!element_span.valid) {
                residue_at(where, "std::span's element type is spelled through a macro");
                return;
            }
            replace_bytes(name, start, closing.end,
                          std::string(owned_name(match.family)) + "<" + text_of(element_span) + ">");
            break;
        }
        case Family::text_view:
        case Family::owned_text:
            replace_bytes(name, start, closing.end, owned_name(match.family));
            break;
        default:
            replace_bytes(name, start, name.end, owned_name(match.family));
            break;
        }
        need(name.path, family_includes(match.family));
    }

    // -- denotation -----------------------------------------------------------

    [[nodiscard]] Denoted denoted(const clang::Expr* raw) {
        Denoted result;
        const clang::Expr* const expression = strip_value(raw, stage_);
        if (expression == nullptr) return result;
        if (expression_family((*expression).getType(), stage_) == Family::none) return result;

        const clang::DeclRefExpr* const reference = llvm::dyn_cast<clang::DeclRefExpr>(expression);
        if (reference != nullptr) {
            const clang::VarDecl* const variable = llvm::dyn_cast<clang::VarDecl>((*reference).getDecl());
            if (variable == nullptr) return result;
            result.key = value_key(variable);
            result.found = !result.key.empty();
            return result;
        }
        const clang::MemberExpr* const member = llvm::dyn_cast<clang::MemberExpr>(expression);
        if (member != nullptr) {
            const clang::ValueDecl* const target = (*member).getMemberDecl();
            if (!llvm::isa<clang::FieldDecl>(target) && !llvm::isa<clang::VarDecl>(target)) return result;
            result.key = value_key(target);
            result.found = !result.key.empty();
            return result;
        }
        const clang::CallExpr* const call = llvm::dyn_cast<clang::CallExpr>(expression);
        if (call == nullptr) return result;
        const clang::FunctionDecl* const callee = (*call).getDirectCallee();
        if (callee == nullptr) return result;
        if ((is_standard_entity(callee, "move") || is_standard_entity(callee, "forward")) &&
            (*call).getNumArgs() == 1U) {
            result = denoted((*call).getArg(0U));
            result.prvalue = false;
            return result;
        }
        if (stage_ == Stage::v5_ownership) {
            if (pointer_cast_replacement(callee) != nullptr && (*call).getNumArgs() == 1U) {
                result = denoted((*call).getArg(0U));
                result.prvalue = true;
                return result;
            }
            const clang::CXXMemberCallExpr* const member_call = llvm::dyn_cast<clang::CXXMemberCallExpr>(call);
            if (member_call != nullptr && (*callee).getDeclName().isIdentifier() && (*callee).getName() == "lock") {
                const clang::Expr* const object = (*member_call).getImplicitObjectArgument();
                if (object != nullptr && expression_family((*object).getType(), stage_) == Family::weak) {
                    result = denoted(object);
                    result.prvalue = true;
                    return result;
                }
            }
            if ((*callee).getDeclName().isIdentifier() && (*callee).getName() == "shared_from_this") {
                const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
                const clang::ClassTemplateSpecializationDecl* const base = method == nullptr ? nullptr
                    : llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>((*method).getParent());
                if (base != nullptr && (*base).isInStdNamespace() && (*base).getTemplateArgs().size() > 0U &&
                    (*base).getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type) {
                    const clang::CXXRecordDecl* const owner =
                        (*(*base).getTemplateArgs()[0].getAsType()).getAsCXXRecordDecl();
                    if (owner != nullptr) {
                        result.key = "B:" + usr_of(owner);
                        result.found = true;
                        result.prvalue = true;
                    }
                }
                return result;
            }
        }
        if ((*callee).isInStdNamespace()) return result;
        const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
        if (method != nullptr && (*(*method).getParent()).isInStdNamespace()) return result;
        result.key = return_key(callee);
        result.found = !result.key.empty();
        result.prvalue = (*expression).isPRValue();
        result.result_call = stage_ == Stage::v3_owned_text && options_.allocating_returns_result &&
            result.prvalue && is_converted(result.key);
        return result;
    }

    // -- flows ----------------------------------------------------------------

    [[nodiscard]] Destination destination_for(const clang::QualType type, const std::string& key,
                                              const EntityKind kind) {
        Destination destination;
        destination.known = true;
        destination.key = key;
        destination.type = type;
        const FamilyMatch match = match_family(type, kind, stage_, context_);
        destination.family = match.family;
        destination.value_type = match.value_type;
        destination.converted = is_converted(key);
        const Family already = owned_family(type);
        if (already != Family::none) {
            destination.converted = true;
            destination.family = already;
            const clang::ClassTemplateSpecializationDecl* const owned =
                llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                    (*type.getNonReferenceType().getCanonicalType()).getAsCXXRecordDecl());
            if (owned != nullptr && (*owned).getTemplateArgs().size() > 0U &&
                (*owned).getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type) {
                destination.value_type = (*owned).getTemplateArgs()[0].getAsType();
            }
        }
        if (destination.family == Family::none) destination.family = expression_family(type, stage_);
        destination.dependent = (*type).isDependentType();
        return destination;
    }

    [[nodiscard]] Destination destination_for_parameter(const clang::FunctionDecl& callee, const unsigned index) {
        if (index >= callee.getNumParams()) return Destination();
        const clang::ParmVarDecl* const parameter = callee.getParamDecl(index);
        Destination destination = destination_for((*parameter).getType(), parameter_key(parameter),
                                                  EntityKind::parameter);
        const clang::FunctionDecl* const pattern = callee.getTemplateInstantiationPattern();
        if (pattern != nullptr && index < (*pattern).getNumParams() &&
            (*(*(*pattern).getParamDecl(index)).getType()).isDependentType()) {
            destination.dependent = true;
        }
        destination.ambiguous_braces = has_sibling_overload(callee);
        return destination;
    }

    [[nodiscard]] Destination destination_for_return(const clang::FunctionDecl& function) {
        Destination destination = destination_for(function.getReturnType(), return_key(&function),
                                                  EntityKind::function_return);
        destination.result_return = stage_ == Stage::v3_owned_text && options_.allocating_returns_result &&
            destination.converted && owned_family(function.getReturnType()) == Family::none;
        return destination;
    }

    [[nodiscard]] SourceShape classify(const clang::Expr* source) {
        SourceShape shape;
        const clang::Expr* expression = (*source).IgnoreImplicit();
        while (llvm::isa<clang::ParenExpr>(expression)) {
            expression = (*llvm::cast<clang::ParenExpr>(expression)).getSubExpr();
        }
        shape.expression = expression;
        if (llvm::isa<clang::ConditionalOperator>(expression)) {
            shape.kind = SourceKind::conditional;
            return shape;
        }
        const clang::CXXFunctionalCastExpr* const functional = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(expression);
        if (functional != nullptr && expression_family((*functional).getType(), stage_) != Family::none) {
            shape.kind = SourceKind::explicit_temporary;
            const clang::Expr* const inside = (*(*functional).getSubExpr()).IgnoreImplicit();
            const clang::CXXConstructExpr* const construction = llvm::dyn_cast<clang::CXXConstructExpr>(inside);
            if (construction != nullptr && (*construction).getNumArgs() > 0U) {
                shape.inner = (*construction).getArg(0U);
            } else if (construction == nullptr && !llvm::isa<clang::InitListExpr>(inside)) {
                shape.inner = inside;
            }
            return shape;
        }
        const clang::CXXTemporaryObjectExpr* const temporary = llvm::dyn_cast<clang::CXXTemporaryObjectExpr>(expression);
        if (temporary != nullptr && expression_family((*temporary).getType(), stage_) != Family::none) {
            shape.kind = SourceKind::explicit_temporary;
            if ((*temporary).getNumArgs() > 0U) shape.inner = (*temporary).getArg(0U);
            return shape;
        }
        const clang::CXXConstructExpr* const construction = llvm::dyn_cast<clang::CXXConstructExpr>(expression);
        if (construction != nullptr && expression_family((*construction).getType(), stage_) != Family::none) {
            if ((*construction).getNumArgs() == 0U) {
                shape.kind = SourceKind::empty;
                return shape;
            }
            if ((*construction).getNumArgs() == 1U && is_transparent_construction(*construction, stage_)) {
                const SourceShape inner = classify((*construction).getArg(0U));
                return inner;
            }
            if ((*construction).getNumArgs() == 1U) {
                const clang::Expr* const argument = (*(*construction).getArg(0U)).IgnoreImplicit();
                // A sequence or owner of another family (a vector made a
                // span): classify what it is made from.
                if (expression_family((*argument).getType(), stage_) != Family::none ||
                    owned_family((*argument).getType()) != Family::none) {
                    const SourceShape inner = classify(argument);
                    return inner;
                }
                shape.expression = argument;
                if (llvm::isa<clang::CXXStdInitializerListExpr>(argument)) {
                    shape.kind = SourceKind::init_list;
                } else if (is_nullopt_type((*argument).getType())) {
                    shape.kind = SourceKind::absent_literal;
                } else if (is_nullptr_type((*argument).getType())) {
                    shape.kind = SourceKind::null_pointer;
                } else if (llvm::isa<clang::StringLiteral>(argument)) {
                    shape.kind = SourceKind::literal;
                } else {
                    shape.kind = SourceKind::value;
                }
                return shape;
            }
            shape.kind = SourceKind::init_list;
            return shape;
        }
        const clang::InitListExpr* const list = llvm::dyn_cast<clang::InitListExpr>(expression);
        if (list != nullptr) {
            shape.kind = (*list).getNumInits() == 0U ? SourceKind::empty : SourceKind::init_list;
            return shape;
        }
        if (llvm::isa<clang::CXXNullPtrLiteralExpr>(expression)) {
            shape.kind = SourceKind::null_pointer;
            return shape;
        }
        // operator=(nullopt_t) takes the tag by value: a copy of std::nullopt.
        if (is_nullopt_type((*expression).getType())) {
            shape.kind = SourceKind::absent_literal;
            return shape;
        }
        if (is_nullptr_type((*expression).getType())) {
            shape.kind = SourceKind::null_pointer;
            return shape;
        }
        const clang::DeclRefExpr* const reference = llvm::dyn_cast<clang::DeclRefExpr>(expression);
        if (reference != nullptr && is_standard_entity((*reference).getDecl(), "nullopt")) {
            shape.kind = SourceKind::absent_literal;
            return shape;
        }
        const clang::CallExpr* const call = llvm::dyn_cast<clang::CallExpr>(expression);
        if (call != nullptr) {
            const clang::FunctionDecl* const callee = (*call).getDirectCallee();
            if (is_standard_entity(callee, "make_optional") || is_standard_entity(callee, "make_shared") ||
                is_standard_entity(callee, "make_unique")) {
                shape.kind = SourceKind::make_call;
                return shape;
            }
        }
        shape.denoted = denoted(expression);
        if (shape.denoted.found && is_converted(shape.denoted.key)) {
            shape.kind = SourceKind::converted;
            return shape;
        }
        if (is_text_view((*expression).getType())) {
            shape.kind = SourceKind::text_view;
            return shape;
        }
        if (owned_family((*expression).getType()) != Family::none) {
            shape.kind = SourceKind::owned;
            return shape;
        }
        if (llvm::isa<clang::StringLiteral>(expression)) {
            shape.kind = SourceKind::literal;
            return shape;
        }
        if (expression_family((*expression).getType(), stage_) != Family::none) {
            shape.kind = SourceKind::standard;
            return shape;
        }
        shape.kind = SourceKind::value;
        return shape;
    }

    [[nodiscard]] bool wants_edge(const std::string& destination, const std::string& source) const {
        if (destination.empty() || source.empty()) return false;
        if (options_.flow_edges || stage_ == Stage::v5_ownership) return true;
        const bool local = destination.compare(0U, 2U, "L:") == 0 || source.compare(0U, 2U, "L:") == 0;
        return local;
    }

    void edge(const std::string& first, const std::string& second, const bool strict) {
        EdgeFact fact;
        fact.first = first;
        fact.second = second;
        fact.strict = strict;
        harvest_.edges.push_back(fact);
    }

    // One value flowing into a destination. `assigned` is the left side
    // when the flow is an assignment (`lhs = source`).
    void flow(const clang::Expr* source, const Destination& destination, const clang::Expr* assigned) {
        if (source == nullptr) return;
        const clang::Expr* const plain = (*source).IgnoreImplicit();
        const clang::ConditionalOperator* const conditional =
            llvm::dyn_cast<clang::ConditionalOperator>((*plain).IgnoreParens());
        if (conditional != nullptr) {
            flow((*conditional).getTrueExpr(), destination, nullptr);
            flow((*conditional).getFalseExpr(), destination, nullptr);
            return;
        }
        if (plan_) {
            const Denoted from = denoted(source);
            if (!from.found || !wants_edge(destination.key, from.key)) return;
            const bool strict = stage_ == Stage::v5_ownership || destination.key.compare(0U, 2U, "L:") == 0;
            edge(destination.key, from.key, strict);
            return;
        }
        if (!destination.known) {
            const Denoted from = denoted(source);
            if (from.found && is_converted(from.key)) {
                residue_at((*source).getBeginLoc(),
                           "a converted value is passed where no parameter is declared (variadic or unresolved)");
            }
            return;
        }
        const SourceShape shape = classify(source);
        if (destination.converted) {
            into_converted(shape, destination, assigned, *source);
            return;
        }
        if (shape.kind == SourceKind::converted) out_of_converted(shape, destination, *source);
    }

    // The OperationResult<Utf8String> of a converted V3 call, read with fail-fast.
    void take_result(const Span& call) {
        wrap(call, "stxlib::take_or_fail_fast(", ")");
        need(call.path, include_fail_fast);
    }

    void into_converted(const SourceShape& shape, const Destination& destination, const clang::Expr* assigned,
                        const clang::Expr& source) {
        const clang::SourceLocation where = source.getBeginLoc();
        const Span span = span_of(shape.expression);
        if (shape.kind == SourceKind::converted || shape.kind == SourceKind::owned) {
            if (shape.kind == SourceKind::converted && shape.denoted.result_call && !destination.result_return) {
                if (!editable(span, where)) return;
                take_result(span);
            }
            const Family from = shape.kind == SourceKind::owned ? owned_family((*shape.expression).getType())
                                                                : expression_family((*shape.expression).getType(), stage_);
            const bool vector_into_view = from == Family::vector &&
                (destination.family == Family::view || destination.family == Family::mutable_view);
            if (vector_into_view) {
                if (!editable(span, where)) return;
                if (!(*shape.expression).isLValue()) {
                    residue_at(where, "a temporary vector passed as a view would dangle");
                    return;
                }
                close_at(span, destination.family == Family::view ? ".view()" : ".mutable_view()");
            }
            if (stage_ == Stage::v5_ownership && from == Family::unique && destination.family == Family::shared) {
                residue_at(where, "stxlib::Shared cannot adopt a stxlib::Unique");
            }
            if (destination.result_return && shape.kind == SourceKind::converted && !shape.denoted.result_call) {
                if (!editable(span, where)) return;
                wrap(span, "stxlib::OperationResult<stxlib::Utf8String>::success(", ")");
                need(span.path, include_result);
            }
            return;
        }
        if (shape.kind == SourceKind::empty) {
            if (assigned != nullptr) {
                assign_reset(*assigned, source, destination);
                return;
            }
            if (destination.ambiguous_braces) {
                residue_at(where, "a braced {} argument is ambiguous between the converted overload and its sibling");
            }
            return;
        }
        if (!editable(span, where)) return;
        const clang::DeclContext* const use = context_of(source);
        switch (destination.family) {
        case Family::optional:
            into_optional(shape, destination, assigned, source, span, use);
            return;
        case Family::text_view:
        case Family::text_reference:
            into_text_view(shape, span, where);
            return;
        case Family::owned_text:
            into_owned_text(shape, destination, assigned, source, span, use);
            return;
        case Family::vector:
            into_vector(shape, span, where);
            return;
        case Family::view:
        case Family::mutable_view:
            into_view(shape, destination, span, where, use);
            return;
        case Family::shared:
        case Family::weak:
        case Family::unique:
            into_ownership(shape, destination, assigned, source, span, use);
            return;
        default:
            break;
        }
        residue_at(where, "a value flows into a converted destination of an unknown family");
    }

    void assign_reset(const clang::Expr& assigned, const clang::Expr& source, const Destination& destination) {
        const Span left = span_of(&assigned);
        const Span right = span_of(&source);
        if (!editable(left, source.getBeginLoc()) || !right.valid || left.path != right.path) return;
        if (destination.family == Family::optional || destination.family == Family::shared ||
            destination.family == Family::weak || destination.family == Family::unique) {
            replace_bytes(left, left.end, right.end, ".reset()");
            return;
        }
        if (destination.family == Family::owned_text || destination.family == Family::vector) {
            replace_bytes(left, left.end, right.end, ".clear()");
            return;
        }
    }

    void into_optional(const SourceShape& shape, const Destination& destination, const clang::Expr* assigned,
                       const clang::Expr& source, const Span& span, const clang::DeclContext* use) {
        const clang::SourceLocation where = source.getBeginLoc();
        const std::string type = owned_type(Family::optional, destination.value_type, use);
        Span left;
        if (assigned != nullptr) {
            left = span_of(assigned);
            if (!left.valid || left.path != span.path) {
                residue_at(where, "the assignment's sides are not in one file");
                return;
            }
        }
        const Span whole = span_of(&source);
        switch (shape.kind) {
        case SourceKind::absent_literal:
            if (assigned != nullptr) {
                replace_bytes(left, left.end, whole.end, ".reset()");
            } else {
                replace_span(span, type + "::none()");
            }
            break;
        case SourceKind::value:
        case SourceKind::literal:
        case SourceKind::text_view:
            if (assigned != nullptr) {
                replace_bytes(left, left.end, span.begin, ".set(");
                close_at(span, ")");
            } else {
                wrap(span, type + "::of(", ")");
            }
            break;
        case SourceKind::make_call: {
            const clang::CallExpr* const call = llvm::cast<clang::CallExpr>(shape.expression);
            if ((*call).getNumArgs() != 1U) {
                residue_at(where, "std::make_optional with in-place arguments");
                return;
            }
            const Span argument = span_of((*call).getArg(0U));
            if (!argument.valid || argument.path != span.path) {
                residue_at(where, "std::make_optional's argument is spelled through a macro");
                return;
            }
            if (assigned != nullptr) {
                replace_bytes(left, left.end, argument.begin, ".set(");
            } else {
                replace_bytes(span, span.begin, argument.begin, type + "::of(");
            }
            break;
        }
        case SourceKind::explicit_temporary: {
            const bool absent = shape.inner == nullptr || is_nullopt_type((*shape.inner).getType());
            if (absent) {
                if (assigned != nullptr) {
                    replace_bytes(left, left.end, whole.end, ".reset()");
                } else {
                    replace_span(span, type + "::none()");
                }
                break;
            }
            const Span argument = span_of(shape.inner);
            if (!argument.valid || argument.path != span.path) {
                residue_at(where, "the temporary's argument is spelled through a macro");
                return;
            }
            if (assigned != nullptr) {
                replace_bytes(left, left.end, argument.begin, ".set(");
            } else {
                replace_bytes(span, span.begin, argument.begin, type + "::of(");
            }
            replace_bytes(span, argument.end, span.end, ")");
            break;
        }
        case SourceKind::standard:
            if (!bridge_allowed(span.path)) {
                residue_at(where, "a std::optional flows into a converted declaration in a public header (no bridge)");
                return;
            }
            wrap(span, "gui_forms::platform::stxlib_optional(", ")");
            need(span.path, include_bridge);
            break;
        default:
            residue_at(where, "this initialiser has no stxlib::Optional form (in-place or braced list)");
            return;
        }
        need(span.path, include_optional);
    }

    void into_text_view(const SourceShape& shape, const Span& span, const clang::SourceLocation where) {
        if (shape.kind == SourceKind::literal || shape.kind == SourceKind::text_view) return;
        if (!bridge_allowed(span.path)) {
            residue_at(where, "standard text passed as stxlib::TextView in a public header (no bridge)");
            return;
        }
        wrap(span, "stxlib::take_or_fail_fast(gui_forms::platform::validated_text(", "))");
        need(span.path, include_fail_fast | include_bridge);
    }

    void into_owned_text(const SourceShape& shape, const Destination& destination, const clang::Expr* assigned,
                         const clang::Expr& source, const Span& span, const clang::DeclContext* use) {
        (void)use;
        const clang::SourceLocation where = source.getBeginLoc();
        std::string value_open;
        std::string value_close;
        std::uint32_t headers = include_fail_fast | include_text;
        if (shape.kind == SourceKind::literal || shape.kind == SourceKind::text_view) {
            value_open = assigned != nullptr ? "" : "stxlib::take_or_fail_fast(stxlib::Utf8String::copy_of(";
            value_close = assigned != nullptr ? "" : "))";
        } else if (shape.kind == SourceKind::standard || is_standard_string((*shape.expression).getType()) ||
                   is_standard_string_view((*shape.expression).getType())) {
            if (!bridge_allowed(span.path)) {
                residue_at(where, "standard text flows into stxlib::Utf8String in a public header (no bridge)");
                return;
            }
            headers |= include_bridge;
            if (assigned != nullptr) {
                value_open = "stxlib::take_or_fail_fast(gui_forms::platform::validated_text(";
                value_close = "))";
            } else {
                value_open = "stxlib::take_or_fail_fast(gui_forms::platform::stxlib_text_copy(";
                value_close = "))";
            }
        } else {
            residue_at(where, "this value has no stxlib::Utf8String form");
            return;
        }
        if (assigned != nullptr) {
            const Span left = span_of(assigned);
            if (!left.valid || left.path != span.path) {
                residue_at(where, "the assignment's sides are not in one file");
                return;
            }
            open_at(left, "stxlib::check_or_fail_fast(");
            replace_bytes(left, left.end, span.begin, ".assign(" + value_open);
            close_at(span, value_close + "))");
            need(span.path, headers);
            return;
        }
        if (destination.result_return) {
            wrap(span, "stxlib::OperationResult<stxlib::Utf8String>::success(" + value_open, value_close + ")");
            headers |= include_result;
        } else {
            wrap(span, value_open, value_close);
        }
        need(span.path, headers);
    }

    void into_vector(const SourceShape& shape, const Span& span, const clang::SourceLocation where) {
        if (shape.kind != SourceKind::standard) {
            residue_at(where, "this initialiser has no stxlib::Vector form (a braced list or a size)");
            return;
        }
        if (!bridge_allowed(span.path)) {
            residue_at(where, "a std::vector flows into stxlib::Vector in a public header (no bridge)");
            return;
        }
        wrap(span, "stxlib::take_or_fail_fast(gui_forms::platform::stxlib_vector_copy(", "))");
        need(span.path, include_fail_fast | include_bridge | include_vector);
    }

    void into_view(const SourceShape& shape, const Destination& destination, const Span& span,
                   const clang::SourceLocation where, const clang::DeclContext* use) {
        if (!bridge_allowed(span.path)) {
            residue_at(where, "standard elements flow into a stxlib view in a public header (no bridge)");
            return;
        }
        const bool mutable_view = destination.family == Family::mutable_view;
        const std::string function = mutable_view ? "gui_forms::platform::stxlib_mutable_view"
                                                  : "gui_forms::platform::stxlib_view";
        if (shape.kind == SourceKind::standard && expression_family((*shape.expression).getType(), stage_) ==
                destination.family) {
            wrap(span, function + "(", ")");
        } else if (shape.kind == SourceKind::standard || shape.kind == SourceKind::value) {
            wrap(span, function + "<" + spell_type(destination.value_type.getUnqualifiedType(), use) + ">(", ")");
        } else {
            residue_at(where, "this initialiser has no stxlib view form");
            return;
        }
        need(span.path, include_bridge | include_view);
    }

    void into_ownership(const SourceShape& shape, const Destination& destination, const clang::Expr* assigned,
                        const clang::Expr& source, const Span& span, const clang::DeclContext* use) {
        const clang::SourceLocation where = source.getBeginLoc();
        if (shape.kind == SourceKind::null_pointer) {
            if (assigned != nullptr) {
                assign_reset(*assigned, source, destination);
                return;
            }
            replace_span(span, "{}");
            return;
        }
        if (shape.kind == SourceKind::explicit_temporary && shape.inner == nullptr) {
            replace_span(span, owned_type(destination.family, destination.value_type, use) + "()");
            need(span.path, family_includes(destination.family));
            return;
        }
        if (shape.kind != SourceKind::make_call) {
            residue_at(where, "no bridge between standard and stxlib ownership: convert the source too, or by hand");
            return;
        }
        const clang::CallExpr* const call = llvm::cast<clang::CallExpr>(shape.expression);
        const clang::FunctionDecl* const callee = (*call).getDirectCallee();
        const bool unique = is_standard_entity(callee, "make_unique");
        if ((unique && destination.family != Family::unique) || (!unique && destination.family != Family::shared)) {
            residue_at(where, "the make call does not match the converted destination");
            return;
        }
        const clang::DeclRefExpr* const callee_reference =
            llvm::dyn_cast<clang::DeclRefExpr>((*(*call).getCallee()).IgnoreImplicit());
        if (callee_reference == nullptr || (*callee_reference).getNumTemplateArgs() != 1U) {
            residue_at(where, "the make call does not name its type");
            return;
        }
        const Span callee_span = span_of((*callee_reference).getSourceRange());
        const Span argument = span_of((*callee_reference).getTemplateArgs()[0].getSourceRange());
        if (!callee_span.valid || !argument.valid || callee_span.path != span.path) {
            residue_at(where, "the make call is spelled through a macro");
            return;
        }
        const std::string made = std::string(unique ? "stxlib::Unique<" : "stxlib::Shared<") + text_of(argument) +
            ">::make";
        replace_span(callee_span, made);
        wrap(span, "stxlib::take_or_fail_fast(", ")");
        need(span.path, include_fail_fast | family_includes(destination.family));
    }

    // A converted value flowing into a destination that stays standard.
    void out_of_converted(const SourceShape& shape, const Destination& destination, const clang::Expr& source) {
        const clang::SourceLocation where = source.getBeginLoc();
        const Span span = span_of(shape.expression);
        if (!editable(span, where)) return;
        if (shape.denoted.result_call) take_result(span);
        const bool prvalue = shape.denoted.prvalue || shape.denoted.result_call;
        const clang::QualType target = destination.type;
        const Family source_family = stage_ == Stage::v2_text_view ? Family::text_view
            : expression_family((*shape.expression).getType(), stage_);

        if (stage_ == Stage::v2_text_view) {
            if (!bridge_allowed(span.path)) {
                residue_at(where, "a converted stxlib::TextView is used as standard text in a public header");
                return;
            }
            if (is_standard_string(target)) {
                wrap(span, "gui_forms::platform::standard_text_copy(", ")");
            } else {
                wrap(span, "gui_forms::platform::standard_text(", ")");
            }
            need(span.path, include_bridge);
            return;
        }
        if (destination.dependent) return;
        if (!bridge_allowed(span.path)) {
            residue_at(where, "a converted value flows into a standard declaration in a public header (no bridge)");
            return;
        }
        switch (source_family) {
        case Family::optional:
            if (expression_family(target, stage_) != Family::optional) break;
            wrap(span, "gui_forms::platform::standard_optional(", ")");
            need(span.path, include_bridge);
            return;
        case Family::owned_text:
            if (prvalue) {
                residue_at(where, "a temporary stxlib::Utf8String used as standard text: bind it to a local first");
                return;
            }
            if (is_standard_string(target)) {
                wrap(span, "gui_forms::platform::standard_text_copy(", ".view())");
            } else if (is_standard_string_view(target)) {
                wrap(span, "gui_forms::platform::standard_text(", ".view())");
            } else if (is_text_view(target)) {
                close_at(span, ".view()");
                return;
            } else {
                break;
            }
            need(span.path, include_bridge);
            return;
        case Family::vector:
            if (prvalue) {
                residue_at(where, "a temporary stxlib::Vector used as a standard sequence: bind it to a local first");
                return;
            }
            if (expression_family(target, stage_) == Family::vector) {
                wrap(span, "gui_forms::platform::standard_vector_copy(", ".view())");
            } else if (expression_family(target, stage_) == Family::view ||
                       expression_family(target, stage_) == Family::mutable_view) {
                wrap(span, "gui_forms::platform::standard_span(", ".view())");
            } else {
                break;
            }
            need(span.path, include_bridge);
            return;
        case Family::view:
        case Family::mutable_view:
            if (expression_family(target, stage_) != Family::view &&
                expression_family(target, stage_) != Family::mutable_view) {
                break;
            }
            wrap(span, "gui_forms::platform::standard_span(", ")");
            need(span.path, include_bridge);
            return;
        case Family::shared:
        case Family::weak:
        case Family::unique:
            residue_at(where, "a converted owner flows into a standard owner: there is no bridge");
            return;
        default:
            break;
        }
        if (expression_family(target, stage_) == Family::none && owned_family(target) == Family::none) return;
        residue_at(where, "a converted value flows into " + target.getAsString());
    }

    // -- uses -------------------------------------------------------------------

    void handle_use(const clang::Expr& expression, const Denoted& entity);
    void use_member(const clang::Expr& top, const Denoted& entity, bool xvalue, const clang::MemberExpr& member);
    void use_conversion(const clang::Expr& top, const clang::MemberExpr& member);
    void use_operator(const clang::Expr& top, const Denoted& entity, bool xvalue,
                      const clang::CXXOperatorCallExpr& operation);
    void use_comparison(const clang::Expr& top, const Denoted& entity, const clang::CXXOperatorCallExpr& operation);
    void use_fallback(const clang::Expr& top, const Denoted& entity, const char* what);

    clang::ASTContext& context_;
    clang::SourceManager& sources_;
    const clang::LangOptions& language_;
    const AnalysisInput& input_;
    const Options& options_;
    TranslationUnitHarvest& harvest_;
    const bool plan_;
    const Stage stage_;
    llvm::Regex only_;
    std::map<unsigned, std::string> paths_;
};

// -- declarations ----------------------------------------------------------------

bool Analysis::VisitFunctionDecl(clang::FunctionDecl* function) {
    if (function == nullptr || (*function).isImplicit()) return true;
    const std::string qualified = (*function).getQualifiedNameAsString();
    const std::string simple = (*function).getDeclName().isIdentifier() ? std::string((*function).getName())
                                                                         : qualified;
    const clang::FunctionTypeLoc prototype = (*function).getFunctionTypeLoc();
    const clang::TypeLoc return_loc = prototype.isNull() ? clang::TypeLoc() : prototype.getReturnLoc();
    if (!llvm::isa<clang::CXXConstructorDecl>(function) && !llvm::isa<clang::CXXDestructorDecl>(function)) {
        consider_entity(*function, return_key(function), EntityKind::function_return, (*function).getReturnType(),
                        return_loc, (*function).getLocation(), simple, qualified, function, 0U);
    }
    for (unsigned index = 0U; index < (*function).getNumParams(); ++index) {
        const clang::ParmVarDecl* const parameter = (*function).getParamDecl(index);
        const clang::TypeSourceInfo* const information = (*parameter).getTypeSourceInfo();
        const clang::TypeLoc written = information == nullptr ? clang::TypeLoc() : (*information).getTypeLoc();
        const clang::SourceLocation name = (*parameter).getLocation().isValid() ? (*parameter).getLocation()
                                                                               : (*parameter).getBeginLoc();
        consider_entity(*parameter, parameter_key(parameter), EntityKind::parameter, (*parameter).getType(), written,
                        name, simple, qualified, function, index);
    }
    if (plan_) {
        const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
        if (method != nullptr) {
            for (const clang::CXXMethodDecl* overridden : (*method).overridden_methods()) {
                edge(return_key(method), return_key(overridden), true);
                const unsigned count = std::min((*method).getNumParams(), (*overridden).getNumParams());
                for (unsigned index = 0U; index < count; ++index) {
                    edge(parameter_key((*method).getParamDecl(index)), parameter_key((*overridden).getParamDecl(index)),
                         true);
                }
            }
        }
    }
    return true;
}

bool Analysis::VisitVarDecl(clang::VarDecl* variable) {
    if (variable == nullptr || (*variable).isImplicit()) return true;
    const clang::ParmVarDecl* const parameter = llvm::dyn_cast<clang::ParmVarDecl>(variable);
    if (parameter != nullptr) {
        if ((*parameter).hasDefaultArg() && !(*parameter).hasUnparsedDefaultArg() &&
            !(*parameter).hasUninstantiatedDefaultArg()) {
            const Destination destination = destination_for((*parameter).getType(), parameter_key(parameter),
                                                            EntityKind::parameter);
            flow((*parameter).getDefaultArg(), destination, nullptr);
        }
        return true;
    }
    if (llvm::isa<clang::DecompositionDecl>(variable)) {
        if ((*variable).getInit() != nullptr) {
            const Denoted from = denoted((*variable).getInit());
            if (from.found && is_converted(from.key)) {
                residue_at((*variable).getLocation(), "a structured binding of a converted value");
            }
        }
        return true;
    }
    const bool local = (*variable).isLocalVarDecl();
    const bool deduced = (*(*variable).getType()).getContainedAutoType() != nullptr;
    const EntityKind kind = local ? EntityKind::local : EntityKind::variable;
    const std::string key = value_key(variable);
    if (!local || options_.propagate_locals || deduced) {
        const clang::TypeSourceInfo* const information = (*variable).getTypeSourceInfo();
        const clang::TypeLoc written = information == nullptr ? clang::TypeLoc() : (*information).getTypeLoc();
        consider_entity(*variable, key, kind, (*variable).getType(), written, (*variable).getLocation(),
                        std::string((*variable).getName()), local ? std::string() : (*variable).getQualifiedNameAsString(),
                        nullptr, 0U);
    }
    if ((*variable).getInit() == nullptr) return true;
    const bool entity_key = !local || options_.propagate_locals || deduced;
    Destination destination = destination_for((*variable).getType(), entity_key ? key : std::string(), kind);
    if (plan_ && deduced && local) {
        // An auto local takes its initialiser's type, so it changes with it.
        const Denoted from = denoted((*variable).getInit());
        if (from.found) edge(key, from.key, true);
        return true;
    }
    flow((*variable).getInit(), destination, nullptr);
    return true;
}

bool Analysis::VisitFieldDecl(clang::FieldDecl* field) {
    if (field == nullptr || (*field).isImplicit()) return true;
    const clang::TypeSourceInfo* const information = (*field).getTypeSourceInfo();
    const clang::TypeLoc written = information == nullptr ? clang::TypeLoc() : (*information).getTypeLoc();
    const std::string key = value_key(field);
    consider_entity(*field, key, EntityKind::field, (*field).getType(), written, (*field).getLocation(),
                    std::string((*field).getName()), (*field).getQualifiedNameAsString(), nullptr, 0U);
    if ((*field).hasInClassInitializer() && (*field).getInClassInitializer() != nullptr) {
        const Destination destination = destination_for((*field).getType(), key, EntityKind::field);
        flow((*field).getInClassInitializer(), destination, nullptr);
    }
    return true;
}

bool Analysis::VisitTypedefNameDecl(clang::TypedefNameDecl* alias) {
    if (alias == nullptr || (*alias).isImplicit()) return true;
    const clang::TypeSourceInfo* const information = (*alias).getTypeSourceInfo();
    const clang::TypeLoc written = information == nullptr ? clang::TypeLoc() : (*information).getTypeLoc();
    const std::string usr = usr_of(alias);
    if (usr.empty()) return true;
    consider_entity(*alias, "A:" + usr, EntityKind::alias, (*alias).getUnderlyingType(), written,
                    (*alias).getLocation(), std::string((*alias).getName()), (*alias).getQualifiedNameAsString(),
                    nullptr, 0U);
    return true;
}

bool Analysis::VisitCXXRecordDecl(clang::CXXRecordDecl* record) {
    if (stage_ != Stage::v5_ownership || record == nullptr || !(*record).hasDefinition()) return true;
    if (!(*record).isThisDeclarationADefinition()) return true;
    for (const clang::CXXBaseSpecifier& base : (*record).bases()) {
        const clang::QualType type = base.getType();
        if (expression_family(type, stage_) != Family::shared_from_this) continue;
        const clang::TypeSourceInfo* const information = base.getTypeSourceInfo();
        const clang::TypeLoc written = information == nullptr ? clang::TypeLoc() : (*information).getTypeLoc();
        consider_entity(*record, "B:" + usr_of(record), EntityKind::shared_base, type, written,
                        (*record).getLocation(), std::string((*record).getName()),
                        (*record).getQualifiedNameAsString(), nullptr, 0U);
    }
    return true;
}

bool Analysis::VisitCXXConstructorDecl(clang::CXXConstructorDecl* constructor) {
    if (constructor == nullptr || (*constructor).isImplicit()) return true;
    for (const clang::CXXCtorInitializer* initializer : (*constructor).inits()) {
        if (!(*initializer).isWritten() || !(*initializer).isMemberInitializer()) continue;
        const clang::FieldDecl* const field = (*initializer).getMember();
        const Destination destination = destination_for((*field).getType(), value_key(field), EntityKind::field);
        flow((*initializer).getInit(), destination, nullptr);
    }
    return true;
}

// -- statements and expressions ------------------------------------------------------

bool Analysis::VisitReturnStmt(clang::ReturnStmt* statement) {
    if (statement == nullptr || (*statement).getRetValue() == nullptr) return true;
    const clang::FunctionDecl* const function = enclosing_function(*statement);
    if (function == nullptr) return true;
    const Destination destination = destination_for_return(*function);
    flow((*statement).getRetValue(), destination, nullptr);
    return true;
}

bool Analysis::VisitCallExpr(clang::CallExpr* call) {
    if (call == nullptr) return true;
    const clang::FunctionDecl* const callee = (*call).getDirectCallee();

    // The call as a use of a converted return.
    if (callee != nullptr && !(*callee).isInStdNamespace()) {
        const Denoted entity = denoted(call);
        if (entity.found && entity.key == return_key(callee) && (plan_ || is_converted(entity.key))) {
            handle_use(*call, entity);
        }
    }
    if (callee == nullptr) {
        for (unsigned index = 0U; index < (*call).getNumArgs(); ++index) {
            const Denoted from = denoted((*call).getArg(index));
            if (from.found && is_converted(from.key)) {
                residue_at((*(*call).getArg(index)).getBeginLoc(), "a converted value passed to an unresolved callee");
            }
        }
        return true;
    }
    if (is_standard_entity(callee, "move") || is_standard_entity(callee, "forward") ||
        is_standard_entity(callee, "make_optional") || is_standard_entity(callee, "make_shared") ||
        is_standard_entity(callee, "make_unique") || pointer_cast_replacement(callee) != nullptr ||
        is_standard_entity(callee, "const_pointer_cast")) {
        return true;
    }
    unsigned offset = 0U;
    const clang::CXXOperatorCallExpr* const operation = llvm::dyn_cast<clang::CXXOperatorCallExpr>(call);
    if (operation != nullptr) {
        const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
        const bool member_operator = method != nullptr && !(*method).isStatic();
        if ((*operation).getOperator() == clang::OO_Equal && member_operator && (*operation).getNumArgs() == 2U) {
            const clang::Expr* const left = (*operation).getArg(0U);
            const clang::Expr* const right = (*operation).getArg(1U);
            const Denoted target = denoted(left);
            Destination destination = destination_for((*left).getType(), target.found ? target.key : std::string(),
                                                      EntityKind::field);
            if ((*(*method).getParent()).isInStdNamespace() || owned_family((*left).getType()) != Family::none) {
                flow(right, destination, left);
            }
            return true;
        }
        if ((*callee).isInStdNamespace()) return true;
        if (method != nullptr && (*(*method).getParent()).isInStdNamespace()) return true;
        if (member_operator) offset = 1U;
    }
    for (unsigned index = offset; index < (*call).getNumArgs(); ++index) {
        const clang::Expr* const argument = (*call).getArg(index);
        if (llvm::isa<clang::CXXDefaultArgExpr>(argument)) continue;
        const Destination destination = destination_for_parameter(*callee, index - offset);
        flow(argument, destination, nullptr);
    }
    return true;
}

bool Analysis::VisitCXXConstructExpr(clang::CXXConstructExpr* construction) {
    if (construction == nullptr) return true;
    if (is_transparent_construction(*construction, stage_)) return true;
    const clang::CXXConstructorDecl* const constructor = (*construction).getConstructor();
    if (constructor == nullptr) return true;
    if (expression_family((*construction).getType(), stage_) != Family::none) return true;
    if (owned_family((*construction).getType()) != Family::none) return true;
    for (unsigned index = 0U; index < (*construction).getNumArgs(); ++index) {
        const clang::Expr* const argument = (*construction).getArg(index);
        if (llvm::isa<clang::CXXDefaultArgExpr>(argument)) continue;
        const Destination destination = destination_for_parameter(*constructor, index);
        flow(argument, destination, nullptr);
    }
    return true;
}

bool Analysis::VisitInitListExpr(clang::InitListExpr* list) {
    if (list == nullptr) return true;
    const clang::InitListExpr* semantic = list;
    if ((*list).isSyntacticForm() && (*list).getSemanticForm() != nullptr) semantic = (*list).getSemanticForm();
    const clang::CXXRecordDecl* const record = (*(*semantic).getType().getCanonicalType()).getAsCXXRecordDecl();
    if (record == nullptr || !(*record).hasDefinition() || (*record).isUnion()) return true;
    if ((*record).isInStdNamespace()) return true;
    if (owned_family((*semantic).getType()) != Family::none) return true;
    unsigned index = (*record).getNumBases();
    for (const clang::FieldDecl* field : (*record).fields()) {
        if ((*field).isUnnamedBitField()) continue;
        if (index >= (*semantic).getNumInits()) break;
        const clang::Expr* const initializer = (*semantic).getInit(index);
        ++index;
        if (initializer == nullptr) continue;
        if (llvm::isa<clang::ImplicitValueInitExpr>(initializer) || llvm::isa<clang::CXXDefaultInitExpr>(initializer) ||
            llvm::isa<clang::NoInitExpr>(initializer)) {
            continue;
        }
        const Destination destination = destination_for((*field).getType(), value_key(field), EntityKind::field);
        flow(initializer, destination, nullptr);
    }
    return true;
}

bool Analysis::VisitDeclRefExpr(clang::DeclRefExpr* reference) {
    if (reference == nullptr || !llvm::isa<clang::VarDecl>((*reference).getDecl())) return true;
    const Denoted entity = denoted(reference);
    if (!entity.found) return true;
    if (!plan_ && !is_converted(entity.key)) return true;
    handle_use(*reference, entity);
    return true;
}

bool Analysis::VisitMemberExpr(clang::MemberExpr* member) {
    if (member == nullptr) return true;
    const clang::ValueDecl* const target = (*member).getMemberDecl();
    if (!llvm::isa<clang::FieldDecl>(target) && !llvm::isa<clang::VarDecl>(target)) return true;
    const Denoted entity = denoted(member);
    if (!entity.found) return true;
    if (!plan_ && !is_converted(entity.key)) return true;
    handle_use(*member, entity);
    return true;
}

// -- uses -------------------------------------------------------------------------

void Analysis::handle_use(const clang::Expr& expression, const Denoted& entity) {
    const clang::Expr* top = &expression;
    bool xvalue = false;
    clang::DynTypedNode parent_node;
    bool found = false;
    for (unsigned guard = 0U; guard < 64U; ++guard) {
        parent_node = parent_of(clang::DynTypedNode::create(*top), found);
        if (!found) return;
        const clang::Expr* const parent = parent_node.get<clang::Expr>();
        if (parent == nullptr) break;
        if (is_transparent_parent(*parent, stage_)) {
            top = parent;
            continue;
        }
        const clang::CallExpr* const call = llvm::dyn_cast<clang::CallExpr>(parent);
        if (call != nullptr && !llvm::isa<clang::CXXOperatorCallExpr>(call) && (*call).getNumArgs() == 1U &&
            (*call).getArg(0U) == top) {
            const clang::FunctionDecl* const callee = (*call).getDirectCallee();
            if (is_standard_entity(callee, "move") || is_standard_entity(callee, "forward")) {
                top = parent;
                xvalue = true;
                continue;
            }
            const char* const cast = stage_ == Stage::v5_ownership ? pointer_cast_replacement(callee) : nullptr;
            if (cast != nullptr) {
                const clang::DeclRefExpr* const callee_reference =
                    llvm::dyn_cast<clang::DeclRefExpr>((*(*call).getCallee()).IgnoreImplicit());
                if (callee_reference != nullptr) {
                    const Span name = token_span((*callee_reference).getNameInfo().getLoc());
                    if (editable(name, (*call).getBeginLoc())) {
                        bool clean = true;
                        const unsigned start = qualifier_start(buffer_of(name.file), name.begin, clean);
                        replace_bytes(name, start, name.end, cast);
                        need(name.path, include_shared);
                    }
                }
                top = parent;
                continue;
            }
        }
        if (stage_ == Stage::v5_ownership) {
            const clang::MemberExpr* const member = llvm::dyn_cast<clang::MemberExpr>(parent);
            if (member != nullptr && (*(*member).getMemberDecl()).getDeclName().isIdentifier() &&
                (*(*member).getMemberDecl()).getName() == "lock") {
                bool call_found = false;
                const clang::DynTypedNode call_node = parent_of(clang::DynTypedNode::create(*member), call_found);
                const clang::Expr* const lock_call = call_found ? call_node.get<clang::Expr>() : nullptr;
                if (lock_call != nullptr) {
                    top = lock_call;
                    continue;
                }
            }
        }
        if (llvm::isa<clang::ConditionalOperator>(parent)) {
            top = parent;
            continue;
        }
        break;
    }
    const clang::Expr* const parent = parent_node.get<clang::Expr>();
    if (entity.result_call && parent != nullptr && (llvm::isa<clang::MemberExpr>(parent) ||
                                                    llvm::isa<clang::CXXOperatorCallExpr>(parent))) {
        const Span call = span_of(top);
        if (editable(call, (*top).getBeginLoc())) take_result(call);
    }
    if (parent == nullptr) {
        const clang::VarDecl* const variable = parent_node.get<clang::VarDecl>();
        if (variable != nullptr && (*variable).isImplicit()) {
            // The range of a range-based for.
            if (stage_ == Stage::v4_sequences && expression_family((*top).getType(), stage_) == Family::vector) return;
            residue_at((*top).getBeginLoc(), "a converted value iterated by a range-based for (no begin/end)");
            return;
        }
        if (variable != nullptr || parent_node.get<clang::FieldDecl>() != nullptr ||
            parent_node.get<clang::CXXCtorInitializer>() != nullptr) {
            return;
        }
        const clang::Stmt* const statement = parent_node.get<clang::Stmt>();
        if (statement != nullptr && llvm::isa<clang::CXXForRangeStmt>(statement)) {
            if (stage_ == Stage::v4_sequences && expression_family((*top).getType(), stage_) == Family::vector) return;
            residue_at((*top).getBeginLoc(), "a converted value iterated by a range-based for (no begin/end)");
            return;
        }
        if (statement != nullptr) return;
        residue_at((*top).getBeginLoc(), "an unclassified use of a converted value");
        return;
    }
    const clang::MemberExpr* const member = llvm::dyn_cast<clang::MemberExpr>(parent);
    if (member != nullptr) {
        use_member(*top, entity, xvalue, *member);
        return;
    }
    const clang::CXXOperatorCallExpr* const operation = llvm::dyn_cast<clang::CXXOperatorCallExpr>(parent);
    if (operation != nullptr) {
        use_operator(*top, entity, xvalue, *operation);
        return;
    }
    if (llvm::isa<clang::CallExpr>(parent) || llvm::isa<clang::CXXConstructExpr>(parent) ||
        llvm::isa<clang::InitListExpr>(parent)) {
        return;
    }
    const clang::UnaryOperator* const unary = llvm::dyn_cast<clang::UnaryOperator>(parent);
    if (unary != nullptr && (*unary).getOpcode() == clang::UO_AddrOf) return;
    use_fallback(*top, entity, (*parent).getStmtClassName());
}

void Analysis::use_fallback(const clang::Expr& top, const Denoted& entity, const char* what) {
    (void)entity;
    const Span span = span_of(&top);
    if (stage_ == Stage::v2_text_view && editable(span, top.getBeginLoc())) {
        if (!bridge_allowed(span.path)) {
            residue_at(top.getBeginLoc(), std::string("a converted stxlib::TextView used as standard text (") + what +
                                              ") in a public header");
            return;
        }
        wrap(span, "gui_forms::platform::standard_text(", ")");
        need(span.path, include_bridge);
        return;
    }
    if (stage_ == Stage::v3_owned_text && editable(span, top.getBeginLoc()) && top.isLValue()) {
        if (!bridge_allowed(span.path)) {
            residue_at(top.getBeginLoc(), std::string("a converted stxlib::Utf8String used as standard text (") +
                                              what + ") in a public header");
            return;
        }
        wrap(span, "gui_forms::platform::standard_text(", ".view())");
        need(span.path, include_bridge);
        return;
    }
    residue_at(top.getBeginLoc(), std::string("a converted value used in a form with no rule (") + what + ")");
}

void Analysis::use_conversion(const clang::Expr& top, const clang::MemberExpr& member) {
    if (stage_ == Stage::v5_ownership) return;
    if (stage_ != Stage::v1_optional) {
        residue_at(top.getBeginLoc(), "a conversion operator of the standard type has no counterpart");
        return;
    }
    const Span span = span_of(&top);
    if (!editable(span, top.getBeginLoc())) return;
    bool found = false;
    clang::DynTypedNode node = parent_of(clang::DynTypedNode::create(member), found);
    const clang::Expr* above = found ? node.get<clang::Expr>() : nullptr;
    for (unsigned guard = 0U; guard < 8U && above != nullptr; ++guard) {
        if (!llvm::isa<clang::CXXMemberCallExpr>(above) && !llvm::isa<clang::ImplicitCastExpr>(above) &&
            !llvm::isa<clang::ParenExpr>(above)) {
            break;
        }
        node = parent_of(clang::DynTypedNode::create(*above), found);
        const clang::Expr* const next = found ? node.get<clang::Expr>() : nullptr;
        if (next == nullptr) {
            above = nullptr;
            break;
        }
        const clang::UnaryOperator* const negation = llvm::dyn_cast<clang::UnaryOperator>(next);
        if (negation != nullptr && (*negation).getOpcode() == clang::UO_LNot) {
            const Span operator_span = token_span((*negation).getOperatorLoc());
            if (operator_span.valid && operator_span.path == span.path) {
                replace_span(operator_span, "");
                close_at(span, ".absent()");
                return;
            }
        }
        above = next;
    }
    close_at(span, ".present()");
}

void Analysis::use_member(const clang::Expr& top, const Denoted& entity, const bool xvalue,
                          const clang::MemberExpr& member) {
    const clang::ValueDecl* const target = member.getMemberDecl();
    if (llvm::isa<clang::CXXConversionDecl>(target)) {
        use_conversion(top, member);
        return;
    }
    if (plan_) return;
    const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(target);
    if (method == nullptr || !(*method).getDeclName().isIdentifier()) {
        use_fallback(top, entity, "a member that is not a named method");
        return;
    }
    bool found = false;
    const clang::DynTypedNode call_node = parent_of(clang::DynTypedNode::create(member), found);
    const clang::CXXMemberCallExpr* const call = found ? call_node.get<clang::CXXMemberCallExpr>() : nullptr;
    if (call == nullptr) {
        residue_at(top.getBeginLoc(), "a method of a converted value named without a call");
        return;
    }
    const llvm::StringRef name = (*method).getName();
    const Span object = span_of(&top);
    const Span name_token = token_span(member.getMemberLoc());
    const Span whole = span_of(call);
    if (!editable(object, top.getBeginLoc()) || !name_token.valid || !whole.valid) return;
    const Family family = stage_ == Stage::v2_text_view ? Family::text_view
                                                         : expression_family(written_type(top), stage_);
    const unsigned arguments = (*call).getNumArgs();

    switch (family) {
    case Family::optional: {
        if (name == "has_value") {
            bool above_found = false;
            clang::DynTypedNode node = parent_of(clang::DynTypedNode::create(*call), above_found);
            const clang::Expr* above = above_found ? node.get<clang::Expr>() : nullptr;
            while (above != nullptr && (llvm::isa<clang::ImplicitCastExpr>(above) || llvm::isa<clang::ParenExpr>(above))) {
                node = parent_of(clang::DynTypedNode::create(*above), above_found);
                above = above_found ? node.get<clang::Expr>() : nullptr;
            }
            const clang::UnaryOperator* const negation = llvm::dyn_cast_or_null<clang::UnaryOperator>(above);
            if (negation != nullptr && (*negation).getOpcode() == clang::UO_LNot) {
                const Span operator_span = token_span((*negation).getOperatorLoc());
                if (operator_span.valid && operator_span.path == object.path) {
                    replace_span(operator_span, "");
                    replace_span(name_token, "absent");
                    return;
                }
            }
            replace_span(name_token, "present");
            return;
        }
        if (name == "value") {
            if (xvalue) {
                residue_at(top.getBeginLoc(), "value() on a moved std::optional: use take() by hand");
                return;
            }
            if (entity.prvalue || entity.result_call) {
                replace_span(name_token, "take");
                return;
            }
            open_at(whole, "(*");
            close_at(whole, ")");
            return;
        }
        if (name == "value_or" || name == "reset") return;
        if (name == "emplace") {
            replace_span(name_token, "set");
            if (arguments == 1U) return;
            const clang::DeclContext* const use = context_of(top);
            const FamilyMatch match = match_family(top.getType(), EntityKind::field, stage_, context_);
            const std::string value_type = spell_type(match.value_type, use);
            if (arguments == 0U) {
                replace_bytes(whole, name_token.end, whole.end, "(" + value_type + "{})");
                return;
            }
            const Span first = span_of((*call).getArg(0U));
            const Span last = span_of((*call).getArg(arguments - 1U));
            if (!first.valid || !last.valid) {
                residue_at(top.getBeginLoc(), "emplace arguments spelled through a macro");
                return;
            }
            Span arguments_span = first;
            arguments_span.end = last.end;
            wrap(arguments_span, value_type + "(", ")");
            return;
        }
        residue_at(top.getBeginLoc(), "std::optional::" + name.str() + " has no stxlib::Optional counterpart");
        return;
    }
    case Family::text_view:
        if (name == "size" || name == "length") {
            replace_span(name_token, "count");
            return;
        }
        if (name == "empty" || name == "data") return;
        use_fallback(top, entity, "a standard text method");
        return;
    case Family::owned_text:
        if (name == "size" || name == "length") {
            replace_span(name_token, "count");
            return;
        }
        if (name == "c_str" || name == "data") {
            replace_span(name_token, "c_text");
            return;
        }
        if (name == "empty" || name == "clear") return;
        use_fallback(top, entity, "a std::string method");
        return;
    case Family::vector: {
        if (name == "size") {
            replace_span(name_token, "count");
            return;
        }
        if (name == "empty" || name == "clear" || name == "begin" || name == "end") return;
        if (name == "front" || name == "back" || name == "pop_back" || name == "data") {
            const char* const replacement = name == "front" ? "first_element"
                : name == "back" ? "last_element"
                : name == "pop_back" ? "remove_last" : "begin";
            replace_span(name_token, replacement);
            return;
        }
        if (name == "push_back" || name == "emplace_back" || name == "reserve" ||
            (name == "resize" && arguments == 1U)) {
            std::string replacement = name.str();
            if (name == "push_back") {
                const clang::Expr* const argument = (*(*call).getArg(0U)).IgnoreImplicit();
                const bool movable = (*argument).isPRValue() || (*argument).isXValue();
                replacement = movable ? "push" : "push_copy";
            } else if (name == "emplace_back") {
                replacement = "emplace";
            }
            replace_span(name_token, replacement);
            wrap(whole, "stxlib::check_or_fail_fast(", ")");
            need(whole.path, include_fail_fast);
            return;
        }
        residue_at(top.getBeginLoc(), "std::vector::" + name.str() + " has no stxlib::Vector rule");
        return;
    }
    case Family::view:
    case Family::mutable_view:
        if (name == "size") {
            replace_span(name_token, "count");
            return;
        }
        if (name == "empty" || name == "data" || name == "first") return;
        if (name == "subspan" && arguments == 1U) {
            replace_span(name_token, "from");
            return;
        }
        residue_at(top.getBeginLoc(), "std::span::" + name.str() + " has no stxlib view rule");
        return;
    case Family::shared:
    case Family::weak:
    case Family::unique:
        if ((name == "reset" && arguments == 0U) || name == "get" || name == "lock" || name == "expired") return;
        if (name == "use_count") {
            replace_span(name_token, "owner_count");
            return;
        }
        residue_at(top.getBeginLoc(), "the standard owner's " + name.str() + " has no stxlib counterpart");
        return;
    default:
        break;
    }
    use_fallback(top, entity, "a method");
}

void Analysis::use_operator(const clang::Expr& top, const Denoted& entity, const bool xvalue,
                            const clang::CXXOperatorCallExpr& operation) {
    const clang::OverloadedOperatorKind kind = operation.getOperator();
    if (kind == clang::OO_EqualEqual || kind == clang::OO_ExclaimEqual) {
        use_comparison(top, entity, operation);
        return;
    }
    if (kind == clang::OO_Equal) return;
    const clang::FunctionDecl* const callee = operation.getDirectCallee();
    if (callee != nullptr && !(*callee).isInStdNamespace()) {
        const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
        if (method == nullptr || !(*(*method).getParent()).isInStdNamespace()) return;
    }
    if (plan_) return;
    const Span object = span_of(&top);
    if (!editable(object, top.getBeginLoc())) return;
    const Family family = expression_family(written_type(top), stage_);
    const Span operator_span = token_span(operation.getOperatorLoc());

    if (kind == clang::OO_Arrow && operation.getArg(0U) == &top) {
        if (!operator_span.valid || operator_span.path != object.path) {
            residue_at(top.getBeginLoc(), "the arrow is spelled through a macro");
            return;
        }
        Span opened = object;
        opened.end = operator_span.end;
        if (family == Family::optional) {
            if (xvalue) {
                residue_at(top.getBeginLoc(), "-> on a moved std::optional");
                return;
            }
            if (entity.prvalue || entity.result_call) {
                replace_span(operator_span, ".take().");
                return;
            }
            open_at(opened, "(*");
            replace_span(operator_span, ".value()).");
            return;
        }
        if (family == Family::shared || family == Family::unique) {
            open_at(opened, "(*");
            replace_span(operator_span, ").");
            return;
        }
    }
    if (kind == clang::OO_Star && operation.getNumArgs() == 1U) {
        if (family != Family::optional) return;
        if (!operator_span.valid || operator_span.path != object.path) {
            residue_at(top.getBeginLoc(), "the dereference is spelled through a macro");
            return;
        }
        if (xvalue) {
            residue_at(top.getBeginLoc(), "* on a moved std::optional: use take() by hand");
            return;
        }
        if (entity.prvalue || entity.result_call) {
            replace_span(operator_span, "");
            close_at(object, ".take()");
            return;
        }
        replace_span(operator_span, "(*");
        close_at(object, ".value())");
        return;
    }
    if (kind == clang::OO_Subscript && operation.getArg(0U) == &top &&
        (family == Family::vector || family == Family::view || family == Family::mutable_view)) {
        return;
    }
    if (kind == clang::OO_PlusEqual && family == Family::owned_text && operation.getArg(0U) == &top &&
        operation.getNumArgs() == 2U) {
        const clang::Expr* const right = operation.getArg(1U);
        const Span right_span = span_of(right);
        if (!right_span.valid || right_span.path != object.path) return;
        const clang::Expr* const right_plain = (*right).IgnoreImplicit();
        std::string open = ".append(";
        std::string close = "))";
        std::uint32_t headers = include_fail_fast;
        if (llvm::isa<clang::StringLiteral>(right_plain) || is_text_view((*right_plain).getType())) {
        } else if (owned_family((*right_plain).getType()) == Family::owned_text && (*right_plain).isLValue()) {
            close = ".view()))";
        } else if (bridge_allowed(object.path)) {
            open = ".append(stxlib::take_or_fail_fast(gui_forms::platform::validated_text(";
            close = "))))";
            headers |= include_bridge;
        } else {
            residue_at(top.getBeginLoc(), "+= of standard text in a public header (no bridge)");
            return;
        }
        open_at(object, "stxlib::check_or_fail_fast(");
        replace_bytes(object, object.end, right_span.begin, open);
        close_at(right_span, close);
        need(object.path, headers);
        return;
    }
    if (kind == clang::OO_Less || kind == clang::OO_Greater || kind == clang::OO_LessEqual ||
        kind == clang::OO_GreaterEqual || kind == clang::OO_Spaceship) {
        residue_at(top.getBeginLoc(), "an ordering comparison: stxlib's vocabulary has no ordering, compare by hand");
        return;
    }
    use_fallback(top, entity, "an operator");
}

void Analysis::use_comparison(const clang::Expr& top, const Denoted& entity,
                              const clang::CXXOperatorCallExpr& operation) {
    if (operation.getNumArgs() != 2U) return;
    const bool top_left = operation.getArg(0U) == &top;
    const clang::Expr* const other = operation.getArg(top_left ? 1U : 0U);
    const Denoted other_entity = denoted(other);
    if (plan_) {
        if (other_entity.found && other_entity.key != entity.key && (options_.flow_edges || stage_ == Stage::v5_ownership)) {
            edge(entity.key, other_entity.key, stage_ == Stage::v5_ownership);
        }
        return;
    }
    bool equal = operation.getOperator() == clang::OO_EqualEqual;
    bool rewritten_found = false;
    const clang::DynTypedNode rewritten_node = parent_of(clang::DynTypedNode::create(operation), rewritten_found);
    const clang::CXXRewrittenBinaryOperator* const rewritten =
        rewritten_found ? rewritten_node.get<clang::CXXRewrittenBinaryOperator>() : nullptr;
    if (rewritten != nullptr) equal = (*rewritten).getOperator() == clang::BO_EQ;

    const Span object = span_of(&top);
    const Span other_span = span_of(other);
    if (!editable(object, top.getBeginLoc())) return;
    if (!other_span.valid || other_span.path != object.path) {
        residue_at(top.getBeginLoc(), "a comparison whose other side is spelled through a macro");
        return;
    }
    if (other_entity.found && is_converted(other_entity.key)) return;
    const clang::Expr* const other_plain = strip_value(other, stage_);
    if (owned_family((*other_plain).getType()) != Family::none &&
        owned_family((*other_plain).getType()) == owned_family(top.getType())) {
        return;
    }
    const Family family = stage_ == Stage::v2_text_view ? Family::text_view : expression_family(written_type(top), stage_);

    // nullopt and nullptr: absence tests.
    const bool absent_literal = is_nullopt_type((*other_plain).getType());
    const bool null_literal = is_nullptr_type((*other_plain).getType());
    if ((family == Family::optional && absent_literal) ||
        ((family == Family::shared || family == Family::unique || family == Family::weak) && null_literal)) {
        const std::string test = family == Family::optional ? (equal ? ".absent()" : ".present()")
                                                            : (equal ? ".empty()" : ".present()");
        if (top_left) {
            replace_bytes(object, object.end, other_span.end, test);
        } else {
            replace_bytes(object, other_span.begin, object.begin, "");
            close_at(object, test);
        }
        return;
    }
    switch (family) {
    case Family::optional: {
        if (expression_family((*other_plain).getType(), stage_) == Family::optional) {
            if (!bridge_allowed(other_span.path)) {
                residue_at(top.getBeginLoc(), "compared with a std::optional in a public header (no bridge)");
                return;
            }
            wrap(other_span, "gui_forms::platform::stxlib_optional(", ")");
            need(other_span.path, include_bridge);
            return;
        }
        const FamilyMatch match = match_family(top.getType(), EntityKind::field, stage_, context_);
        const std::string type = owned_type(Family::optional, match.value_type, context_of(top));
        wrap(other_span, type + "::of(", ")");
        need(other_span.path, include_optional);
        return;
    }
    case Family::text_view:
        if (llvm::isa<clang::StringLiteral>(other_plain) || is_text_view((*other_plain).getType())) return;
        use_fallback(top, entity, "a comparison with standard text");
        return;
    case Family::owned_text: {
        const bool literal = llvm::isa<clang::StringLiteral>(other_plain) || is_text_view((*other_plain).getType());
        std::string argument_open;
        std::string argument_close;
        std::uint32_t headers = include_none;
        if (!literal) {
            if (!bridge_allowed(object.path)) {
                residue_at(top.getBeginLoc(), "compared with standard text in a public header (no bridge)");
                return;
            }
            argument_open = "stxlib::take_or_fail_fast(gui_forms::platform::validated_text(";
            argument_close = "))";
            headers = include_fail_fast | include_bridge;
        }
        if (top_left) {
            if (!equal) open_at(object, "!");
            replace_bytes(object, object.end, other_span.begin, ".equals(" + argument_open);
            close_at(other_span, argument_close + ")");
        } else {
            if (!literal) {
                residue_at(top.getBeginLoc(), "standard text compared with converted text on the right: reorder by hand");
                return;
            }
            const std::string other_text = text_of(other_span);
            replace_bytes(object, other_span.begin, object.begin, equal ? "" : "!");
            close_at(object, ".equals(" + other_text + ")");
        }
        need(object.path, headers);
        return;
    }
    default:
        break;
    }
    residue_at(top.getBeginLoc(), "a comparison with a value of another vocabulary");
}

// -- the frontend plumbing -------------------------------------------------------------

class Consumer final : public clang::ASTConsumer {
public:
    Consumer(const AnalysisInput& input, TranslationUnitHarvest& harvest) : input_(input), harvest_(harvest) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        Analysis analysis(context, input_, harvest_);
        analysis.run();
    }

private:
    const AnalysisInput& input_;
    TranslationUnitHarvest& harvest_;
};

class Action final : public clang::ASTFrontendAction {
public:
    Action(const AnalysisInput& input, TranslationUnitHarvest& harvest) : input_(input), harvest_(harvest) {}

protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& instance,
                                                          llvm::StringRef file) override {
        (void)instance;
        (void)file;
        std::unique_ptr<clang::ASTConsumer> consumer = std::make_unique<Consumer>(input_, harvest_);
        return consumer;
    }

private:
    const AnalysisInput& input_;
    TranslationUnitHarvest& harvest_;
};

class Factory final : public clang::tooling::FrontendActionFactory {
public:
    Factory(const AnalysisInput& input, TranslationUnitHarvest& harvest) : input_(input), harvest_(harvest) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        std::unique_ptr<clang::FrontendAction> action = std::make_unique<Action>(input_, harvest_);
        return action;
    }

private:
    const AnalysisInput input_;
    TranslationUnitHarvest& harvest_;
};

} // namespace

std::unique_ptr<clang::tooling::FrontendActionFactory> make_action_factory(const AnalysisInput& input,
                                                                          TranslationUnitHarvest& harvest) {
    std::unique_ptr<clang::tooling::FrontendActionFactory> factory = std::make_unique<Factory>(input, harvest);
    return factory;
}

} // namespace d15
