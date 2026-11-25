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
    // A control was arranged (Control::arrange or arrange_self) while the
    // layout pass was arranging another control: a custom layout arranged a
    // child directly instead of publishing its slot with set_child_layout.
    // The pass places each child from its slot, so the direct arrangement
    // would be overwritten; it is refused, changing nothing, and counted in
    // MetricsSnapshot::layout_arrange_refusals. `child` names the control
    // arranged, `ancestor` the control the pass was arranging. Recorded at
    // every refusal.
    arrange_outside_slot = 202,
    // Content overflowed its viewport (stage VA): an overflow appeared, or
    // its answer, cause or applied scale changed. `child` names the
    // viewport owner, `ancestor` is empty; the window's overflow records
    // (SurfaceRoot::overflow_records) hold the extents and the answer.
    // Recorded when the overflow appears or changes, not on every pass. A
    // warning when the overflow is answered, an error when it is not; the
    // code's own severity (layout_diagnostic_severity) is the warning.
    content_overflow = 203,
    // Layout liveness (docs/user/layout.md, "Retained layout"). Each is
    // recorded once per control and code for the window's lifetime, with
    // one line on the standard error stream; `child` names the control,
    // `ancestor` is empty. All are errors.
    //
    // A control invalidated its own layout from its arrange (or on_layout)
    // in two arranges of one flush, both in the same bounds: arranging it
    // again would do the same. The flush stops arranging it and keeps the
    // geometry its last arrange left.
    layout_loop = 204,
    // Layout was still dirty after the flush's last pass
    // (MetricsSnapshot::bounded_pass_limit_hits): each control still
    // dirty is named, and its layout request is dropped, so the window
    // shows the last layout it reached instead of laying out again at
    // every frame and every geometry read.
    layout_not_converged = 205,
    // A control invalidated its own layout from its measure.
    layout_invalidated_in_measure = 206,
    // A control's layout was invalidated while the window painted.
    layout_invalidated_in_paint = 207,
    // A ScrollableControl moved its own scroll position from its arrange
    // (scroll_to, scroll_by or a set value), which asks for another
    // arrange. A control painting its own content states its extent
    // (ScrollableControl::painted_content_extent) so the position holds.
    scroll_in_own_arrange = 208,
};

[[nodiscard]] GUI_FORMS_API LayoutDiagnosticSeverity layout_diagnostic_severity(LayoutDiagnosticCode code) noexcept;
// The code's snake_case name, such as `containing_block_not_parent`.
[[nodiscard]] GUI_FORMS_API std::string_view layout_diagnostic_code_name(LayoutDiagnosticCode code) noexcept;

struct GUI_FORMS_API LayoutDiagnostic final {
    LayoutDiagnosticSeverity severity{LayoutDiagnosticSeverity::warning};
    LayoutDiagnosticCode code{LayoutDiagnosticCode::containing_block_not_parent};
    // The stable identities of the child the pass placed and of the
    // ancestor whose box it was placed in (the window's root control for
    // its client area); for arrange_outside_slot, of the control arranged
    // directly and of the control the pass was arranging.
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
