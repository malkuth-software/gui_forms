#include "ribbon_tool_tip_bubble.hpp"

#include "../../core/window/tool_tips/tool_tip_width.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

namespace gui_forms {
namespace {

// Bubble geometry in logical units at text scale 1: an 8-unit margin and a
// 32-unit image beside the description. The width follows the text by the
// tooltip width rule (tool_tip_width.hpp), as every tooltip's does.
constexpr double margin = 8.0;
constexpr double image_side = 32.0;
constexpr double image_gap = 8.0;
constexpr double shortcut_gap = 6.0;
constexpr double block_gap = 6.0;
constexpr double rule_gap = 5.0;
constexpr double minimum_text_width = 80.0;

double text_width(const Control& measurer, std::string_view text, FontSpec font) {
    if (text.empty()) return 0.0;
    const double result = measurer.measure_text_extent_utf8(text, font).logical_size.width;
    return result;
}

double line_height(const Control& measurer, FontSpec font) {
    const double result = std::max(font.size * 1.35, measurer.measure_text_extent_utf8("Mg", font).logical_size.height);
    return result;
}

// Greedy word wrap of `text` into lines no wider than `width`, honouring
// newlines (a trailing newline ends in an empty line); a single word wider
// than `width` keeps its own line. Appends to `lines` with bounds from (`x`,
// `y`), each as wide as `width`; returns the y below the last line and
// raises `widest` to the widest line's measured width. Each candidate line
// is measured whole, in one buffer reused across words.
double wrap_text(const Control& measurer, std::string_view text, FontSpec font, double x, double y,
                 double width, std::vector<RibbonToolTipLine>& lines, double& widest) {
    const double height = line_height(measurer, font);
    std::string candidate;
    std::size_t paragraph_start = 0U;
    while (paragraph_start <= text.size()) {
        std::size_t paragraph_end = text.find('\n', paragraph_start);
        if (paragraph_end == std::string_view::npos) paragraph_end = text.size();
        const std::string_view paragraph = text.substr(paragraph_start, paragraph_end - paragraph_start);
        std::string current;
        std::size_t word_start = 0U;
        while (word_start < paragraph.size()) {
            std::size_t word_end = paragraph.find(' ', word_start);
            if (word_end == std::string_view::npos) word_end = paragraph.size();
            const std::string_view word = paragraph.substr(word_start, word_end - word_start);
            candidate.assign(current);
            if (!candidate.empty()) candidate += ' ';
            candidate += word;
            if (!current.empty() && text_width(measurer, candidate, font) > width) {
                widest = std::max(widest, text_width(measurer, current, font));
                lines.push_back({Rect{x, y, width, height}, std::move(current)});
                y += height;
                current = std::string(word);
            } else {
                current.swap(candidate);
            }
            word_start = word_end + 1U;
        }
        widest = std::max(widest, text_width(measurer, current, font));
        lines.push_back({Rect{x, y, width, height}, std::move(current)});
        y += height;
        paragraph_start = paragraph_end + 1U;
    }
    return y;
}

// The widths of `text`'s paragraphs unwrapped: added to `total`, and
// `widest` raised to the widest.
void measure_paragraphs(const Control& measurer, std::string_view text, FontSpec font, double& total,
                        double& widest) {
    std::size_t paragraph_start = 0U;
    while (paragraph_start <= text.size()) {
        std::size_t paragraph_end = text.find('\n', paragraph_start);
        if (paragraph_end == std::string_view::npos) paragraph_end = text.size();
        const double width = text_width(measurer, text.substr(paragraph_start, paragraph_end - paragraph_start), font);
        total += width;
        widest = std::max(widest, width);
        paragraph_start = paragraph_end + 1U;
    }
}

} // namespace

RibbonToolTipLayout layout_ribbon_tool_tip(const Control& measurer, const RibbonToolTipBubbleContent& content,
                                           double wrap_width) {
    const double scale = measurer.effective_text_scale();
    RibbonToolTipLayout result;
    result.title_font = measurer.effective_font({FontRole::control, 10.5, 700, false, 0.12});
    result.body_font = measurer.effective_font({FontRole::control, 10.5, 400, false, 0.12});
    const double inner = margin * scale;
    const double wrap = std::max(wrap_width, minimum_text_width * scale);
    const double image_offset = content.image.value != 0U ? (image_side + image_gap) * scale : 0.0;
    const double text_left = inner;
    double y = inner;

    // The title row: the title, then the shortcut in parentheses.
    const double title_height = line_height(measurer, result.title_font);
    const double title_width = text_width(measurer, content.title, result.title_font);
    result.title = {text_left, y, title_width, title_height};
    double used = title_width;
    if (!content.shortcut.empty()) {
        const double shortcut_x = text_left + title_width + shortcut_gap * scale;
        const double shortcut_width = text_width(measurer, content.shortcut, result.body_font);
        result.shortcut = {shortcut_x, y, shortcut_width, title_height};
        used = title_width + shortcut_gap * scale + shortcut_width;
    }
    y += title_height;

    // The description, wrapped at `wrap` beside the image when there is one.
    if (!content.description.empty() || content.image.value != 0U) {
        y += block_gap * scale;
        const double description_left = text_left + image_offset;
        if (content.image.value != 0U) result.image = {text_left, y, image_side * scale, image_side * scale};
        double bottom = y;
        double widest = 0.0;
        if (!content.description.empty()) {
            bottom = wrap_text(measurer, content.description, result.body_font, description_left, y, wrap,
                               result.description, widest);
        }
        used = std::max(used, image_offset + widest);
        y = std::max(bottom, result.image.empty() ? y : result.image.y + result.image.height);
    }

    // Why the target is disabled, wrapped as wide as the description's
    // column reaches.
    if (!content.disabled_reason.empty()) {
        y += block_gap * scale;
        double widest = 0.0;
        y = wrap_text(measurer, content.disabled_reason, result.body_font, text_left, y, wrap + image_offset,
                      result.disabled_reason, widest);
        used = std::max(used, widest);
    }

    // The help line under a rule.
    if (!content.help_line.empty()) {
        y += rule_gap * scale;
        result.help_rule = {text_left, y, 0.0, 1.0};
        y += rule_gap * scale;
        const double help_height = line_height(measurer, result.body_font);
        result.help_line = {text_left, y, 0.0, help_height};
        used = std::max(used, text_width(measurer, content.help_line, result.body_font));
        y += help_height;
    }
    y += inner;

    // The face is as wide as its widest line, title row included.
    const double text_block = used;
    const double width = text_block + inner * 2.0;
    result.title.width = std::min(result.title.width, text_block);
    if (!result.shortcut.empty()) {
        result.shortcut.width = std::max(0.0, std::min(result.shortcut.width, text_left + text_block - result.shortcut.x));
    }
    for (RibbonToolTipLine& line : result.description) line.bounds.width = text_block - image_offset;
    for (RibbonToolTipLine& line : result.disabled_reason) line.bounds.width = text_block;
    result.face = {0.0, 0.0, width, y};
    result.shadow = result.face;
    if (!content.help_line.empty()) {
        result.help_rule.width = text_block;
        result.help_line.width = text_block;
    }
    result.size = {width, y};
    return result;
}

RibbonToolTipLayout fit_ribbon_tool_tip(const Control& measurer, const RibbonToolTipBubbleContent& content,
                                        double cap, double room) {
    const double scale = measurer.effective_text_scale();
    const FontSpec body_font = measurer.effective_font({FontRole::control, 10.5, 400, false, 0.12});
    const double inner = margin * scale;
    const double image_offset = content.image.value != 0U ? (image_side + image_gap) * scale : 0.0;
    const Size chrome{inner * 2.0 + image_offset, 0.0};
    const detail::ToolTipWrapRange range = detail::tool_tip_wrap_range(body_font.size, cap, room - chrome.width);
    detail::ToolTipWrapInput input{};
    measure_paragraphs(measurer, content.description, body_font, input.total_width, input.widest_width);
    measure_paragraphs(measurer, content.disabled_reason, body_font, input.total_width, input.widest_width);
    input.line_height = line_height(measurer, body_font);
    input.range = range;
    // The face's height round the wrapped text: the title row, the gaps,
    // the help line and the margins, measured from a layout at the one-line
    // width less its wrapped lines.
    RibbonToolTipLayout chosen = layout_ribbon_tool_tip(measurer, content, range.one_line);
    const std::size_t wrapped = chosen.description.size() + chosen.disabled_reason.size();
    input.chrome = {chrome.width, std::max(0.0, chosen.size.height - static_cast<double>(wrapped) * input.line_height)};
    const detail::ToolTipWrapCandidates candidates = detail::tool_tip_wrap_candidates(input);
    if (candidates.count == 1U && input.widest_width <= range.one_line) return chosen;
    double chosen_error = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < candidates.count; ++index) {
        RibbonToolTipLayout candidate = layout_ribbon_tool_tip(measurer, content, candidates.widths[index]);
        const double error = detail::tool_tip_aspect_error(candidate.size);
        if (error < chosen_error) {
            chosen_error = error;
            chosen = std::move(candidate);
        }
    }
    return chosen;
}

RibbonToolTipBubble::RibbonToolTipBubble(StableId stable_id, RibbonToolTipBubbleContent content,
                                         RibbonToolTipLayout layout)
    : Control(std::move(stable_id)), content_(std::move(content)), layout_(std::move(layout)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
}

void RibbonToolTipBubble::on_paint(Painter& painter, Rect) {
    PartPaint part;
    part.family = ControlVisualRole::tool_tip;
    part.role = PartRole::shadow;
    part.bounds = layout_.shadow;
    paint_part(painter, *this, part);
    part.role = PartRole::face;
    part.bounds = layout_.face;
    paint_part(painter, *this, part);

    part.role = PartRole::label;
    part.font = layout_.title_font;
    part.bounds = layout_.title;
    part.text = lend_paint_text(content_.title);
    paint_part(painter, *this, part);
    part.font = layout_.body_font;
    if (!layout_.shortcut.empty()) {
        part.bounds = layout_.shortcut;
        part.text = lend_paint_text(content_.shortcut);
        paint_part(painter, *this, part);
    }
    part.text = {};
    if (!layout_.image.empty()) {
        part.role = PartRole::icon;
        part.bounds = layout_.image;
        part.image = content_.image;
        paint_part(painter, *this, part);
        part.image = {};
    }
    part.role = PartRole::content;
    for (const RibbonToolTipLine& line : layout_.description) {
        part.bounds = line.bounds;
        part.text = lend_paint_text(line.text);
        paint_part(painter, *this, part);
    }
    // The reason a disabled command is unavailable reads as disabled text.
    part.states.insert(AnatomyState::disabled);
    for (const RibbonToolTipLine& line : layout_.disabled_reason) {
        part.bounds = line.bounds;
        part.text = lend_paint_text(line.text);
        paint_part(painter, *this, part);
    }
    part.states.erase(AnatomyState::disabled);
    part.text = {};
    if (!layout_.help_rule.empty()) {
        part.role = PartRole::divider;
        part.bounds = layout_.help_rule;
        paint_part(painter, *this, part);
        part.role = PartRole::content;
        part.bounds = layout_.help_line;
        part.text = lend_paint_text(content_.help_line);
        paint_part(painter, *this, part);
    }
}

Insets RibbonToolTipBubble::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

bool RibbonToolTipBubble::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor RibbonToolTipBubble::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::tool_tip;
    descriptor.name = content_.title;
    descriptor.description = content_.description;
    if (!content_.disabled_reason.empty()) {
        if (!descriptor.description.empty()) descriptor.description += ' ';
        descriptor.description += content_.disabled_reason;
    }
    descriptor.exposed = true;
    descriptor.include_descendants = false;
    return descriptor;
}

} // namespace gui_forms
