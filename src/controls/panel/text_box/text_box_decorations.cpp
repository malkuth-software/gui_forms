// TextBox's styled text runs: the decoration layers, how a change of them
// repaints, the fonts they give the multiline layout, and how a decorated
// row is painted. See text_box.hpp.

#include "gui_forms/controls/text_box.hpp"

#include "../text_editing/text_editing_engine.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace gui_forms {
namespace {

// A squiggle's geometry, in logical units: the height of one wave and the
// width of each of its strokes.
constexpr double squiggle_height = 2.0;
constexpr double squiggle_step = 2.0;

// One clip of a styled run's glyphs: left of, inside or right of the
// selection band.
struct GlyphClip final {
    double left{};
    double right{};
    bool selected{};
};

} // namespace

void TextBox::require_decoration_range(Utf8Range range) const {
    const std::size_t size = (*editing_).text().size();
    if (range.start.value() > size || range.end.value() > size) {
        throw std::out_of_range("A text decoration's range lies past the text");
    }
}

void TextBox::set_decorations(TextDecorationLayer layer, std::span<const TextDecoration> decorations) {
    require_mutable();
    for (const TextDecoration& decoration : decorations) {
        require_decoration_range(decoration.range);
    }
    const TextDecorationChange change = decorations_.replace(layer, decorations);
    decorations_changed(change);
}

void TextBox::replace_decorations(TextDecorationLayer layer, Utf8Range range,
                                  std::span<const TextDecoration> decorations) {
    require_mutable();
    require_decoration_range(range);
    const TextDecorationChange change = decorations_.replace_range(layer, range, decorations);
    decorations_changed(change);
}

void TextBox::clear_decorations(TextDecorationLayer layer) {
    require_mutable();
    const TextDecorationChange change = decorations_.clear(layer);
    decorations_changed(change);
}

void TextBox::clear_all_decorations() {
    require_mutable();
    const TextDecorationChange change = decorations_.clear_all();
    decorations_changed(change);
}

void TextBox::styled_runs(Utf8Range range, std::vector<StyledTextRun>& runs) {
    require_decoration_range(range);
    decorations_.compose(range, runs);
}

void TextBox::decorations_changed(const TextDecorationChange& change) {
    // A single-line box draws no decorations, so it has nothing to repaint.
    if (!change.changed || !multiline.read()) return;
    if (change.layout) {
        // Glyphs move: the rows are measured again, the caret may move and
        // the accessibility layout changes with them.
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
        return;
    }
    const std::optional<Rect> damage = multiline_range_damage(change.range);
    if (!damage.has_value()) {
        invalidate(Dirty::paint);
        return;
    }
    if (!(*damage).empty()) invalidate(*damage);
}

std::optional<Rect> TextBox::multiline_range_damage(Utf8Range range) const {
    // Rows measured for another text, font, width or decoration layout, or a
    // scroll the next paint will make, would put the range elsewhere.
    const bool rows_current = !visual_lines_.empty() && multiline_width_ >= 0.0 &&
                              multiline_revision_ == (*editing_).store().revision() &&
                              multiline_decoration_revision_ == decorations_.layout_revision() &&
                              !reveal_pending_ &&
                              !has_dirty(dirty(), Dirty::measure | Dirty::arrange);
    if (!rows_current) return std::nullopt;
    struct BeforeVisualLine final {
        bool operator()(const std::size_t value, const VisualLine& line) const {
            const bool before = value < line.offsets.front();
            return before;
        }
    };
    const std::span<const VisualLine> rows(visual_lines_);
    const BeforeVisualLine before{};
    // The row holding the range's start, and the last row starting before
    // its end (or the first row, for an empty range).
    const std::size_t after_first = detail::upper_bound_index(rows, range.start.value(), before);
    const std::size_t first = after_first == 0U ? 0U : after_first - 1U;
    std::size_t last = first;
    if (range.end.value() > range.start.value()) {
        const std::size_t after_last = detail::upper_bound_index(rows, range.end.value() - 1U, before);
        last = std::max(first, after_last == 0U ? 0U : after_last - 1U);
    }
    const Rect text = multiline_text_rectangle();
    const Point offset = scroll_offset();
    const double top = 4.0 + static_cast<double>(first) * line_height_ - offset.y;
    const double height = static_cast<double>(last - first + 1U) * line_height_;
    const Rect band{text.x, top, text.width, height};
    const Rect damage = Rect::intersection(band, text);
    return damage;
}

TextBox::RunFont TextBox::run_font_at(std::size_t offset, std::size_t limit, FontSpec content_font) const {
    RunFont result{.font = content_font, .end = limit};
    if (styled_scratch_.empty()) return result;
    struct EndsAtOrBefore final {
        bool operator()(const StyledTextRun& run, const std::size_t value) const {
            const bool before = run.range.end.value() <= value;
            return before;
        }
    };
    const std::vector<StyledTextRun>::const_iterator holding =
        std::lower_bound(styled_scratch_.cbegin(), styled_scratch_.cend(), offset, EndsAtOrBefore{});
    if (holding == styled_scratch_.cend()) return result;
    FontSpec font = content_font;
    if ((*holding).style.weight.has_value()) font.weight = *(*holding).style.weight;
    if ((*holding).style.italic.has_value()) font.italic = *(*holding).style.italic;
    // Neighbouring runs that differ only in colour share the shaping run.
    std::vector<StyledTextRun>::const_iterator next = holding;
    std::size_t end = (*holding).range.end.value();
    for (++next; next != styled_scratch_.cend(); ++next) {
        FontSpec following = content_font;
        if ((*next).style.weight.has_value()) following.weight = *(*next).style.weight;
        if ((*next).style.italic.has_value()) following.italic = *(*next).style.italic;
        if (!(following == font)) break;
        end = (*next).range.end.value();
    }
    result.font = font;
    result.end = std::min(end, limit);
    return result;
}

PartPaint TextBox::decorated_text_part(const PartPaint& base, const TextDecorationStyle& style) const {
    PartPaint part = base;
    if (style.ink.has_value()) {
        // A theme ink replaces an overridden field's authored text colour.
        part.authored_foreground.reset();
        part.tone = PartTextTone::ordinary;
        switch (*style.ink) {
        case TextInkRole::text:
            part.family = ControlVisualRole::editor;
            break;
        case TextInkRole::muted:
            part.family = ControlVisualRole::editor;
            part.tone = PartTextTone::secondary;
            break;
        case TextInkRole::link:
            part.family = ControlVisualRole::link;
            break;
        case TextInkRole::visited_link:
            part.family = ControlVisualRole::visited_link;
            break;
        case TextInkRole::notice:
            part.family = ControlVisualRole::notice;
            break;
        default:
            // An ink outside the enumeration (a foreign cast) keeps the
            // field's own family, with the ordinary tone set above.
            break;
        }
    }
    if (style.foreground.has_value()) part.authored_foreground = *style.foreground;
    return part;
}

Color TextBox::decoration_underline_color(const PartPaint& part, const TextDecorationStyle& style) const {
    const ControlVisualContext context = part_visual_context(*this, part.states);
    // Authored colours stand outside high contrast only (T4-1).
    if (!context.high_contrast) {
        if (style.underline_color.has_value()) return *style.underline_color;
        if (part.authored_foreground.has_value()) return *part.authored_foreground;
    }
    const ResolvedControlRecipe resolved = effective_theme().resolve(*this, part.family, context, part.states);
    const ControlVisualRecipe& recipe = resolved.recipe();
    const Color ink = part.tone == PartTextTone::secondary ? recipe.muted_text : recipe.text;
    return ink;
}

void TextBox::paint_decorated_row(Painter& painter, const DecoratedRow& decorated, const PartPaint& text_part,
                                  const PartPaint& selected_part) {
    const VisualLine& row = visual_lines_[decorated.line];
    const Utf8Range window{Utf8Offset(row.offsets.front()), Utf8Offset(row.offsets.back())};
    decorations_.compose(window, styled_scratch_);
    const bool high_contrast = part_visual_context(*this, text_part.states).high_contrast;
    const std::size_t line = decorated.line;
    const double y = decorated.y;

    // Grounds first, under the selection band; high contrast leaves them
    // out, as it does every authored colour.
    if (!high_contrast) {
        for (const StyledTextRun& styled : styled_scratch_) {
            if (!styled.style.background.has_value()) continue;
            const double left = decorated.origin + multiline_boundary_x(line, styled.range.start);
            const double right = decorated.origin + multiline_boundary_x(line, styled.range.end);
            if (right <= left) continue;
            painter.fill_rect({left, y, right - left, line_height_}, *styled.style.background);
        }
    }
    if (decorated.selected) paint_selection_band(painter, decorated.band);

    // Each glyph is drawn once: every styled run under its own clip, split
    // at the selection band, whose part takes the selection's ink. The
    // row's first and last runs reach the clip's edges, so glyphs leaning
    // past either end of the row keep their ink.
    const BorrowedText source = (*editing_).text();
    const double band_left = decorated.band.x;
    const double band_right = decorated.band.x + decorated.band.width;
    for (std::size_t index = 0U; index < styled_scratch_.size(); ++index) {
        const StyledTextRun& styled = styled_scratch_[index];
        const double left = index == 0U ? decorated.clip_left
                                        : decorated.origin + multiline_boundary_x(line, styled.range.start);
        const double right = index + 1U == styled_scratch_.size()
                                 ? decorated.clip_right
                                 : decorated.origin + multiline_boundary_x(line, styled.range.end);
        if (right <= left) continue;
        const PartPaint ink = decorated_text_part(text_part, styled.style);
        std::array<GlyphClip, 3> clips{};
        std::size_t clip_count = 0U;
        if (!decorated.selected) {
            clips[0] = GlyphClip{.left = left, .right = right, .selected = false};
            clip_count = 1U;
        } else {
            clips[0] = GlyphClip{.left = left, .right = std::min(right, band_left), .selected = false};
            clips[1] = GlyphClip{.left = std::max(left, band_left), .right = std::min(right, band_right), .selected = true};
            clips[2] = GlyphClip{.left = std::max(left, band_right), .right = right, .selected = false};
            clip_count = 3U;
        }
        for (std::size_t clip = 0U; clip < clip_count; ++clip) {
            const GlyphClip& where = clips[clip];
            if (where.right <= where.left) continue;
            painter.save();
            painter.clip_rect({where.left, y, where.right - where.left, line_height_});
            for (const VisualRun& run : row.runs) {
                // Only runs reaching into the clip can leave ink there.
                const double run_left = decorated.origin + run.x;
                const double run_right = decorated.origin + multiline_boundary_x(line, Utf8Offset(run.end));
                if (run_left >= where.right + line_height_ || run_right <= where.left - line_height_) continue;
                PartPaint run_part = where.selected ? selected_part : ink;
                run_part.font = run.font;
                run_part.text = source.substr(run.start, run.end - run.start);
                const Point baseline{run_left, y + line_ascent_};
                paint_text_part_at(painter, *this, run_part, baseline, 0.0);
            }
            painter.restore();
        }
    }

    // Underlines over the glyphs, through the band too: an error marker
    // stays visible on selected text.
    const double baseline = y + line_ascent_;
    for (const StyledTextRun& styled : styled_scratch_) {
        if (!styled.style.underline.has_value() || *styled.style.underline == TextUnderline::none) continue;
        const double left = decorated.origin + multiline_boundary_x(line, styled.range.start);
        const double right = decorated.origin + multiline_boundary_x(line, styled.range.end);
        if (right <= left) continue;
        const PartPaint ink = decorated_text_part(text_part, styled.style);
        const Color colour = decoration_underline_color(ink, styled.style);
        if (*styled.style.underline == TextUnderline::single) {
            const double under = std::min(baseline + 2.0, y + line_height_ - 1.0);
            painter.draw_line({left, under}, {right, under}, colour, 1.0);
            continue;
        }
        // A squiggle: strokes of squiggle_step alternating between the top
        // and the bottom of a wave squiggle_height tall, inside the line box.
        const double top = std::min(baseline + 1.0, y + line_height_ - squiggle_height - 0.5);
        const std::size_t strokes = static_cast<std::size_t>(std::ceil((right - left) / squiggle_step));
        double x = left;
        bool rising = false;
        for (std::size_t stroke = 0U; stroke < strokes; ++stroke) {
            const double next_x = std::min(right, x + squiggle_step);
            const double from_y = rising ? top + squiggle_height : top;
            const double to_y = rising ? top : top + squiggle_height;
            painter.draw_line({x, from_y}, {next_x, to_y}, colour, 1.0);
            x = next_x;
            rising = !rising;
        }
    }
}

} // namespace gui_forms
