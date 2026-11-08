#include "tool_tip_bubble.hpp"

#include "gui_forms/text/text_store/text_store.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>

namespace gui_forms::detail {

ToolTipLayer::ToolTipLayer(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    set_focusable(false);
}

bool ToolTipLayer::hit_test_local(Point) const {
    return false;
}

namespace {

[[nodiscard]] double measured_width(const Control& measurer, std::string_view text, FontSpec font) {
    const ResolvedTextLayout layout = measurer.resolve_text_layout_utf8(text, font);
    return layout.logical_size.width;
}

[[nodiscard]] bool breaks_words(char value) noexcept {
    return value == ' ' || value == '\t';
}

// Appends `line` to `lines` and starts it again empty.
void finish_line(ToolTipLines& lines, std::string& line) {
    lines.lines.push_back(line);
    line.clear();
}

// Breaks one word wider than `maximum_width` between graphemes, continuing
// `line`, which holds the start of the paragraph's current line.
void break_long_word(const Control& measurer, std::string_view word, double maximum_width,
                     ToolTipLines& lines, std::string& line) {
    const TextStore store(word);
    const std::size_t graphemes = store.grapheme_count().value();
    if (!line.empty()) line.push_back(' ');
    for (std::size_t index = 0U; index < graphemes; ++index) {
        const Utf8Range range = store.grapheme_range(GraphemeIndex(index));
        const std::string_view cluster =
            word.substr(range.start.value(), range.end.value() - range.start.value());
        const std::size_t previous_length = line.size();
        line.append(cluster);
        if (previous_length != 0U && measured_width(measurer, line, lines.font) > maximum_width) {
            line.resize(previous_length);
            if (line.back() == ' ') line.pop_back();
            finish_line(lines, line);
            line.assign(cluster);
        }
    }
}

// Breaks one paragraph (no line feeds) into `lines`.
void break_paragraph(const Control& measurer, std::string_view paragraph, double maximum_width,
                     ToolTipLines& lines, std::string& line) {
    std::size_t cursor = 0U;
    while (cursor < paragraph.size()) {
        while (cursor < paragraph.size() && breaks_words(paragraph[cursor])) ++cursor;
        if (cursor >= paragraph.size()) break;
        std::size_t word_end = cursor;
        while (word_end < paragraph.size() && !breaks_words(paragraph[word_end])) ++word_end;
        const std::string_view word = paragraph.substr(cursor, word_end - cursor);
        cursor = word_end;
        if (measured_width(measurer, word, lines.font) > maximum_width) {
            break_long_word(measurer, word, maximum_width, lines, line);
            continue;
        }
        const std::size_t previous_length = line.size();
        if (previous_length != 0U) line.push_back(' ');
        line.append(word);
        if (previous_length != 0U && measured_width(measurer, line, lines.font) > maximum_width) {
            line.resize(previous_length);
            finish_line(lines, line);
            line.assign(word);
        }
    }
    finish_line(lines, line);
}

} // namespace

void break_tool_tip_lines(const Control& measurer, std::string_view text, double maximum_width,
                          ToolTipLines& lines) {
    lines.lines.clear();
    lines.text_width = 0.0;
    std::string line{};
    std::size_t paragraph_start = 0U;
    while (paragraph_start <= text.size()) {
        const std::size_t newline = text.find('\n', paragraph_start);
        const std::size_t paragraph_end = newline == std::string_view::npos ? text.size() : newline;
        break_paragraph(measurer, text.substr(paragraph_start, paragraph_end - paragraph_start),
                        maximum_width, lines, line);
        if (newline == std::string_view::npos) break;
        paragraph_start = newline + 1U;
    }
    // A line is as tall as the painter's text line (paint_text): the larger
    // of the measured height and 1.2 times the size.
    double line_height = lines.font.size * 1.2;
    for (const std::string& measured : lines.lines) {
        const ResolvedTextLayout layout = measurer.resolve_text_layout_utf8(measured, lines.font);
        lines.text_width = std::max(lines.text_width, layout.logical_size.width);
        line_height = std::max(line_height, layout.logical_size.height);
    }
    lines.line_height = line_height;
}

ToolTipBubble::ToolTipBubble(StableId stable_id, std::string text, ToolTipLines lines)
    : Control(std::move(stable_id)), text_(std::move(text)), lines_(std::move(lines)) {
    set_paint_plane(PaintPlane::overlay);
    set_focusable(false);
}

void ToolTipBubble::set_content(const std::string_view text, ToolTipLines& lines) {
    text_.assign(text);
    std::swap(lines_, lines);
    invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
}

Size ToolTipBubble::face_size() const noexcept {
    const double text_height = static_cast<double>(lines_.lines.size()) * lines_.line_height;
    const Size size{lines_.text_width + 2.0 * horizontal_margin, text_height + 2.0 * vertical_margin};
    return size;
}

void ToolTipBubble::on_paint(Painter& painter, Rect) {
    const Rect arranged = committed_arranged_bounds();
    const Rect face{0.0, 0.0, arranged.width, arranged.height};
    PartPaint part;
    part.family = ControlVisualRole::tool_tip;
    part.bounds = face;
    if (shadow_) {
        part.role = PartRole::shadow;
        paint_part(painter, *this, part);
    }
    part.role = PartRole::face;
    paint_part(painter, *this, part);
    // Each line is a label part as wide as the text block; the block may be
    // cut off by a face narrower than it (a window narrower than the text).
    const double line_width = std::max(0.0, arranged.width - 2.0 * horizontal_margin);
    for (std::size_t index = 0U; index < lines_.lines.size(); ++index) {
        PartPaint label;
        label.family = ControlVisualRole::tool_tip;
        label.role = PartRole::label;
        label.bounds = {horizontal_margin,
                        vertical_margin + static_cast<double>(index) * lines_.line_height,
                        line_width, lines_.line_height};
        label.text = lines_.lines[index];
        label.font = lines_.font;
        paint_part(painter, *this, label);
    }
}

void ToolTipBubble::set_shadow(const bool cast) noexcept {
    if (shadow_ == cast) return;
    shadow_ = cast;
    invalidate(Dirty::layout | Dirty::paint);
}

Insets ToolTipBubble::visual_outsets() const noexcept {
    if (!shadow_) return Insets{};
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

bool ToolTipBubble::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor ToolTipBubble::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::tool_tip;
    descriptor.name = text_;
    descriptor.exposed = true;
    descriptor.include_descendants = false;
    return descriptor;
}

} // namespace gui_forms::detail
