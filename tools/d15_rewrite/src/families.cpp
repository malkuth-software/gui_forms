#include "families.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/TemplateBase.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

namespace d15 {

namespace {

// The standard class template specialisation `type` is (references and
// cv-qualifiers looked through), or null.
[[nodiscard]] const clang::ClassTemplateSpecializationDecl* standard_specialization(clang::QualType type) {
    if (type.isNull()) return nullptr;
    const clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    const clang::CXXRecordDecl* const record = (*canonical).getAsCXXRecordDecl();
    const clang::ClassTemplateSpecializationDecl* const specialization =
        llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(record);
    if (specialization == nullptr) return nullptr;
    if (!(*specialization).isInStdNamespace()) return nullptr;
    return specialization;
}

// The record `type` is when it lives in namespace stxlib (any depth), or null.
[[nodiscard]] const clang::CXXRecordDecl* owned_record(clang::QualType type) {
    if (type.isNull()) return nullptr;
    const clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    const clang::CXXRecordDecl* const record = (*canonical).getAsCXXRecordDecl();
    if (record == nullptr) return nullptr;
    const std::string qualified = (*record).getQualifiedNameAsString();
    if (llvm::StringRef(qualified).starts_with("stxlib::")) return record;
    return nullptr;
}

[[nodiscard]] bool argument_is_char(const clang::TemplateArgument& argument) {
    if (argument.getKind() != clang::TemplateArgument::Type) return false;
    const clang::QualType element = argument.getAsType().getCanonicalType();
    const bool result = (*element).isCharType();
    return result;
}

[[nodiscard]] bool argument_is_standard_record(const clang::TemplateArgument& argument, llvm::StringRef name) {
    if (argument.getKind() != clang::TemplateArgument::Type) return false;
    const clang::CXXRecordDecl* const record = (*argument.getAsType().getCanonicalType()).getAsCXXRecordDecl();
    if (record == nullptr) return false;
    const bool result = (*record).isInStdNamespace() && (*record).getName() == name;
    return result;
}

// The first template argument as written (with sugar) when `type` was
// spelled as a template specialisation, else the canonical one.
[[nodiscard]] clang::QualType first_argument(clang::QualType type,
                                             const clang::ClassTemplateSpecializationDecl& specialization) {
    const clang::QualType plain = type.getNonReferenceType();
    const clang::TemplateSpecializationType* const written =
        (*plain).getAs<clang::TemplateSpecializationType>();
    if (written != nullptr) {
        const llvm::ArrayRef<clang::TemplateArgument> arguments = (*written).template_arguments();
        if (!arguments.empty() && arguments[0].getKind() == clang::TemplateArgument::Type) {
            const clang::QualType result = arguments[0].getAsType();
            return result;
        }
    }
    const clang::TemplateArgumentList& canonical_arguments = specialization.getTemplateArgs();
    if (canonical_arguments.size() == 0U) return clang::QualType();
    if (canonical_arguments[0].getKind() != clang::TemplateArgument::Type) return clang::QualType();
    const clang::QualType result = canonical_arguments[0].getAsType();
    return result;
}

[[nodiscard]] bool span_extent_is_dynamic(const clang::ClassTemplateSpecializationDecl& specialization) {
    const clang::TemplateArgumentList& arguments = specialization.getTemplateArgs();
    if (arguments.size() < 2U) return true;
    if (arguments[1].getKind() != clang::TemplateArgument::Integral) return false;
    const llvm::APSInt extent = arguments[1].getAsIntegral();
    const bool result = extent.isMaxValue();
    return result;
}

// The family of a standard specialisation for a stage, before the entity rules.
[[nodiscard]] Family specialization_family(const clang::ClassTemplateSpecializationDecl& specialization,
                                           Stage stage, bool element_const) {
    const llvm::StringRef name = specialization.getName();
    const clang::TemplateArgumentList& arguments = specialization.getTemplateArgs();
    switch (stage) {
    case Stage::v1_optional:
        if (name == "optional") return Family::optional;
        return Family::none;
    case Stage::v2_text_view:
        if (name == "basic_string_view" && arguments.size() > 0U && argument_is_char(arguments[0])) {
            return Family::text_view;
        }
        if (name == "basic_string" && arguments.size() > 0U && argument_is_char(arguments[0])) {
            return Family::text_reference;
        }
        return Family::none;
    case Stage::v3_owned_text:
        if (name == "basic_string" && arguments.size() > 0U && argument_is_char(arguments[0])) {
            return Family::owned_text;
        }
        return Family::none;
    case Stage::v4_sequences:
        if (name == "vector") return Family::vector;
        if (name == "span") return element_const ? Family::view : Family::mutable_view;
        return Family::none;
    case Stage::v5_ownership:
        if (name == "shared_ptr") return Family::shared;
        if (name == "weak_ptr") return Family::weak;
        if (name == "unique_ptr") return Family::unique;
        if (name == "enable_shared_from_this") return Family::shared_from_this;
        return Family::none;
    default:
        break;
    }
    return Family::none;
}

[[nodiscard]] bool span_element_const(const clang::ClassTemplateSpecializationDecl& specialization) {
    const clang::TemplateArgumentList& arguments = specialization.getTemplateArgs();
    if (arguments.size() == 0U || arguments[0].getKind() != clang::TemplateArgument::Type) return false;
    const bool result = arguments[0].getAsType().isConstQualified();
    return result;
}

} // namespace

const char* stage_name(const Stage stage) noexcept {
    switch (stage) {
    case Stage::v1_optional: return "V1";
    case Stage::v2_text_view: return "V2";
    case Stage::v3_owned_text: return "V3";
    case Stage::v4_sequences: return "V4";
    case Stage::v5_ownership: return "V5";
    default: break;
    }
    return "V?";
}

const char* family_name(const Family family) noexcept {
    switch (family) {
    case Family::none: return "none";
    case Family::optional: return "optional";
    case Family::text_view: return "text_view";
    case Family::text_reference: return "text_reference";
    case Family::owned_text: return "owned_text";
    case Family::vector: return "vector";
    case Family::view: return "view";
    case Family::mutable_view: return "mutable_view";
    case Family::shared: return "shared";
    case Family::weak: return "weak";
    case Family::unique: return "unique";
    case Family::shared_from_this: return "shared_from_this";
    default: break;
    }
    return "unknown";
}

const char* entity_kind_name(const EntityKind kind) noexcept {
    switch (kind) {
    case EntityKind::field: return "field";
    case EntityKind::variable: return "variable";
    case EntityKind::local: return "local";
    case EntityKind::parameter: return "parameter";
    case EntityKind::function_return: return "return";
    case EntityKind::alias: return "alias";
    case EntityKind::shared_base: return "shared_base";
    default: break;
    }
    return "unknown";
}

Stage family_stage(const Family family) noexcept {
    switch (family) {
    case Family::optional: return Stage::v1_optional;
    case Family::text_view: return Stage::v2_text_view;
    case Family::text_reference: return Stage::v2_text_view;
    case Family::owned_text: return Stage::v3_owned_text;
    case Family::vector: return Stage::v4_sequences;
    case Family::view: return Stage::v4_sequences;
    case Family::mutable_view: return Stage::v4_sequences;
    case Family::shared: return Stage::v5_ownership;
    case Family::weak: return Stage::v5_ownership;
    case Family::unique: return Stage::v5_ownership;
    case Family::shared_from_this: return Stage::v5_ownership;
    default: break;
    }
    return Stage::v1_optional;
}

const char* owned_name(const Family family) noexcept {
    switch (family) {
    case Family::optional: return "stxlib::Optional";
    case Family::text_view: return "stxlib::TextView";
    case Family::text_reference: return "stxlib::TextView";
    case Family::owned_text: return "stxlib::Utf8String";
    case Family::vector: return "stxlib::Vector";
    case Family::view: return "stxlib::View";
    case Family::mutable_view: return "stxlib::MutableView";
    case Family::shared: return "stxlib::Shared";
    case Family::weak: return "stxlib::Weak";
    case Family::unique: return "stxlib::Unique";
    case Family::shared_from_this: return "stxlib::SharedFromThis";
    default: break;
    }
    return "";
}

std::uint32_t family_includes(const Family family) noexcept {
    switch (family) {
    case Family::optional: return include_optional;
    case Family::text_view: return include_text_view;
    case Family::text_reference: return include_text_view;
    case Family::owned_text: return include_text;
    case Family::vector: return include_vector;
    case Family::view: return include_view;
    case Family::mutable_view: return include_view;
    case Family::shared: return include_shared;
    case Family::weak: return include_shared;
    case Family::unique: return include_unique;
    case Family::shared_from_this: return include_shared;
    default: break;
    }
    return include_none;
}

bool is_standard_entity(const clang::NamedDecl* const declaration, const llvm::StringRef name) {
    if (declaration == nullptr) return false;
    if (!(*declaration).isInStdNamespace()) return false;
    const clang::DeclarationName declared = (*declaration).getDeclName();
    if (!declared.isIdentifier()) return false;
    const bool result = (*declaration).getName() == name;
    return result;
}

bool is_nullopt_type(const clang::QualType type) {
    if (type.isNull()) return false;
    const clang::CXXRecordDecl* const record =
        (*type.getNonReferenceType().getCanonicalType()).getAsCXXRecordDecl();
    if (record == nullptr) return false;
    const bool result = (*record).isInStdNamespace() && (*record).getName() == "nullopt_t";
    return result;
}

bool is_nullptr_type(const clang::QualType type) {
    if (type.isNull()) return false;
    const bool result = (*type.getNonReferenceType().getCanonicalType()).isNullPtrType();
    return result;
}

bool is_standard_string(const clang::QualType type) {
    const clang::ClassTemplateSpecializationDecl* const specialization = standard_specialization(type);
    if (specialization == nullptr) return false;
    const clang::TemplateArgumentList& arguments = (*specialization).getTemplateArgs();
    const bool result = (*specialization).getName() == "basic_string" && arguments.size() > 0U &&
        argument_is_char(arguments[0]);
    return result;
}

bool is_standard_string_view(const clang::QualType type) {
    const clang::ClassTemplateSpecializationDecl* const specialization = standard_specialization(type);
    if (specialization == nullptr) return false;
    const clang::TemplateArgumentList& arguments = (*specialization).getTemplateArgs();
    const bool result = (*specialization).getName() == "basic_string_view" && arguments.size() > 0U &&
        argument_is_char(arguments[0]);
    return result;
}

bool is_text_view(const clang::QualType type) {
    const clang::CXXRecordDecl* const record = owned_record(type);
    if (record == nullptr) return false;
    const bool result = (*record).getName() == "TextView";
    return result;
}

bool is_owned_counterpart(const clang::QualType type, const Family family) {
    const clang::CXXRecordDecl* const record = owned_record(type);
    if (record == nullptr) return false;
    const llvm::StringRef name = (*record).getName();
    switch (family) {
    case Family::optional: return name == "Optional";
    case Family::text_view: return name == "TextView";
    case Family::text_reference: return name == "TextView";
    case Family::owned_text: return name == "Utf8String";
    case Family::vector: return name == "Vector";
    case Family::view: return name == "View";
    case Family::mutable_view: return name == "MutableView";
    case Family::shared: return name == "Shared";
    case Family::weak: return name == "Weak";
    case Family::unique: return name == "Unique";
    case Family::shared_from_this: return name == "SharedFromThis";
    default: break;
    }
    return false;
}

Family expression_family(const clang::QualType type, const Stage stage) {
    const clang::ClassTemplateSpecializationDecl* const specialization = standard_specialization(type);
    if (specialization == nullptr) return Family::none;
    const bool element_const = span_element_const(*specialization);
    Family family = specialization_family(*specialization, stage, element_const);
    if (family == Family::text_reference) family = Family::owned_text;
    return family;
}

FamilyMatch match_family(const clang::QualType declared, const EntityKind kind, const Stage stage,
                         clang::ASTContext& context) {
    FamilyMatch match;
    const clang::ClassTemplateSpecializationDecl* const specialization = standard_specialization(declared);
    if (specialization == nullptr) return match;
    const bool reference = (*declared).isReferenceType();
    const bool const_reference = reference && declared.getNonReferenceType().isConstQualified();
    const bool element_const = span_element_const(*specialization);
    const Family family = specialization_family(*specialization, stage, element_const);
    if (family == Family::none) return match;
    match.reference = reference;
    match.value_type = first_argument(declared, *specialization);

    // The entity rules per stage (plan section 4 and the 2026-11-16 decisions).
    if (stage == Stage::v2_text_view) {
        if (kind != EntityKind::parameter) return match;
        if (family == Family::text_reference && !const_reference) return match;
        match.family = family;
        return match;
    }
    if (stage == Stage::v3_owned_text) {
        // Owned text replaces stored and returned strings; a const
        // std::string& parameter is V2's, and a non-const reference is an
        // out-parameter left for review.
        if (reference) {
            if (const_reference) return match;
            match.family = family;
            match.unsupported = "non-const std::string& (an out-parameter): review by hand";
            return match;
        }
        match.family = family;
        return match;
    }
    if (family == Family::shared_from_this) {
        if (kind != EntityKind::shared_base) return match;
        match.family = family;
        return match;
    }
    match.family = family;
    if (family == Family::optional || family == Family::vector) {
        if (match.value_type.isNull()) {
            match.unsupported = "the element type is not a type argument";
        } else if (!copy_is_nothrow(match.value_type, context, 0U)) {
            match.unsupported = family == Family::optional
                ? "the value type's copy can throw (or is deleted), so stxlib::Optional of it would not copy "
                  "(waits for V3/V4/V5)"
                : "the element type's copy can throw (or is deleted), so stxlib::Vector of it would not copy "
                  "(waits for V3/V5)";
        } else if (!move_is_nothrow(match.value_type, context, 0U) || !destruction_is_nothrow(match.value_type)) {
            // stxlib::Optional static_asserts a nothrow move and destructor
            // (defect 2 of the 2026-11-16 V1 run: MotionValue).
            match.unsupported = family == Family::optional
                ? "the value type's move or destruction can throw (or is deleted), and stxlib::Optional requires "
                  "both nothrow"
                : "the element type's move or destruction can throw (or is deleted), and stxlib::Vector requires "
                  "both nothrow";
        }
        if (family == Family::vector) {
            const clang::TemplateArgumentList& arguments = (*specialization).getTemplateArgs();
            if (arguments.size() > 1U && !argument_is_standard_record(arguments[1], "allocator")) {
                match.unsupported = "std::vector with a custom allocator";
            }
        }
    }
    if ((family == Family::view || family == Family::mutable_view) && !span_extent_is_dynamic(*specialization)) {
        match.unsupported = "std::span with a fixed extent";
    }
    if (family == Family::unique) {
        const clang::TemplateArgumentList& arguments = (*specialization).getTemplateArgs();
        if (arguments.size() > 1U && !argument_is_standard_record(arguments[1], "default_delete")) {
            match.unsupported = "std::unique_ptr with a custom deleter";
        }
        if (!match.value_type.isNull() && (*match.value_type).isArrayType()) {
            match.unsupported = "std::unique_ptr of an array";
        }
    }
    return match;
}

namespace {

// The exception specification of a special member, when Sema resolved it:
// `known` is false for an unevaluated or uninstantiated one.
struct NothrowAnswer final {
    bool known{false};
    bool nothrow{false};
};

[[nodiscard]] NothrowAnswer resolved_nothrow(const clang::FunctionDecl& function) {
    NothrowAnswer answer;
    const clang::FunctionProtoType* const prototype = (*function.getType()).getAs<clang::FunctionProtoType>();
    if (prototype == nullptr) return answer;
    const clang::ExceptionSpecificationType specification = (*prototype).getExceptionSpecType();
    const bool unresolved = specification == clang::EST_Unevaluated ||
        specification == clang::EST_Uninstantiated || specification == clang::EST_Unparsed;
    if (unresolved) return answer;
    answer.known = true;
    answer.nothrow = (*prototype).isNothrow(false);
    return answer;
}

} // namespace

bool copy_is_nothrow(const clang::QualType type, clang::ASTContext& context, const unsigned depth) {
    if (type.isNull() || depth > 8U) return false;
    const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
    if ((*canonical).isReferenceType()) return true;
    if ((*canonical).isScalarType()) return true;
    const clang::ConstantArrayType* const array = context.getAsConstantArrayType(canonical);
    if (array != nullptr) {
        const bool result = copy_is_nothrow((*array).getElementType(), context, depth + 1U);
        return result;
    }
    const clang::CXXRecordDecl* const declared = (*canonical).getAsCXXRecordDecl();
    if (declared == nullptr || !(*declared).hasDefinition()) return false;
    const clang::CXXRecordDecl* const record = (*declared).getDefinition();

    // stxlib's vocabulary, by name (the owner's 2026-11-16 copy decision).
    if (owned_record(canonical) != nullptr) {
        const llvm::StringRef name = (*record).getName();
        if (name == "Unique" || name == "OperationResult" || name == "FrozenMap" || name == "FrozenTextMap") return false;
        const clang::ClassTemplateSpecializationDecl* const owned =
            llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
        if ((name == "Optional" || name == "Vector") && owned != nullptr) {
            const clang::TemplateArgumentList& arguments = (*owned).getTemplateArgs();
            if (arguments.size() > 0U && arguments[0].getKind() == clang::TemplateArgument::Type) {
                const bool result = copy_is_nothrow(arguments[0].getAsType(), context, depth + 1U);
                return result;
            }
        }
        return true;
    }

    // A deleted copy is no copy, though Clang calls a copy constructor
    // deleted on its first declaration trivial when the members are.
    for (const clang::CXXConstructorDecl* constructor : (*record).ctors()) {
        if ((*constructor).isCopyConstructor() && (*constructor).isDeleted()) return false;
    }
    if (!(*record).hasUserDeclaredCopyConstructor()) {
        // The implicit copy is deleted by a declared move, or by a member.
        if ((*record).hasUserDeclaredMoveConstructor() || (*record).hasUserDeclaredMoveAssignment()) return false;
        if ((*record).defaultedCopyConstructorIsDeleted()) return false;
    }
    if ((*record).hasTrivialCopyConstructor()) return true;

    for (const clang::CXXConstructorDecl* constructor : (*record).ctors()) {
        if (!(*constructor).isCopyConstructor()) continue;
        const NothrowAnswer answer = resolved_nothrow(*constructor);
        if (answer.known) return answer.nothrow;
        if (!(*constructor).isDefaulted()) return false;
        break;
    }

    // An implicit or defaulted copy: every base and member must copy without throwing.
    for (const clang::CXXBaseSpecifier& base : (*record).bases()) {
        if (!copy_is_nothrow(base.getType(), context, depth + 1U)) return false;
    }
    for (const clang::FieldDecl* field : (*record).fields()) {
        if (!copy_is_nothrow((*field).getType(), context, depth + 1U)) return false;
    }
    return true;
}

bool move_is_nothrow(const clang::QualType type, clang::ASTContext& context, const unsigned depth) {
    if (type.isNull() || depth > 8U) return false;
    const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
    if ((*canonical).isReferenceType()) return true;
    if ((*canonical).isScalarType()) return true;
    const clang::ConstantArrayType* const array = context.getAsConstantArrayType(canonical);
    if (array != nullptr) {
        const bool result = move_is_nothrow((*array).getElementType(), context, depth + 1U);
        return result;
    }
    const clang::CXXRecordDecl* const declared = (*canonical).getAsCXXRecordDecl();
    if (declared == nullptr || !(*declared).hasDefinition()) return false;
    const clang::CXXRecordDecl* const record = (*declared).getDefinition();

    // stxlib's vocabulary moves without throwing; an Optional moves its value.
    if (owned_record(canonical) != nullptr) {
        const clang::ClassTemplateSpecializationDecl* const owned =
            llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
        if ((*record).getName() == "Optional" && owned != nullptr) {
            const clang::TemplateArgumentList& arguments = (*owned).getTemplateArgs();
            if (arguments.size() > 0U && arguments[0].getKind() == clang::TemplateArgument::Type) {
                const bool result = move_is_nothrow(arguments[0].getAsType(), context, depth + 1U);
                return result;
            }
        }
        return true;
    }

    // A declared move constructor decides (a deleted one makes the value
    // immovable: overload resolution picks it and fails).
    for (const clang::CXXConstructorDecl* constructor : (*record).ctors()) {
        if (!(*constructor).isMoveConstructor()) continue;
        if ((*constructor).isDeleted()) return false;
        if ((*constructor).isImplicit() && (*record).hasTrivialMoveConstructor()) return true;
        const NothrowAnswer answer = resolved_nothrow(*constructor);
        if (answer.known) return answer.nothrow;
        if (!(*constructor).isDefaulted()) return false;
        for (const clang::CXXBaseSpecifier& base : (*record).bases()) {
            if (!move_is_nothrow(base.getType(), context, depth + 1U)) return false;
        }
        for (const clang::FieldDecl* field : (*record).fields()) {
            if (!move_is_nothrow((*field).getType(), context, depth + 1U)) return false;
        }
        return true;
    }
    if ((*record).hasUserDeclaredMoveConstructor()) return false;

    // No move constructor is declared. A declared copy operation or
    // destructor suppresses the implicit one, and a defaulted one that
    // would be deleted is not declared: a move is then the copy.
    const bool suppressed = (*record).hasUserDeclaredCopyConstructor() ||
        (*record).hasUserDeclaredCopyAssignment() || (*record).hasUserDeclaredMoveAssignment() ||
        (*record).hasUserDeclaredDestructor() || (*record).defaultedMoveConstructorIsDeleted();
    if (suppressed) {
        const bool result = copy_is_nothrow(type, context, depth);
        return result;
    }
    if ((*record).hasTrivialMoveConstructor()) return true;
    for (const clang::CXXBaseSpecifier& base : (*record).bases()) {
        if (!move_is_nothrow(base.getType(), context, depth + 1U)) return false;
    }
    for (const clang::FieldDecl* field : (*record).fields()) {
        if (!move_is_nothrow((*field).getType(), context, depth + 1U)) return false;
    }
    return true;
}

bool destruction_is_nothrow(const clang::QualType type) {
    if (type.isNull()) return false;
    const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
    const clang::CXXRecordDecl* const declared = (*canonical).getAsCXXRecordDecl();
    if (declared == nullptr) return true;
    if (!(*declared).hasDefinition()) return false;
    const clang::CXXDestructorDecl* const destructor = (*(*declared).getDefinition()).getDestructor();
    if (destructor == nullptr) return true;
    if ((*destructor).isDeleted()) return false;
    const NothrowAnswer answer = resolved_nothrow(*destructor);
    if (answer.known) return answer.nothrow;
    return true;
}

} // namespace d15
