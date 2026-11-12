#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms {

// Nonfatal layout diagnostics (stage L, phase L5; PLAN section 2.5): what a
// layout pass did that a source may not expect, kept by the window
// (Window::layout_diagnostics) in the form T3's diagnostics use, a severity,
// a code and the nodes.

enum class LayoutDiagnosticSeverity : std::uint8_t { warning, error };

enum class LayoutDiagnosticCode : std::uint16_t {
    // An absolutely positioned child's containing block is the padding box
    // of a positioned ancestor other than its parent, or the window's client
    // area (decision L-Q6: warned, and allowed). Recorded when the child's
    // resolution changes, not on every pass.
    containing_block_not_parent = 201,
};

[[nodiscard]] GUI_FORMS_API LayoutDiagnosticSeverity layout_diagnostic_severity(LayoutDiagnosticCode code) noexcept;
// The code's snake_case name, such as `containing_block_not_parent`.
[[nodiscard]] GUI_FORMS_API std::string_view layout_diagnostic_code_name(LayoutDiagnosticCode code) noexcept;

struct GUI_FORMS_API LayoutDiagnostic final {
    LayoutDiagnosticSeverity severity{LayoutDiagnosticSeverity::warning};
    LayoutDiagnosticCode code{LayoutDiagnosticCode::containing_block_not_parent};
    // The stable identities of the child the pass placed and of the
    // ancestor whose box it was placed in (the window's root control for
    // its client area).
    std::string child{};
    std::string ancestor{};

    friend bool operator==(const LayoutDiagnostic& left, const LayoutDiagnostic& right) noexcept {
        return left.severity == right.severity && left.code == right.code && left.child == right.child &&
               left.ancestor == right.ancestor;
    }
};

// The most records a window keeps; later ones are counted.
inline constexpr std::size_t maximum_layout_diagnostics = 1000U;

// The most out-of-flow dependents one containing block keeps (PLAN
// section 3.3); above it the block arranges its whole subtree instead.
inline constexpr std::size_t maximum_containing_block_dependents = 4'096U;

} // namespace gui_forms
