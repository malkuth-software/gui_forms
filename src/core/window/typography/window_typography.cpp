#include "gui_forms/window/window.hpp"

namespace gui_forms {

void Window::set_text_metrics_provider(TextMetricsProvider* provider) {
    require_root_thread("set_text_metrics_provider");
    if (text_metrics_provider_ == provider) return;
    text_metrics_provider_ = provider;
    if (root_ && (*root_).is_alive()) {
        (*root_).invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
                            Dirty::semantics | Dirty::accessibility);
    }
}

ResolvedTextLayout Window::resolve_text_layout_utf8(
    std::string_view utf8, FontSpec effective_font) const {
    if (!valid_font_spec(effective_font)) {
        return estimate_text_layout_utf8(utf8, effective_font);
    }
    return text_metrics_provider_ != nullptr
        ? (*text_metrics_provider_).resolve_text_layout_utf8(
              utf8, effective_font)
        : estimate_text_layout_utf8(utf8, effective_font);
}

TextResolutionStatus Window::fill_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) const {
    if (text_metrics_provider_ == nullptr) {
        return estimate_line_break_opportunities_utf8(utf8, opportunities);
    }
    const TextResolutionStatus status =
        (*text_metrics_provider_).fill_line_break_opportunities_utf8(utf8, opportunities);
    return status;
}

} // namespace gui_forms
