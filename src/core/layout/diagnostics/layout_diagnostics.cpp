// Layout diagnostics (stage L, phase L5): the severity and name of each code.

#include "gui_forms/layout/diagnostics/layout_diagnostics.hpp"

namespace gui_forms {

LayoutDiagnosticSeverity layout_diagnostic_severity(LayoutDiagnosticCode code) noexcept {
    switch (code) {
    case LayoutDiagnosticCode::containing_block_not_parent:
        return LayoutDiagnosticSeverity::warning;
    case LayoutDiagnosticCode::arrange_outside_slot:
        return LayoutDiagnosticSeverity::error;
    case LayoutDiagnosticCode::content_overflow:
        return LayoutDiagnosticSeverity::warning;
    case LayoutDiagnosticCode::layout_loop:
    case LayoutDiagnosticCode::layout_not_converged:
    case LayoutDiagnosticCode::layout_invalidated_in_measure:
    case LayoutDiagnosticCode::layout_invalidated_in_paint:
    case LayoutDiagnosticCode::scroll_in_own_arrange:
        return LayoutDiagnosticSeverity::error;
    }
    return LayoutDiagnosticSeverity::error;
}

std::string_view layout_diagnostic_code_name(LayoutDiagnosticCode code) noexcept {
    switch (code) {
    case LayoutDiagnosticCode::containing_block_not_parent:
        return "containing_block_not_parent";
    case LayoutDiagnosticCode::arrange_outside_slot:
        return "arrange_outside_slot";
    case LayoutDiagnosticCode::content_overflow:
        return "content_overflow";
    case LayoutDiagnosticCode::layout_loop:
        return "layout_loop";
    case LayoutDiagnosticCode::layout_not_converged:
        return "layout_not_converged";
    case LayoutDiagnosticCode::layout_invalidated_in_measure:
        return "layout_invalidated_in_measure";
    case LayoutDiagnosticCode::layout_invalidated_in_paint:
        return "layout_invalidated_in_paint";
    case LayoutDiagnosticCode::scroll_in_own_arrange:
        return "scroll_in_own_arrange";
    }
    return "unknown";
}

} // namespace gui_forms
