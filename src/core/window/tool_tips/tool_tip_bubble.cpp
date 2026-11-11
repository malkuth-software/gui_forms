#include "tool_tip_bubble.hpp"

#include "gui_forms/text/text_store/text_store.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>

namespace gui_forms::detail {

ToolTipLayer::ToolTipLayer(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
    // A tooltip's ink and plate are the tool_tip role's: an authored
    // ForeColor or BackColor on the window or its panels does not reach it.
    stop_inherited_colours();
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

// Writes `line` as line `filled` of `lines` (assigned into a kept string
// where there is one, so its storage is reused), counts it, and starts
// `line` again empty.
void finish_line(ToolTipLines& lines, std::size_t& filled, std::string& line) {
    if (filled < lines.lines.size()) lines.lines[filled].assign(line);
    else lines.lines.push_back(line);
    ++filled;
    line.clear();
}

// Breaks one word wider than `maximum_width` between graphemes, continuing
// `line`, which holds the start of the paragraph's current line.
void break_long_word(const Control& measurer, std::string_view word, double maximum_width,
                     ToolTipLines& lines, std::size_t& filled, std::string& line) {
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
            finish_line(lines, filled, line);
            line.assign(cluster);
        }
    }
}

// Breaks one paragraph (no line feeds) into `lines`.
void break_paragraph(const Control& measurer, std::string_view paragraph, double maximum_width,
                     ToolTipLines& lines, std::size_t& filled, std::string& line) {
    std::size_t cursor = 0U;
    while (cursor < paragraph.size()) {
        while (cursor < paragraph.size() && breaks_words(paragraph[cursor])) ++cursor;
        if (cursor >= paragraph.size()) break;
        std::size_t word_end = cursor;
        while (word_end < paragraph.size() && !breaks_words(paragraph[word_end])) ++word_end;
        const std::string_view word = paragraph.substr(cursor, word_end - cursor);
        cursor = word_end;
        if (measured_width(measurer, word, lines.font) > maximum_width) {
            break_long_word(measurer, word, maximum_width, lines, filled, line);
            continue;
        }
        const std::size_t previous_length = line.size();
        if (previous_length != 0U) line.push_back(' ');
        line.append(word);
        if (previous_length != 0U && measured_width(measurer, line, lines.font) > maximum_width) {
            line.resize(previous_length);
            finish_line(lines, filled, line);
            line.assign(word);
        }
    }
    finish_line(lines, filled, line);
}

} // namespace

void break_tool_tip_lines(const Control& measurer, std::string_view text, double maximum_width,
                          ToolTipLines& lines) {
    lines.text_width = 0.0;
    // The count of lines written so far; the kept strings past it are
    // dropped at the end.
    std::size_t filled = 0U;
    std::string& line = lines.scratch;
    line.clear();
    std::size_t paragraph_start = 0U;
    while (paragraph_start <= text.size()) {
        const std::size_t newline = text.find('\n', paragraph_start);
        const std::size_t paragraph_end = newline == std::string_view::npos ? text.size() : newline;
        break_paragraph(measurer, text.substr(paragraph_start, paragraph_end - paragraph_start),
                        maximum_width, lines, filled, line);
        if (newline == std::string_view::npos) break;
        paragraph_start = newline + 1U;
    }
    lines.lines.resize(filled);
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
    focusable = false;
}

void ToolTipBubble::set_content(const std::string_view text, ToolTipLines& lines) {
    text_.assign(text);
    std::swap(lines_, lines);
    baked_.reset();
    invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
}

Size ToolTipBubble::face_size() const noexcept {
    const Size size = tool_tip_face_size(lines_);
    return size;
}

Size tool_tip_face_size(const ToolTipLines& lines) noexcept {
    const double text_height = static_cast<double>(lines.lines.size()) * lines.line_height;
    const Size size{lines.text_width + 2.0 * ToolTipBubble::horizontal_margin,
                    text_height + 2.0 * ToolTipBubble::vertical_margin};
    return size;
}

void paint_tool_tip_bubble(Painter& painter, const Control& theme_source, const Size face_size,
                           const ToolTipLines& lines, const bool shadow) {
    const Rect face{0.0, 0.0, face_size.width, face_size.height};
    PartPaint part;
    part.family = ControlVisualRole::tool_tip;
    part.bounds = face;
    if (shadow) {
        part.role = PartRole::shadow;
        paint_part(painter, theme_source, part);
    }
    part.role = PartRole::face;
    paint_part(painter, theme_source, part);
    // Each line is a label part as wide as the text block; the block may be
    // cut off by a face narrower than it (a window narrower than the text).
    const double line_width = std::max(0.0, face_size.width - 2.0 * ToolTipBubble::horizontal_margin);
    for (std::size_t index = 0U; index < lines.lines.size(); ++index) {
        PartPaint label;
        label.family = ControlVisualRole::tool_tip;
        label.role = PartRole::label;
        label.bounds = {ToolTipBubble::horizontal_margin,
                        ToolTipBubble::vertical_margin + static_cast<double>(index) * lines.line_height,
                        line_width, lines.line_height};
        label.text = lines.lines[index];
        label.font = lines.font;
        paint_part(painter, theme_source, label);
    }
}

void ToolTipBubble::on_paint(Painter& painter, Rect) {
    const Rect arranged = committed_arranged_bounds();
    const Size face{arranged.width, arranged.height};
    // The offer is used once (set_baked), whichever way this paint goes.
    const std::shared_ptr<const PartRaster> baked = std::move(baked_);
    baked_.reset();
    const SurfaceRoot* const root = attached_surface_root();
    if (baked && root != nullptr) {
        ToolTipBakeKey current{};
        current.text = text_;
        current.face = face;
        current.font = lines_.font;
        current.shadow = shadow_;
        current.scale = (*root).scale();
        if (current == baked_key_) {
            const Insets outsets = visual_outsets();
            painter.draw_part_raster(baked, Point{-outsets.left, -outsets.top}, 1.0);
            return;
        }
    }
    paint_tool_tip_bubble(painter, *this, face, lines_, shadow_);
}

void ToolTipBubble::set_baked(std::shared_ptr<const PartRaster> raster, ToolTipBakeKey key) {
    baked_ = std::move(raster);
    baked_key_ = std::move(key);
    invalidate(Dirty::paint);
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
