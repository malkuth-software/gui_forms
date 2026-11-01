// GUI.Forms localizable-literal plugin for Clang 22.
//
// Loaded with -fplugin during the normal compile, it finds every string
// literal that reaches a parameter or field annotated
// GUI_FORMS_LOCALIZABLE(property) (gui_forms/text/localizable/localizable_property.hpp)
// and writes one table per translation unit into a dedicated section of the
// object file: the property, a content-derived variant name, the call site
// (file, line, column, enclosing function) and the literal's bytes. It rewrites
// nothing; the source and the generated code are unchanged apart from the
// table. The runtime reads the tables back with gui_forms::localizable_literals().
//
// Table format (version 1), one ELF-note-shaped blob per translation unit,
// every integer a little-endian uint32:
//   namesz = 9, descsz, type = 0x47464c01, name "GUIForms\0" padded to 12;
//   desc: record count, then per record
//     property, name, file (each: length, bytes), line, column,
//     function (length, bytes), literal (length, bytes);
//   the blob is zero-padded to a multiple of 4.
// Section: Mach-O __DATA,__gui_forms_loc; ELF .note.gui_forms.loc (an
// SHT_NOTE section, so linkers place it in a PT_NOTE segment); COFF
// .gfloc$m.
//
// The plugin is named gui_forms_localizable (the driver splits
// -fplugin-arg-<name>-<argument> at the first dash, so the name has none).
// Arguments (-fplugin-arg-gui_forms_localizable-<argument>):
//   root=<directory>  record file names relative to this directory.

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

constexpr llvm::StringLiteral annotation_prefix = "gui_forms.localizable:";
constexpr std::uint32_t note_type = 0x47464c01U;
constexpr unsigned maximum_constant_depth = 8U;

struct Record final {
    std::string property;
    std::string name;
    std::string file;
    std::uint32_t line{};
    std::uint32_t column{};
    std::string function;
    std::string literal;
};

// The property a declaration's GUI_FORMS_LOCALIZABLE annotation names, or
// empty when it has none.
std::string annotated_property(const clang::Decl* declaration) {
    if (declaration == nullptr) return {};
    for (const clang::AnnotateAttr* attribute : (*declaration).specific_attrs<clang::AnnotateAttr>()) {
        const llvm::StringRef annotation = (*attribute).getAnnotation();
        if (annotation.starts_with(annotation_prefix)) {
            return annotation.drop_front(annotation_prefix.size()).str();
        }
    }
    return {};
}

// The property of parameter `index` of `function`, looking through every
// redeclaration and every method it overrides, so an override or an
// out-of-line definition keeps the annotation of the declaration in the
// header.
std::string parameter_property(const clang::FunctionDecl* function, unsigned index, unsigned depth = 0U) {
    if (function == nullptr || depth > 16U) return {};
    for (const clang::FunctionDecl* declaration : (*function).redecls()) {
        if (index >= (*declaration).getNumParams()) continue;
        std::string property = annotated_property((*declaration).getParamDecl(index));
        if (!property.empty()) return property;
    }
    if (const clang::FunctionDecl* pattern = (*function).getTemplateInstantiationPattern()) {
        if (pattern != function) {
            std::string property = parameter_property(pattern, index, depth + 1U);
            if (!property.empty()) return property;
        }
    }
    if (const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(function)) {
        for (const clang::CXXMethodDecl* overridden : (*method).overridden_methods()) {
            std::string property = parameter_property(overridden, index, depth + 1U);
            if (!property.empty()) return property;
        }
    }
    return {};
}

// A 64-bit FNV-1a hash of the literal, the variant name's content part.
std::uint64_t fnv1a(llvm::StringRef bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::string variant_name(llvm::StringRef literal) {
    static constexpr char digits[] = "0123456789abcdef";
    const std::uint64_t hash = fnv1a(literal);
    std::string name = "v-";
    for (int shift = 44; shift >= 0; shift -= 4) {
        name.push_back(digits[(hash >> static_cast<unsigned>(shift)) & 0xfU]);
    }
    return name;
}

class Finder final : public clang::RecursiveASTVisitor<Finder> {
public:
    Finder(clang::ASTContext& context, const std::string& root) : context_(context), root_(normalized(root)) {
        if (!root_.empty()) {
            llvm::SmallString<256> real;
            if (!llvm::sys::fs::real_path(root_, real)) real_root_ = real.str().str();
        }
    }

    // Templates are searched in their instantiations, where every call is
    // resolved; a literal is recorded once however many instantiations
    // repeat it.
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitLambdaBodies() const { return true; }

    bool VisitCallExpr(clang::CallExpr* call) {
        const clang::FunctionDecl* callee = (*call).getDirectCallee();
        if (callee == nullptr) return true;
        unsigned first_argument = 0U;
        // A member operator call passes the object as argument 0.
        if (llvm::isa<clang::CXXOperatorCallExpr>(call) && llvm::isa<clang::CXXMethodDecl>(callee)) {
            first_argument = 1U;
            assignment(call);
        }
        for (unsigned index = first_argument; index < (*call).getNumArgs(); ++index) {
            const std::string property = parameter_property(callee, index - first_argument);
            if (!property.empty()) collect((*call).getArg(index), property, call, 0U);
        }
        return true;
    }

    bool VisitCXXConstructExpr(clang::CXXConstructExpr* construct) {
        const clang::CXXConstructorDecl* constructor = (*construct).getConstructor();
        for (unsigned index = 0U; index < (*construct).getNumArgs(); ++index) {
            const std::string property = parameter_property(constructor, index);
            if (!property.empty()) collect((*construct).getArg(index), property, construct, 0U);
        }
        return true;
    }

    // A generic lambda's body is searched in each of its instantiations,
    // which the traversal of the lambda expression does not reach.
    bool VisitLambdaExpr(clang::LambdaExpr* lambda) {
        if (clang::FunctionTemplateDecl* call = (*lambda).getDependentCallOperator()) {
            for (clang::FunctionDecl* specialization : (*call).specializations()) TraverseDecl(specialization);
        }
        return true;
    }

    // Aggregate initialisation, designated or positional, of a struct whose
    // fields are annotated. The traversal visits the list as written; its
    // semantic form has one initializer per field.
    bool VisitInitListExpr(clang::InitListExpr* written) {
        const clang::InitListExpr* list = (*written).isSemanticForm() ? written : (*written).getSemanticForm();
        if (list == nullptr) return true;
        const clang::RecordDecl* record = (*(*list).getType()).getAsRecordDecl();
        if (record == nullptr || (*(*list).getType()).isArrayType()) return true;
        // A C++17 aggregate initialises its bases before its fields.
        const clang::CXXRecordDecl* class_record = llvm::dyn_cast<clang::CXXRecordDecl>(record);
        unsigned index = class_record != nullptr ? (*class_record).getNumBases() : 0U;
        for (const clang::FieldDecl* field : (*record).fields()) {
            if (index >= (*list).getNumInits()) break;
            const std::string property = annotated_property(field);
            if (!property.empty()) collect((*list).getInit(index), property, list, 0U);
            ++index;
        }
        return true;
    }

    // A built-in assignment to an annotated field (a field of a type
    // without an assignment operator call, such as const char*).
    bool VisitBinaryOperator(clang::BinaryOperator* binary) {
        if ((*binary).getOpcode() != clang::BO_Assign) return true;
        const clang::MemberExpr* member = llvm::dyn_cast<clang::MemberExpr>((*(*binary).getLHS()).IgnoreParenImpCasts());
        if (member == nullptr) return true;
        const std::string property = annotated_property((*member).getMemberDecl());
        if (!property.empty()) collect((*binary).getRHS(), property, binary, 0U);
        return true;
    }

    const std::vector<Record>& records() const noexcept { return records_; }

private:
    // `spec.text = "Open"` for a std::string field: operator= on a member.
    void assignment(clang::CallExpr* call) {
        const clang::CXXOperatorCallExpr* operator_call = llvm::cast<clang::CXXOperatorCallExpr>(call);
        if ((*operator_call).getOperator() != clang::OO_Equal || (*call).getNumArgs() != 2U) return;
        const clang::MemberExpr* member = llvm::dyn_cast<clang::MemberExpr>((*(*call).getArg(0)).IgnoreParenImpCasts());
        if (member == nullptr) return;
        const std::string property = annotated_property((*member).getMemberDecl());
        if (!property.empty()) collect((*call).getArg(1), property, call, 0U);
    }

    // Records every string literal that reaches `expression` without a
    // computation: through conversions, temporaries, string and string_view
    // construction, initializer lists, both arms of a conditional, and
    // constants initialised by such an expression. A call result, a
    // parameter, or a string built at run time ends the search.
    void collect(const clang::Expr* expression, const std::string& property, const clang::Stmt* site,
                 unsigned depth) {
        if (expression == nullptr || depth > maximum_constant_depth) return;
        expression = (*expression).IgnoreParenImpCasts();
        if (const clang::StringLiteral* literal = llvm::dyn_cast<clang::StringLiteral>(expression)) {
            add(literal, property, site);
            return;
        }
        if (const clang::ExprWithCleanups* cleanups = llvm::dyn_cast<clang::ExprWithCleanups>(expression)) {
            collect((*cleanups).getSubExpr(), property, site, depth);
            return;
        }
        if (const clang::MaterializeTemporaryExpr* temporary =
                llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expression)) {
            collect((*temporary).getSubExpr(), property, site, depth);
            return;
        }
        if (const clang::CXXBindTemporaryExpr* bound = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expression)) {
            collect((*bound).getSubExpr(), property, site, depth);
            return;
        }
        if (const clang::CastExpr* cast = llvm::dyn_cast<clang::CastExpr>(expression)) {
            collect((*cast).getSubExpr(), property, site, depth);
            return;
        }
        if (const clang::AbstractConditionalOperator* conditional =
                llvm::dyn_cast<clang::AbstractConditionalOperator>(expression)) {
            collect((*conditional).getTrueExpr(), property, site, depth);
            collect((*conditional).getFalseExpr(), property, site, depth);
            return;
        }
        if (const clang::UserDefinedLiteral* user = llvm::dyn_cast<clang::UserDefinedLiteral>(expression)) {
            if ((*user).getLiteralOperatorKind() == clang::UserDefinedLiteral::LOK_String && (*user).getNumArgs() > 0U) {
                collect((*user).getArg(0), property, site, depth);
            }
            return;
        }
        if (const clang::CXXConstructExpr* construct = llvm::dyn_cast<clang::CXXConstructExpr>(expression)) {
            // std::string("…"), std::string_view{"…"}, a copy of a constant,
            // std::vector<std::string>{…}: the first argument carries the text.
            if ((*construct).getNumArgs() > 0U) collect((*construct).getArg(0), property, site, depth);
            return;
        }
        if (const clang::CXXStdInitializerListExpr* initializer =
                llvm::dyn_cast<clang::CXXStdInitializerListExpr>(expression)) {
            collect((*initializer).getSubExpr(), property, site, depth);
            return;
        }
        if (const clang::InitListExpr* list = llvm::dyn_cast<clang::InitListExpr>(expression)) {
            for (const clang::Expr* element : (*list).inits()) collect(element, property, site, depth);
            return;
        }
        const clang::ValueDecl* referenced = nullptr;
        if (const clang::DeclRefExpr* reference = llvm::dyn_cast<clang::DeclRefExpr>(expression)) {
            referenced = (*reference).getDecl();
        } else if (const clang::MemberExpr* member = llvm::dyn_cast<clang::MemberExpr>(expression)) {
            referenced = (*member).getMemberDecl();
        }
        // A named constant: a const or constexpr variable (or static data
        // member) whose initializer is itself a literal.
        // (`constexpr const char* name = "…"` is a const pointer: constexpr
        // makes the variable itself const.)
        const clang::VarDecl* variable = llvm::dyn_cast_or_null<clang::VarDecl>(referenced);
        if (variable != nullptr && !llvm::isa<clang::ParmVarDecl>(variable) &&
            ((*variable).isConstexpr() || (*variable).getType().isConstQualified())) {
            collect((*variable).getAnyInitializer(), property, site, depth + 1U);
        }
    }

    void add(const clang::StringLiteral* literal, const std::string& property, const clang::Stmt* site) {
        if (!(*literal).isOrdinary() && !(*literal).isUTF8()) return;
        const clang::SourceManager& sources = context_.getSourceManager();
        const clang::SourceLocation location = sources.getExpansionLoc((*literal).getBeginLoc());
        const clang::PresumedLoc presumed = sources.getPresumedLoc(location);
        if (presumed.isInvalid()) return;
        const llvm::StringRef bytes = (*literal).getBytes();
        std::string function = enclosing_function(site);
        const Key key{presumed.getFilename(), presumed.getLine(), presumed.getColumn(), property, function};
        if (!seen_.insert(key).second) return;
        Record record;
        record.property = property;
        record.name = variant_name(bytes);
        record.file = relative(presumed.getFilename());
        record.line = presumed.getLine();
        record.column = presumed.getColumn();
        record.function = std::move(function);
        record.literal = bytes.str();
        records_.push_back(std::move(record));
    }

    // An absolute path without "." and ".." components, with forward
    // slashes; a relative path is taken from the compiler's working
    // directory.
    std::string normalized(llvm::StringRef file) const {
        if (file.empty()) return {};
        llvm::SmallString<256> path(file);
        context_.getSourceManager().getFileManager().makeAbsolutePath(path);
        llvm::sys::path::remove_dots(path, true);
        return llvm::sys::path::convert_to_slash(path);
    }

    static bool strip_prefix(const std::string& path, const std::string& root, std::string& rest) {
        if (root.empty() || path.size() <= root.size() || path.compare(0U, root.size(), root) != 0) return false;
        if (path[root.size()] != '/' && root.back() != '/') return false;
        rest = path.substr(root.size());
        while (!rest.empty() && rest.front() == '/') rest.erase(0U, 1U);
        return true;
    }

    // The file relative to the root argument (comparing real paths when the
    // spellings differ), or its normalized absolute path outside the root.
    std::string relative(llvm::StringRef file) const {
        const std::string path = normalized(file);
        std::string rest;
        if (strip_prefix(path, root_, rest)) return rest;
        llvm::SmallString<256> real;
        if (!real_root_.empty() && !llvm::sys::fs::real_path(path, real) &&
            strip_prefix(llvm::sys::path::convert_to_slash(real), real_root_, rest)) {
            return rest;
        }
        return path;
    }

    // The qualified name of the function the call site is in, named after
    // its template (so every instantiation names the same function); a
    // lambda's body is named after the function that holds the lambda.
    std::string enclosing_function(const clang::Stmt* site) {
        clang::DynTypedNodeList parents = context_.getParents(*site);
        unsigned steps = 0U;
        bool in_lambda = false;
        while (!parents.empty() && steps < 4096U) {
            const clang::DynTypedNode& parent = parents[0];
            if (const clang::FunctionDecl* function = parent.get<clang::FunctionDecl>()) {
                const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
                if (method != nullptr && (*(*method).getParent()).isLambda()) {
                    in_lambda = true;
                } else {
                    const clang::FunctionDecl* pattern = (*function).getTemplateInstantiationPattern();
                    std::string name = (*(pattern != nullptr ? pattern : function)).getQualifiedNameAsString();
                    if (in_lambda) name += "::<lambda>";
                    return name;
                }
            }
            if (const clang::LambdaExpr* lambda = parent.get<clang::LambdaExpr>()) {
                static_cast<void>(lambda);
                in_lambda = true;
            }
            parents = context_.getParents(parent);
            ++steps;
        }
        return in_lambda ? std::string("<lambda>") : std::string("<global>");
    }

    clang::ASTContext& context_;
    std::string root_;
    std::string real_root_;
    std::vector<Record> records_;
    // A literal is recorded once per location, property and function, so
    // the instantiations of a template add nothing, and a named constant
    // used in several functions is listed under each.
    using Key = std::tuple<std::string, unsigned, unsigned, std::string, std::string>;
    std::set<Key> seen_;
};

void put_u32(std::string& out, std::uint32_t value) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) out.push_back(static_cast<char>((value >> shift) & 0xffU));
}

void put_text(std::string& out, const std::string& text) {
    put_u32(out, static_cast<std::uint32_t>(text.size()));
    out.append(text);
}

std::string encode(const std::vector<Record>& records) {
    std::string description;
    put_u32(description, static_cast<std::uint32_t>(records.size()));
    for (const Record& record : records) {
        put_text(description, record.property);
        put_text(description, record.name);
        put_text(description, record.file);
        put_u32(description, record.line);
        put_u32(description, record.column);
        put_text(description, record.function);
        put_text(description, record.literal);
    }
    std::string blob;
    put_u32(blob, 9U);
    put_u32(blob, static_cast<std::uint32_t>(description.size()));
    put_u32(blob, note_type);
    blob.append("GUIForms", 8U);
    blob.append(4U, '\0');
    blob.append(description);
    while (blob.size() % 4U != 0U) blob.push_back('\0');
    return blob;
}

std::string section_name(const clang::ASTContext& context) {
    const llvm::Triple& triple = context.getTargetInfo().getTriple();
    if (triple.isOSBinFormatMachO()) return "__DATA,__gui_forms_loc";
    if (triple.isOSBinFormatCOFF()) return ".gfloc$m";
    return ".note.gui_forms.loc";
}

class Consumer final : public clang::ASTConsumer {
public:
    Consumer(clang::CompilerInstance& instance, std::string root) : instance_(instance), root_(std::move(root)) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        if (instance_.getDiagnostics().hasErrorOccurred()) return;
        Finder finder(context, root_);
        finder.TraverseDecl(context.getTranslationUnitDecl());
        if (finder.records().empty()) return;
        const std::string blob = encode(finder.records());
        // The table is an internal constant array the code generator emits
        // like any other: `static const char table[] = "…"` with section,
        // used and retain attributes, handed to it as a top-level declaration
        // before it finishes the translation unit.
        const clang::QualType element = context.CharTy.withConst();
        const clang::QualType array = context.getConstantArrayType(
            element, llvm::APInt(64, blob.size()), nullptr, clang::ArraySizeModifier::Normal, 0U);
        clang::StringLiteral* initializer = clang::StringLiteral::Create(
            context, blob, clang::StringLiteralKind::Ordinary, false, array, clang::SourceLocation());
        clang::TranslationUnitDecl* unit = context.getTranslationUnitDecl();
        clang::VarDecl* table = clang::VarDecl::Create(
            context, unit, clang::SourceLocation(), clang::SourceLocation(),
            &context.Idents.get("__gui_forms_localizable_table"), array, context.getTrivialTypeSourceInfo(array),
            clang::SC_Static);
        (*table).setInit(initializer);
        (*table).addAttr(clang::SectionAttr::CreateImplicit(context, section_name(context)));
        // Notes are 4-byte aligned, so the linker's concatenation of every
        // translation unit's table stays a well-formed note sequence.
        clang::Expr* alignment = clang::IntegerLiteral::Create(
            context, llvm::APInt(context.getIntWidth(context.IntTy), 4U), context.IntTy, clang::SourceLocation());
        (*table).addAttr(clang::AlignedAttr::CreateImplicit(context, true, alignment));
        // Not instrumented by AddressSanitizer: it would put a poisoned
        // redzone after each table inside the section, which the reader then
        // walks as part of the note sequence (a global-buffer-overflow in a
        // sanitized program, and a malformed sequence when the redzone bytes
        // are not zero).
        std::array<llvm::StringRef, 2U> unsanitized{llvm::StringRef("address"), llvm::StringRef("hwaddress")};
        (*table).addAttr(clang::NoSanitizeAttr::CreateImplicit(
            context, unsanitized.data(), static_cast<unsigned>(unsanitized.size())));
        (*table).addAttr(clang::UsedAttr::CreateImplicit(context));
        (*table).addAttr(clang::RetainAttr::CreateImplicit(context));
        (*unit).addDecl(table);
        instance_.getASTConsumer().HandleTopLevelDecl(clang::DeclGroupRef(table));
    }

private:
    clang::CompilerInstance& instance_;
    std::string root_;
};

class Action final : public clang::PluginASTAction {
protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& instance,
                                                          llvm::StringRef) override {
        return std::make_unique<Consumer>(instance, root_);
    }

    bool ParseArgs(const clang::CompilerInstance& instance, const std::vector<std::string>& arguments) override {
        for (const std::string& argument : arguments) {
            const llvm::StringRef text(argument);
            if (text.starts_with("root=")) {
                root_ = text.drop_front(5).str();
                continue;
            }
            clang::DiagnosticsEngine& diagnostics = instance.getDiagnostics();
            const unsigned id = diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error,
                                                            "unknown gui_forms_localizable argument '%0'");
            diagnostics.Report(id) << argument;
            return false;
        }
        return true;
    }

    ActionType getActionType() override { return AddBeforeMainAction; }

private:
    std::string root_;
};

} // namespace

static clang::FrontendPluginRegistry::Add<Action> registration(
    "gui_forms_localizable", "record string literals passed to GUI.Forms localizable properties");
