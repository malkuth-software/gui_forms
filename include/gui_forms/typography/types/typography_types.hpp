#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Text resolution is deliberately separate from FontSpec. FontSpec is the
// committed renderer-neutral request; this record describes what a terminal
// text engine actually measured and which bundled faces it selected.
enum class TextResolutionStatus : std::uint8_t {
    exact,
    estimated,
    missing_primary_face,
    missing_cluster_coverage,
    invalid_request,
};

struct GUI_FORMS_API ResolvedFontRun final {
    std::size_t utf8_start{};
    std::size_t utf8_length{};
    std::string family;
    std::uint16_t registered_weight{400};
    bool registered_italic{};
    bool fallback{};
    friend bool operator==(const ResolvedFontRun& left,
                           const ResolvedFontRun& right) noexcept {
        return left.utf8_start == right.utf8_start &&
               left.utf8_length == right.utf8_length &&
               left.family == right.family &&
               left.registered_weight == right.registered_weight &&
               left.registered_italic == right.registered_italic &&
               left.fallback == right.fallback;
    }
};

// A face's vertical design metrics at a size, in logical pixels: its
// ascender, descender (positive below the baseline) and line gap from the
// face's tables, scaled by size over units per em and not rounded. CSS's
// `line-height: normal` rounds each to a whole pixel and sums them, where
// ResolvedTextLayout's ascent and descent are already rounded outward.
struct GUI_FORMS_API FontVerticalMetrics final {
    double ascent{};
    double descent{};
    double line_gap{};
    // The height of a lowercase x (OS/2 sxHeight), or zero when the face
    // does not record one.
    double x_height{};
    friend bool operator==(const FontVerticalMetrics& left,
                           const FontVerticalMetrics& right) noexcept {
        return left.ascent == right.ascent && left.descent == right.descent &&
               left.line_gap == right.line_gap && left.x_height == right.x_height;
    }
};

struct GUI_FORMS_API ResolvedTextLayout final {
    FontSpec effective_font{};
    Size logical_size{};
    double ascent{};
    double descent{};
    // The preferred primary face's line gap at this size: what its line
    // height, rounded to whole pixels, adds to its rounded ascent and
    // descent, never negative. A line box is the larger of the logical
    // height and ascent + descent + line_gap, for TextBox and prepared text
    // alike.
    double line_gap{};
    // The preferred primary face's design metrics at this size, when the
    // terminal engine reads them; an estimate has none.
    std::optional<FontVerticalMetrics> design_metrics;
    std::string primary_family;
    std::vector<ResolvedFontRun> runs;
    std::size_t missing_clusters{};
    TextResolutionStatus status{TextResolutionStatus::estimated};

    [[nodiscard]] bool exact() const noexcept {
        return status == TextResolutionStatus::exact;
    }
    [[nodiscard]] double baseline() const noexcept { return ascent; }
    friend bool operator==(const ResolvedTextLayout& left,
                           const ResolvedTextLayout& right) {
        return left.effective_font == right.effective_font &&
               left.logical_size == right.logical_size &&
               left.ascent == right.ascent &&
               left.descent == right.descent &&
               left.line_gap == right.line_gap &&
               left.design_metrics == right.design_metrics &&
               left.primary_family == right.primary_family &&
               left.runs == right.runs &&
               left.missing_clusters == right.missing_clusters &&
               left.status == right.status;
    }
};

[[nodiscard]] GUI_FORMS_API const char* text_resolution_status_name(
    TextResolutionStatus status) noexcept;

// What a line may do after one byte of UTF-8 text (UAX #14's classes, as
// libunibreak reports them). A byte that does not end a scalar value is
// inside_character; the last byte of a text is always mandatory.
enum class LineBreakOpportunity : std::uint8_t {
    prohibited,
    allowed,
    mandatory,
    inside_character,
};

// A text's extent without its runs or names (D2): the part of a
// ResolvedTextLayout that measuring reads. A provider answers it with the
// same values its resolve_text_layout_utf8 reports, without copying the
// layout's strings and run vector.
struct GUI_FORMS_API TextExtent final {
    Size logical_size{};
    double ascent{};
    double descent{};
    double line_gap{};
    TextResolutionStatus status{TextResolutionStatus::estimated};
};

[[nodiscard]] GUI_FORMS_API TextExtent text_extent_of(const ResolvedTextLayout& layout) noexcept;

// Portable non-owning service seam. Implementations may use a private text
// engine, but this contract contains no HarfBuzz, FreeType, Skia, AppKit, or
// Win32 type. A Window invalidates measurement when the installed provider
// changes.
class GUI_FORMS_API TextMetricsProvider {
public:
    virtual ~TextMetricsProvider() = default;
    [[nodiscard]] virtual ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view utf8, FontSpec effective_font) = 0;
    // The extent resolve_text_layout_utf8 reports (TextExtent). The default
    // resolves the layout and reads it; a provider that keeps its layouts
    // reads the kept one.
    [[nodiscard]] virtual TextExtent measure_text_extent_utf8(
        std::string_view utf8, FontSpec effective_font);
    // Fills `opportunities`, whose size must equal utf8.size(), with the
    // line-break opportunity after each byte. Line breaking depends on the
    // text only, not on a font. Returns invalid_request, with
    // `opportunities` unchanged, for invalid UTF-8 or a size mismatch. The
    // default is estimate_line_break_opportunities_utf8; the bundled text
    // engine answers UAX #14's opportunities and returns exact.
    [[nodiscard]] virtual TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view utf8, std::span<LineBreakOpportunity> opportunities);
};

// Deterministic renderer-free line breaking: a line may end after a run of
// spaces or tabs and must end after a line feed, or a carriage return not
// followed by one. Returns estimated, or invalid_request with
// `opportunities` unchanged as TextMetricsProvider's query does.
[[nodiscard]] GUI_FORMS_API TextResolutionStatus estimate_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) noexcept;

// Deterministic renderer-free fallback. It is explicitly labelled estimated;
// callers must not report it as a resolved bundled face or a raster metric.
[[nodiscard]] GUI_FORMS_API ResolvedTextLayout estimate_text_layout_utf8(
    std::string_view utf8, FontSpec effective_font) noexcept;

// One line box of CSS text in a primary face (CSS 2.1 section 10.8 with
// `line-height: normal` as browsers compute it): its height, its baseline
// from its top, and the face's x-height.
struct GUI_FORMS_API LineBoxMetrics final {
    double height{};
    double baseline{};
    double x_height{};
    friend bool operator==(const LineBoxMetrics& left, const LineBoxMetrics& right) noexcept {
        return left.height == right.height && left.baseline == right.baseline &&
               left.x_height == right.x_height;
    }
};

// The line box of `primary` (a resolution of any text in the used font).
// The face's ascent, descent and line gap are each rounded to a whole
// logical pixel; `normal` (no used_line_height) is their sum. The leading
// (the gap, or a used line height's excess over the rounded ascent and
// descent, negative when it is less) goes half above the text, rounded
// down, and the rest below: Chromium's rounding, measured against Chrome
// 155 from 12 to 21 px (notes:planning/stages/L-layout/PLAN.md section 12.5).
// Without design metrics (an estimate) it uses the resolution's ascent,
// descent and line gap.
[[nodiscard]] GUI_FORMS_API LineBoxMetrics line_box_metrics(const ResolvedTextLayout& primary,
                                              std::optional<double> used_line_height) noexcept;

// Baselines are authored in logical coordinates. This helper rounds only the
// final placement to a device pixel and therefore does not change line
// measurement or the font identity when a window crosses displays.
[[nodiscard]] GUI_FORMS_API double snap_text_baseline(double logical_baseline,
                                        double device_scale) noexcept;

} // namespace gui_forms
