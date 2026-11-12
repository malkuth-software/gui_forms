// Layout diagnostics (stage L, phase L5): the severity and name of each code.

#include "gui_forms/layout/diagnostics/layout_diagnostics.hpp"

namespace gui_forms {

LayoutDiagnosticSeverity layout_diagnostic_severity(LayoutDiagnosticCode code) noexcept {
    switch (code) {
    case LayoutDiagnosticCode::containing_block_not_parent:
        return LayoutDiagnosticSeverity::warning;
    case LayoutDiagnosticCode::arrange_outside_slot:
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
    }
    return "unknown";
}

} // namespace gui_forms
