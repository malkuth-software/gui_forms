#pragma once

// The one order of sort keys and filter comparisons (binding_schema.hpp,
// compare_binding_keys), on a view of a value that does not own its text,
// so the view index can compare its own compact copies the same way.

#include "gui_forms/binding/binding_schema/binding_schema.hpp"

#include <cstdint>
#include <string_view>

namespace gui_forms::detail {

// A key as compare_binding_key_views orders it. For kind signed_integer,
// unsigned_integer, number and boolean the value is in the matching member;
// for text, text views the bytes (owned elsewhere for the view's life).
struct BindingKeyView final {
    BindingValueKind kind{BindingValueKind::null};
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    double number{};
    bool boolean{};
    std::string_view text;
};

// The view of value; text views value's own string.
[[nodiscard]] BindingKeyView binding_key_view(const BindingValue& value) noexcept;

[[nodiscard]] int compare_binding_key_views(const BindingKeyView& left, const BindingKeyView& right) noexcept;

// The text order (the owner's ruling, 2026-11-10): code point by code point
// after simple case folding (ASCII, Latin-1, basic Greek and Cyrillic; other
// scripts compare by code point). A byte that is not part of well-formed
// UTF-8 orders after every code point, by its value. This is the only text
// comparer of the binding model.
[[nodiscard]] int compare_text_ignoring_case(std::string_view left, std::string_view right) noexcept;
// Whether text starts with, or contains, part under the same folding.
[[nodiscard]] bool text_starts_with_ignoring_case(std::string_view text, std::string_view part) noexcept;
[[nodiscard]] bool text_contains_ignoring_case(std::string_view text, std::string_view part) noexcept;

} // namespace gui_forms::detail
