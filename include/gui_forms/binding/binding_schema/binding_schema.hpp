#pragma once

#include "gui_forms/binding/value/binding_value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The data-provider binding model (audit N31, owner ruling 2026-11-10): a
// binding source's rows come from a provider (binding_provider.hpp) that
// declares its columns once, as a table schema, and answers cell by cell.
// The source copies every answer into storage it owns and checks it against
// the schema and the bounds below before keeping it.

// One column of a provider's table. The name is stored as
// canonical_binding_name spells it (trimmed, ASCII letters lowered), so any
// spelling that folds to it finds the column. Kind null declares an untyped
// column, which takes a value of any kind; every column takes the null
// value. A read-only column is never written by the source.
struct BindingColumn final {
    std::string name;
    BindingValueKind kind{BindingValueKind::null};
    bool read_only{};

    friend bool operator==(const BindingColumn& left, const BindingColumn& right) noexcept {
        return left.name == right.name && left.kind == right.kind && left.read_only == right.read_only;
    }
};

// The column index that names a whole row: a row's own error text rather
// than one cell's, or a change to every cell of the row.
inline constexpr std::size_t binding_whole_row = static_cast<std::size_t>(-1);

// The bounds every provider answer is held to. A larger answer is a data
// error: the cell shows an error and the source keeps nothing of it.
inline constexpr std::size_t binding_maximum_columns = 1024U;
inline constexpr std::size_t binding_maximum_text_bytes = std::size_t{1} << 20U;
inline constexpr std::size_t binding_maximum_error_bytes = 4096U;
// A provider's row count past this is refused as a data error (the source
// shows no rows until the next reset). The view index numbers rows in 32
// bits, which this keeps clear of.
inline constexpr std::size_t binding_maximum_rows = std::size_t{1} << 31U;

// A table's columns, declared once per provider.
class BindingSchema final {
public:
    BindingSchema() = default;
    // Throws std::invalid_argument for an empty name, a name repeated under
    // canonical_binding_name, or more than binding_maximum_columns columns:
    // these are programming errors (N70).
    explicit BindingSchema(std::vector<BindingColumn> columns);

    [[nodiscard]] std::size_t size() const noexcept { return columns_.size(); }
    [[nodiscard]] std::span<const BindingColumn> columns() const& noexcept { return columns_; }
    std::span<const BindingColumn> columns() const&& = delete;
    // The column at index (below size()).
    [[nodiscard]] const BindingColumn& column(std::size_t index) const { return columns_.at(index); }
    // The index of the column named name in any spelling that folds to it,
    // or empty. A name that is blank finds nothing.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view name) const;
    // Appends a column under the constructor's checks; returns its index.
    std::size_t append(BindingColumn column);
    // Whether value may be stored in column: the null value always, else a
    // value of the column's kind, or of any kind for an untyped column.
    [[nodiscard]] bool admits(std::size_t column, const BindingValue& value) const noexcept;

    friend bool operator==(const BindingSchema& left, const BindingSchema& right) noexcept {
        return left.columns_ == right.columns_;
    }

private:
    std::vector<BindingColumn> columns_;
};

// One row given whole to a provider (BindingTable, insert_row): its stable
// ID (the StableId segment rule), its values in column order (fewer than the
// schema's columns are padded with null), and whether it may be edited.
struct BindingRow final {
    std::string stable_id;
    std::vector<BindingValue> values;
    bool editable{true};

    friend bool operator==(const BindingRow& left, const BindingRow& right) noexcept(noexcept(
        left.stable_id == right.stable_id && left.values == right.values)) {
        return left.stable_id == right.stable_id && left.values == right.values &&
               left.editable == right.editable;
    }
};

// ---------------------------------------------------------------------------
// Sorting and filtering (Windows Forms' BindingSource.Sort and .Filter).

enum class BindingSortDirection : std::uint8_t {
    ascending,
    descending,
};

// One sort key: a column by name and its direction. Rows whose keys are all
// equal keep their provider order (the owner's ruling, 2026-11-10: a stable
// sort, a recorded divergence from List<T>.Sort).
struct BindingSortKey final {
    std::string column;
    BindingSortDirection direction{BindingSortDirection::ascending};

    friend bool operator==(const BindingSortKey& left, const BindingSortKey& right) noexcept {
        return left.column == right.column && left.direction == right.direction;
    }
};

// The filter's one predicate type (the owner's ruling, 2026-11-10:
// structured predicates; a RowFilter string is later parsed into these). A
// comparison orders the cell against operand as compare_binding_keys does;
// contains and starts_with match text ignoring case; is_null and
// is_not_null ignore operand.
enum class BindingPredicateOperator : std::uint8_t {
    equal,
    not_equal,
    less,
    less_or_equal,
    greater,
    greater_or_equal,
    contains,
    starts_with,
    is_null,
    is_not_null,
};

struct BindingPredicate final {
    std::string column;
    BindingPredicateOperator op{BindingPredicateOperator::equal};
    BindingValue operand;

    friend bool operator==(const BindingPredicate& left, const BindingPredicate& right) noexcept(noexcept(
        left.operand == right.operand)) {
        return left.column == right.column && left.op == right.op && left.operand == right.operand;
    }
};

// A filter admits a row when every predicate holds; no predicates admit
// every row.
struct BindingFilter final {
    std::vector<BindingPredicate> all_of;

    [[nodiscard]] bool empty() const noexcept { return all_of.empty(); }
    friend bool operator==(const BindingFilter& left, const BindingFilter& right) noexcept(noexcept(
        left.all_of == right.all_of)) {
        return left.all_of == right.all_of;
    }
};

// The order of two values as sort keys: negative, zero or positive. Null
// comes first; numbers (signed, unsigned and floating) compare by value
// across their kinds; text compares by code point ignoring case (the owner's
// ruling, 2026-11-10); false before true; values of different families
// order by kind; other kinds (geometry, colours, fonts, images, objects,
// collections) are equal to every value of their kind.
[[nodiscard]] int compare_binding_keys(const BindingValue& left, const BindingValue& right) noexcept;

// Whether value satisfies predicate.
[[nodiscard]] bool binding_predicate_holds(const BindingPredicate& predicate, const BindingValue& value) noexcept;

} // namespace gui_forms
