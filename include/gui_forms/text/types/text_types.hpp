#pragma once

#include "gui_forms/export/application_export.hpp"
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms {

template <typename Tag> class TextUnitIndex final {
public:
    explicit constexpr TextUnitIndex(std::size_t value = 0) noexcept
        : value_(value) {}
    [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }
    friend constexpr bool operator==(const TextUnitIndex& left,
                                     const TextUnitIndex& right) noexcept {
        return left.value_ == right.value_;
    }
    friend constexpr std::strong_ordering operator<=>(
        const TextUnitIndex& left, const TextUnitIndex& right) noexcept {
        return left.value_ <=> right.value_;
    }
private:
    std::size_t value_{};
};

struct Utf8OffsetTag;
struct Utf16OffsetTag;
struct ScalarIndexTag;
struct GraphemeIndexTag;
struct LineIndexTag;
using Utf8Offset = TextUnitIndex<Utf8OffsetTag>;
using Utf16Offset = TextUnitIndex<Utf16OffsetTag>;
using ScalarIndex = TextUnitIndex<ScalarIndexTag>;
using GraphemeIndex = TextUnitIndex<GraphemeIndexTag>;
using LineIndex = TextUnitIndex<LineIndexTag>;

template <typename Position> struct TextRange final {
    Position start{};
    Position end{};
    friend constexpr bool operator==(const TextRange& left,
                                     const TextRange& right) noexcept(noexcept(
        left.start == right.start && left.end == right.end)) {
        return left.start == right.start && left.end == right.end;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return start == end; }
};

using Utf8Range = TextRange<Utf8Offset>;
using Utf16Range = TextRange<Utf16Offset>;
using ScalarRange = TextRange<ScalarIndex>;
using GraphemeRange = TextRange<GraphemeIndex>;

enum class Utf8ValidationError : std::uint8_t {
    none, unexpected_continuation, invalid_continuation, truncated_sequence,
    overlong_encoding, surrogate_code_point, code_point_out_of_range,
};

struct GUI_FORMS_API Utf8ValidationResult final {
    Utf8ValidationError error{Utf8ValidationError::none};
    Utf8Offset error_offset{};
    std::size_t scalar_count{};
    std::size_t utf16_unit_count{};
    [[nodiscard]] constexpr bool valid() const noexcept {
        return error == Utf8ValidationError::none;
    }
};

[[nodiscard]] GUI_FORMS_API Utf8ValidationResult validate_utf8(
    std::string_view text) noexcept;
[[nodiscard]] GUI_FORMS_API std::string_view to_string(Utf8ValidationError error) noexcept;

// One scalar decoded strictly (the rules of validate_utf8: no overlong
// forms, surrogates or values past U+10FFFF). At a malformed or truncated
// sequence `valid` is false, `length` is 1 (the caller steps one byte) and
// `scalar` is U+FFFD. `offset` must be inside `text`.
struct GUI_FORMS_API Utf8ScalarDecode final {
    char32_t scalar{};
    std::size_t length{};
    bool valid{};
};
[[nodiscard]] GUI_FORMS_API Utf8ScalarDecode decode_utf8_scalar(std::string_view text, Utf8Offset offset) noexcept;
[[nodiscard]] GUI_FORMS_API std::string_view grapheme_unicode_version() noexcept;

struct GUI_FORMS_API TextStyleId final {
    std::uint32_t value{};
    friend constexpr bool operator==(const TextStyleId& left,
                                     const TextStyleId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const TextStyleId& left, const TextStyleId& right) noexcept {
        return left.value <=> right.value;
    }
};

struct GUI_FORMS_API TextStyleSpan final {
    Utf8Range range{};
    TextStyleId style{};
    friend constexpr bool operator==(const TextStyleSpan& left,
                                     const TextStyleSpan& right) noexcept {
        return left.range == right.range && left.style == right.style;
    }
};

struct GUI_FORMS_API TextStoreLimits final {
    std::size_t maximum_utf8_bytes{16U * 1024U * 1024U};
    std::size_t maximum_style_spans{65'536U};
};

struct GUI_FORMS_API TextStoreSnapshot final {
    std::size_t utf8_bytes{};
    std::size_t utf16_units{};
    std::size_t scalars{};
    std::size_t graphemes{};
    std::size_t lines{};
    std::size_t style_spans{};
    std::uint64_t revision{};
    std::uint64_t edit_count{};
    std::uint64_t style_update_count{};
    std::uint64_t metadata_rebuild_count{};
    std::uint64_t rejected_mutation_count{};
    std::uint64_t rejected_position_query_count{};
    friend constexpr bool operator==(const TextStoreSnapshot& left,
                                     const TextStoreSnapshot& right) noexcept {
        return left.utf8_bytes == right.utf8_bytes &&
               left.utf16_units == right.utf16_units &&
               left.scalars == right.scalars &&
               left.graphemes == right.graphemes && left.lines == right.lines &&
               left.style_spans == right.style_spans &&
               left.revision == right.revision &&
               left.edit_count == right.edit_count &&
               left.style_update_count == right.style_update_count &&
               left.metadata_rebuild_count == right.metadata_rebuild_count &&
               left.rejected_mutation_count == right.rejected_mutation_count &&
               left.rejected_position_query_count ==
                   right.rejected_position_query_count;
    }
};

struct GUI_FORMS_API TextEditResult final {
    Utf8Range removed{};
    Utf8Range inserted{};
    std::size_t removed_scalars{};
    std::size_t inserted_scalars{};
    std::uint64_t revision{};
    bool changed{};
};

} // namespace gui_forms
