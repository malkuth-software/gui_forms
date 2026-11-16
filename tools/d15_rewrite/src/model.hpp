#pragma once

// The records the D15 rewriter's phases exchange (README.md, "How it
// works"). Everything here is plain data owned by its holder: a
// translation unit's analysis fills a TranslationUnitHarvest, the driver
// merges the harvests under one lock and then owns the merged records.

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d15 {

// The plan's stages that rewrite code (V6, the surface check, is a
// configuration of tools/check_std_rules.py, not a rewrite).
enum class Stage : std::uint8_t {
    v1_optional,
    v2_text_view,
    v3_owned_text,
    v4_sequences,
    v5_ownership,
};

// The standard vocabulary a declaration's type is, as one stage sees it.
enum class Family : std::uint8_t {
    none,
    optional,            // std::optional<T> -> stxlib::Optional<T> (V1)
    text_view,           // std::string_view parameter -> stxlib::TextView (V2)
    text_reference,      // const std::string& parameter -> stxlib::TextView (V2)
    owned_text,          // std::string -> stxlib::Utf8String (V3)
    vector,              // std::vector<T> -> stxlib::Vector<T> (V4)
    view,                // std::span<const T> -> stxlib::View<T> (V4)
    mutable_view,        // std::span<T> -> stxlib::MutableView<T> (V4)
    shared,              // std::shared_ptr<T> -> stxlib::Shared<T> (V5)
    weak,                // std::weak_ptr<T> -> stxlib::Weak<T> (V5)
    unique,              // std::unique_ptr<T> -> stxlib::Unique<T> (V5)
    shared_from_this,    // std::enable_shared_from_this<T> base -> stxlib::SharedFromThis<T> (V5)
};

// What kind of declaration an entity is. An entity is one thing whose
// declared type changes as a unit: every redeclaration of a function's
// return or parameter, a field, a variable, an alias.
enum class EntityKind : std::uint8_t {
    field,
    variable,         // namespace-scope or static member variable
    local,            // block-scope variable (joins only by propagation)
    parameter,
    function_return,
    alias,            // a first-party typedef or using-alias of the family
    shared_base,      // a class's std::enable_shared_from_this base (V5)
};

// One entity as one translation unit saw it. The plan merges the facts of
// every unit by key; `ineligible` is empty when the entity can change.
struct EntityFact final {
    std::string key;
    std::string path;          // relative to the source root
    unsigned line{0U};
    unsigned column{0U};
    EntityKind kind{EntityKind::field};
    Family family{Family::none};
    std::string name;          // the simple name (a function's for returns and parameters)
    std::string qualified_name;
    bool seeded{false};
    // On the consumer surface (--surface-headers, --surface-exports), or
    // every entity when no surface is given. Locals are always on it.
    bool surface{true};
    std::string ineligible;
};

// Two entities that must change together. A strict edge (overrides,
// aliases, V5 ownership flows) also shares ineligibility: if one end cannot
// change, neither can the other. A soft edge (a V1 value flow) only joins
// the component; a soft edge to an entity that does not change becomes a
// bridged or residual boundary site in the rewrite.
// `flow` marks a value flow (an initialisation, assignment, argument,
// return or comparison) as against a structural tie (an override, an
// alias, an auto local's type): with a consumer surface, a flow that
// reaches a non-local entity off the surface is dropped, and that site
// becomes a bridged boundary.
struct EdgeFact final {
    std::string first;
    std::string second;
    bool strict{false};
    bool flow{false};
};

// One edit to one file, by byte offset into the file as it was read.
// Replacements must not overlap each other; insertions (length 0) at one
// offset are ordered by role and span so that wrappers nest: openers of
// longer spans first, closers of shorter spans first. `group` names the
// rewrite of one site (its rule and anchor offset): the edits of a group
// are applied together or not at all, so a conflict never leaves half a
// wrapper behind.
enum class EditRole : std::uint8_t {
    replace,
    open,
    close,
};

struct Edit final {
    std::uint64_t offset{0U};
    std::uint64_t length{0U};
    std::string text;
    EditRole role{EditRole::replace};
    std::uint64_t span{0U};    // length of the wrapped expression, for openers and closers
    std::string group;
};

// Something the rewriter could not rewrite, with the reason. Residue is the
// list of hand fixes a stage still needs.
struct Residue final {
    std::string path;
    unsigned line{0U};
    unsigned column{0U};
    std::string reason;
    std::string snippet;
};

// Headers a rewritten file needs (a bit per header).
enum IncludeNeed : std::uint32_t {
    include_none = 0U,
    include_optional = 1U << 0U,
    include_text_view = 1U << 1U,
    include_text = 1U << 2U,
    include_vector = 1U << 3U,
    include_view = 1U << 4U,
    include_shared = 1U << 5U,
    include_unique = 1U << 6U,
    include_result = 1U << 7U,
    include_fail_fast = 1U << 8U,
    include_bridge = 1U << 9U,
};

// Everything one translation unit's analysis produced. Owned by the task
// that analysed it until the driver merges it.
struct TranslationUnitHarvest final {
    std::string source;
    std::vector<EntityFact> entities;
    std::vector<EdgeFact> edges;
    std::map<std::string, std::vector<Edit>> edits;          // by relative path
    std::map<std::string, std::uint32_t> includes;           // by relative path
    std::vector<Residue> residue;
    std::set<std::string> files_seen;                        // relative paths holding a visited declaration
    unsigned errors{0U};
    std::string first_error;
};

// The command line, read once by main and then only read.
struct Options final {
    Stage stage{Stage::v1_optional};
    std::string source_root;                 // absolute, normalised
    std::vector<std::string> seed_directories;
    std::vector<std::string> seed_exclusions;
    std::vector<std::string> scope_directories;
    std::vector<std::string> scope_exclusions;
    std::string only_pattern;                // regular expression over qualified names; empty: all
    std::set<EntityKind> seed_kinds;
    bool propagate_locals{true};
    bool flow_edges{true};
    bool allocating_returns_result{false};   // V3: Q1's OperationResult<Utf8String> returns
    bool drop_standard_includes{false};
    // The consumer surface (V2 onwards: the owner limited D15 to it). When
    // set, only entities on it are seeded, and value flows do not carry a
    // conversion to non-local declarations off it.
    bool surface_mode{false};
    std::vector<std::string> surface_headers;   // relative paths (files or directories)
    std::set<std::string> surface_names;        // qualified names a module exports (gui_forms::Name)
};

[[nodiscard]] const char* stage_name(Stage stage) noexcept;
[[nodiscard]] const char* family_name(Family family) noexcept;
[[nodiscard]] const char* entity_kind_name(EntityKind kind) noexcept;

} // namespace d15
