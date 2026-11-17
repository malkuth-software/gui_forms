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
    // V2: a first-party parameter the stage could convert (a by-value
    // std::string_view or a const std::string&), whether or not it does.
    bool candidate{false};
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

// The std::string whose implicit conversion to std::string_view
// `expression` is (`const std::string& name` passed where a view is
// taken), or null. V2 converts both to stxlib::TextView, so the
// conversion denotes its object (the 2026-11-16 V2 run's "conversion
// operator" residue).
[[nodiscard]] const clang::Expr* standard_text_conversion_object(const clang::Expr* expression) {
    const clang::CXXMemberCallExpr* const call = llvm::dyn_cast_or_null<clang::CXXMemberCallExpr>(expression);
    if (call == nullptr) return nullptr;
    if (!llvm::isa_and_nonnull<clang::CXXConversionDecl>((*call).getMethodDecl())) return nullptr;
    const clang::Expr* const object = (*call).getImplicitObjectArgument();
    if (object == nullptr) return nullptr;
    if (!is_standard_string((*object).getType()) || !is_standard_string_view((*call).getType())) return nullptr;
    return object;
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
        const clang::Expr* const converted_object =
            stage == Stage::v2_text_view ? standard_text_conversion_object(expression) : nullptr;
        if (converted_object != nullptr) {
            expression = converted_object;
            continue;
        }
        break;
    }
    return expression;
}

// The string literal an expression is, as written or behind the implicit
// construction that makes it a std::string_view or std::string argument
// (`name == "gauge"`), or null.
[[nodiscard]] const clang::StringLiteral* literal_text(const clang::Expr* expression) {
    for (unsigned guard = 0U; guard < 16U && expression != nullptr; ++guard) {
        const clang::Expr* const plain = (*(*expression).IgnoreImplicit()).IgnoreParens();
        const clang::StringLiteral* const literal = llvm::dyn_cast<clang::StringLiteral>(plain);
        if (literal != nullptr) return literal;
        const clang::CXXConstructExpr* const construction = llvm::dyn_cast<clang::CXXConstructExpr>(plain);
        if (construction == nullptr || llvm::isa<clang::CXXTemporaryObjectExpr>(construction) ||
            (*construction).getNumArgs() == 0U) {
            return nullptr;
        }
        expression = (*construction).getArg(0U);
    }
    return nullptr;
}

[[nodiscard]] bool is_literal_text(const clang::Expr* expression) {
    const bool result = literal_text(expression) != nullptr;
    return result;
}

// True when `bytes` are well-formed UTF-8 (Unicode table 3-7: no overlong
// forms, no surrogates, nothing above U+10FFFF), as stxlib::TextView's
// consteval literal constructor requires.
[[nodiscard]] bool utf8_well_formed(const llvm::StringRef bytes) {
    const std::size_t count = bytes.size();
    std::size_t index = 0U;
    while (index < count) {
        const unsigned char lead = static_cast<unsigned char>(bytes[index]);
        if (lead < 0x80U) {
            ++index;
            continue;
        }
        unsigned length = 0U;
        unsigned char low = 0x80U;
        unsigned char high = 0xBFU;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2U;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3U;
            if (lead == 0xE0U) low = 0xA0U;
            if (lead == 0xEDU) high = 0x9FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4U;
            if (lead == 0xF0U) low = 0x90U;
            if (lead == 0xF4U) high = 0x8FU;
        } else {
            return false;
        }
        if (count - index < length) return false;
        const unsigned char second = static_cast<unsigned char>(bytes[index + 1U]);
        if (second < low || second > high) return false;
        for (unsigned position = 2U; position < length; ++position) {
            const unsigned char continuation = static_cast<unsigned char>(bytes[index + position]);
            if (continuation < 0x80U || continuation > 0xBFU) return false;
        }
        index += length;
    }
    return true;
}

// A literal a stxlib::TextView cannot be made from: not a narrow (or
// UTF-8) literal, or not well-formed UTF-8 ("\xc3\x28").
[[nodiscard]] bool literal_refuses_text_view(const clang::StringLiteral& literal) {
    if (!literal.isOrdinary() && !literal.isUTF8()) return true;
    const bool result = !utf8_well_formed(literal.getBytes());
    return result;
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

// Names the site whose edits are being recorded for as long as it lives,
// then restores the enclosing site's name (EditPlan::resolve applies a
// site's edits together or not at all). Borrows the slot, which outlives it.
class GroupScope final {
public:
    GroupScope(std::string& slot, std::string label) : slot_(&slot), saved_(slot) {
        *slot_ = std::move(label);
    }
    ~GroupScope() {
        *slot_ = saved_;
    }
    GroupScope(const GroupScope&) = delete;
    GroupScope& operator=(const GroupScope&) = delete;

private:
    std::string* slot_;
    std::string saved_;
};

// Finds, in one instantiation of a template, the expression written at
// the same place as an expression of the pattern (instantiation keeps the
// pattern's locations): the outermost expression with the same range.
class InstantiatedExpression final : public clang::RecursiveASTVisitor<InstantiatedExpression> {
public:
    InstantiatedExpression(const clang::SourceLocation begin, const clang::SourceLocation end)
        : begin_(begin), end_(end) {}

    [[nodiscard]] bool shouldVisitTemplateInstantiations() const { return true; }

    bool VisitExpr(clang::Expr* expression) {
        if (found_ != nullptr) return false;
        if ((*expression).getBeginLoc() != begin_ || (*expression).getEndLoc() != end_) return true;
        found_ = expression;
        return false;
    }

    [[nodiscard]] const clang::Expr* found() const noexcept { return found_; }

private:
    clang::SourceLocation begin_;
    clang::SourceLocation end_;
    const clang::Expr* found_{nullptr};
};

class Analysis final : public clang::RecursiveASTVisitor<Analysis> {
public:
    Analysis(clang::ASTContext& context, const AnalysisInput& input, TranslationUnitHarvest& harvest)
        : context_(context), sources_(context.getSourceManager()), language_(context.getLangOpts()),
          input_(input), options_(*input.options), harvest_(harvest), plan_(input.phase == Phase::plan),
          stage_((*input.options).stage), only_((*input.options).only_pattern) {}

    void run() {
        TraverseDecl(context_.getTranslationUnitDecl());
        if (plan_) apply_refusals();
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
        std::vector<Edit>& file = harvest_.edits[path];
        file.push_back(edit);
        file.back().group = group_;
    }

    // The name of the site at `location`: the rule's kind and the file
    // offset of the anchor, the same in every unit that sees the site.
    [[nodiscard]] std::string site_label(const char* kind, const clang::SourceLocation location) {
        std::string result(kind);
        if (location.isInvalid()) return result;
        const clang::SourceLocation file_location = sources_.getFileLoc(location);
        const std::pair<clang::FileID, unsigned> decomposed = sources_.getDecomposedLoc(file_location);
        result += ":" + std::to_string(decomposed.second);
        return result;
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

    // A function that converts one vocabulary into the other: it takes a
    // standard type and returns its stxlib counterpart of the same value
    // type, or the reverse (connected_topology_from_standard, the D15 Q3
    // helpers), or is marked [[clang::annotate("gui_forms::d15_bridge")]].
    // Converting its standard side would defeat it (defect 3 of the
    // 2026-11-16 V1 run). The private bridge header is excluded by path.
    [[nodiscard]] bool is_vocabulary_bridge(const clang::FunctionDecl& function) {
        for (const clang::AnnotateAttr* annotation : function.specific_attrs<clang::AnnotateAttr>()) {
            if ((*annotation).getAnnotation() == "gui_forms::d15_bridge") return true;
        }
        const clang::QualType returned = function.getReturnType();
        const Family returned_standard = expression_family(returned, stage_);
        const Family returned_owned = owned_family(returned);
        if (returned_standard == Family::none && returned_owned == Family::none) return false;
        for (const clang::ParmVarDecl* parameter : function.parameters()) {
            const clang::QualType taken = (*parameter).getType();
            const bool into_owned = returned_owned != Family::none &&
                returned_owned == expression_family(taken, stage_);
            const bool into_standard = returned_standard != Family::none &&
                returned_standard == owned_family(taken);
            if (!into_owned && !into_standard) continue;
            if (same_value_type(returned, taken)) return true;
        }
        return false;
    }

    // True when two vocabulary types hold the same first template argument
    // (stxlib::Optional<T> and std::optional<T>), or one is not a template
    // (std::string_view and stxlib::TextView).
    [[nodiscard]] static bool same_value_type(const clang::QualType first, const clang::QualType second) {
        const clang::ClassTemplateSpecializationDecl* const first_record =
            llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                (*first.getNonReferenceType().getCanonicalType()).getAsCXXRecordDecl());
        const clang::ClassTemplateSpecializationDecl* const second_record =
            llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                (*second.getNonReferenceType().getCanonicalType()).getAsCXXRecordDecl());
        // The text families hold no value type: any pairing is the same.
        if (first_record == nullptr || second_record == nullptr) return true;
        const clang::TemplateArgumentList& first_arguments = (*first_record).getTemplateArgs();
        const clang::TemplateArgumentList& second_arguments = (*second_record).getTemplateArgs();
        if (first_arguments.size() == 0U || second_arguments.size() == 0U) {
            return first_arguments.size() == second_arguments.size();
        }
        if (first_arguments[0].getKind() != clang::TemplateArgument::Type ||
            second_arguments[0].getKind() != clang::TemplateArgument::Type) {
            return false;
        }
        const bool result = first_arguments[0].getAsType().getCanonicalType().getUnqualifiedType() ==
            second_arguments[0].getAsType().getCanonicalType().getUnqualifiedType();
        return result;
    }

    // On the consumer surface: declared in one of --surface-headers, or
    // inside a declaration --surface-exports names (the outermost named
    // declaration enclosing it, below its namespace: a class for a member,
    // the function for a parameter or return). Everything is on it when no
    // surface is given.
    [[nodiscard]] bool on_surface(const clang::Decl& declaration, const std::string& path) const {
        if (!options_.surface_mode) return true;
        if (!path.empty() && any_directory_contains(path, options_.surface_headers)) return true;
        if (options_.surface_names.empty()) return false;
        const clang::Decl* current = &declaration;
        for (unsigned guard = 0U; guard < 64U && current != nullptr; ++guard) {
            const clang::DeclContext* const context = (*current).getDeclContext();
            if (context == nullptr) return false;
            const bool top = llvm::isa<clang::NamespaceDecl>(context) || llvm::isa<clang::TranslationUnitDecl>(context) ||
                llvm::isa<clang::LinkageSpecDecl>(context);
            if (top) {
                const clang::NamedDecl* const named = llvm::dyn_cast<clang::NamedDecl>(current);
                if (named == nullptr) return false;
                const bool result = options_.surface_names.find((*named).getQualifiedNameAsString()) !=
                    options_.surface_names.end();
                return result;
            }
            current = clang::Decl::castFromDeclContext(context);
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
        } else if (!written.isNull()) {
            const TypeRewrite planned = declared_type_rewrite(written, match);
            if (!planned.problem.empty()) fact.ineligible = planned.problem;
        }
        if (fact.ineligible.empty() && function != nullptr) {
            const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
            if (method != nullptr && (*(*method).getParent()).isLambda()) {
                fact.ineligible = "a lambda's parameter or return";
            } else if (is_vocabulary_bridge(*function)) {
                fact.ineligible = "a D15 bridge between the standard and stxlib vocabularies (Q3): left as written";
            } else if ((*function).hasAttr<clang::DeprecatedAttr>() && has_sibling_overload(*function)) {
                fact.ineligible = "a deprecated D15 compatibility overload (Q3), removed at V6";
            } else if (kind == EntityKind::parameter && would_collide(*function, index, match)) {
                fact.ineligible = "changing it would give the function the signature of an existing overload";
            }
        }
        const core_alias_result alias = first_party_alias(written);
        if (alias.alias != nullptr) edge(key, "A:" + usr_of(alias.alias), true, false);
        fact.surface = kind == EntityKind::local || on_surface(declaration, fact.path);
        fact.seeded = kind != EntityKind::local && name_span.valid && in_seed(name_span.path) &&
            options_.seed_kinds.find(kind) != options_.seed_kinds.end() && only_matches(qualified) && fact.surface;
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

    // The rewrite of one declared type: either `problem` (why it cannot be
    // written), or nothing to write (`needed` false: auto, a first-party
    // alias that changes on its own), or one replacement and its headers.
    // The plan phase uses the problem to keep an entity whose declaration
    // cannot change out of the converted set, so that its uses are not
    // rewritten without it (a second run then finds nothing to do).
    struct TypeRewrite final {
        std::string problem;
        bool needed{false};
        Span file;
        unsigned begin{0U};
        unsigned end{0U};
        std::string text;
        std::uint32_t headers{include_none};
    };

    [[nodiscard]] TypeRewrite declared_type_rewrite(const clang::TypeLoc whole, const FamilyMatch& match) {
        TypeRewrite result;
        if (whole.isNull()) {
            result.problem = "the converted declaration's type is not written here";
            return result;
        }
        if (match.family == Family::text_reference || (match.family == Family::text_view && match.reference)) {
            const Span whole_span = span_of(whole.getSourceRange());
            if (!whole_span.valid) {
                result.problem = "inside a macro body: the site cannot be rewritten";
                return result;
            }
            // `const std::string& name` becomes `stxlib::TextView name`:
            // the view is passed by value, so the const before it goes too.
            result.needed = true;
            result.file = whole_span;
            result.begin = leading_const_start(buffer_of(whole_span.file), whole_span.begin);
            result.end = whole_span.end;
            result.text = "stxlib::TextView";
            result.headers = include_text_view;
            return result;
        }
        const clang::TypeLoc core = core_type_loc(whole);
        if (core.isNull()) {
            result.problem = "the declared type is not spelled as the standard type";
            return result;
        }
        if (!core.getAs<clang::AutoTypeLoc>().isNull()) return result;
        const clang::TypedefTypeLoc typedef_loc = core.getAs<clang::TypedefTypeLoc>();
        if (!typedef_loc.isNull()) {
            const clang::TypedefNameDecl* const alias = (*typedef_loc.getTypePtr()).getDecl();
            if (alias == nullptr || !(*alias).isInStdNamespace()) return result;
            const Span name = token_span(typedef_loc.getNameLoc());
            if (!name.valid) {
                result.problem = "inside a macro body: the site cannot be rewritten";
                return result;
            }
            bool clean = true;
            const unsigned start = qualifier_start(buffer_of(name.file), name.begin, clean);
            if (!clean) {
                result.problem = "the standard name is qualified by something other than std::";
                return result;
            }
            result.needed = true;
            result.file = name;
            result.begin = start;
            result.end = name.end;
            result.text = owned_name(match.family);
            result.headers = family_includes(match.family);
            return result;
        }
        const clang::TemplateSpecializationTypeLoc specialization = core.getAs<clang::TemplateSpecializationTypeLoc>();
        if (specialization.isNull()) {
            result.problem = "the declared type is not spelled as the standard template";
            return result;
        }
        const Span name = token_span(specialization.getTemplateNameLoc());
        const Span closing = token_span(specialization.getRAngleLoc());
        if (!name.valid) {
            result.problem = "inside a macro body: the site cannot be rewritten";
            return result;
        }
        if (!closing.valid || closing.path != name.path) {
            result.problem = "the declared type's closing bracket is spelled through a macro";
            return result;
        }
        bool clean = true;
        const unsigned start = qualifier_start(buffer_of(name.file), name.begin, clean);
        if (!clean) {
            result.problem = "the standard name is qualified by something other than std::";
            return result;
        }
        result.file = name;
        result.begin = start;
        result.headers = family_includes(match.family);
        switch (match.family) {
        case Family::view:
        case Family::mutable_view: {
            if (specialization.getNumArgs() < 1U) {
                result.problem = "std::span without a written element type";
                return result;
            }
            const clang::TemplateArgumentLoc argument = specialization.getArgLoc(0U);
            const clang::TypeSourceInfo* const information = argument.getTypeSourceInfo();
            if (information == nullptr) {
                result.problem = "std::span's element is not a written type";
                return result;
            }
            clang::TypeLoc element = (*information).getTypeLoc();
            const clang::QualifiedTypeLoc qualified = element.getAs<clang::QualifiedTypeLoc>();
            if (!qualified.isNull()) element = qualified.getUnqualifiedLoc();
            const Span element_span = span_of(element.getSourceRange());
            if (!element_span.valid) {
                result.problem = "std::span's element type is spelled through a macro";
                return result;
            }
            result.end = closing.end;
            result.text = std::string(owned_name(match.family)) + "<" + text_of(element_span) + ">";
            break;
        }
        case Family::text_view:
        case Family::owned_text:
            result.end = closing.end;
            result.text = owned_name(match.family);
            break;
        default:
            result.end = name.end;
            result.text = owned_name(match.family);
            break;
        }
        result.needed = true;
        return result;
    }

    void rewrite_declared_type(const clang::TypeLoc whole, const FamilyMatch& match, const clang::SourceLocation where) {
        const GroupScope site(group_, site_label("type", where));
        const TypeRewrite planned = declared_type_rewrite(whole, match);
        if (!planned.problem.empty()) {
            residue_at(where, planned.problem);
            return;
        }
        if (!planned.needed || !editable(planned.file, where)) return;
        replace_bytes(planned.file, planned.begin, planned.end, planned.text);
        need(planned.file.path, planned.headers);
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
            // The argument as written keeps its sugar (std::size_t, not the
            // platform's unsigned long); the canonical one otherwise.
            const clang::TemplateSpecializationType* const written =
                (*type.getNonReferenceType()).getAs<clang::TemplateSpecializationType>();
            const clang::ClassTemplateSpecializationDecl* const owned =
                llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                    (*type.getNonReferenceType().getCanonicalType()).getAsCXXRecordDecl());
            if (written != nullptr && !(*written).template_arguments().empty() &&
                (*written).template_arguments()[0].getKind() == clang::TemplateArgument::Type) {
                destination.value_type = (*written).template_arguments()[0].getAsType();
            } else if (owned != nullptr && (*owned).getTemplateArgs().size() > 0U &&
                       (*owned).getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type) {
                destination.value_type = (*owned).getTemplateArgs()[0].getAsType();
            }
        }
        destination.candidate = stage_ == Stage::v2_text_view && match.family != Family::none &&
            kind == EntityKind::parameter && !key.empty();
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
        if (destination.candidate && !first_party_function(callee)) destination.candidate = false;
        return destination;
    }

    // Declared in a scoped first-party file, not by the standard library
    // or a system header.
    [[nodiscard]] bool first_party_function(const clang::FunctionDecl& function) {
        if (is_standard_callee(&function)) return false;
        const clang::SourceLocation location = function.getLocation();
        if (location.isInvalid() || sources_.isInSystemHeader(location)) return false;
        std::string relative;
        if (!relative_path(sources_.getFileID(sources_.getFileLoc(location)), relative)) return false;
        const bool result = in_scope(relative);
        return result;
    }

    // The standard library's function or a member of its class.
    [[nodiscard]] bool is_standard_callee(const clang::FunctionDecl* callee) const {
        if (callee == nullptr) return false;
        if ((*callee).isInStdNamespace()) return true;
        const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
        if (method != nullptr && (*(*method).getParent()).isInStdNamespace()) return true;
        const clang::SourceLocation location = (*callee).getLocation();
        const bool result = location.isValid() && sources_.isInSystemHeader(location);
        return result;
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
        // V2: a const std::string& made a view is the converted text itself.
        const clang::Expr* const converted_object =
            stage_ == Stage::v2_text_view ? standard_text_conversion_object(expression) : nullptr;
        if (converted_object != nullptr) {
            expression = (*(*converted_object).IgnoreImplicit()).IgnoreParens();
        }
        shape.expression = expression;
        if (llvm::isa<clang::ParenListExpr>(expression)) {
            shape.kind = SourceKind::init_list;
            return shape;
        }
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
                // V2: a std::string made from text is standard text in its
                // own right; text_use bridges its argument, so a flow must
                // not bridge it a second time (the 2026-11-17 V2 run's
                // `standard_text(standard_text_copy(id)`).
                if (stage_ == Stage::v2_text_view && is_standard_string((*construction).getType())) {
                    shape.kind = SourceKind::standard;
                    return shape;
                }
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

    // `flow` marks a value flow (EdgeFact): one a consumer surface may cut.
    void edge(const std::string& first, const std::string& second, const bool strict, const bool flow) {
        EdgeFact fact;
        fact.first = first;
        fact.second = second;
        fact.strict = strict;
        fact.flow = flow;
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
            if (stage_ == Stage::v2_text_view) plan_text_flow(*source, destination);
            const Denoted from = denoted(source);
            if (!from.found || !wants_edge(destination.key, from.key)) return;
            const bool strict = stage_ == Stage::v5_ownership || destination.key.compare(0U, 2U, "L:") == 0;
            edge(destination.key, from.key, strict, true);
            return;
        }
        const GroupScope site(group_, site_label("flow", (*source).getBeginLoc()));
        if (!destination.known) {
            const Denoted from = denoted(source);
            if (from.found && is_converted(from.key)) {
                residue_at((*source).getBeginLoc(),
                           "a converted value is passed where no parameter is declared (variadic or unresolved)");
            }
            return;
        }
        // A template-dependent source (a call through a template parameter,
        // a parenthesised initialiser list) has no type here: its
        // instantiations decide (defect 1 of the 2026-11-16 V1 run).
        if ((*source).isTypeDependent()) {
            dependent_flow(*source, destination, assigned);
            return;
        }
        const SourceShape shape = classify(source);
        if (destination.converted) {
            into_converted(shape, destination, assigned, *source);
            return;
        }
        if (shape.kind == SourceKind::converted) out_of_converted(shape, destination, *source);
    }

    // Fills `found` (cleared first) with the expressions that every
    // instantiation of the enclosing template, seen in this unit, has where
    // the pattern has `pattern`: function templates and members of class
    // templates. Empty when the pattern is not in a template, or no
    // instantiation of it was made here.
    void instantiated_counterparts(const clang::Expr& pattern, std::vector<const clang::Expr*>& found) {
        found.clear();
        const clang::FunctionDecl* const function = enclosing_function(pattern);
        if (function == nullptr) return;
        std::vector<clang::FunctionDecl*> instances;
        clang::FunctionTemplateDecl* const described = (*function).getDescribedFunctionTemplate();
        if (described != nullptr) {
            for (clang::FunctionDecl* specialization : (*described).specializations()) instances.push_back(specialization);
        } else {
            const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
            clang::ClassTemplateDecl* const owner =
                method == nullptr ? nullptr : (*(*method).getParent()).getDescribedClassTemplate();
            if (owner != nullptr) {
                for (clang::ClassTemplateSpecializationDecl* specialization : (*owner).specializations()) {
                    for (clang::Decl* member : (*specialization).decls()) {
                        clang::CXXMethodDecl* const candidate = llvm::dyn_cast<clang::CXXMethodDecl>(member);
                        if (candidate == nullptr) continue;
                        const clang::FunctionDecl* const instantiated_from = (*candidate).getTemplateInstantiationPattern();
                        if (instantiated_from == nullptr) continue;
                        if ((*instantiated_from).getCanonicalDecl() != (*function).getCanonicalDecl()) continue;
                        instances.push_back(candidate);
                    }
                }
            }
        }
        for (clang::FunctionDecl* instance : instances) {
            clang::FunctionDecl* const definition = (*instance).getDefinition();
            if (definition == nullptr || (*definition).getBody() == nullptr) continue;
            InstantiatedExpression finder(pattern.getBeginLoc(), pattern.getEndLoc());
            finder.TraverseDecl(definition);
            if (finder.found() != nullptr) found.push_back(finder.found());
        }
    }

    // A template-dependent value flowing into a converted destination.
    // Nothing changes when every instantiation already gives the stxlib
    // type (or a converted value); a plain value in every instantiation is
    // wrapped as usual; anything else is residue. A template never
    // instantiated in this unit is left to the units that instantiate it.
    void dependent_flow(const clang::Expr& source, const Destination& destination, const clang::Expr* assigned) {
        if (!destination.converted || destination.dependent) return;
        std::vector<const clang::Expr*> instances;
        instantiated_counterparts(source, instances);
        if (instances.empty()) return;
        bool every_unchanged = true;
        bool every_value = true;
        for (const clang::Expr* instance : instances) {
            const SourceShape shape = classify(instance);
            const bool unchanged = shape.kind == SourceKind::converted || shape.kind == SourceKind::owned ||
                shape.kind == SourceKind::empty;
            const bool value = shape.kind == SourceKind::value || shape.kind == SourceKind::literal;
            every_unchanged = every_unchanged && unchanged;
            every_value = every_value && value;
        }
        if (every_unchanged) return;
        const clang::SourceLocation where = source.getBeginLoc();
        if (!every_value || destination.family != Family::optional) {
            residue_at(where, "a template-dependent value flows into a converted declaration, and its "
                              "instantiations need a rewrite the template cannot spell: rewrite by hand");
            return;
        }
        SourceShape shape;
        shape.kind = SourceKind::value;
        shape.expression = (*source.IgnoreImplicit()).IgnoreParens();
        const Span span = span_of(shape.expression);
        if (!editable(span, where)) return;
        into_optional(shape, destination, assigned, source, span, context_of(source));
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
        // V2: an empty standard text spelled with its type (a default
        // argument `= std::string_view()`) cannot initialise a TextView;
        // the empty TextView needs no bridge, so headers take it too.
        const bool empty_text = shape.kind == SourceKind::empty ||
            (shape.kind == SourceKind::explicit_temporary && shape.inner == nullptr);
        const std::string written = stage_ == Stage::v2_text_view && empty_text ? text_of(span) : std::string();
        // A default argument's range can start at its `=` (`= {}`): the
        // spelling starts after it, and a bare `{}` already makes the empty
        // TextView (the 2026-11-17 V2 run's `TextView text stxlib::TextView()`).
        const std::size_t spelled_start = written.find_first_not_of("= \t\r\n");
        const bool spelled_empty = spelled_start != std::string::npos && written[spelled_start] != '{';
        if (spelled_empty) {
            if (!editable(span, where)) return;
            replace_bytes(span, span.begin + static_cast<unsigned>(spelled_start), span.end, "stxlib::TextView()");
            need(span.path, include_text_view);
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

    // -- V2 text boundaries -------------------------------------------------------
    //
    // V2 has no flow edges, so a converted parameter meets standard text at
    // every site that still takes it. Outside include/ such a site goes
    // through the private bridge. A public header cannot include the
    // bridge, so the plan phase keeps the parameter standard instead of
    // leaving a site that does not compile: its uses that would need the
    // bridge refuse it (the refusal becomes the entity's ineligibility,
    // reported by the closure), and a header flow between two candidate
    // parameters ties them strictly, so they change together or not at all.

    // The key of the candidate parameter `expression` names (a by-value
    // std::string_view or const std::string& parameter, looked through
    // copies and the std::string to view conversion), or empty.
    [[nodiscard]] std::string text_candidate(const clang::Expr* expression) {
        const clang::Expr* const plain = strip_value(expression, stage_);
        const clang::DeclRefExpr* const reference = llvm::dyn_cast_or_null<clang::DeclRefExpr>(plain);
        if (reference == nullptr) return std::string();
        const clang::ParmVarDecl* const parameter = llvm::dyn_cast<clang::ParmVarDecl>((*reference).getDecl());
        if (parameter == nullptr) return std::string();
        const FamilyMatch match = match_family((*parameter).getType(), EntityKind::parameter, stage_, context_);
        if (match.family == Family::none) return std::string();
        const std::string result = parameter_key(parameter);
        return result;
    }

    // True when a bridge can be written around `at`: its range maps to one
    // scoped file outside include/. Otherwise `reason` says why not.
    [[nodiscard]] bool bridge_site(const clang::Expr& at, std::string& reason) {
        const Span span = span_of(&at);
        if (!span.valid) {
            reason = "inside a macro body";
            return false;
        }
        if (!in_scope(span.path)) {
            reason = "outside the rewrite scope";
            return false;
        }
        if (!bridge_allowed(span.path)) {
            reason = "in a public header (no bridge)";
            return false;
        }
        return true;
    }

    [[nodiscard]] std::string location_text(const clang::SourceLocation location) {
        if (location.isInvalid()) return std::string();
        const clang::SourceLocation file_location = sources_.getFileLoc(location);
        const std::pair<clang::FileID, unsigned> decomposed = sources_.getDecomposedLoc(file_location);
        std::string relative;
        if (!relative_path(decomposed.first, relative)) return std::string();
        const unsigned line = sources_.getLineNumber(decomposed.first, decomposed.second);
        const std::string result = relative + ":" + std::to_string(line);
        return result;
    }

    // Keeps the entity `key` standard (plan) or reports the site (rewrite,
    // where a refused entity is not converted and its uses are not seen,
    // so this is only reached for a site the plan did not foresee).
    // The smallest reason wins, so the result does not depend on order.
    void refuse(const std::string& key, const clang::SourceLocation where, const std::string& reason) {
        if (key.empty()) return;
        if (!plan_) {
            residue_at(where, reason);
            return;
        }
        const std::string located = "V2: " + reason + " (" + location_text(where) + ")";
        std::map<std::string, std::string>::iterator found = refusals_.find(key);
        if (found == refusals_.end()) {
            refusals_.emplace(key, located);
            return;
        }
        if (located < (*found).second) (*found).second = located;
    }

    // The plan's refusals become the ineligibility of this unit's facts
    // (the closure merges facts by key, so one refusing unit suffices).
    void apply_refusals() {
        for (EntityFact& fact : harvest_.entities) {
            if (!fact.ineligible.empty()) continue;
            std::map<std::string, std::string>::const_iterator found = refusals_.find(fact.key);
            if (found != refusals_.end()) fact.ineligible = (*found).second;
        }
    }

    // A V2 value flow seen by the plan. Outside include/ the rewrite
    // bridges whatever the closure decides, except a template-dependent
    // value or an unresolved destination, which it cannot rewrite.
    void plan_text_flow(const clang::Expr& source, const Destination& destination) {
        const std::string from = text_candidate(&source);
        const clang::SourceLocation where = source.getBeginLoc();
        if (!destination.known) {
            refuse(from, where, "converted text passed where no parameter is declared (variadic or unresolved)");
            return;
        }
        if (destination.candidate && source.isTypeDependent()) {
            refuse(destination.key, where, "receives a template-dependent value");
            return;
        }
        const clang::StringLiteral* const literal = literal_text(&source);
        if (destination.candidate && literal != nullptr && literal_refuses_text_view(*literal)) {
            refuse(destination.key, where, "receives a literal that is not well-formed UTF-8: it takes arbitrary bytes");
            return;
        }
        std::string reason;
        if (bridge_site(source, reason)) return;
        if (!from.empty()) {
            if (destination.candidate) {
                edge(destination.key, from, true, false);
                return;
            }
            if (is_text_view(destination.type)) return;
            refuse(from, where, "converted text flows into standard " + destination.type.getAsString() + " " + reason);
            return;
        }
        if (!destination.candidate) return;
        const SourceShape shape = classify(&source);
        const bool needs_nothing = shape.kind == SourceKind::literal || shape.kind == SourceKind::text_view ||
            shape.kind == SourceKind::empty ||
            (shape.kind == SourceKind::explicit_temporary && shape.inner == nullptr) || is_literal_text(&source);
        if (needs_nothing) return;
        refuse(destination.key, where, "receives standard text " + reason);
    }

    // Refuses every candidate parameter of `function`.
    void refuse_parameters(const clang::FunctionDecl& function, const clang::SourceLocation where,
                           const std::string& reason) {
        for (const clang::ParmVarDecl* parameter : function.parameters()) {
            const FamilyMatch match = match_family((*parameter).getType(), EntityKind::parameter, stage_, context_);
            if (match.family == Family::none) continue;
            refuse(parameter_key(parameter), where, reason);
        }
    }

    // A call a template leaves unresolved (`bind_form_value(target,
    // property, source, direction)` with a dependent `source`) picks its
    // overload per instantiation, which the rewrite never sees: a
    // candidate parameter of any overload that receives anything but a
    // literal or a TextView there stays standard, so the overloads keep
    // matching the template's arguments.
    void unresolved_text_call(const clang::CallExpr& call) {
        const clang::OverloadExpr* const overloads =
            llvm::dyn_cast<clang::OverloadExpr>((*call.getCallee()).IgnoreImplicit());
        if (overloads == nullptr) return;
        for (unsigned index = 0U; index < call.getNumArgs(); ++index) {
            const clang::Expr* const argument = call.getArg(index);
            const clang::StringLiteral* const literal = literal_text(argument);
            if (literal != nullptr && !literal_refuses_text_view(*literal)) continue;
            if (!(*argument).isTypeDependent() && is_text_view((*argument).getType())) continue;
            for (const clang::NamedDecl* candidate : (*overloads).decls()) {
                const clang::NamedDecl* const target = (*candidate).getUnderlyingDecl();
                const clang::FunctionDecl* function = llvm::dyn_cast<clang::FunctionDecl>(target);
                const clang::FunctionTemplateDecl* const pattern = llvm::dyn_cast<clang::FunctionTemplateDecl>(target);
                if (pattern != nullptr) function = (*pattern).getTemplatedDecl();
                if (function == nullptr || index >= (*function).getNumParams()) continue;
                const clang::ParmVarDecl* const parameter = (*function).getParamDecl(index);
                const FamilyMatch match = match_family((*parameter).getType(), EntityKind::parameter, stage_, context_);
                if (match.family == Family::none) continue;
                refuse(parameter_key(parameter), (*argument).getBeginLoc(),
                       "receives a value from a call a template leaves unresolved");
            }
        }
    }

    void function_reference(const clang::DeclRefExpr& reference, const clang::FunctionDecl& function);

    // -- uses -------------------------------------------------------------------

    void text_use(const clang::Expr& expression, const Denoted& entity);
    void text_member(const std::string& key, const clang::Expr& spelled, const clang::MemberExpr& member);
    void text_operator(const std::string& key, const clang::Expr& node, const clang::Expr& spelled,
                       const clang::CXXOperatorCallExpr& operation);
    void text_equality(const std::string& key, const clang::Expr& spelled, const clang::Expr& other);
    void text_cast(const std::string& key, const clang::Expr& spelled, const clang::CXXFunctionalCastExpr& cast);
    void text_bridge(const std::string& key, const clang::Expr& spelled, bool copy, const char* what);
    void handle_use(const clang::Expr& expression, const Denoted& entity);
    void condition_use(const clang::DeclRefExpr& reference, const clang::VarDecl& variable);
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
    std::string group_;      // the site whose edits are being recorded (GroupScope)
    std::map<std::string, std::string> refusals_;   // V2 plan: entity key -> why it stays standard
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
        if (method != nullptr && stage_ == Stage::v2_text_view &&
            (*method).getOverloadedOperator() == clang::OO_Call) {
            refuse_parameters(*method, (*method).getLocation(),
                              "a call operator's parameter: the code that calls the object (an event, a "
                              "template) fixes its signature");
        }
        if (method != nullptr) {
            for (const clang::CXXMethodDecl* overridden : (*method).overridden_methods()) {
                edge(return_key(method), return_key(overridden), true, false);
                const unsigned count = std::min((*method).getNumParams(), (*overridden).getNumParams());
                for (unsigned index = 0U; index < count; ++index) {
                    edge(parameter_key((*method).getParamDecl(index)), parameter_key((*overridden).getParamDecl(index)),
                         true, false);
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
        // V2 converts no local: an auto local keeps the standard type its
        // initialiser had, which the rewrite bridges (flow), or a public
        // header refuses.
        if (stage_ == Stage::v2_text_view) plan_text_flow(*(*variable).getInit(), destination);
        // An auto local takes its initialiser's type, so it changes with it.
        const Denoted from = denoted((*variable).getInit());
        if (from.found) edge(key, from.key, true, false);
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
            if (plan_ && stage_ == Stage::v2_text_view) {
                refuse(text_candidate((*call).getArg(index)), (*(*call).getArg(index)).getBeginLoc(),
                       "converted text passed to an unresolved callee");
            }
        }
        if (plan_ && stage_ == Stage::v2_text_view) unresolved_text_call(*call);
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
            // V2: a standard operator='s parameter, after deduction, is the
            // text it takes (variant's and optional's converting operator=
            // take the std::string itself), so it picks the bridge.
            if (stage_ == Stage::v2_text_view && (*(*method).getParent()).isInStdNamespace() &&
                (*method).getNumParams() == 1U) {
                destination = destination_for_parameter(*method, 0U);
            }
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
    if (reference == nullptr) return true;
    const clang::FunctionDecl* const function = llvm::dyn_cast<clang::FunctionDecl>((*reference).getDecl());
    if (function != nullptr && plan_ && stage_ == Stage::v2_text_view) function_reference(*reference, *function);
    if (!llvm::isa<clang::VarDecl>((*reference).getDecl())) return true;
    const Denoted entity = denoted(reference);
    if (!entity.found) return true;
    if (!plan_ && !is_converted(entity.key)) return true;
    // The reference Sema makes for a condition declaration (`if (T x =
    // f())`) carries the declaration's own location: there is no written
    // use to rewrite there (defect 4 of the 2026-11-16 V1 run).
    const clang::VarDecl* const variable = llvm::cast<clang::VarDecl>((*reference).getDecl());
    if (!(*variable).isImplicit() && (*reference).getLocation() == (*variable).getLocation()) {
        condition_use(*reference, *variable);
        return true;
    }
    handle_use(*reference, entity);
    return true;
}

// A converted variable declared in a condition, tested by its implicit
// conversion to bool. An `if` without an init-statement takes the test as
// one: `if (stxlib::Optional<T> x = f(); x.present())`, which keeps the
// variable's scope. A while, a switch, a for, or an `if` that has an
// init-statement already are residue. V5's owners keep their own test.
void Analysis::condition_use(const clang::DeclRefExpr& reference, const clang::VarDecl& variable) {
    if (plan_ || stage_ == Stage::v5_ownership) return;
    const GroupScope site(group_, site_label("condition", variable.getLocation()));
    const clang::Expr* top = &reference;
    clang::DynTypedNode node;
    bool found = false;
    for (unsigned guard = 0U; guard < 16U; ++guard) {
        node = parent_of(clang::DynTypedNode::create(*top), found);
        if (!found) return;
        const clang::Expr* const parent = node.get<clang::Expr>();
        if (parent == nullptr) break;
        top = parent;
    }
    const clang::IfStmt* const statement = node.get<clang::IfStmt>();
    const bool testable = stage_ == Stage::v1_optional && statement != nullptr &&
        (*statement).getConditionVariable() == &variable && (*statement).getInit() == nullptr &&
        (*(*top).getType()).isBooleanType();
    if (!testable) {
        residue_at(variable.getLocation(), "a converted value declared in a condition: rewrite the condition by hand");
        return;
    }
    const Span declaration = span_of(variable.getSourceRange());
    if (!editable(declaration, variable.getLocation())) return;
    close_at(declaration, "; " + std::string(variable.getName()) + ".present()");
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
    const GroupScope site(group_, site_label("use", expression.getBeginLoc()));
    if (stage_ == Stage::v2_text_view) {
        text_use(expression, entity);
        return;
    }
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

// -- V2 uses -------------------------------------------------------------------

// A function named other than as a callee: its address is taken (a
// member bound as an event handler, `Delegate<const std::string&>::bind<T,
// &T::handler>`, `on(event, *this, &T::handler)`, a WeakMemberCallback).
// The code holding the pointer fixes the function's type, so its candidate
// parameters stay standard (the 2026-11-17 V2 run's event slots).
void Analysis::function_reference(const clang::DeclRefExpr& reference, const clang::FunctionDecl& function) {
    const clang::Expr* node = &reference;
    for (unsigned guard = 0U; guard < 8U; ++guard) {
        bool found = false;
        const clang::DynTypedNode parent_node = parent_of(clang::DynTypedNode::create(*node), found);
        const clang::Expr* const parent = found ? parent_node.get<clang::Expr>() : nullptr;
        if (parent == nullptr) break;
        if (llvm::isa<clang::ImplicitCastExpr>(parent) || llvm::isa<clang::ParenExpr>(parent)) {
            node = parent;
            continue;
        }
        const clang::CallExpr* const call = llvm::dyn_cast<clang::CallExpr>(parent);
        if (call != nullptr && (*call).getCallee() == node) return;
        break;
    }
    refuse_parameters(function, reference.getBeginLoc(),
                      "the function's address is taken (a callback or event handler): its signature must keep "
                      "matching");
}
//
// One use of a converted text parameter (only parameters convert in V2).
// Every rule is decided the same way in both phases: the plan refuses the
// parameter where the rewrite could not write the site (text_bridge), and
// the rewrite writes it. Sites whose value flows somewhere (an argument,
// an initialiser, a return) are flow()'s.
void Analysis::text_use(const clang::Expr& expression, const Denoted& entity) {
    if (plan_ && text_candidate(&expression).empty()) return;
    const std::string& key = entity.key;
    const clang::SourceLocation where = expression.getBeginLoc();
    // `node` climbs the layers that keep the text as it is (implicit
    // casts, temporaries, copies, the std::string to view conversion,
    // std::move); `expression` stays the spelled name the bridge wraps.
    const clang::Expr* node = &expression;
    clang::DynTypedNode parent_node;
    bool found = false;
    for (unsigned guard = 0U; guard < 64U; ++guard) {
        parent_node = parent_of(clang::DynTypedNode::create(*node), found);
        if (!found) return;
        const clang::Expr* const parent = parent_node.get<clang::Expr>();
        if (parent == nullptr) break;
        const bool layer = llvm::isa<clang::ImplicitCastExpr>(parent) || llvm::isa<clang::ParenExpr>(parent) ||
            llvm::isa<clang::MaterializeTemporaryExpr>(parent) || llvm::isa<clang::CXXBindTemporaryExpr>(parent) ||
            llvm::isa<clang::FullExpr>(parent);
        // A conditional's branch: flow() bridges each branch on its own.
        const bool branch = llvm::isa<clang::ConditionalOperator>(parent);
        if (layer || branch) {
            node = parent;
            continue;
        }
        const clang::CXXConstructExpr* const copy = llvm::dyn_cast<clang::CXXConstructExpr>(parent);
        if (copy != nullptr && is_transparent_construction(*copy, stage_)) {
            node = parent;
            continue;
        }
        const clang::MemberExpr* const conversion = llvm::dyn_cast<clang::MemberExpr>(parent);
        if (conversion != nullptr && llvm::isa<clang::CXXConversionDecl>((*conversion).getMemberDecl())) {
            bool call_found = false;
            const clang::DynTypedNode call_node = parent_of(clang::DynTypedNode::create(*conversion), call_found);
            const clang::CXXMemberCallExpr* const call = call_found ? call_node.get<clang::CXXMemberCallExpr>() : nullptr;
            if (call == nullptr || standard_text_conversion_object(call) == nullptr) {
                refuse(key, where, "a conversion of converted text with no counterpart");
                return;
            }
            node = call;
            continue;
        }
        const clang::CallExpr* const forwarding = llvm::dyn_cast<clang::CallExpr>(parent);
        if (forwarding != nullptr && !llvm::isa<clang::CXXOperatorCallExpr>(forwarding) &&
            (*forwarding).getNumArgs() == 1U && (*forwarding).getArg(0U) == node &&
            (is_standard_entity((*forwarding).getDirectCallee(), "move") ||
             is_standard_entity((*forwarding).getDirectCallee(), "forward"))) {
            node = parent;
            continue;
        }
        break;
    }
    const clang::Expr* const parent = parent_node.get<clang::Expr>();
    if (parent == nullptr) {
        // The range of a range-based for is held by an implicit variable.
        const clang::VarDecl* const variable = parent_node.get<clang::VarDecl>();
        const clang::Stmt* const statement = parent_node.get<clang::Stmt>();
        const bool range = (variable != nullptr && (*variable).isImplicit()) ||
            (statement != nullptr && llvm::isa<clang::CXXForRangeStmt>(statement));
        if (range) text_bridge(key, expression, false, "iterated by a range-based for");
        // Otherwise an initialiser, a return or a member initialiser: flows.
        return;
    }
    const clang::MemberExpr* const member = llvm::dyn_cast<clang::MemberExpr>(parent);
    if (member != nullptr) {
        text_member(key, expression, *member);
        return;
    }
    const clang::CXXOperatorCallExpr* const operation = llvm::dyn_cast<clang::CXXOperatorCallExpr>(parent);
    if (operation != nullptr) {
        text_operator(key, *node, expression, *operation);
        return;
    }
    // A comparison C++20 rewrote (`a != b` through ==, `a < b` through
    // <=>), when the parent map shows the written form.
    const clang::CXXRewrittenBinaryOperator* const rewritten = llvm::dyn_cast<clang::CXXRewrittenBinaryOperator>(parent);
    if (rewritten != nullptr) {
        const clang::BinaryOperatorKind written = (*rewritten).getOperator();
        if (written == clang::BO_EQ || written == clang::BO_NE) {
            const clang::Expr* const left = (*rewritten).getLHS();
            const clang::Expr* const other = left == node ? (*rewritten).getRHS() : left;
            text_equality(key, expression, *other);
            return;
        }
        text_bridge(key, expression, false, "compared by order");
        return;
    }
    const clang::CXXConstructExpr* const construction = llvm::dyn_cast<clang::CXXConstructExpr>(parent);
    if (construction != nullptr) {
        // A record other than standard text takes the text as an argument
        // (VisitCXXConstructExpr flows it); standard text made from it is
        // written here: `std::string(name)` becomes the bridge's copy.
        if (expression_family((*construction).getType(), stage_) == Family::none) return;
        const clang::Expr* above = construction;
        const clang::CXXFunctionalCastExpr* cast = nullptr;
        for (unsigned guard = 0U; guard < 8U; ++guard) {
            bool above_found = false;
            const clang::DynTypedNode above_node = parent_of(clang::DynTypedNode::create(*above), above_found);
            const clang::Expr* const next = above_found ? above_node.get<clang::Expr>() : nullptr;
            if (next == nullptr) break;
            cast = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(next);
            if (cast != nullptr) break;
            if (!llvm::isa<clang::CXXBindTemporaryExpr>(next) && !llvm::isa<clang::MaterializeTemporaryExpr>(next) &&
                !llvm::isa<clang::ImplicitCastExpr>(next)) {
                break;
            }
            above = next;
        }
        unsigned written_arguments = 0U;
        for (unsigned index = 0U; index < (*construction).getNumArgs(); ++index) {
            if (!llvm::isa<clang::CXXDefaultArgExpr>((*construction).getArg(index))) ++written_arguments;
        }
        if (cast != nullptr && written_arguments == 1U) {
            text_cast(key, expression, *cast);
            return;
        }
        // `std::string moved(id)`: the bridge's copy initialises it whole.
        const bool copied = written_arguments == 1U && is_standard_string((*construction).getType());
        text_bridge(key, expression, copied, "made into standard text");
        return;
    }
    const clang::CXXFunctionalCastExpr* const functional = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(parent);
    if (functional != nullptr) {
        text_cast(key, expression, *functional);
        return;
    }
    const clang::ExplicitCastExpr* const cast = llvm::dyn_cast<clang::ExplicitCastExpr>(parent);
    if (cast != nullptr) {
        if ((*(*cast).getType()).isVoidType()) return;
        text_bridge(key, expression, is_standard_string((*cast).getType()), "cast");
        return;
    }
    const clang::CallExpr* const call = llvm::dyn_cast<clang::CallExpr>(parent);
    if (call != nullptr) {
        const clang::FunctionDecl* const callee = (*call).getDirectCallee();
        const bool factory = is_standard_entity(callee, "make_shared") || is_standard_entity(callee, "make_unique") ||
            is_standard_entity(callee, "make_optional");
        // A forwarding factory hides the constructor it calls; the flow
        // cannot see whether that constructor's parameter converts.
        if (factory) refuse(key, where, "converted text passed through a forwarding factory");
        return;
    }
    // A lambda's capture copies the text as it is.
    if (llvm::isa<clang::LambdaExpr>(parent)) return;
    const clang::InitListExpr* const list = llvm::dyn_cast<clang::InitListExpr>(parent);
    if (list != nullptr) {
        // A first-party aggregate's fields are flows (VisitInitListExpr);
        // an array's elements (an initializer_list's) read the bridge.
        const clang::QualType listed = (*list).getType();
        const clang::CXXRecordDecl* const record = (*listed).getAsCXXRecordDecl();
        if (record != nullptr && !(*record).isInStdNamespace() && !(*record).isUnion()) return;
        const clang::ArrayType* const array = context_.getAsArrayType(listed);
        if (array == nullptr) {
            refuse(key, where, "converted text in a braced list of a standard type");
            return;
        }
        const clang::QualType element = (*array).getElementType();
        if (is_text_view(element)) return;
        text_bridge(key, expression, is_standard_string(element), "an element of a braced list");
        return;
    }
    const clang::UnaryOperator* const unary = llvm::dyn_cast<clang::UnaryOperator>(parent);
    if (unary != nullptr && (*unary).getOpcode() == clang::UO_AddrOf) {
        refuse(key, where, "the address of converted text is taken");
        return;
    }
    refuse(key, where, std::string("converted text in a form with no V2 rule (") + (*parent).getStmtClassName() + ")");
}

// A method of converted text. size and length are TextView's count;
// empty and data are TextView's own. Any other std::string_view method
// reads a standard view of the same bytes; a std::string method that
// returns std::string reads a standard copy. c_str has no counterpart
// (a TextView promises no terminating NUL), nor do std::string's
// methods that a view lacks.
void Analysis::text_member(const std::string& key, const clang::Expr& spelled, const clang::MemberExpr& member) {
    const clang::SourceLocation where = spelled.getBeginLoc();
    const clang::CXXMethodDecl* const method = llvm::dyn_cast<clang::CXXMethodDecl>(member.getMemberDecl());
    if (method == nullptr || !(*method).getDeclName().isIdentifier()) {
        refuse(key, where, "a member of converted text that is not a named method");
        return;
    }
    bool found = false;
    const clang::DynTypedNode call_node = parent_of(clang::DynTypedNode::create(member), found);
    const clang::CXXMemberCallExpr* const call = found ? call_node.get<clang::CXXMemberCallExpr>() : nullptr;
    if (call == nullptr) {
        refuse(key, where, "a method of converted text named without a call");
        return;
    }
    const llvm::StringRef name = (*method).getName();
    if (name == "empty" || name == "data") return;
    if (name == "size" || name == "length") {
        const Span name_token = token_span(member.getMemberLoc());
        const Span object = span_of(&spelled);
        if (!name_token.valid || !object.valid || name_token.path != object.path || !in_scope(name_token.path)) {
            refuse(key, where, "a size of converted text spelled through a macro");
            return;
        }
        if (plan_) return;
        replace_span(name_token, "count");
        return;
    }
    if (name == "c_str") {
        refuse(key, where, "c_str() of converted text: stxlib::TextView promises no terminating NUL");
        return;
    }
    const bool from_string = (*(*method).getParent()).getName() == "basic_string";
    static const char* const view_methods[] = {
        "substr", "find", "rfind", "find_first_of", "find_last_of", "find_first_not_of", "find_last_not_of",
        "starts_with", "ends_with", "contains", "compare", "front", "back", "at", "begin", "end", "cbegin",
        "cend", "rbegin", "rend", "crbegin", "crend", "copy", "max_size",
    };
    bool viewable = false;
    for (const char* const view_method : view_methods) {
        if (name == view_method) viewable = true;
    }
    if (from_string && !viewable) {
        refuse(key, where, "std::string::" + name.str() + " of converted text has no std::string_view counterpart");
        return;
    }
    const bool copy = from_string && is_standard_string((*call).getType());
    text_bridge(key, spelled, copy, "calls a standard text method");
}

// Converted text as an operand of an operator. A first-party operator's
// operands flow (VisitCallExpr). Of the standard operators, == and !=
// need nothing against literals, TextViews and converted text; ordering
// and the rest read a standard view, or a standard copy where the
// operator takes std::string (operator+). An assignment to the text
// itself has no counterpart; an assignment from it is a flow.
void Analysis::text_operator(const std::string& key, const clang::Expr& node, const clang::Expr& spelled,
                             const clang::CXXOperatorCallExpr& operation) {
    const clang::SourceLocation where = spelled.getBeginLoc();
    const clang::FunctionDecl* const callee = operation.getDirectCallee();
    if (callee != nullptr && !is_standard_callee(callee)) return;
    unsigned index = operation.getNumArgs();
    for (unsigned position = 0U; position < operation.getNumArgs(); ++position) {
        if (operation.getArg(position) == &node) index = position;
    }
    if (index == operation.getNumArgs()) {
        refuse(key, where, "converted text inside an operator expression with no V2 rule");
        return;
    }
    const clang::OverloadedOperatorKind kind = operation.getOperator();
    if (kind == clang::OO_Equal) {
        if (index == 0U) refuse(key, where, "converted text is assigned to");
        return;
    }
    if ((kind == clang::OO_EqualEqual || kind == clang::OO_ExclaimEqual) && operation.getNumArgs() == 2U) {
        text_equality(key, spelled, *operation.getArg(index == 0U ? 1U : 0U));
        return;
    }
    const bool ordering = kind == clang::OO_Less || kind == clang::OO_Greater || kind == clang::OO_LessEqual ||
        kind == clang::OO_GreaterEqual || kind == clang::OO_Spaceship;
    if (ordering) {
        // stxlib's vocabulary has no ordering; the standard view orders the
        // same bytes. Each converted side wraps itself.
        text_bridge(key, spelled, false, "compared by order");
        return;
    }
    bool copy = false;
    const clang::CXXMethodDecl* const method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
    const bool member_operator = method != nullptr && !(*method).isStatic();
    if (callee != nullptr && !(member_operator && index == 0U)) {
        const unsigned parameter = member_operator ? index - 1U : index;
        if (parameter < (*callee).getNumParams()) {
            copy = is_standard_string((*(*callee).getParamDecl(parameter)).getType());
        }
    }
    text_bridge(key, spelled, copy, "an operand of a standard operator");
}

// Converted text compared for equality. A literal or a TextView compares
// with a TextView as it is. Another candidate parameter: in a public
// header the two are tied (both convert or neither); elsewhere the
// rewrite looks at whether the other side converted.
void Analysis::text_equality(const std::string& key, const clang::Expr& spelled, const clang::Expr& other) {
    const clang::StringLiteral* const literal = literal_text(&other);
    if (literal != nullptr) {
        if (literal_refuses_text_view(*literal)) {
            refuse(key, spelled.getBeginLoc(), "compared with a literal that is not well-formed UTF-8");
        }
        return;
    }
    const clang::Expr* const other_plain = strip_value(&other, stage_);
    if (other_plain != nullptr && is_text_view((*other_plain).getType())) return;
    const std::string other_key = text_candidate(&other);
    if (!other_key.empty()) {
        std::string reason;
        const bool bridged = bridge_site(spelled, reason);
        if (plan_) {
            if (!bridged) edge(key, other_key, true, false);
            return;
        }
        if (is_converted(other_key)) return;
    }
    text_bridge(key, spelled, false, "compared with standard text");
}

// Converted text cast to standard text. `std::string(name)` (one
// argument, parenthesised) becomes the bridge's copy,
// `gui_forms::platform::standard_text_copy(name)`; any other functional
// cast reads a standard view inside the cast.
void Analysis::text_cast(const std::string& key, const clang::Expr& spelled, const clang::CXXFunctionalCastExpr& cast) {
    const Span whole = span_of(&cast);
    const Span opening = token_span(cast.getLParenLoc());
    const bool replaceable = is_standard_string(cast.getType()) && cast.getLParenLoc().isValid() && whole.valid &&
        opening.valid && opening.path == whole.path && opening.begin >= whole.begin;
    if (!replaceable) {
        text_bridge(key, spelled, false, "cast to standard text");
        return;
    }
    std::string reason;
    if (!bridge_site(cast, reason)) {
        refuse(key, spelled.getBeginLoc(), "converted text cast to std::string " + reason);
        return;
    }
    if (plan_) return;
    replace_bytes(whole, whole.begin, opening.end, "gui_forms::platform::standard_text_copy(");
    need(whole.path, include_bridge);
}

// Wraps the spelled text in the bridge's standard view (or copy), or,
// where the bridge cannot be written, refuses the parameter.
void Analysis::text_bridge(const std::string& key, const clang::Expr& spelled, const bool copy, const char* what) {
    std::string reason;
    if (!bridge_site(spelled, reason)) {
        refuse(key, spelled.getBeginLoc(), std::string("converted text ") + what + " " + reason);
        return;
    }
    if (plan_) return;
    const Span span = span_of(&spelled);
    wrap(span, copy ? "gui_forms::platform::standard_text_copy(" : "gui_forms::platform::standard_text(", ")");
    need(span.path, include_bridge);
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
            edge(entity.key, other_entity.key, stage_ == Stage::v5_ownership, true);
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
    if ((*other).isTypeDependent()) {
        // The instantiations decide, as for a dependent flow.
        std::vector<const clang::Expr*> instances;
        instantiated_counterparts(*other, instances);
        bool unchanged = true;
        for (const clang::Expr* instance : instances) {
            const clang::Expr* const plain = strip_value(instance, stage_);
            const Denoted instance_entity = denoted(instance);
            const bool converted = instance_entity.found && is_converted(instance_entity.key);
            const bool owned = owned_family((*plain).getType()) != Family::none;
            unchanged = unchanged && (converted || owned);
        }
        if (!unchanged) {
            residue_at(top.getBeginLoc(), "compared with a template-dependent value whose instantiations need a "
                                          "rewrite: compare by hand");
        }
        return;
    }
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
