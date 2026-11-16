#pragma once

// Which standard vocabulary a type is, and what replaces it, per stage
// (the migration plan's section 1 table).

#include "model.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>

namespace d15 {

// A declared type matched against the stage. `value_type` is the first
// template argument with its sugar (std::size_t stays std::size_t) when
// the type was written as a template specialisation, else the canonical
// argument. `unsupported` is empty when the replacement exists.
struct FamilyMatch final {
    Family family{Family::none};
    clang::QualType value_type;
    bool reference{false};
    std::string unsupported;
};

// The family of `declared` for an entity of `kind`, or Family::none.
// References and cv-qualifiers are looked through where the stage admits
// them (V2 admits only by-value string_view and const std::string&
// parameters; the others admit const and non-const references).
[[nodiscard]] FamilyMatch match_family(clang::QualType declared, EntityKind kind, Stage stage,
                                       clang::ASTContext& context);

// The family of an expression's type under the stage, ignoring the entity
// rules: what a source or a destination of a flow is. References are
// looked through.
[[nodiscard]] Family expression_family(clang::QualType type, Stage stage);

// True when `type` (references and cv looked through) is already the
// stxlib counterpart of `family` (stxlib::Optional for optional, and so on).
[[nodiscard]] bool is_owned_counterpart(clang::QualType type, Family family);

// True when `type` is stxlib::TextView.
[[nodiscard]] bool is_text_view(clang::QualType type);

// The stage a family belongs to.
[[nodiscard]] Stage family_stage(Family family) noexcept;

// The replacement's template name, qualified ("stxlib::Optional"), or the
// whole replacement type for the non-template families ("stxlib::TextView").
[[nodiscard]] const char* owned_name(Family family) noexcept;

// The headers a declaration of the replacement needs.
[[nodiscard]] std::uint32_t family_includes(Family family) noexcept;

// True when copying a value of `type` cannot throw, as far as the AST says:
// scalars, trivially copyable records, noexcept copy constructors, and
// records whose implicit copy copies only such members. A deleted copy
// (declared, or implicitly by a declared move or a member) is false. stxlib's
// vocabulary is answered by name (owner, 2026-11-16: owned text and
// vectors copy noexcept; Unique and Result do not copy).
[[nodiscard]] bool copy_is_nothrow(clang::QualType type, clang::ASTContext& context, unsigned depth);

// True when moving a value of `type` cannot throw, as far as the AST says:
// a declared move constructor's specification, or, where none is declared,
// the copy (when a declared copy operation or destructor suppresses the
// implicit move) or every base and member's move. A deleted move is false.
// stxlib::Optional static_asserts this of its value type.
[[nodiscard]] bool move_is_nothrow(clang::QualType type, clang::ASTContext& context, unsigned depth);

// True when destroying a value of `type` cannot throw (an unresolved
// implicit destructor counts as nothrow, as the language makes it).
[[nodiscard]] bool destruction_is_nothrow(clang::QualType type);

// True when `declaration` is the standard library entity `name` (any
// inline namespace).
[[nodiscard]] bool is_standard_entity(const clang::NamedDecl* declaration, llvm::StringRef name);

// True when `type` is std::nullopt_t or std::nullptr_t respectively.
[[nodiscard]] bool is_nullopt_type(clang::QualType type);
[[nodiscard]] bool is_nullptr_type(clang::QualType type);

// True when `type` is a standard string (std::string) or string_view.
[[nodiscard]] bool is_standard_string(clang::QualType type);
[[nodiscard]] bool is_standard_string_view(clang::QualType type);

} // namespace d15
