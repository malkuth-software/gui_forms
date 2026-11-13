#include "gui_forms/typography.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {

const char* text_resolution_status_name(TextResolutionStatus status) noexcept {
    switch (status) {
    case TextResolutionStatus::exact: return "exact";
    case TextResolutionStatus::estimated: return "estimated";
    case TextResolutionStatus::missing_primary_face:
        return "missing_primary_face";
    case TextResolutionStatus::missing_cluster_coverage:
        return "missing_cluster_coverage";
    case TextResolutionStatus::invalid_request: return "invalid_request";
    }
    return "invalid_request";
}

ResolvedTextLayout estimate_text_layout_utf8(
    std::string_view utf8, FontSpec effective_font) noexcept {
    ResolvedTextLayout result;
    result.effective_font = effective_font;
    if (!valid_font_spec(effective_font) || !validate_utf8(utf8).valid()) {
        result.status = TextResolutionStatus::invalid_request;
        return result;
    }
    std::size_t scalars{};
    for (const unsigned char byte : utf8) {
        if ((byte & 0xc0U) != 0x80U) ++scalars;
    }
    const double tracking = scalars > 1U
        ? static_cast<double>(scalars - 1U) * effective_font.letter_spacing
        : 0.0;
    result.logical_size = {
        std::max(0.0, static_cast<double>(scalars) *
                          effective_font.size * 0.55 + tracking),
        effective_font.size * 1.2};
    result.ascent = effective_font.size;
    result.descent = std::max(0.0, result.logical_size.height - result.ascent);
    result.status = TextResolutionStatus::estimated;
    return result;
}

namespace {

[[nodiscard]] bool line_space(char character) noexcept {
    return character == ' ' || character == '\t';
}

[[nodiscard]] bool continuation_byte(char character) noexcept {
    return (static_cast<unsigned char>(character) & 0xc0U) == 0x80U;
}

} // namespace

TextResolutionStatus estimate_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) noexcept {
    if (opportunities.size() != utf8.size() || !validate_utf8(utf8).valid()) {
        return TextResolutionStatus::invalid_request;
    }
    const std::size_t size = utf8.size();
    for (std::size_t index = 0U; index < size; ++index) {
        const char current = utf8[index];
        const bool last = index + 1U == size;
        const char next = last ? '\0' : utf8[index + 1U];
        LineBreakOpportunity opportunity = LineBreakOpportunity::prohibited;
        if (last) {
            opportunity = LineBreakOpportunity::mandatory;
        } else if (continuation_byte(next)) {
            opportunity = LineBreakOpportunity::inside_character;
        } else if (current == '\n' || (current == '\r' && next != '\n')) {
            opportunity = LineBreakOpportunity::mandatory;
        } else if (line_space(current) && !line_space(next) && next != '\n' && next != '\r') {
            opportunity = LineBreakOpportunity::allowed;
        }
        opportunities[index] = opportunity;
    }
    return TextResolutionStatus::estimated;
}

TextExtent text_extent_of(const ResolvedTextLayout& layout) noexcept {
    TextExtent extent{};
    extent.logical_size = layout.logical_size;
    extent.ascent = layout.ascent;
    extent.descent = layout.descent;
    extent.line_gap = layout.line_gap;
    extent.status = layout.status;
    return extent;
}

TextExtent TextMetricsProvider::measure_text_extent_utf8(
    std::string_view utf8, FontSpec effective_font) {
    const ResolvedTextLayout layout = resolve_text_layout_utf8(utf8, effective_font);
    const TextExtent extent = text_extent_of(layout);
    return extent;
}

TextResolutionStatus TextMetricsProvider::fill_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) {
    return estimate_line_break_opportunities_utf8(utf8, opportunities);
}

TextResolutionStatus TextMetricsProvider::fill_cluster_advances_utf8(
    std::string_view utf8, FontSpec effective_font, std::span<double> advances) {
    if (advances.size() != utf8.size() || !valid_font_spec(effective_font) || !validate_utf8(utf8).valid()) {
        return TextResolutionStatus::invalid_request;
    }
    // Each scalar value alone: its width, then the letter spacing a cluster
    // that is not the text's last carries.
    TextResolutionStatus status = TextResolutionStatus::exact;
    std::size_t offset = 0U;
    while (offset < utf8.size()) {
        const Utf8ScalarDecode decoded = decode_utf8_scalar(utf8, Utf8Offset(offset));
        const std::string_view scalar = utf8.substr(offset, decoded.length);
        const TextExtent extent = measure_text_extent_utf8(scalar, effective_font);
        if (static_cast<std::uint8_t>(extent.status) > static_cast<std::uint8_t>(status)) status = extent.status;
        const std::size_t next = offset + decoded.length;
        double advance = extent.logical_size.width;
        if (next < utf8.size()) advance += effective_font.letter_spacing;
        advances[offset] = advance;
        for (std::size_t index = offset + 1U; index < next; ++index) advances[index] = 0.0;
        offset = next;
    }
    return status;
}

std::unique_ptr<ShapingWorkspace> TextMetricsProvider::create_shaping_workspace() {
    return nullptr;
}

TextExtent TextMetricsProvider::shape_in_workspace_utf8(ShapingWorkspace&, std::string_view, FontSpec) {
    TextExtent refused{};
    refused.status = TextResolutionStatus::invalid_request;
    return refused;
}

TextResolutionStatus estimate_cluster_advances_utf8(
    std::string_view utf8, FontSpec effective_font, std::span<double> advances) noexcept {
    if (advances.size() != utf8.size() || !valid_font_spec(effective_font) || !validate_utf8(utf8).valid()) {
        return TextResolutionStatus::invalid_request;
    }
    // estimate_text_layout_utf8's width: 0.55 of the size per scalar value,
    // the letter spacing between them.
    const double width = effective_font.size * 0.55;
    std::size_t last_start = 0U;
    for (std::size_t index = 0U; index < utf8.size(); ++index) {
        const bool starts = (static_cast<unsigned char>(utf8[index]) & 0xc0U) != 0x80U;
        advances[index] = starts ? width + effective_font.letter_spacing : 0.0;
        if (starts) last_start = index;
    }
    if (!utf8.empty()) advances[last_start] = width;
    return TextResolutionStatus::estimated;
}

LineBoxMetrics line_box_metrics(const ResolvedTextLayout& primary,
                                std::optional<double> used_line_height) noexcept {
    FontVerticalMetrics design{primary.ascent, primary.descent, primary.line_gap, 0.0};
    if (primary.design_metrics) design = *primary.design_metrics;
    const double ascent = std::round(design.ascent);
    const double descent = std::round(design.descent);
    const double gap = std::round(design.line_gap);
    LineBoxMetrics metrics{};
    metrics.height = used_line_height ? *used_line_height : ascent + descent + gap;
    const double leading = metrics.height - (ascent + descent);
    metrics.baseline = ascent + std::floor(leading * 0.5);
    metrics.x_height = design.x_height;
    return metrics;
}

double snap_text_baseline(double logical_baseline,
                          double device_scale) noexcept {
    if (!std::isfinite(logical_baseline) || !std::isfinite(device_scale) ||
        device_scale <= 0.0) {
        return logical_baseline;
    }
    return std::round(logical_baseline * device_scale) / device_scale;
}

} // namespace gui_forms
