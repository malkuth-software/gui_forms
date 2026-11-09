#include "ribbon_tool_tip_bubble.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

namespace gui_forms {
namespace {

// Bubble geometry in logical units at text scale 1: Office 2010's enhanced
// tooltip is about 318 units wide with an 8-unit margin and a 32-unit image
// beside the description.
constexpr double margin = 8.0;
constexpr double image_side = 32.0;
constexpr double image_gap = 8.0;
constexpr double shortcut_gap = 6.0;
constexpr double block_gap = 6.0;
constexpr double rule_gap = 5.0;
constexpr double minimum_text_width = 80.0;

double text_width(const Control& measurer, std::string_view text, FontSpec font) {
    if (text.empty()) return 0.0;
    const double result = measurer.resolve_text_layout_utf8(text, font).logical_size.width;
    return result;
}

double line_height(const Control& measurer, FontSpec font) {
    const double result = std::max(font.size * 1.35, measurer.resolve_text_layout_utf8("Mg", font).logical_size.height);
    return result;
}

// Greedy word wrap of `text` into lines no wider than `width`, honouring
// newlines (a trailing newline ends in an empty line); a single word wider
// than `width` keeps its own line. Appends to `lines` with bounds from (`x`,
// `y`); returns the y below the last line. Each candidate line is measured
// whole, in one buffer reused across words.
double wrap_text(const Control& measurer, std::string_view text, FontSpec font, double x, double y,
                 double width, std::vector<RibbonToolTipLine>& lines) {
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
                lines.push_back({Rect{x, y, width, height}, std::move(current)});
                y += height;
                current = std::string(word);
            } else {
                current.swap(candidate);
            }
            word_start = word_end + 1U;
        }
        lines.push_back({Rect{x, y, width, height}, std::move(current)});
        y += height;
        paragraph_start = paragraph_end + 1U;
    }
    return y;
}

} // namespace

RibbonToolTipLayout layout_ribbon_tool_tip(const Control& measurer, const RibbonToolTipBubbleContent& content,
                                           double maximum_width) {
    const double scale = measurer.effective_text_scale();
    RibbonToolTipLayout result;
    result.title_font = measurer.effective_font({FontRole::control, 10.5, 700, false, 0.12});
    result.body_font = measurer.effective_font({FontRole::control, 10.5, 400, false, 0.12});
    const double inner = margin * scale;
    const double face_width = std::max(maximum_width, minimum_text_width * scale + inner * 2.0);
    const double text_left = inner;
    const double text_right = face_width - inner;
    double y = inner;

    // The title row: the title, then the shortcut in parentheses.
    const double title_height = line_height(measurer, result.title_font);
    const double title_width =
        std::min(text_width(measurer, content.title, result.title_font), text_right - text_left);
    result.title = {text_left, y, title_width, title_height};
    if (!content.shortcut.empty()) {
        const double shortcut_x = text_left + title_width + shortcut_gap * scale;
        const double shortcut_width =
            std::max(0.0, std::min(text_width(measurer, content.shortcut, result.body_font), text_right - shortcut_x));
        result.shortcut = {shortcut_x, y, shortcut_width, title_height};
    }
    y += title_height;

    // The description, beside the image when there is one.
    if (!content.description.empty() || content.image.value != 0U) {
        y += block_gap * scale;
        double description_left = text_left;
        if (content.image.value != 0U) {
            result.image = {text_left, y, image_side * scale, image_side * scale};
            description_left = text_left + (image_side + image_gap) * scale;
        }
        double bottom = y;
        if (!content.description.empty()) {
            bottom = wrap_text(measurer, content.description, result.body_font, description_left, y,
                               std::max(1.0, text_right - description_left), result.description);
        }
        y = std::max(bottom, result.image.empty() ? y : result.image.y + result.image.height);
    }

    // Why the target is disabled.
    if (!content.disabled_reason.empty()) {
        y += block_gap * scale;
        y = wrap_text(measurer, content.disabled_reason, result.body_font, text_left, y,
                      std::max(1.0, text_right - text_left), result.disabled_reason);
    }

    // The help line under a rule.
    if (!content.help_line.empty()) {
        y += rule_gap * scale;
        result.help_rule = {text_left, y, text_right - text_left, 1.0};
        y += rule_gap * scale;
        const double help_height = line_height(measurer, result.body_font);
        result.help_line = {text_left, y, text_right - text_left, help_height};
        y += help_height;
    }
    y += inner;

    // A tooltip with only a short title is as wide as it needs.
    double used = title_width + (content.shortcut.empty() ? 0.0 : shortcut_gap * scale + result.shortcut.width);
    if (!result.description.empty() || !result.disabled_reason.empty() || !content.help_line.empty() ||
        content.image.value != 0U) {
        used = text_right - text_left;
    }
    const double width = std::min(face_width, used + inner * 2.0);
    result.face = {0.0, 0.0, width, y};
    result.shadow = result.face;
    if (!result.help_rule.empty()) result.help_rule.width = width - inner * 2.0;
    if (!result.help_line.empty()) result.help_line.width = width - inner * 2.0;
    result.size = {width, y};
    return result;
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
    part.text = content_.title;
    paint_part(painter, *this, part);
    part.font = layout_.body_font;
    if (!layout_.shortcut.empty()) {
        part.bounds = layout_.shortcut;
        part.text = content_.shortcut;
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
        part.text = line.text;
        paint_part(painter, *this, part);
    }
    // The reason a disabled command is unavailable reads as disabled text.
    part.states.insert(AnatomyState::disabled);
    for (const RibbonToolTipLine& line : layout_.disabled_reason) {
        part.bounds = line.bounds;
        part.text = line.text;
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
        part.text = content_.help_line;
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
