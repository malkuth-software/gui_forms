#pragma once

// A structural difference of two form documents (stage MCP G7's `diff`
// tool): which objects, by stable ID, were added, removed, moved to another
// parent or retyped, and which of their attributes and bindings changed.
// A model converting a form reads it to see what an edit, or a second
// conversion, did; it is not a text diff, so reindenting changes nothing.
//
// An element without an `id` (a `bind`, a `span`) is part of its owner:
// bindings are compared as a set of `property` keys per owner; spans as the
// owner's `text` content in order.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

enum class FormDiffKind : std::uint8_t {
    object_added,
    object_removed,
    // Under another parent, or at another position among its siblings.
    object_moved,
    object_retyped,
    attribute_added,
    attribute_removed,
    attribute_changed,
    binding_added,
    binding_removed,
    binding_changed,
};

[[nodiscard]] std::string_view form_diff_kind_name(FormDiffKind kind) noexcept;

struct FormDiffEntry final {
    FormDiffKind kind{FormDiffKind::object_added};
    // The object's stable ID.
    std::string id{};
    // The attribute's name or the binding's property; empty for an object.
    std::string name{};
    // Before and after: values, types, or parents with positions
    // (`main.root#2`); empty where there is none.
    std::string before{};
    std::string after{};
};

// The most entries one diff reports; the rest are counted.
inline constexpr std::size_t maximum_form_diff_entries = 5000U;

struct FormDiff final {
    std::vector<FormDiffEntry> entries{};
    std::size_t dropped{};
    // Empty when both documents were read; otherwise which was not and why.
    std::string problem{};
};

enum class FormDiffOutcome : std::uint8_t { compared, refused };

// Compares `before` with `after`, both form document texts. refused, with
// `diff.problem` set, when either is not XML the reader admits.
[[nodiscard]] FormDiffOutcome diff_form_documents(std::string_view before, std::string_view after, FormDiff& diff);

// One line per entry: `object_moved main.ok: main.root#3 -> main.panel#0`.
[[nodiscard]] std::string form_diff_text(const FormDiff& diff);

} // namespace gui_forms::winforms_converter
