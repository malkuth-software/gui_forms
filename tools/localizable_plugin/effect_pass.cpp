// The gui_forms plugin's effect pass: see effect_pass.hpp for what it does,
// and EFFECT_PROGRAMS_DESIGN.md §2 and §3 for the rules and the cost model.
//
// The walk. From an effect's run (and setup), every statement and expression
// is visited once per function. A call is followed into its callee when the
// callee is neither vocabulary (GUI_FORMS_EFFECT_VOCABULARY) nor one of the
// few standard accessors admitted below; the callee's body must be visible
// and obey the same rules. Each function's cost is computed once and kept,
// so a helper shared by several effects is walked and reported once, and a
// function met again while its own walk is still running is recursion.
//
// The cost model (design note §3): each arithmetic, comparison or logical
// operator counts 1; a call to a verified function counts 1 plus its body;
// a vocabulary call counts its declared fixed cost; a loop multiplies its
// body (and its own comparison and step, 2 a pass) by its bound; if/else,
// ?: and switch take the larger branch (a switch takes the sum of its body,
// which bounds every path through it); argument expressions are counted
// where they are evaluated. Plain assignment, member access, indexing,
// casts and trivial copies count nothing. All sums saturate at 2^64 − 1.
//
// Admitted without a walk: std::array's operator[], size, empty, front,
// back and data, and std::move, std::forward and std::as_const (each 0 plus
// its arguments; the compiler's builtins for the last three). Their libc++
// bodies may carry hardening checks that call the opaque abort routine,
// which the walk would otherwise reject.

#include "effect_pass.hpp"

#include "clang/AST/APValue.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringRef.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gui_forms_plugin {
namespace {

constexpr llvm::StringLiteral vocabulary_annotation = "gui_forms.effect_vocabulary";
constexpr std::uint64_t saturated = std::numeric_limits<std::uint64_t>::max();
// The operations a range-for's own comparison and step take each pass.
constexpr std::uint64_t loop_pass_overhead = 2U;

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) {
    if (saturated - left < right) return saturated;
    const std::uint64_t sum = left + right;
    return sum;
}

std::uint64_t saturating_multiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0U && right > saturated / left) return saturated;
    const std::uint64_t product = left * right;
    return product;
}

std::uint64_t larger(std::uint64_t left, std::uint64_t right) {
    const std::uint64_t result = left > right ? left : right;
    return result;
}

// Whether `declaration` is directly in the top-level namespace `name`.
bool in_top_namespace(const clang::Decl* declaration, llvm::StringRef name) {
    const clang::DeclContext* context = (*declaration).getDeclContext();
    while (context != nullptr && !(*context).isNamespace() && !(*context).isTranslationUnit()) {
        context = (*context).getParent();
    }
    const clang::NamespaceDecl* space = llvm::dyn_cast_or_null<clang::NamespaceDecl>(context);
    if (space == nullptr) return false;
    const clang::DeclContext* outer = (*(*space).getParent()).getRedeclContext();
    if (!(*outer).isTranslationUnit()) return false;
    return (*space).getName() == name;
}

// The record a type names, through references and qualifiers, or null.
const clang::CXXRecordDecl* record_of(clang::QualType type) {
    const clang::QualType plain = type.getNonReferenceType().getCanonicalType();
    const clang::CXXRecordDecl* record = (*plain).getAsCXXRecordDecl();
    return record;
}

// The integral template argument `index` of a class template
// specialization, if it has one.
std::optional<std::uint64_t> integral_argument(const clang::CXXRecordDecl* record, unsigned index) {
    const clang::ClassTemplateSpecializationDecl* specialization =
        llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(record);
    if (specialization == nullptr) return std::nullopt;
    const clang::TemplateArgumentList& arguments = (*specialization).getTemplateArgs();
    if (index >= arguments.size()) return std::nullopt;
    const clang::TemplateArgument& argument = arguments.get(index);
    if (argument.getKind() != clang::TemplateArgument::Integral) return std::nullopt;
    const llvm::APSInt value = argument.getAsIntegral();
    if (value.isSigned() && value.isNegative()) return std::nullopt;
    const std::uint64_t bound = value.getZExtValue();
    return bound;
}

// A vocabulary function's declared fixed cost, looking through its
// redeclarations and the template member it was instantiated from.
std::optional<std::uint64_t> vocabulary_cost(const clang::FunctionDecl* function, const clang::ASTContext& context) {
    std::vector<const clang::FunctionDecl*> candidates;
    candidates.push_back(function);
    const clang::FunctionDecl* pattern = (*function).getTemplateInstantiationPattern();
    if (pattern != nullptr) candidates.push_back(pattern);
    for (const clang::FunctionDecl* candidate : candidates) {
        for (const clang::FunctionDecl* declaration : (*candidate).redecls()) {
            for (const clang::AnnotateAttr* attribute : (*declaration).specific_attrs<clang::AnnotateAttr>()) {
                if ((*attribute).getAnnotation() != vocabulary_annotation || (*attribute).args_size() < 1U) continue;
                const clang::Expr* fixed = *(*attribute).args_begin();
                const std::optional<llvm::APSInt> value = (*fixed).getIntegerConstantExpr(context);
                if (!value || (*value).isNegative()) return std::nullopt;
                const std::uint64_t cost = (*value).getZExtValue();
                return cost;
            }
        }
    }
    return std::nullopt;
}

bool contains(llvm::StringRef text, llvm::StringRef part) {
    const bool found = text.contains(part);
    return found;
}

// Why a call into the standard library or the system is refused, by the
// header that declares the callee and its name; empty for no category.
std::string library_category(const clang::FunctionDecl* function, const clang::SourceManager& sources) {
    const clang::SourceLocation location = sources.getSpellingLoc((*function).getLocation());
    const llvm::StringRef file = sources.getFilename(location);
    const std::string name = (*function).getNameAsString();
    const clang::CXXRecordDecl* owner = llvm::dyn_cast<clang::CXXRecordDecl>((*function).getDeclContext());
    const std::string owner_name = owner != nullptr ? (*owner).getNameAsString() : std::string();
    if (contains(file, "random") || name == "rand" || name == "srand" || name == "random" ||
        name == "arc4random" || name == "drand48") {
        return "library randomness";
    }
    if (contains(file, "math") || contains(file, "complex")) return "<cmath> (the host's libm)";
    if (contains(file, "chrono") || contains(file, "time.h") || contains(file, "ctime") || name == "clock" ||
        name == "time" || name == "clock_gettime" || name == "mach_absolute_time") {
        return "a clock";
    }
    if (contains(file, "atomic") || llvm::StringRef(owner_name).starts_with("atomic")) return "atomics";
    if (contains(file, "thread") || contains(file, "mutex") || contains(file, "condition_variable") ||
        contains(file, "future") || contains(file, "semaphore") || contains(file, "latch") ||
        contains(file, "barrier")) {
        return "threads";
    }
    if (contains(file, "stdio") || contains(file, "stream") || contains(file, "print") ||
        contains(file, "unistd") || contains(file, "fcntl") || contains(file, "filesystem")) {
        return "I/O";
    }
    if (name == "malloc" || name == "calloc" || name == "realloc" || name == "free" || name == "aligned_alloc" ||
        (*function).getOverloadedOperator() == clang::OO_New ||
        (*function).getOverloadedOperator() == clang::OO_Delete ||
        (*function).getOverloadedOperator() == clang::OO_Array_New ||
        (*function).getOverloadedOperator() == clang::OO_Array_Delete || contains(file, "memory") ||
        contains(file, "__new") || contains(file, "/new")) {
        return "allocation";
    }
    return {};
}

// Whether `function` is one of the standard accessors admitted without a
// walk (see the head of this file).
bool admitted_accessor(const clang::FunctionDecl* function) {
    const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
    if (method != nullptr) {
        const clang::CXXRecordDecl* owner = (*method).getParent();
        if (!(*owner).isInStdNamespace() || (*owner).getName() != "array") return false;
        if ((*method).getOverloadedOperator() == clang::OO_Subscript) return true;
        if (!(*method).getDeclName().isIdentifier()) return false;
        const llvm::StringRef name = (*method).getName();
        return name == "size" || name == "empty" || name == "front" || name == "back" || name == "data";
    }
    if (!(*function).isInStdNamespace() || !(*function).getDeclName().isIdentifier()) return false;
    const llvm::StringRef name = (*function).getName();
    return name == "move" || name == "forward" || name == "as_const";
}

// A standard class an effect may not construct: std::function, or any
// standard class with a non-trivial destructor (std::vector, std::string,
// std::unique_ptr and the like own storage). Empty when admitted.
std::string refused_standard_type(clang::QualType type) {
    const clang::CXXRecordDecl* record = record_of(type);
    if (record == nullptr || !(*record).isInStdNamespace() || !(*record).hasDefinition()) return {};
    if ((*record).getName() == "function" || (*record).getName() == "move_only_function") return "std::function";
    if ((*record).hasNonTrivialDestructor()) return "allocation";
    return {};
}

// One loop's share of a function's bound, for the over-budget breakdown.
struct LoopCost final {
    clang::SourceLocation location;
    std::uint64_t passes{};
    std::uint64_t per_pass{};
};

// What the walk found for one function: whether it obeys the rules, its
// bound in operations, its loops and the verified functions it calls.
struct FunctionCost final {
    bool obeys{true};
    std::uint64_t operations{};
    std::vector<LoopCost> loops;
    std::vector<const clang::FunctionDecl*> callees;
};

// A counted for's bound, or the reason it has none.
struct CountedBound final {
    std::optional<std::uint64_t> passes;
    std::string reason;
};

class EffectWalker final {
public:
    EffectWalker(clang::ASTContext& context, clang::DiagnosticsEngine& diagnostics)
        : context_(context), diagnostics_(diagnostics),
          error_(diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0")),
          note_(diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Note, "%0")) {}

    // The effect whose walk is running, named in each error's note.
    void begin_effect(const std::string& name, clang::SourceLocation location) {
        effect_name_ = name;
        effect_location_ = location;
    }

    void error(clang::SourceLocation location, const std::string& message) {
        diagnostics_.Report(location, error_) << message;
        diagnostics_.Report(effect_location_, note_) << ("while verifying effect '" + effect_name_ + "'");
        if (!frames_.empty()) frames_.back().obeys = false;
    }

    void note(clang::SourceLocation location, const std::string& message) {
        diagnostics_.Report(location, note_) << message;
    }

    // The walk of one function: its cost, computed once.
    const FunctionCost& function_cost(const clang::FunctionDecl* function, clang::SourceLocation call_site) {
        const clang::FunctionDecl* key = (*function).getCanonicalDecl();
        const std::map<const clang::FunctionDecl*, FunctionCost>::const_iterator known = costs_.find(key);
        if (known != costs_.end()) return (*known).second;
        for (const clang::FunctionDecl* running : running_) {
            if (running != key) continue;
            std::string path;
            for (const clang::FunctionDecl* step : running_) path += (*step).getQualifiedNameAsString() + " → ";
            path += (*function).getQualifiedNameAsString();
            error(call_site, "effect code may not recurse: '" + (*function).getQualifiedNameAsString() +
                                 "' is called again while it runs (" + path + ")");
            failed_recursion_.obeys = false;
            return failed_recursion_;
        }
        const clang::FunctionDecl* definition = nullptr;
        if (!(*function).hasBody(definition) || definition == nullptr) {
            error(call_site, "effect code may not call '" + (*function).getQualifiedNameAsString() +
                                 "': its body is not visible to the effect pass (define it in this file or a "
                                 "header, or mark it GUI_FORMS_EFFECT_VOCABULARY)");
            failed_recursion_.obeys = false;
            return failed_recursion_;
        }
        running_.push_back(key);
        frames_.push_back(FunctionCost{});
        std::uint64_t operations = 0U;
        const clang::CXXConstructorDecl* constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(definition);
        if (constructor != nullptr) {
            for (const clang::CXXCtorInitializer* initializer : (*constructor).inits()) {
                operations = saturating_add(operations, cost((*initializer).getInit()));
            }
        }
        operations = saturating_add(operations, cost((*definition).getBody()));
        FunctionCost result = frames_.back();
        result.operations = operations;
        frames_.pop_back();
        running_.pop_back();
        if (!frames_.empty() && !result.obeys) frames_.back().obeys = false;
        const std::pair<std::map<const clang::FunctionDecl*, FunctionCost>::iterator, bool> inserted =
            costs_.emplace(key, result);
        return (*inserted.first).second;
    }

    // The cost of an effect's entry point: its body, not counted as a call.
    FunctionCost entry_cost(const clang::FunctionDecl* function) {
        frames_.push_back(FunctionCost{});
        FunctionCost result = function_cost(function, (*function).getLocation());
        FunctionCost outer = frames_.back();
        frames_.pop_back();
        if (!outer.obeys) result.obeys = false;
        return result;
    }

    // Every loop reached from `function`, its own and its callees', for the
    // over-budget breakdown.
    void collect_loops(const clang::FunctionDecl* function, std::set<const clang::FunctionDecl*>& seen,
                       std::vector<LoopCost>& out) const {
        const clang::FunctionDecl* key = (*function).getCanonicalDecl();
        if (!seen.insert(key).second) return;
        const std::map<const clang::FunctionDecl*, FunctionCost>::const_iterator known = costs_.find(key);
        if (known == costs_.end()) return;
        for (const LoopCost& loop : (*known).second.loops) out.push_back(loop);
        for (const clang::FunctionDecl* callee : (*known).second.callees) collect_loops(callee, seen, out);
    }

private:
    std::uint64_t cost(const clang::Stmt* statement) {
        if (statement == nullptr) return 0U;
        if (llvm::isa<clang::WhileStmt>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use a 'while' loop");
            return 0U;
        }
        if (llvm::isa<clang::DoStmt>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use a 'do' loop");
            return 0U;
        }
        if (llvm::isa<clang::GotoStmt>(statement) || llvm::isa<clang::IndirectGotoStmt>(statement) ||
            llvm::isa<clang::AddrLabelExpr>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use 'goto'");
            return 0U;
        }
        if (llvm::isa<clang::AsmStmt>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use inline assembly");
            return 0U;
        }
        if (llvm::isa<clang::CXXTryStmt>(statement) || llvm::isa<clang::CXXThrowExpr>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not throw or catch exceptions");
            return 0U;
        }
        if (llvm::isa<clang::CXXDynamicCastExpr>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use dynamic_cast");
            return 0U;
        }
        if (llvm::isa<clang::CXXTypeidExpr>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use typeid");
            return 0U;
        }
        if (llvm::isa<clang::CXXNewExpr>(statement) || llvm::isa<clang::CXXDeleteExpr>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not allocate: 'new' and 'delete' are refused");
            return 0U;
        }
        if (llvm::isa<clang::AtomicExpr>(statement)) {
            error((*statement).getBeginLoc(), "effect code may not use atomics");
            return 0U;
        }
        if (const clang::CXXForRangeStmt* range_for = llvm::dyn_cast<clang::CXXForRangeStmt>(statement)) {
            return range_for_cost(range_for);
        }
        if (const clang::ForStmt* counted = llvm::dyn_cast<clang::ForStmt>(statement)) {
            return counted_for_cost(counted);
        }
        if (const clang::IfStmt* branch = llvm::dyn_cast<clang::IfStmt>(statement)) {
            std::uint64_t operations = cost((*branch).getInit());
            operations = saturating_add(operations, cost((*branch).getConditionVariableDeclStmt()));
            operations = saturating_add(operations, cost((*branch).getCond()));
            const std::uint64_t taken = larger(cost((*branch).getThen()), cost((*branch).getElse()));
            return saturating_add(operations, taken);
        }
        if (const clang::AbstractConditionalOperator* conditional =
                llvm::dyn_cast<clang::AbstractConditionalOperator>(statement)) {
            const std::uint64_t condition = cost((*conditional).getCond());
            const std::uint64_t taken =
                larger(cost((*conditional).getTrueExpr()), cost((*conditional).getFalseExpr()));
            return saturating_add(condition, taken);
        }
        if (const clang::DeclStmt* declarations = llvm::dyn_cast<clang::DeclStmt>(statement)) {
            return declaration_cost(declarations);
        }
        if (const clang::CallExpr* call = llvm::dyn_cast<clang::CallExpr>(statement)) return call_cost(call);
        if (const clang::CXXConstructExpr* construct = llvm::dyn_cast<clang::CXXConstructExpr>(statement)) {
            return construct_cost(construct);
        }
        if (const clang::CXXDefaultArgExpr* argument = llvm::dyn_cast<clang::CXXDefaultArgExpr>(statement)) {
            return cost((*argument).getExpr());
        }
        if (const clang::CXXDefaultInitExpr* field = llvm::dyn_cast<clang::CXXDefaultInitExpr>(statement)) {
            return cost((*field).getExpr());
        }
        if (const clang::CXXBindTemporaryExpr* bound = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(statement)) {
            const std::uint64_t operations = cost((*bound).getSubExpr());
            const clang::CXXDestructorDecl* destructor = (*(*bound).getTemporary()).getDestructor();
            return saturating_add(operations, destructor_cost(destructor, (*bound).getBeginLoc()));
        }
        if (const clang::LambdaExpr* lambda = llvm::dyn_cast<clang::LambdaExpr>(statement)) {
            std::uint64_t operations = 0U;
            for (const clang::Expr* capture : (*lambda).capture_inits()) {
                operations = saturating_add(operations, cost(capture));
            }
            return operations;
        }
        if (const clang::ArrayInitLoopExpr* array = llvm::dyn_cast<clang::ArrayInitLoopExpr>(statement)) {
            const std::uint64_t elements = (*array).getArraySize().getZExtValue();
            return saturating_multiply(elements, cost((*array).getSubExpr()));
        }
        if (llvm::isa<clang::UnaryExprOrTypeTraitExpr>(statement)) return 0U;
        if (const clang::BinaryOperator* binary = llvm::dyn_cast<clang::BinaryOperator>(statement)) {
            return binary_cost(binary);
        }
        if (const clang::UnaryOperator* unary = llvm::dyn_cast<clang::UnaryOperator>(statement)) {
            return unary_cost(unary);
        }
        if (const clang::DeclRefExpr* reference = llvm::dyn_cast<clang::DeclRefExpr>(statement)) {
            check_reference((*reference).getDecl(), (*reference).getBeginLoc(), false);
            return 0U;
        }
        if (const clang::MemberExpr* member = llvm::dyn_cast<clang::MemberExpr>(statement)) {
            check_reference((*member).getMemberDecl(), (*member).getMemberLoc(), false);
            return cost((*member).getBase());
        }
        std::uint64_t operations = 0U;
        for (const clang::Stmt* child : (*statement).children()) operations = saturating_add(operations, cost(child));
        return operations;
    }

    // A variable, function or static member named outside a call: a
    // non-constexpr variable with static or thread storage is a global
    // (read or written), and a function named outside a call has its
    // address taken.
    void check_reference(const clang::ValueDecl* declaration, clang::SourceLocation location, bool written) {
        if (const clang::VarDecl* variable = llvm::dyn_cast_or_null<clang::VarDecl>(declaration)) {
            if (!(*variable).hasGlobalStorage() || (*variable).isConstexpr()) return;
            const std::string name = (*variable).getQualifiedNameAsString();
            if (written) {
                error(location, "effect code may not write the global variable '" + name + "'");
            } else {
                error(location, "effect code may not read the global variable '" + name +
                                    "', which is not constexpr");
            }
            return;
        }
        if (llvm::isa_and_nonnull<clang::FunctionDecl>(declaration)) {
            error(location, "effect code may not take the address of '" + (*declaration).getQualifiedNameAsString() +
                                "': calls through function pointers are refused");
        }
    }

    // The variable or static member an lvalue expression names at its root
    // (x, x.a, x.a[i]), if it names one.
    const clang::Expr* lvalue_root(const clang::Expr* expression) const {
        const clang::Expr* current = (*expression).IgnoreParenImpCasts();
        for (unsigned step = 0U; step < 64U; ++step) {
            if (const clang::ArraySubscriptExpr* subscript = llvm::dyn_cast<clang::ArraySubscriptExpr>(current)) {
                current = (*(*subscript).getBase()).IgnoreParenImpCasts();
                continue;
            }
            if (const clang::MemberExpr* member = llvm::dyn_cast<clang::MemberExpr>(current)) {
                if (llvm::isa<clang::VarDecl>((*member).getMemberDecl())) return current;
                current = (*(*member).getBase()).IgnoreParenImpCasts();
                continue;
            }
            return current;
        }
        return current;
    }

    // A write through an assignment, an increment or an address taken: its
    // target is checked as written, the rest of it as read.
    std::uint64_t written_cost(const clang::Expr* target) {
        const clang::Expr* root = lvalue_root(target);
        if (const clang::DeclRefExpr* reference = llvm::dyn_cast<clang::DeclRefExpr>(root)) {
            const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>((*reference).getDecl());
            if (variable != nullptr && (*variable).hasGlobalStorage()) {
                check_reference(variable, (*reference).getBeginLoc(), true);
                return 0U;
            }
        }
        if (const clang::MemberExpr* member = llvm::dyn_cast<clang::MemberExpr>(root)) {
            const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>((*member).getMemberDecl());
            if (variable != nullptr && (*variable).hasGlobalStorage()) {
                check_reference(variable, (*member).getMemberLoc(), true);
                return cost((*member).getBase());
            }
        }
        return cost(target);
    }

    std::uint64_t binary_cost(const clang::BinaryOperator* binary) {
        const clang::BinaryOperatorKind kind = (*binary).getOpcode();
        if (kind == clang::BO_PtrMemD || kind == clang::BO_PtrMemI) {
            error((*binary).getOperatorLoc(), "effect code may not use pointers to members");
            return 0U;
        }
        const std::uint64_t right = cost((*binary).getRHS());
        if ((*binary).isAssignmentOp()) {
            const std::uint64_t left = written_cost((*binary).getLHS());
            const std::uint64_t own = kind == clang::BO_Assign ? 0U : 1U;
            return saturating_add(saturating_add(left, right), own);
        }
        const std::uint64_t left = cost((*binary).getLHS());
        const std::uint64_t own = kind == clang::BO_Comma ? 0U : 1U;
        return saturating_add(saturating_add(left, right), own);
    }

    std::uint64_t unary_cost(const clang::UnaryOperator* unary) {
        const clang::UnaryOperatorKind kind = (*unary).getOpcode();
        if ((*unary).isIncrementDecrementOp() || kind == clang::UO_AddrOf) {
            const clang::Expr* operand = (*(*unary).getSubExpr()).IgnoreParenImpCasts();
            if (const clang::DeclRefExpr* reference = llvm::dyn_cast<clang::DeclRefExpr>(operand)) {
                if (llvm::isa<clang::FunctionDecl>((*reference).getDecl())) {
                    check_reference((*reference).getDecl(), (*reference).getBeginLoc(), false);
                    return 0U;
                }
            }
            const std::uint64_t operand_cost = written_cost((*unary).getSubExpr());
            const std::uint64_t own = kind == clang::UO_AddrOf ? 0U : 1U;
            return saturating_add(operand_cost, own);
        }
        const std::uint64_t operand_cost = cost((*unary).getSubExpr());
        const bool counted = kind == clang::UO_Minus || kind == clang::UO_Not || kind == clang::UO_LNot;
        return saturating_add(operand_cost, counted ? 1U : 0U);
    }

    std::uint64_t declaration_cost(const clang::DeclStmt* declarations) {
        std::uint64_t operations = 0U;
        for (const clang::Decl* declaration : (*declarations).decls()) {
            const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>(declaration);
            if (variable == nullptr) continue;
            if ((*variable).hasGlobalStorage() && !(*variable).isConstexpr()) {
                error((*variable).getLocation(), "effect code may not declare the static variable '" +
                                                     (*variable).getNameAsString() + "', which is a global");
                continue;
            }
            const std::string refused = refused_standard_type((*variable).getType());
            if (!refused.empty()) {
                standard_type_error((*variable).getLocation(), refused, (*variable).getType());
                continue;
            }
            operations = saturating_add(operations, cost((*variable).getInit()));
        }
        return operations;
    }

    void standard_type_error(clang::SourceLocation location, const std::string& refused, clang::QualType type) {
        const std::string spelled = type.getAsString();
        if (refused == "std::function") {
            error(location, "effect code may not use std::function ('" + spelled + "'): calls go to named functions");
            return;
        }
        error(location, "effect code may not allocate: '" + spelled + "' is a standard type that owns storage");
    }

    std::uint64_t destructor_cost(const clang::CXXDestructorDecl* destructor, clang::SourceLocation location) {
        if (destructor == nullptr || (*destructor).isTrivial()) return 0U;
        const FunctionCost& body = function_cost(destructor, location);
        note_callee(destructor);
        return saturating_add(1U, body.operations);
    }

    void note_callee(const clang::FunctionDecl* function) {
        if (!frames_.empty()) frames_.back().callees.push_back((*function).getCanonicalDecl());
    }

    std::uint64_t arguments_cost(const clang::CallExpr* call) {
        std::uint64_t operations = 0U;
        for (const clang::Expr* argument : (*call).arguments()) {
            operations = saturating_add(operations, cost(argument));
        }
        return operations;
    }

    std::uint64_t call_cost(const clang::CallExpr* call) {
        const clang::FunctionDecl* callee = (*call).getDirectCallee();
        const clang::SourceLocation location = (*call).getBeginLoc();
        if (callee == nullptr) {
            error(location, "effect code may not call through a function pointer");
            return 0U;
        }
        const std::string name = (*callee).getQualifiedNameAsString();
        const clang::CXXOperatorCallExpr* operator_call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(call);
        std::uint64_t operations = 0U;
        if (operator_call != nullptr && (*operator_call).isAssignmentOp() && (*call).getNumArgs() == 2U) {
            // The target of a class assignment is written, not read.
            operations = saturating_add(written_cost((*call).getArg(0U)), cost((*call).getArg(1U)));
        } else {
            operations = arguments_cost(call);
        }
        if (const clang::CXXMemberCallExpr* member = llvm::dyn_cast<clang::CXXMemberCallExpr>(call)) {
            operations = saturating_add(operations, cost((*member).getImplicitObjectArgument()));
        }
        const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
        if (method != nullptr && (*method).isVirtual()) {
            error(location, "effect code may not make a virtual call ('" + name + "')");
            return operations;
        }
        if (method != nullptr) {
            const clang::CXXRecordDecl* owner = (*method).getParent();
            if ((*owner).isInStdNamespace() &&
                ((*owner).getName() == "function" || (*owner).getName() == "move_only_function")) {
                error(location, "effect code may not use std::function ('" + name + "'): calls go to named functions");
                return operations;
            }
        }
        // A trivial copy or move assignment copies bytes: no operations.
        if ((*callee).isTrivial()) return operations;
        const std::optional<std::uint64_t> vocabulary = vocabulary_cost(callee, context_);
        if (vocabulary) return saturating_add(operations, *vocabulary);
        if (admitted_accessor(callee)) return operations;
        const unsigned builtin = (*callee).getBuiltinID();
        const clang::SourceManager& sources = context_.getSourceManager();
        const bool system = sources.isInSystemHeader((*callee).getLocation());
        if (builtin != 0U || (*callee).isInStdNamespace() || system) {
            const std::string category = library_category(callee, sources);
            if (category.empty()) {
                error(location, "effect code may not call '" + name +
                                    "': it is a standard library or system function, not effect vocabulary");
            } else {
                error(location, "effect code may not call '" + name + "': " + category +
                                    (category == "<cmath> (the host's libm)" ? "; maths goes through effect_* "
                                                                               "vocabulary and engine_math"
                                                                             : std::string()));
            }
            return operations;
        }
        const FunctionCost& body = function_cost(callee, location);
        note_callee(callee);
        operations = saturating_add(operations, 1U);
        return saturating_add(operations, body.operations);
    }

    std::uint64_t construct_cost(const clang::CXXConstructExpr* construct) {
        const clang::SourceLocation location = (*construct).getBeginLoc();
        const std::string refused = refused_standard_type((*construct).getType());
        if (!refused.empty()) {
            standard_type_error(location, refused, (*construct).getType());
            return 0U;
        }
        std::uint64_t operations = 0U;
        for (const clang::Expr* argument : (*construct).arguments()) {
            operations = saturating_add(operations, cost(argument));
        }
        const clang::CXXConstructorDecl* constructor = (*construct).getConstructor();
        if (constructor == nullptr || (*constructor).isTrivial()) return operations;
        const std::optional<std::uint64_t> vocabulary = vocabulary_cost(constructor, context_);
        if (vocabulary) return saturating_add(operations, *vocabulary);
        const clang::SourceManager& sources = context_.getSourceManager();
        if ((*constructor).isInStdNamespace() || sources.isInSystemHeader((*constructor).getLocation())) {
            const std::string category = library_category(constructor, sources);
            const std::string name = (*(*constructor).getParent()).getQualifiedNameAsString();
            error(location, "effect code may not construct '" + name + "': " +
                                (category.empty() ? std::string("it is a standard library type, not effect vocabulary")
                                                  : category));
            return operations;
        }
        const FunctionCost& body = function_cost(constructor, location);
        note_callee(constructor);
        operations = saturating_add(operations, saturating_add(1U, body.operations));
        const clang::CXXRecordDecl* record = (*constructor).getParent();
        if ((*record).hasDefinition() && (*record).hasNonTrivialDestructor()) {
            operations = saturating_add(operations, destructor_cost((*record).getDestructor(), location));
        }
        return operations;
    }

    std::uint64_t range_for_cost(const clang::CXXForRangeStmt* loop) {
        const clang::Expr* range = (*loop).getRangeInit();
        const clang::CXXRecordDecl* record = range != nullptr ? record_of((*range).getType()) : nullptr;
        std::optional<std::uint64_t> bound;
        if (record != nullptr && in_top_namespace(record, "gui_forms") && (*record).getName() == "BoundedRange") {
            bound = integral_argument(record, 0U);
        } else if (record != nullptr && (*record).isInStdNamespace() && (*record).getName() == "array") {
            bound = integral_argument(record, 1U);
        } else if (record != nullptr && in_top_namespace(record, "gui_forms") && (*record).getName() == "EffectArray") {
            bound = integral_argument(record, 1U);
        }
        if (!bound) {
            const std::string spelled = range != nullptr ? (*range).getType().getAsString() : std::string("?");
            error((*loop).getForLoc(), "effect code may not use this 'for' loop: a range-for runs only over "
                                       "bounded_range<N>(count), std::array or EffectArray, not '" +
                                           spelled + "'");
            return 0U;
        }
        std::uint64_t operations = cost((*loop).getInit());
        operations = saturating_add(operations, cost(range));
        const std::uint64_t per_pass = saturating_add(loop_pass_overhead, cost((*loop).getBody()));
        record_loop((*loop).getForLoc(), *bound, per_pass);
        return saturating_add(operations, saturating_multiply(*bound, per_pass));
    }

    void record_loop(clang::SourceLocation location, std::uint64_t passes, std::uint64_t per_pass) {
        if (frames_.empty()) return;
        LoopCost loop;
        loop.location = location;
        loop.passes = passes;
        loop.per_pass = per_pass;
        frames_.back().loops.push_back(loop);
    }

    std::uint64_t counted_for_cost(const clang::ForStmt* loop) {
        const CountedBound bound = counted_bound(loop);
        if (!bound.passes) {
            error((*loop).getForLoc(), "effect code may not use this 'for' loop: " + bound.reason);
            return 0U;
        }
        const std::uint64_t passes = *bound.passes;
        const std::uint64_t condition = cost((*loop).getCond());
        std::uint64_t per_pass = saturating_add(condition, cost((*loop).getInc()));
        per_pass = saturating_add(per_pass, cost((*loop).getBody()));
        record_loop((*loop).getForLoc(), passes, per_pass);
        std::uint64_t operations = cost((*loop).getInit());
        operations = saturating_add(operations, condition);
        return saturating_add(operations, saturating_multiply(passes, per_pass));
    }

    // The bound of `for (T i = c0; i OP c1; step) body`, where c0, c1 and
    // the step are constants and the body does not write i.
    CountedBound counted_bound(const clang::ForStmt* loop) {
        CountedBound result;
        const clang::DeclStmt* initial = llvm::dyn_cast_or_null<clang::DeclStmt>((*loop).getInit());
        if (initial == nullptr || !(*initial).isSingleDecl()) {
            result.reason = "a counted for declares one loop variable with a constant initial value";
            return result;
        }
        const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>((*initial).getSingleDecl());
        if (variable == nullptr || !(*(*variable).getType()).isIntegerType() || (*variable).getInit() == nullptr) {
            result.reason = "a counted for declares one integer loop variable with a constant initial value";
            return result;
        }
        const std::optional<llvm::APSInt> start = (*(*variable).getInit()).getIntegerConstantExpr(context_);
        if (!start) {
            result.reason = "its loop variable's initial value is not a constant";
            return result;
        }
        const clang::BinaryOperator* condition =
            llvm::dyn_cast_or_null<clang::BinaryOperator>((*loop).getCond());
        if (condition == nullptr || !(*condition).isComparisonOp() || (*condition).getOpcode() == clang::BO_Cmp) {
            result.reason = "its condition is not a comparison of the loop variable with a constant";
            return result;
        }
        clang::BinaryOperatorKind kind = (*condition).getOpcode();
        const clang::Expr* limit_expression = nullptr;
        if (names((*condition).getLHS(), variable)) {
            limit_expression = (*condition).getRHS();
        } else if (names((*condition).getRHS(), variable)) {
            limit_expression = (*condition).getLHS();
            kind = clang::BinaryOperator::reverseComparisonOp(kind);
        }
        const std::optional<llvm::APSInt> limit =
            limit_expression != nullptr ? (*limit_expression).getIntegerConstantExpr(context_) : std::nullopt;
        if (!limit) {
            result.reason = "its condition is not a comparison of the loop variable with a constant";
            return result;
        }
        const bool variable_signed = (*(*variable).getType()).isSignedIntegerType();
        if (variable_signed && (*(*(*condition).getLHS()).getType()).isUnsignedIntegerType()) {
            result.reason = "its condition converts the signed loop variable to unsigned";
            return result;
        }
        std::optional<llvm::APSInt> step;
        bool upward = true;
        const clang::Expr* increment = (*loop).getInc();
        if (const clang::UnaryOperator* unary = llvm::dyn_cast_or_null<clang::UnaryOperator>(increment)) {
            if ((*unary).isIncrementDecrementOp() && names((*unary).getSubExpr(), variable)) {
                step = llvm::APSInt::get(1);
                upward = (*unary).isIncrementOp();
            }
        } else if (const clang::CompoundAssignOperator* compound =
                       llvm::dyn_cast_or_null<clang::CompoundAssignOperator>(increment)) {
            const clang::BinaryOperatorKind step_kind = (*compound).getOpcode();
            if ((step_kind == clang::BO_AddAssign || step_kind == clang::BO_SubAssign) &&
                names((*compound).getLHS(), variable)) {
                step = (*(*compound).getRHS()).getIntegerConstantExpr(context_);
                upward = step_kind == clang::BO_AddAssign;
            }
        }
        if (!step || (*step).isNegative() || (*step).isZero()) {
            result.reason = "its step is not ++, --, += or -= of a positive constant on the loop variable";
            return result;
        }
        if (writes((*loop).getBody(), variable)) {
            result.reason = "its loop variable '" + (*variable).getNameAsString() + "' is written in the body";
            return result;
        }
        // Wide enough for any 64-bit value and their differences.
        const unsigned width = 130U;
        const llvm::APInt first = (*start).extend(width);
        const llvm::APInt last = (*limit).extend(width);
        const llvm::APInt stride = (*step).extend(width);
        const unsigned variable_bits = static_cast<unsigned>(context_.getTypeSize((*variable).getType()));
        const llvm::APInt lowest = variable_signed ? llvm::APInt::getSignedMinValue(variable_bits).sext(width)
                                                   : llvm::APInt::getZero(width);
        const llvm::APInt highest = variable_signed ? llvm::APInt::getSignedMaxValue(variable_bits).sext(width)
                                                    : llvm::APInt::getMaxValue(variable_bits).zext(width);
        // The condition is normalised to the loop variable on the left.
        llvm::APInt passes = llvm::APInt::getZero(width);
        bool bounded = false;
        if (upward) {
            if (kind == clang::BO_LT || kind == clang::BO_LE || kind == clang::BO_NE) {
                // The last value run is the greatest first + k·stride that
                // satisfies the condition; the step past it must not wrap.
                llvm::APInt end = last;
                if (kind == clang::BO_LE) end = last + 1;
                if (end.sle(first)) {
                    bounded = true;
                } else {
                    const llvm::APInt span = end - first;
                    if (kind == clang::BO_NE && !span.srem(stride).isZero()) {
                        result.reason = "its condition '!=' is never false: the step does not reach the limit";
                        return result;
                    }
                    passes = (span + stride - 1).sdiv(stride);
                    // The value after the last pass must stay representable,
                    // or the loop variable wraps and the loop never ends.
                    const llvm::APInt past = first + passes * stride;
                    bounded = past.sle(highest);
                }
            } else {
                // i > c or i >= c counting up: runs zero times or forever.
                const bool initially = kind == clang::BO_GT ? first.sgt(last) : first.sge(last);
                bounded = !initially;
            }
        } else {
            if (kind == clang::BO_GT || kind == clang::BO_GE || kind == clang::BO_NE) {
                llvm::APInt end = last;
                if (kind == clang::BO_GE) end = last - 1;
                if (first.sle(end)) {
                    bounded = true;
                } else {
                    const llvm::APInt span = first - end;
                    if (kind == clang::BO_NE && !span.srem(stride).isZero()) {
                        result.reason = "its condition '!=' is never false: the step does not reach the limit";
                        return result;
                    }
                    passes = (span + stride - 1).sdiv(stride);
                    // The value after the last pass must stay representable,
                    // or the loop variable wraps and the loop never ends.
                    const llvm::APInt past = first - passes * stride;
                    bounded = past.sge(lowest);
                }
            } else {
                const bool initially = kind == clang::BO_LT ? first.slt(last) : first.sle(last);
                bounded = !initially;
            }
        }
        if (!bounded) {
            result.reason = "its loop variable never fails the condition (it counts away from the limit, or "
                            "wraps past it)";
            return result;
        }
        if (passes.getActiveBits() > 64U) {
            result.reason = "its bound does not fit 64 bits";
            return result;
        }
        result.passes = passes.getZExtValue();
        return result;
    }

    bool names(const clang::Expr* expression, const clang::VarDecl* variable) const {
        const clang::DeclRefExpr* reference =
            llvm::dyn_cast<clang::DeclRefExpr>((*expression).IgnoreParenImpCasts());
        return reference != nullptr && (*reference).getDecl() == variable;
    }

    // Whether `statement` writes `variable`: assigns or increments it, takes
    // its address, or binds it to a reference that may modify it.
    bool writes(const clang::Stmt* statement, const clang::VarDecl* variable) const {
        if (statement == nullptr) return false;
        if (const clang::BinaryOperator* binary = llvm::dyn_cast<clang::BinaryOperator>(statement)) {
            if ((*binary).isAssignmentOp() && names((*binary).getLHS(), variable)) return true;
        }
        if (const clang::UnaryOperator* unary = llvm::dyn_cast<clang::UnaryOperator>(statement)) {
            if (((*unary).isIncrementDecrementOp() || (*unary).getOpcode() == clang::UO_AddrOf) &&
                names((*unary).getSubExpr(), variable)) {
                return true;
            }
        }
        if (const clang::CallExpr* call = llvm::dyn_cast<clang::CallExpr>(statement)) {
            const clang::FunctionDecl* callee = (*call).getDirectCallee();
            unsigned first = 0U;
            if (llvm::isa<clang::CXXOperatorCallExpr>(call) && llvm::isa_and_nonnull<clang::CXXMethodDecl>(callee)) {
                first = 1U;
            }
            for (unsigned index = first; index < (*call).getNumArgs(); ++index) {
                if (!names((*call).getArg(index), variable)) continue;
                if (callee == nullptr || index - first >= (*callee).getNumParams()) return true;
                if (modifying_reference((*(*callee).getParamDecl(index - first)).getType())) return true;
            }
        }
        if (const clang::CXXConstructExpr* construct = llvm::dyn_cast<clang::CXXConstructExpr>(statement)) {
            const clang::CXXConstructorDecl* constructor = (*construct).getConstructor();
            for (unsigned index = 0U; index < (*construct).getNumArgs(); ++index) {
                if (!names((*construct).getArg(index), variable)) continue;
                if (constructor == nullptr || index >= (*constructor).getNumParams()) return true;
                if (modifying_reference((*(*constructor).getParamDecl(index)).getType())) return true;
            }
        }
        if (const clang::DeclStmt* declarations = llvm::dyn_cast<clang::DeclStmt>(statement)) {
            for (const clang::Decl* declaration : (*declarations).decls()) {
                const clang::VarDecl* local = llvm::dyn_cast<clang::VarDecl>(declaration);
                if (local == nullptr || (*local).getInit() == nullptr) continue;
                if (modifying_reference((*local).getType()) && names((*local).getInit(), variable)) return true;
            }
        }
        for (const clang::Stmt* child : (*statement).children()) {
            if (writes(child, variable)) return true;
        }
        return false;
    }

    static bool modifying_reference(clang::QualType type) {
        if (!(*type).isReferenceType()) return false;
        const bool constant = (*type).getPointeeType().isConstQualified();
        return !constant || (*type).isRValueReferenceType();
    }

    clang::ASTContext& context_;
    clang::DiagnosticsEngine& diagnostics_;
    unsigned error_;
    unsigned note_;
    std::string effect_name_;
    clang::SourceLocation effect_location_;
    std::map<const clang::FunctionDecl*, FunctionCost> costs_;
    std::vector<const clang::FunctionDecl*> running_;
    std::vector<FunctionCost> frames_;
    // Returned for a call the walk refuses (recursion, an invisible body).
    FunctionCost failed_recursion_;
};

// The fields of an EffectDeclaration the pass needs, read from the
// effect's `declaration` constant.
struct DeclaredEffect final {
    std::string name;
    std::uint64_t fixed{};
    std::uint64_t setup_cost{};
    std::uint64_t state_budget{};
};

const clang::FieldDecl* field_named(const clang::RecordDecl* record, llvm::StringRef name) {
    for (const clang::FieldDecl* field : (*record).fields()) {
        if ((*field).getName() == name) return field;
    }
    return nullptr;
}

std::optional<std::uint64_t> unsigned_field(const clang::APValue& value, const clang::RecordDecl* record,
                                            llvm::StringRef name) {
    const clang::FieldDecl* field = field_named(record, name);
    if (field == nullptr || !value.isStruct()) return std::nullopt;
    const clang::APValue& member = value.getStructField((*field).getFieldIndex());
    if (!member.isInt()) return std::nullopt;
    const std::uint64_t result = member.getInt().getZExtValue();
    return result;
}

// The text of a constant std::string_view: a pointer into a string literal
// and a size, whatever the library names its two fields.
std::optional<std::string> string_view_text(const clang::APValue& value, clang::QualType type) {
    const clang::CXXRecordDecl* record = record_of(type);
    if (record == nullptr || !value.isStruct()) return std::nullopt;
    const clang::StringLiteral* literal = nullptr;
    std::uint64_t offset = 0U;
    std::optional<std::uint64_t> size;
    for (const clang::FieldDecl* field : (*record).fields()) {
        const clang::APValue& member = value.getStructField((*field).getFieldIndex());
        if (member.isLValue()) {
            const clang::Expr* base = member.getLValueBase().dyn_cast<const clang::Expr*>();
            literal = llvm::dyn_cast_or_null<clang::StringLiteral>(base);
            offset = static_cast<std::uint64_t>(member.getLValueOffset().getQuantity());
        } else if (member.isInt()) {
            size = member.getInt().getZExtValue();
        }
    }
    if (literal == nullptr || !size) return std::nullopt;
    const llvm::StringRef bytes = (*literal).getBytes();
    if (offset > bytes.size() || *size > bytes.size() - offset) return std::nullopt;
    return bytes.substr(offset, *size).str();
}

std::optional<DeclaredEffect> read_declaration(const clang::CXXRecordDecl* effect, clang::ASTContext& context) {
    const clang::DeclContext::lookup_result found = (*effect).lookup(&context.Idents.get("declaration"));
    const clang::VarDecl* variable = nullptr;
    for (const clang::NamedDecl* candidate : found) {
        variable = llvm::dyn_cast<clang::VarDecl>(candidate);
        if (variable != nullptr) break;
    }
    if (variable == nullptr) return std::nullopt;
    const clang::APValue* value = (*variable).evaluateValue();
    const clang::RecordDecl* record = (*(*variable).getType().getCanonicalType()).getAsRecordDecl();
    if (value == nullptr || record == nullptr || !(*value).isStruct()) return std::nullopt;
    DeclaredEffect declared;
    const clang::FieldDecl* name = field_named(record, "name");
    const clang::FieldDecl* cost = field_named(record, "cost");
    if (name == nullptr || cost == nullptr) return std::nullopt;
    const std::optional<std::string> text =
        string_view_text((*value).getStructField((*name).getFieldIndex()), (*name).getType());
    const clang::RecordDecl* cost_record = (*(*cost).getType().getCanonicalType()).getAsRecordDecl();
    if (!text || cost_record == nullptr) return std::nullopt;
    const std::optional<std::uint64_t> fixed =
        unsigned_field((*value).getStructField((*cost).getFieldIndex()), cost_record, "fixed");
    const std::optional<std::uint64_t> setup_cost = unsigned_field(*value, record, "setup_cost");
    const std::optional<std::uint64_t> state_budget = unsigned_field(*value, record, "state_budget");
    if (!fixed || !setup_cost || !state_budget) return std::nullopt;
    declared.name = *text;
    declared.fixed = *fixed;
    declared.setup_cost = *setup_cost;
    declared.state_budget = *state_budget;
    return declared;
}

// The effect's method `name` taking `parameters` parameters, or null.
const clang::CXXMethodDecl* method_named(const clang::CXXRecordDecl* effect, llvm::StringRef name,
                                         unsigned parameters, clang::ASTContext& context) {
    const clang::DeclContext::lookup_result found = (*effect).lookup(&context.Idents.get(name));
    for (const clang::NamedDecl* candidate : found) {
        const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(candidate);
        if (method != nullptr && !(*method).isStatic() && (*method).getNumParams() == parameters) return method;
    }
    return nullptr;
}

// Pointer and reference members anywhere in the state's layout, through
// bases, nested records and arrays.
void find_borrowing_members(clang::QualType type, const std::string& path,
                            std::vector<std::pair<std::string, std::string>>& out, unsigned depth) {
    if (depth > 32U) return;
    const clang::QualType canonical = type.getCanonicalType();
    if ((*canonical).isPointerType() || (*canonical).isReferenceType() || (*canonical).isMemberPointerType() ||
        (*canonical).isBlockPointerType()) {
        out.emplace_back(path, type.getAsString());
        return;
    }
    if (const clang::ConstantArrayType* array = llvm::dyn_cast<clang::ConstantArrayType>(canonical.getTypePtr())) {
        find_borrowing_members((*array).getElementType(), path + "[]", out, depth + 1U);
        return;
    }
    const clang::CXXRecordDecl* record = (*canonical).getAsCXXRecordDecl();
    if (record == nullptr || !(*record).hasDefinition()) return;
    for (const clang::CXXBaseSpecifier& base : (*record).bases()) {
        find_borrowing_members(base.getType(), path, out, depth + 1U);
    }
    for (const clang::FieldDecl* field : (*record).fields()) {
        const std::string member = path.empty() ? (*field).getNameAsString() : path + "." + (*field).getNameAsString();
        find_borrowing_members((*field).getType(), member, out, depth + 1U);
    }
}

// One registered effect type: its class, its type as make_effect_entry
// names it, and where that instantiation was made.
struct RegisteredEffect final {
    const clang::CXXRecordDecl* record{};
    clang::QualType type;
    clang::SourceLocation location;
};

// Every effect type registered in the unit: each E of an instantiated
// gui_forms::make_effect_entry<E>.
std::vector<RegisteredEffect> registered_effects(clang::ASTContext& context) {
    std::vector<RegisteredEffect> effects;
    const clang::TranslationUnitDecl* unit = context.getTranslationUnitDecl();
    for (const clang::NamedDecl* space_candidate : (*unit).lookup(&context.Idents.get("gui_forms"))) {
        const clang::NamespaceDecl* space = llvm::dyn_cast<clang::NamespaceDecl>(space_candidate);
        if (space == nullptr) continue;
        for (const clang::NamedDecl* candidate : (*space).lookup(&context.Idents.get("make_effect_entry"))) {
            const clang::FunctionTemplateDecl* maker = llvm::dyn_cast<clang::FunctionTemplateDecl>(candidate);
            if (maker == nullptr) continue;
            for (const clang::FunctionDecl* specialization : (*maker).specializations()) {
                const clang::TemplateArgumentList* arguments = (*specialization).getTemplateSpecializationArgs();
                if (arguments == nullptr || (*arguments).size() < 1U) continue;
                const clang::TemplateArgument& argument = (*arguments).get(0U);
                if (argument.getKind() != clang::TemplateArgument::Type) continue;
                const clang::CXXRecordDecl* effect = record_of(argument.getAsType());
                if (effect == nullptr) continue;
                bool seen = false;
                for (const RegisteredEffect& known : effects) {
                    if (known.record == (*effect).getCanonicalDecl()) seen = true;
                }
                if (seen) continue;
                RegisteredEffect registered;
                registered.record = (*effect).getCanonicalDecl();
                registered.type = argument.getAsType().getCanonicalType();
                registered.location = (*specialization).getPointOfInstantiation();
                effects.push_back(registered);
            }
        }
    }
    return effects;
}

std::string decimal(std::uint64_t value) {
    const std::string text = std::to_string(value);
    return text;
}

} // namespace

std::vector<EffectRecord> verify_effects(clang::ASTContext& context, clang::DiagnosticsEngine& diagnostics) {
    std::vector<EffectRecord> records;
    EffectWalker walker(context, diagnostics);
    const std::vector<RegisteredEffect> effects = registered_effects(context);
    for (const RegisteredEffect& registered : effects) {
        const clang::CXXRecordDecl* effect = (*registered.record).getDefinition();
        const clang::SourceLocation where = registered.location;
        if (effect == nullptr) continue;
        const std::string type_name = (*effect).getQualifiedNameAsString();
        walker.begin_effect(type_name, (*effect).getLocation());
        const std::optional<DeclaredEffect> declared = read_declaration(effect, context);
        if (!declared) {
            walker.error(where, "effect '" + type_name + "': its declaration is not a constant the effect pass "
                                                         "can read");
            continue;
        }
        walker.begin_effect((*declared).name, (*effect).getLocation());
        bool passed = true;

        // State: trivially copyable, no borrows, within its budget.
        const std::uint64_t state_size = static_cast<std::uint64_t>(
            context.getTypeSizeInChars(registered.type).getQuantity());
        if (!(*effect).isTriviallyCopyable()) {
            walker.error((*effect).getLocation(), "effect '" + (*declared).name +
                                                      "': its state is not trivially copyable");
            passed = false;
        }
        std::vector<std::pair<std::string, std::string>> borrows;
        find_borrowing_members(registered.type, std::string(), borrows, 0U);
        for (const std::pair<std::string, std::string>& borrow : borrows) {
            walker.error((*effect).getLocation(), "effect '" + (*declared).name + "': its state member '" +
                                                      borrow.first + "' is a pointer or reference ('" +
                                                      borrow.second + "')");
            passed = false;
        }
        if (state_size > (*declared).state_budget) {
            walker.error((*effect).getLocation(), "effect '" + (*declared).name + "': its state is " +
                                                      decimal(state_size) + " bytes, over its state budget of " +
                                                      decimal((*declared).state_budget));
            passed = false;
        }

        // run and setup: the rules, then the bound against the budget.
        const clang::CXXMethodDecl* run = method_named(effect, "run", 3U, context);
        const clang::CXXMethodDecl* setup = method_named(effect, "setup", 2U, context);
        if (run == nullptr) {
            walker.error((*effect).getLocation(), "effect '" + (*declared).name + "' has no run(view, frame, canvas)");
            continue;
        }
        std::array<std::uint64_t, 2> bounds{0U, 0U};
        const std::array<const clang::CXXMethodDecl*, 2> entries{run, setup};
        const std::array<std::uint64_t, 2> budgets{(*declared).fixed, (*declared).setup_cost};
        const std::array<const char*, 2> labels{"run", "setup"};
        const std::array<const char*, 2> budget_names{"fixed budget (cost.fixed)", "setup budget (setup_cost)"};
        for (std::size_t index = 0U; index < entries.size(); ++index) {
            const clang::CXXMethodDecl* entry = entries[index];
            if (entry == nullptr) continue;
            const FunctionCost result = walker.entry_cost(entry);
            if (!result.obeys) {
                passed = false;
                continue;
            }
            bounds[index] = result.operations;
            if (result.operations <= budgets[index]) continue;
            walker.error((*entry).getLocation(),
                         "effect '" + (*declared).name + "': the static bound of " + labels[index] + " is " +
                             decimal(result.operations) + " operations, over its declared " + budget_names[index] +
                             " of " + decimal(budgets[index]));
            std::set<const clang::FunctionDecl*> seen;
            std::vector<LoopCost> loops;
            walker.collect_loops(entry, seen, loops);
            for (const LoopCost& loop : loops) {
                walker.note(loop.location, "this loop: " + decimal(loop.passes) + " passes × " +
                                               decimal(loop.per_pass) + " operations a pass = " +
                                               decimal(saturating_multiply(loop.passes, loop.per_pass)));
            }
            passed = false;
        }
        if (!passed) continue;
        EffectRecord record;
        record.name = (*declared).name;
        record.bound = static_cast<std::uint32_t>(bounds[0]);
        record.setup_bound = static_cast<std::uint32_t>(bounds[1]);
        record.state_size = static_cast<std::uint32_t>(state_size);
        records.push_back(record);
    }
    return records;
}

} // namespace gui_forms_plugin
