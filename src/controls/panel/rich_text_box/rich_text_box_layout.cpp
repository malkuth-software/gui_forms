#include "gui_forms/controls/panel/rich_text_box/rich_text_box.hpp"

#include "../text_editing/text_editing_engine.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace gui_forms {

namespace {

// RTF's size steps against a 12-point body: 16, 20, 24, 28, 36, 48 and 64
// half-points, as multiples of the body size.
constexpr std::array<double, 7> size_step_scales{
    16.0 / 24.0, 20.0 / 24.0, 1.0, 28.0 / 24.0, 36.0 / 24.0, 48.0 / 24.0, 64.0 / 24.0};

[[nodiscard]] double size_step_scale(int step) noexcept {
    const int clamped = std::clamp(step, static_cast<int>(minimum_markdown_size_step),
                                   static_cast<int>(maximum_markdown_size_step));
    const std::size_t index = static_cast<std::size_t>(clamped - minimum_markdown_size_step);
    return size_step_scales[index];
}

// The size step a heading level is set at: level 1 at +3 down to level 6
// at -1.
[[nodiscard]] int heading_size_step(std::size_t level) noexcept {
    constexpr std::array<int, 6> steps{3, 2, 1, 0, 0, -1};
    const std::size_t index = std::clamp<std::size_t>(level, 1U, steps.size()) - 1U;
    return steps[index];
}

// Superscript and subscript are set smaller and moved off the baseline by
// a share of the size they would otherwise have.
constexpr double script_scale = 0.7;
constexpr double superscript_rise = 0.4;
constexpr double subscript_drop = 0.2;

// How far a run is raised off the baseline (negative: lowered), as a share
// of the size it would have without its baseline.
[[nodiscard]] double script_rise(MarkdownBaseline baseline, double size) noexcept {
    switch (baseline) {
    case MarkdownBaseline::superscript:
        return superscript_rise * size;
    case MarkdownBaseline::subscript:
        return -subscript_drop * size;
    case MarkdownBaseline::normal:
        return 0.0;
    }
    return 0.0;
}

// How far a row's text moves right for its alignment, in room that its
// visible text fills to `visible`.
[[nodiscard]] double alignment_shift(MarkdownAlignment alignment, double room, double visible) noexcept {
    switch (alignment) {
    case MarkdownAlignment::center:
        return std::max(0.0, (room - visible) * 0.5);
    case MarkdownAlignment::right:
        return std::max(0.0, room - visible);
    case MarkdownAlignment::left:
        return 0.0;
    }
    return 0.0;
}

[[nodiscard]] Color rgb_color(MarkdownRgb colour) noexcept {
    const Color result = Color::rgba(colour.red, colour.green, colour.blue);
    return result;
}

// Whether a block's source holds a line starting at `start`. A block with
// an empty source holds no line.
[[nodiscard]] bool block_holds(const MarkdownBlock& block, std::size_t start) noexcept {
    const std::size_t first = block.source.start.value();
    const std::size_t last = block.source.end.value();
    const bool holds = first < last && first <= start && start < last;
    return holds;
}

// Orders spans by where their source starts.
struct SpanStartsAfter final {
    bool operator()(std::size_t offset, const MarkdownSpan& span) const noexcept {
        const bool after = offset < span.source.start.value();
        return after;
    }
};

} // namespace

RichTextBox::LineBlock RichTextBox::line_block(Utf8Range range, std::size_t& block_cursor) const {
    const FontSpec body = text_font(font.read());
    LineBlock result{.font = body};
    const std::vector<MarkdownBlock>& blocks = document_.blocks;
    const std::size_t start = range.start.value();
    while (block_cursor < blocks.size() && blocks[block_cursor].source.end.value() <= start) {
        ++block_cursor;
    }
    std::size_t index = block_cursor;
    // Blocks with an empty source sit between the others: step over them.
    while (index < blocks.size() && blocks[index].source.start.value() <= start) {
        if (block_holds(blocks[index], start)) break;
        ++index;
    }
    if (index == blocks.size() || !block_holds(blocks[index], start)) return result;
    const MarkdownBlock& block = blocks[index];
    result.block = index;
    result.alignment = block.alignment;
    switch (block.kind) {
    case MarkdownBlockKind::heading:
        result.font.size *= size_step_scale(heading_size_step(block.level));
        result.font.weight = 700U;
        return result;
    case MarkdownBlockKind::code_block:
        result.font.role = FontRole::monospace;
        result.code_block = true;
        return result;
    case MarkdownBlockKind::paragraph:
    case MarkdownBlockKind::list_item:
    case MarkdownBlockKind::rule:
        return result;
    }
    return result;
}

std::size_t RichTextBox::add_piece_style(const MarkdownSpanStyle& style, bool markup, bool link, FontSpec block) {
    PieceStyle piece{.style = style, .markup = markup, .link = link, .font = block};
    piece.font.size *= size_step_scale(style.size_step);
    if (style.bold) piece.font.weight = std::max<std::uint16_t>(piece.font.weight, 700U);
    if (style.italic) piece.font.italic = true;
    if (style.code) piece.font.role = FontRole::monospace;
    piece.rise = script_rise(style.baseline, piece.font.size);
    if (style.baseline != MarkdownBaseline::normal) piece.font.size *= script_scale;
    // The line box rule TextBox uses: the shaped height, or ascent, descent
    // and the primary face's line gap, whichever is larger.
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", piece.font);
    piece.line_height = std::max(1.0, std::max(metrics.logical_size.height,
                                               metrics.ascent + metrics.descent + metrics.line_gap));
    piece.ascent = metrics.ascent > 0.0 ? metrics.ascent : piece.font.size;
    const ResolvedTextLayout tab = resolve_text_layout_utf8("    ", piece.font);
    piece.tab = std::max(1.0, tab.logical_size.width);
    piece_styles_.push_back(piece);
    const std::size_t index = piece_styles_.size() - 1U;
    return index;
}

// The line's runs: the parts its spans cover in their style, the rest as
// markup in the line's block font. A span may run over several lines (a
// code block's), so the cursor stays on a span until a later line starts
// past its end.
void RichTextBox::line_runs(Utf8Range range, const LineBlock& block, std::size_t& span_cursor) {
    runs_.clear();
    const std::vector<MarkdownSpan>& spans = document_.spans;
    const std::size_t line_start = range.start.value();
    const std::size_t line_end = range.end.value();
    while (span_cursor < spans.size() && spans[span_cursor].source.end.value() <= line_start) {
        ++span_cursor;
    }
    std::optional<std::size_t> markup_style{};
    std::size_t cursor = line_start;
    for (std::size_t index = span_cursor; index < spans.size(); ++index) {
        const MarkdownSpan& span = spans[index];
        const std::size_t start = span.source.start.value();
        const std::size_t end = span.source.end.value();
        if (start >= line_end) break;
        if (start == end || end <= cursor || span.kind != MarkdownSpanKind::text) continue;
        const std::size_t from = std::max(start, cursor);
        const std::size_t to = std::min(end, line_end);
        if (from > cursor) {
            if (!markup_style) markup_style = add_piece_style(MarkdownSpanStyle{}, true, false, block.font);
            runs_.push_back({.start = cursor, .end = from, .style = *markup_style});
        }
        const std::size_t style = add_piece_style(span.style, false, span.link.has_value(), block.font);
        runs_.push_back({.start = from, .end = to, .style = style});
        cursor = to;
    }
    if (cursor < line_end) {
        if (!markup_style) markup_style = add_piece_style(MarkdownSpanStyle{}, true, false, block.font);
        runs_.push_back({.start = cursor, .end = line_end, .style = *markup_style});
    }
}

// Splits a run of the source into cells, one per grapheme, each with its
// advance. Advances come from prefix measurement within a chunk: a run of
// text up to and including a space, at most maximum_chunk_graphemes_ long.
// A tab is a chunk of its own.
void RichTextBox::measure_run(const Run& run, std::size_t& chunk) {
    const TextStore& store = (*editing_).store();
    const std::string_view text = store.utf8();
    const FontSpec font = piece_styles_[run.style].font;
    std::size_t chunk_start = run.start;
    std::size_t chunk_graphemes = 0U;
    double prefix = 0.0;
    std::size_t cursor = run.start;
    ++chunk;
    while (cursor < run.end) {
        const Utf8Offset position(cursor);
        const char32_t scalar = store.scalar_at(position);
        const std::size_t next = std::min(store.next_grapheme_boundary(position).value(), run.end);
        Cell cell{.start = cursor, .end = next, .style = run.style, .chunk = chunk};
        if (scalar == U'\t') {
            if (chunk_graphemes > 0U) ++chunk;
            cell.chunk = chunk;
            cell.tab = true;
            cell.space = true;
            cells_.push_back(cell);
            ++chunk;
            chunk_start = next;
            chunk_graphemes = 0U;
            prefix = 0.0;
            cursor = next;
            continue;
        }
        if (chunk_graphemes == maximum_chunk_graphemes_) {
            ++chunk;
            cell.chunk = chunk;
            chunk_start = cursor;
            chunk_graphemes = 0U;
            prefix = 0.0;
        }
        const std::string_view measured = text.substr(chunk_start, next - chunk_start);
        const double width = resolve_text_layout_utf8(measured, font).logical_size.width;
        cell.advance = width - prefix;
        cell.space = scalar == U' ';
        cells_.push_back(cell);
        prefix = width;
        ++chunk_graphemes;
        cursor = next;
        if (cell.space) {
            ++chunk;
            chunk_start = next;
            chunk_graphemes = 0U;
            prefix = 0.0;
        }
    }
    ++chunk;
}

double RichTextBox::tab_advance(double x, std::size_t style) const {
    const double stop = piece_styles_[style].tab;
    const double next = (std::floor(x / stop) + 1.0) * stop;
    const double advance = next - x;
    return advance;
}

void RichTextBox::layout_line(std::size_t line, double width, std::size_t& block_cursor,
                              std::size_t& span_cursor, double& y) {
    const TextStore& store = (*editing_).store();
    const Utf8Range range = store.line_content_range(LineIndex(line));
    const LineBlock block = line_block(range, block_cursor);
    const std::size_t block_style = add_piece_style(MarkdownSpanStyle{}, true, false, block.font);
    line_runs(range, block, span_cursor);
    cells_.clear();
    std::size_t chunk = 0U;
    for (const Run& run : runs_) {
        measure_run(run, chunk);
    }
    const double available = word_wrap.read() ? std::max(1.0, width) : std::numeric_limits<double>::infinity();
    RowRequest request{.line = line, .code_block = block.code_block, .block_style = block_style,
                       .alignment = block.alignment, .width = width};
    std::size_t begin = 0U;
    std::size_t index = 0U;
    double x = 0.0;
    std::optional<std::size_t> last_break{};
    while (true) {
        if (index == cells_.size()) {
            request.begin = begin;
            request.finish = index;
            request.start_byte = begin < cells_.size() ? cells_[begin].start
                : begin > 0U ? cells_[begin - 1U].end : range.start.value();
            place_row(request, y);
            return;
        }
        Cell& cell = cells_[index];
        if (cell.tab) cell.advance = tab_advance(x, cell.style);
        // A space may hang past the edge; anything else past it wraps.
        const bool hanging = cell.space && !cell.tab;
        if (word_wrap.read() && index > begin && !hanging && x + cell.advance > available) {
            const std::size_t split = last_break && *last_break > begin ? *last_break : index;
            request.begin = begin;
            request.finish = split;
            request.start_byte = cells_[begin].start;
            place_row(request, y);
            begin = split;
            index = split;
            x = 0.0;
            last_break.reset();
            continue;
        }
        x += cell.advance;
        ++index;
        if (cell.space) last_break = index;
    }
}

void RichTextBox::place_row(const RowRequest& request, double& y) {
    const TextStore& store = (*editing_).store();
    const PieceStyle& block = piece_styles_[request.block_style];
    double ascent = block.ascent;
    double descent = block.line_height - block.ascent;
    double total = 0.0;
    double visible = 0.0;
    for (std::size_t index = request.begin; index < request.finish; ++index) {
        const Cell& cell = cells_[index];
        const PieceStyle& set = piece_styles_[cell.style];
        ascent = std::max(ascent, set.ascent + set.rise);
        descent = std::max(descent, set.line_height - set.ascent - set.rise);
        total += cell.advance;
        if (!cell.space || cell.tab) visible = total;
    }
    const double room = word_wrap.read() ? std::max(0.0, request.width) : std::max(request.width, visible);
    const double shift = alignment_shift(request.alignment, room, visible);
    Row row{.line = request.line, .code_block = request.code_block, .y = y,
            .height = ascent + descent, .baseline = ascent};
    double x = shift;
    row.offsets.push_back(request.start_byte);
    row.positions.push_back(x);
    for (std::size_t index = request.begin; index < request.finish; ++index) {
        const Cell& cell = cells_[index];
        const double left = x;
        x += cell.advance;
        if (store.is_grapheme_boundary(Utf8Offset(cell.end))) {
            row.offsets.push_back(cell.end);
            row.positions.push_back(x);
        }
        if (cell.tab) continue;
        const bool joins = !row.pieces.empty() && index > request.begin &&
                           cells_[index - 1U].chunk == cell.chunk && row.pieces.back().end == cell.start;
        if (joins) {
            row.pieces.back().end = cell.end;
        } else {
            row.pieces.push_back({.start = cell.start, .end = cell.end, .x = left, .style = cell.style});
        }
    }
    document_width_ = std::max(document_width_, x);
    y += row.height;
    rows_.push_back(std::move(row));
}

void RichTextBox::rebuild_rows(double width) {
    const FontSpec font = text_font((*this).font.read());
    const Window* owner = window();
    const double scale = owner == nullptr ? 1.0 : (*owner).scale();
    const TextMetricsProvider* provider = owner == nullptr ? nullptr : (*owner).text_metrics_provider();
    if (!rows_.empty() && layout_revision_ == content_revision_ && layout_font_ == font &&
        layout_width_ == width && layout_provider_ == provider && layout_scale_ == scale &&
        layout_wrap_ == word_wrap.read()) {
        return;
    }
    // Invalidate before rebuilding: a failure leaves the cache uncommitted.
    layout_width_ = -1.0;
    rows_.clear();
    piece_styles_.clear();
    document_width_ = 0.0;
    double y = 0.0;
    std::size_t block_cursor = 0U;
    std::size_t span_cursor = 0U;
    const std::size_t lines = (*editing_).store().line_count();
    for (std::size_t line = 0U; line < lines; ++line) {
        layout_line(line, width, block_cursor, span_cursor, y);
    }
    layout_revision_ = content_revision_;
    layout_font_ = font;
    layout_width_ = width;
    layout_provider_ = provider;
    layout_scale_ = scale;
    layout_wrap_ = word_wrap.read();
    reveal_pending_ = true;
}

double RichTextBox::document_height() const noexcept {
    if (rows_.empty()) return 0.0;
    const double height = rows_.back().y + rows_.back().height;
    return height;
}

Rect RichTextBox::text_rectangle() const noexcept {
    const Rect viewport = auto_scroll.read() ? viewport_rectangle() : local_bounds();
    const double top = bar_height() + text_top_;
    const Rect result{text_left_, top, std::max(0.0, viewport.width - 10.0),
                      std::max(0.0, viewport.height - top - text_top_)};
    return result;
}

void RichTextBox::set_scroll(Point position) {
    if (auto_scroll.read()) {
        if (word_wrap.read()) position.x = 0.0;
        static_cast<void>(scroll_to(position));
        return;
    }
    const Rect viewport = text_rectangle();
    vertical_offset_ = std::clamp(position.y, 0.0, std::max(0.0, document_height() - viewport.height));
    horizontal_offset_ = word_wrap.read() ? 0.0
        : std::clamp(position.x, 0.0, std::max(0.0, document_width_ - viewport.width + 1.0));
}

void RichTextBox::ensure_layout() {
    const Rect bounds = local_bounds();
    const Size client{bounds.width, bounds.height};
    const FontSpec font = text_font((*this).font.read());
    const Window* owner = window();
    const double scale = owner == nullptr ? 1.0 : (*owner).scale();
    const TextMetricsProvider* provider = owner == nullptr ? nullptr : (*owner).text_metrics_provider();
    if (!rows_.empty() && layout_width_ >= 0.0 && layout_revision_ == content_revision_ &&
        layout_font_ == font && layout_provider_ == provider && layout_scale_ == scale &&
        layout_wrap_ == word_wrap.read() && layout_client_ == client && layout_scroll_ == auto_scroll.read() &&
        layout_bar_ == control_bar_visible.read()) {
        return;
    }
    // As TextBox: lay out at the full width first, then once more at the
    // width a vertical bar leaves. The bar above the text takes part of
    // the viewport, so it counts towards the content's height.
    layout_client_ = {-1.0, -1.0};
    rebuild_rows(std::max(1.0, client.width - 10.0));
    bool reveal = reveal_pending_;
    if (auto_scroll.read()) {
        const double chrome = bar_height() + 2.0 * text_top_;
        const double content_width = word_wrap.read() ? 0.0 : document_width_ + 11.0;
        double content_height = document_height() + chrome;
        arrange_scroll_viewport(client, {content_width, content_height});
        const double wrap_width = std::max(1.0, text_rectangle().width);
        if (word_wrap.read() && wrap_width != layout_width_) {
            rebuild_rows(wrap_width);
            reveal = reveal || reveal_pending_;
            content_height = document_height() + chrome;
            arrange_scroll_viewport(client, {0.0, content_height});
        }
    }
    set_scroll(scroll_offset());
    reveal_pending_ = reveal;
    layout_client_ = client;
    layout_scroll_ = auto_scroll.read();
    layout_bar_ = control_bar_visible.read();
}

std::size_t RichTextBox::visual_line_count() {
    require_mutable();
    ensure_layout();
    const std::size_t count = rows_.size();
    return count;
}

std::size_t RichTextBox::caret_row() const {
    struct BeforeRow final {
        bool operator()(const std::size_t value, const Row& row) const {
            const bool before = value < row.offsets.front();
            return before;
        }
    };
    const std::span<const Row> rows(rows_);
    const BeforeRow before{};
    const TextSelection selection = (*editing_).selection();
    const std::size_t found = detail::upper_bound_index(rows, selection.caret.value(), before);
    std::size_t row = found == 0U ? 0U : found - 1U;
    if (caret_upstream_ && row > 0U && rows_[row - 1U].offsets.back() == selection.caret.value()) --row;
    return row;
}

std::size_t RichTextBox::row_at_y(double y) const {
    struct BeforeTop final {
        bool operator()(const double value, const Row& row) const {
            const bool before = value < row.y;
            return before;
        }
    };
    const std::span<const Row> rows(rows_);
    const BeforeTop before{};
    const std::size_t found = detail::upper_bound_index(rows, y, before);
    const std::size_t row = found == 0U ? 0U : found - 1U;
    return row;
}

double RichTextBox::boundary_x(const Row& row, Utf8Offset offset) const {
    const std::span<const std::size_t> offsets(row.offsets);
    const std::size_t found = detail::lower_bound_index(offsets, offset.value());
    const std::size_t index = std::min(found, row.positions.size() - 1U);
    return row.positions[index];
}

Utf8Offset RichTextBox::position_in_row(std::size_t row, double x) const {
    const Row& line = rows_[row];
    // Prefix advances need not grow monotonically in every script: the
    // nearest boundary, as TextBox finds it.
    std::size_t best{};
    double distance = std::abs(x - line.positions.front());
    for (std::size_t index = 1U; index < line.positions.size(); ++index) {
        const double candidate = std::abs(x - line.positions[index]);
        if (candidate <= distance) {
            best = index;
            distance = candidate;
        }
    }
    const Utf8Offset result(line.offsets[best]);
    return result;
}

Utf8Offset RichTextBox::position_at(double local_x, double local_y) {
    ensure_layout();
    const Point offset = scroll_offset();
    const Rect text = text_rectangle();
    const std::size_t row = row_at_y(std::max(0.0, local_y - text.y + offset.y));
    const Utf8Offset position = position_in_row(row, local_x - text_left_ + offset.x);
    next_upstream_ = position.value() == rows_[row].offsets.back();
    return position;
}

std::optional<std::string> RichTextBox::link_at(double local_x, double local_y) {
    ensure_layout();
    const Point offset = scroll_offset();
    const Rect text = text_rectangle();
    const double y = local_y - text.y + offset.y;
    if (y < 0.0 || y >= document_height()) return std::nullopt;
    const Row& row = rows_[row_at_y(y)];
    const double x = local_x - text_left_ + offset.x;
    std::optional<std::size_t> found{};
    for (std::size_t index = 1U; index < row.positions.size(); ++index) {
        const double left = std::min(row.positions[index - 1U], row.positions[index]);
        const double right = std::max(row.positions[index - 1U], row.positions[index]);
        if (x >= left && x < right) {
            found = row.offsets[index - 1U];
            break;
        }
    }
    if (!found) return std::nullopt;
    // The last span starting at or before the character, stepping back over
    // the empty ones a parser places between.
    const std::span<const MarkdownSpan> spans(document_.spans);
    const SpanStartsAfter after{};
    std::size_t index = detail::upper_bound_index(spans, *found, after);
    while (index > 0U) {
        --index;
        const MarkdownSpan& span = spans[index];
        if (span.source.start == span.source.end) continue;
        if (span.source.end.value() <= *found || !span.link) return std::nullopt;
        const std::string target = document_.links[*span.link].target;
        return target;
    }
    return std::nullopt;
}

void RichTextBox::reveal_caret() {
    const Rect viewport = text_rectangle();
    const double width = std::max(1.0, viewport.width);
    const double height = std::max(1.0, viewport.height);
    Point offset = scroll_offset();
    const bool glide = std::exchange(glide_next_reveal_, false);
    if (reveal_pending_ && !rows_.empty()) {
        const Row& row = rows_[caret_row()];
        const double x = boundary_x(row, (*editing_).selection().caret);
        if (row.y < offset.y) offset.y = row.y;
        if (row.y + row.height > offset.y + height) offset.y = row.y + row.height - height;
        if (x < offset.x) offset.x = x;
        if (x > offset.x + width - 1.0) offset.x = x - width + 1.0;
        reveal_pending_ = false;
        // A paging key's reveal glides; with reduced motion it lands at once.
        if (glide && auto_scroll.read()) {
            if (word_wrap.read()) offset.x = 0.0;
            glide_to(offset);
            return;
        }
    }
    set_scroll(offset);
}

std::optional<Rect> RichTextBox::selection_band(const Row& row) const {
    const TextSelection selection = (*editing_).selection();
    const bool selected = !selection.empty() && selection.start().value() <= row.offsets.back() &&
                          selection.end().value() > row.offsets.front();
    if (!selected) return std::nullopt;
    const double left = boundary_x(row, selection.start());
    double right = boundary_x(row, selection.end());
    if (selection.end().value() > row.offsets.back()) {
        right += std::max(3.0, text_font(font.read()).size * 0.35);
    }
    const Rect band{left, row.y, std::max(0.0, right - left), row.height};
    return band;
}

void RichTextBox::paint_piece(Painter& painter, const Row& row, const Piece& piece, const RowInks& inks,
                              bool selected) const {
    // A run is a content part: in the selection's family inside a band,
    // secondary for markup (dimmed, owner decision 2026-10-28), in its own
    // colour as an authored foreground (T4-1), or in the link family, whose
    // underline is the glyph colour's one-unit rule. Underline and strike
    // are underline marks in the run's family.
    const PieceStyle& set = piece_styles_[piece.style];
    PartPaint text;
    text.family = ControlVisualRole::editor;
    text.role = PartRole::content;
    text.states = inks.states;
    text.authored_foreground = inks.authored_text;
    if (selected) {
        text.family = ControlVisualRole::selection;
        text.states.insert(AnatomyState::selected);
        text.authored_foreground = inks.authored_selected_text;
    } else if (set.markup) {
        text.tone = PartTextTone::secondary;
        text.authored_foreground.reset();
    } else if (set.style.colour) {
        text.authored_foreground = rgb_color(*set.style.colour);
    } else if (set.link) {
        text.family = ControlVisualRole::link;
        text.authored_foreground.reset();
    }
    const double baseline = inks.origin.y + row.y + row.baseline - set.rise;
    const double left = inks.origin.x + piece.x;
    const double right = inks.origin.x + boundary_x(row, Utf8Offset(piece.end));
    text.text = (*editing_).text().substr(piece.start, piece.end - piece.start);
    text.font = set.font;
    paint_text_part_at(painter, *this, text, {left, baseline}, std::max(0.0, right - left));
    PartPaint rule;
    rule.family = text.family;
    rule.role = PartRole::glyph;
    rule.mark = PartMark::underline;
    rule.states = text.states;
    rule.authored_foreground = text.authored_foreground;
    if (set.style.underline || set.link) {
        rule.bounds = {left, baseline + 1.0, std::max(0.0, right - left), 1.0};
        paint_part(painter, *this, rule);
    }
    if (set.style.strikethrough) {
        rule.bounds = {left, baseline - set.font.size * 0.3 - 0.5, std::max(0.0, right - left), 1.0};
        paint_part(painter, *this, rule);
    }
}

Rect RichTextBox::paint_rows(Painter& painter) {
    ensure_layout();
    reveal_caret();
    const Rect viewport = text_rectangle();
    const Point offset = scroll_offset();
    const bool themed = !has_background_override() && !has_style_override();
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    RowInks inks{.origin = {text_left_ - offset.x, viewport.y - offset.y}, .states = states};
    if (!themed) {
        inks.authored_text = style().text;
        inks.authored_selected_text = style().highlight;
    }
    painter.save();
    painter.clip_rect(viewport);
    const double code_right = std::max(layout_width_, document_width_);
    const std::size_t first = rows_.empty() ? 0U : row_at_y(offset.y);
    for (std::size_t index = first; index < rows_.size(); ++index) {
        const Row& row = rows_[index];
        if (row.y - offset.y > viewport.height) break;
        const double top = inks.origin.y + row.y;
        // Code is shaded by a chip; a run's own background is an authored
        // ground on a fill (T4-1).
        PartPaint shade;
        shade.family = ControlVisualRole::panel;
        shade.role = PartRole::chip;
        shade.states = states;
        shade.corner_radius = 0.0;
        if (row.code_block) {
            shade.bounds = {inks.origin.x - 2.0, top, std::max(0.0, code_right + 4.0), row.height};
            paint_part(painter, *this, shade);
        }
        for (const Piece& piece : row.pieces) {
            const PieceStyle& set = piece_styles_[piece.style];
            const double right = boundary_x(row, Utf8Offset(piece.end));
            const Rect fill{inks.origin.x + piece.x, top, std::max(0.0, right - piece.x), row.height};
            if (set.style.background) {
                PartPaint ground;
                ground.family = ControlVisualRole::editor;
                ground.role = PartRole::fill;
                ground.bounds = fill;
                ground.states = states;
                ground.corner_radius = 0.0;
                ground.authored_background = rgb_color(*set.style.background);
                paint_part(painter, *this, ground);
            } else if (set.style.code && !row.code_block) {
                shade.bounds = fill;
                paint_part(painter, *this, shade);
            }
        }
        std::optional<Rect> band{};
        if (focused_) band = selection_band(row);
        if (band) {
            PartPaint plate;
            plate.family = ControlVisualRole::selection;
            plate.role = PartRole::selection;
            plate.bounds = {inks.origin.x + (*band).x, inks.origin.y + (*band).y, (*band).width, (*band).height};
            plate.states = states;
            plate.states.insert(AnatomyState::selected);
            if (!themed) plate.authored_background = style().accent;
            paint_part(painter, *this, plate);
        }
        if (!band) {
            for (const Piece& piece : row.pieces) {
                paint_piece(painter, row, piece, inks, false);
            }
            continue;
        }
        // Each glyph is drawn once: in the selection's ink inside the band,
        // in its own ink to either side.
        const double band_left = inks.origin.x + (*band).x;
        const double band_right = band_left + (*band).width;
        const std::array<Rect, 3> spans{{
            {viewport.x, top, std::max(0.0, band_left - viewport.x), row.height},
            {band_left, top, (*band).width, row.height},
            {band_right, top, std::max(0.0, viewport.x + viewport.width - band_right), row.height},
        }};
        for (std::size_t span = 0U; span < spans.size(); ++span) {
            if (spans[span].width <= 0.0) continue;
            painter.save();
            painter.clip_rect(spans[span]);
            for (const Piece& piece : row.pieces) {
                paint_piece(painter, row, piece, inks, span == 1U);
            }
            painter.restore();
        }
    }
    // An empty document shows the field hint while it has focus, on the
    // first row's baseline, in the muted ink placeholder text uses.
    if (focused_ && (*editing_).text().empty() && !rows_.empty()) {
        const std::string hint = field_hint_text();
        if (!hint.empty()) {
            const Point baseline{inks.origin.x, inks.origin.y + rows_[0].y + rows_[0].baseline};
            PartPaint cue;
            cue.family = ControlVisualRole::editor;
            cue.role = PartRole::content;
            cue.tone = PartTextTone::secondary;
            cue.states = states;
            cue.text = hint;
            cue.font = text_font(font.read());
            paint_text_part_at(painter, *this, cue, baseline, viewport.width);
        }
    }
    Rect caret{};
    if (focused_ && !rows_.empty()) {
        const Row& row = rows_[caret_row()];
        const double x = inks.origin.x + boundary_x(row, (*editing_).selection().caret);
        const double y = inks.origin.y + row.y;
        caret = {x - 0.5, y, 1.0, row.height};
        if (caret_visible_) {
            PartPaint caret_part;
            caret_part.family = ControlVisualRole::editor;
            caret_part.role = PartRole::caret;
            caret_part.bounds = caret;
            caret_part.states = states;
            caret_part.authored_foreground = inks.authored_text;
            paint_part(painter, *this, caret_part);
        }
    }
    painter.restore();
    const Rect visible = Rect::intersection(caret, viewport);
    return visible;
}

} // namespace gui_forms
