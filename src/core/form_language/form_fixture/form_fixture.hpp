#pragma once

// A mockup's fixture data (gui_forms/forms/form_mockup; docs/user/mockups.md):
// the sample values a mockup's bindings read and the rows and tree items
// its lists, grids and trees show, written in a file beside the form
// instead of in program code. Read with the form documents' XML reader,
// located the same way, and refused with the same E8xx diagnostics.
//
//   <?xml version="1.0" encoding="UTF-8"?>
//   <fixture xmlns="urn:gui-forms:fixture:1">
//     <value name="autosave" value="true"/>
//     <records name="claims" columns="claim date amount:number paid:boolean"
//         shown-in="claims.grid">
//       <row values="C-1041 2026-11-02 120.50 false"/>
//       <row id="late" values="C-1042 2026-11-03 '1 200' true"/>
//     </records>
//     <tree shown-in="files.tree">
//       <item text="Documents" expanded="true">
//         <item text="Claim.pdf"/>
//       </item>
//     </tree>
//   </fixture>
//
// - `value` is a scalar source, a model's Value: `<bind property="checked"
//   to="autosave"/>` reads it. Its text is read as the bound property's kind
//   (boolean, integer or number) when the mockup binds it.
// - `records` is a BindingSource's rows: `<bind property="text" to="claims"
//   field="claim"/>` reads the current (first) row's field. `columns` lists
//   the fields, each `name` (text) or `name:boolean`, `name:integer` or
//   `name:number`. Each `row`'s `values` gives the row's cells in column
//   order, as a form's list is written (whitespace between items; an item
//   with whitespace is quoted, with the form strings' escapes). Fewer
//   values than columns leave the rest empty. A row's `id` is its stable ID
//   (`row-<n>` by default, counting from 1).
// - `shown-in` names objects by stable ID that show the rows: a grid shows
//   them as its data source, a list view as its items (the first column the
//   item's text, the others its sub-items), and a list box, combo box or
//   any control with a text `items` collection the first column.
// - `tree` is the items of the tree views its `shown-in` names, nested as
//   written. An item's `id` is its stable ID (`item-<n>` by default),
//   `expanded="true"` opens it.
//
// Every element but the root lives directly where shown above, and no
// element holds text. A name is one C++ identifier and is used once across
// the values and records. Refusals: E821 for XML outside the subset or a
// wrong namespace, E801 for another root, E802 for an unknown element,
// E804 for an unknown attribute, E803 for a repeated name or stable ID,
// E807 for a missing attribute, a bad name, a value its column cannot take
// or more values than columns, E815 for a bad escape. Internal.

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/binding/binding_schema/binding_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::form_language {

// The default namespace a fixture's root declares.
inline constexpr std::string_view fixture_namespace = "urn:gui-forms:fixture:1";

// One scalar source.
struct FixtureValue final {
    std::string name{};
    // As written; read as the bound property's kind when it is bound.
    std::string text{};
    // Where the element begins.
    theme_language::SourcePosition position{};
};

// One record set: a BindingSource's columns and rows, and the objects
// that show them.
struct FixtureRecords final {
    std::string name{};
    std::vector<BindingColumn> columns{};
    // Each row's values in column order, of the column's kind; no more
    // than the columns.
    std::vector<BindingRow> rows{};
    // Stable IDs, in the order written.
    std::vector<std::string> shown_in{};
    theme_language::SourcePosition position{};
};

// One tree item, in pre-order (a parent before its children), as a tree
// view's flat model holds it.
struct FixtureTreeItem final {
    std::string stable_id{};
    std::string text{};
    // 0 for an item directly inside `tree`.
    std::size_t depth{};
    // Whether it holds items, and whether it is open.
    bool expandable{};
    bool expanded{};
};

struct FixtureTree final {
    std::vector<std::string> shown_in{};
    std::vector<FixtureTreeItem> items{};
    theme_language::SourcePosition position{};
};

// A read fixture. Caller-owned: replaced whole by a read that succeeds,
// left as it was by one that refuses.
struct FormFixture final {
    std::vector<FixtureValue> values{};
    std::vector<FixtureRecords> records{};
    std::vector<FixtureTree> trees{};
};

enum class FormFixtureOutcome : std::uint8_t {
    read,
    // At least one error, every one found reported in document order;
    // `fixture` is unchanged.
    refused,
};

// Reads the fixture `source` holds (its text, and its file for the
// diagnostics). Borrowed for the call. Empty text is an empty fixture.
[[nodiscard]] FormFixtureOutcome read_form_fixture(const theme_language::ThemeSource& source, FormFixture& fixture,
                                                   theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
