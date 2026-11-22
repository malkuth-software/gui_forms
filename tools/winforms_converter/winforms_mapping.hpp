#pragma once

// How Windows Forms names become GUI.Forms names (stage MCP G7), from the
// ledgers every importer reads (winforms_member_table.hpp, generated from
// manifests/controls.json, manifests/members.json and
// manifests/winforms-divergences.json by emit_member_table.py):
//
// - a type: the registry type that imports it (Button is button,
//   FlowLayoutPanel is flow_layout_panel_compat); Form is the form root;
// - a member: the member manifest's GUI.Forms counterpart, looked up on the
//   type and then on its Windows Forms bases (Button's Text is Control's);
// - a name with no manifest entry: its snake case, which the converter then
//   checks against the form schema.
//
// Spelling of identities: a stable ID segment is lowercase ASCII letters
// and digits joined by single '-' (the C ABI's objects.create rule), so a
// Windows Forms field `textBox1` becomes `text-box1` and `btnOK` becomes
// `btn-ok`.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::winforms_converter {

enum class WinFormsMemberKind : std::uint8_t { property, event };

// The member manifest's status of a Windows Forms member.
enum class WinFormsMemberStatus : std::uint8_t { supported, deferred, excluded, unclassified };

struct WinFormsMemberRow final {
    std::string_view winforms{};
    // The GUI.Forms member, snake case; empty when the manifest knows none.
    std::string_view gui_forms{};
    WinFormsMemberKind kind{WinFormsMemberKind::property};
    WinFormsMemberStatus status{WinFormsMemberStatus::unclassified};
};

struct WinFormsTypeRow final {
    std::string_view winforms{};
    // The Windows Forms base type's short name; empty at the root.
    std::string_view winforms_base{};
    // The GUI.Forms C++ type the manifest pairs it with, and the registry
    // type an importer makes of it (empty: no counterpart to make).
    std::string_view gui_forms_type{};
    std::string_view registry_id{};
    // The type's own members: winforms_member_rows[first_member] on.
    std::uint32_t first_member{};
    std::uint32_t member_count{};
};

struct WinFormsDivergenceRow final {
    // The GUI.Forms type and member, and GUI.Forms' behaviour.
    std::string_view control{};
    std::string_view member{};
    std::string_view gui_forms{};
};

// The type row of a Windows Forms type, by its short or qualified name
// (`System.Windows.Forms.Button`); null when the ledgers do not list it.
[[nodiscard]] const WinFormsTypeRow* find_winforms_type(std::string_view name) noexcept;

// A member found on a type or one of its bases.
struct WinFormsMemberMatch final {
    const WinFormsMemberRow* member{};
    // The type whose entry lists it.
    const WinFormsTypeRow* declared_by{};
};

// Looks `member` of `kind` up on `type`, then on its bases (at most
// maximum_winforms_base_depth of them). Both null when no entry lists it.
[[nodiscard]] WinFormsMemberMatch find_winforms_member(const WinFormsTypeRow& type, std::string_view member,
                                                       WinFormsMemberKind kind) noexcept;

inline constexpr std::size_t maximum_winforms_base_depth = 16U;

// The recorded divergence of a GUI.Forms member, looked up on the GUI.Forms
// type and then on Control; null when none is recorded.
[[nodiscard]] const WinFormsDivergenceRow* find_winforms_divergence(std::string_view gui_forms_type,
                                                                    std::string_view member) noexcept;

// `TabIndex` gives `tab_index`, `BackColor` `back_color`, `HScrollBar`
// `h_scroll_bar`, `AutoScaleDimensions` `auto_scale_dimensions`.
[[nodiscard]] std::string snake_case_of(std::string_view pascal);
// The same with '-' for '_': a form document's spelling.
[[nodiscard]] std::string kebab_case_of(std::string_view pascal);
// A field name as one stable ID segment: `textBox1` gives `text-box1`,
// `btnOK` `btn-ok`, `_name` `name`, `label_2` `label-2`. Empty when nothing
// of the name is a letter or digit.
[[nodiscard]] std::string stable_id_segment_of(std::string_view field);
// A C# or field name as a C++ identifier in snake case, for a model
// member: `bindingSource1` gives `binding_source1`.
[[nodiscard]] std::string member_identifier_of(std::string_view name);

} // namespace gui_forms::winforms_converter
