#include "link_page.hpp"

#include <gui_forms/anatomy.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/theme/part_painting/part_painting.hpp>
#include <gui_forms/types/painter/painter.hpp>
#include <gui_forms/typography/types/typography_types.hpp>

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace link_page_example {

namespace {

// The page's margin around its text, and the space after each paragraph
// as a fraction of a line, in logical pixels and lines.
constexpr double page_margin = 12.0;
constexpr double paragraph_gap = 0.6;

// Orders lines by their bottoms, for the search of the first line below a
// content y (std::upper_bound).
struct AboveLineBottom final {
    template <typename Line>
    bool operator()(const double y, const Line& line) const {
        const bool above = y < line.top + line.height;
        return above;
    }
};

gui_forms::Rect offset_rect(const gui_forms::Rect rect, const gui_forms::Point offset) {
    const gui_forms::Rect result{rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
    return result;
}

} // namespace

LinkPage::LinkPage(gui_forms::StableId id) : gui_forms::ScrollableControl(std::move(id)) {
    auto_scroll = true;
}

void LinkPage::set_paragraphs(std::vector<Paragraph> paragraphs) {
    // Validate everything before changing anything.
    for (const Paragraph& paragraph : paragraphs) {
        std::size_t previous_end = 0U;
        for (const LinkSpan& link : paragraph.links) {
            if (link.begin >= link.end || link.begin < previous_end || link.end > paragraph.text.size()) {
                throw std::invalid_argument("LinkPage: a link is empty, out of order, overlapping or outside its text");
            }
            previous_end = link.end;
        }
    }
    paragraphs_ = std::move(paragraphs);
    link_addresses_.clear();
    for (std::size_t paragraph = 0U; paragraph < paragraphs_.size(); ++paragraph) {
        for (std::size_t span = 0U; span < paragraphs_[paragraph].links.size(); ++span) {
            link_addresses_.push_back(LinkAddress{paragraph, span});
        }
    }
    hovered_link_.reset();
    pressed_link_.reset();
    // A new revision is a new layout key: the next arrange flows the text.
    ++content_revision_;
    invalidate(gui_forms::Dirty::arrange | gui_forms::Dirty::paint);
}

stxlib::Optional<gui_forms::Rect> LinkPage::link_bounds(const std::size_t link) const noexcept {
    if (!laid_out_ || link >= links_.size() || links_[link].piece_count == 0U) {
        return stxlib::Optional<gui_forms::Rect>::none();
    }
    const gui_forms::Rect bounds = offset_rect(pieces_[links_[link].first_piece].bounds, content_origin());
    return stxlib::Optional<gui_forms::Rect>::of(bounds);
}

gui_forms::Size LinkPage::measure(const gui_forms::Size available) {
    // The page takes the size it is given: its content scrolls, so it asks
    // for nothing from its text.
    const gui_forms::Rect requested = requested_bounds();
    const gui_forms::Size result{requested.width > 0.0 ? requested.width : available.width,
                                 requested.height > 0.0 ? requested.height : available.height};
    return result;
}

void LinkPage::arrange(const gui_forms::Rect final_bounds) {
    // The text flows at the viewport's width. When the control's width is
    // unchanged that is the width of the last flow, and ensure_layout
    // returns at once. After a resize, start from the new width less the
    // room the vertical bar took before.
    double flow_width = final_bounds.width;
    if (laid_out_ && final_bounds.width == arranged_width_) {
        flow_width = layout_key_.flow_width;
    } else if (laid_out_ && arranged_width_ >= 0.0) {
        flow_width = std::max(0.0, final_bounds.width - (arranged_width_ - layout_key_.flow_width));
    }
    ensure_layout(flow_width);
    // The base arrange reads painted_content_extent(), shows or hides the
    // bars and keeps the scroll position within the new extent.
    gui_forms::ScrollableControl::arrange(final_bounds);
    // A bar that came or went changed the viewport's width: flow once more
    // at that width, as TextBox::ensure_multiline_layout does.
    const double viewport_width = viewport_rectangle().width;
    if (viewport_width != layout_key_.flow_width) {
        ensure_layout(viewport_width);
        gui_forms::ScrollableControl::arrange(final_bounds);
    }
    arranged_width_ = final_bounds.width;
}

gui_forms::Size LinkPage::painted_content_extent() const noexcept {
    // Wrapped text is never wider than the viewport: no horizontal extent.
    const gui_forms::Size extent{0.0, content_height_};
    return extent;
}

LinkPage::LayoutKey LinkPage::current_key(const double flow_width) const {
    const gui_forms::SurfaceRoot* root = surface_root();
    LayoutKey key{};
    key.content_revision = content_revision_;
    key.flow_width = flow_width;
    key.font = text_font(body_font_);
    key.provider = root == nullptr ? nullptr : (*root).text_metrics_provider();
    key.scale = root == nullptr ? 1.0 : (*root).scale();
    return key;
}

void LinkPage::ensure_layout(const double flow_width) {
    const LayoutKey key = current_key(flow_width);
    if (laid_out_ && key == layout_key_) return;

    ++flow_count_;
    const gui_forms::SurfaceRoot* root = surface_root();
    gui_forms::TextMetricsProvider* provider = root == nullptr ? nullptr : (*root).text_metrics_provider();
    gui_forms::ParagraphStyle style{};
    style.font = key.font;
    style.line_box = gui_forms::paragraph_line_box(provider, key.font, stxlib::Optional<double>::none());
    style.width = std::max(1.0, flow_width - 2.0 * page_margin);
    style.white_space = gui_forms::ParagraphWhiteSpace::preserve_line_ends;
    style.overflow_wrap = gui_forms::ParagraphOverflowWrap::break_word;

    lines_.clear();
    pieces_.clear();
    links_.assign(link_addresses_.size(), LaidLink{});
    double top = page_margin;
    std::size_t next_link = 0U;
    for (std::size_t paragraph = 0U; paragraph < paragraphs_.size(); ++paragraph) {
        flow_paragraph(paragraph, style, provider, top, next_link);
        top += style.line_box.height * paragraph_gap;
    }
    content_height_ = top + page_margin;
    layout_key_ = key;
    laid_out_ = true;
}

void LinkPage::flow_paragraph(const std::size_t paragraph, const gui_forms::ParagraphStyle& style,
                              gui_forms::TextMetricsProvider* provider, double& top, std::size_t& next_link) {
    const Paragraph& source = paragraphs_[paragraph];
    const std::string_view text(source.text);
    const double line_height = style.line_box.height;
    const std::size_t first_link = next_link;
    next_link += source.links.size();

    // Storage sized once for the longest paragraph and reused; a call that
    // needs more says so, and is made again with what it asked for.
    buffers_.reserve(gui_forms::paragraph_storage_bound(text, style.white_space), provider);
    gui_forms::ParagraphLayoutResult result = gui_forms::lay_out_paragraph(provider, text, style, buffers_.storage());
    const gui_forms::ParagraphStorageRequirement* required = result.required_storage();
    if (required != nullptr) {
        buffers_.reserve(*required, provider);
        result = gui_forms::lay_out_paragraph(provider, text, style, buffers_.storage());
    }
    const gui_forms::ParagraphMetrics* metrics = result.metrics();
    if (metrics == nullptr || (*metrics).line_count == 0U) {
        // Empty or refused text (not UTF-8) takes one blank line.
        top += line_height;
        return;
    }
    const std::span<const gui_forms::ParagraphLine> laid_lines = buffers_.lines((*metrics).line_count);

    std::size_t span = 0U;
    for (std::size_t index = 0U; index < laid_lines.size(); ++index) {
        const gui_forms::ParagraphLine& line = laid_lines[index];
        const std::size_t line_begin = line.bytes.start.value();
        const std::size_t line_end = line.bytes.end.value();
        LaidLine laid{};
        laid.top = top + static_cast<double>(index) * line_height;
        laid.height = line_height;
        laid.first_piece = pieces_.size();
        const double baseline = top + line.baseline;
        const double line_left = page_margin + line.x;
        // Split the line where links begin and end. A piece's left edge is
        // the width of the line's text before it, measured once here, so
        // paint and hit testing never measure.
        std::size_t position = line_begin;
        double piece_left = 0.0;
        while (position < line_end) {
            while (span < source.links.size() && source.links[span].end <= position) ++span;
            const bool in_link = span < source.links.size() && source.links[span].begin <= position;
            std::size_t piece_end = line_end;
            if (in_link) {
                piece_end = std::min(line_end, source.links[span].end);
            } else if (span < source.links.size()) {
                piece_end = std::min(line_end, source.links[span].begin);
            }
            const double piece_right = piece_end == line_end
                                           ? line.width
                                           : text_width(provider, text.substr(line_begin, piece_end - line_begin),
                                                        style.font);
            LaidPiece piece{};
            piece.bounds = gui_forms::Rect{line_left + piece_left, laid.top, piece_right - piece_left, line_height};
            piece.baseline = baseline;
            piece.paragraph = paragraph;
            piece.begin = position;
            piece.end = piece_end;
            if (in_link) {
                piece.link = first_link + span;
                LaidLink& link = links_[piece.link];
                if (link.piece_count == 0U) link.first_piece = pieces_.size();
                ++link.piece_count;
            }
            pieces_.push_back(piece);
            position = piece_end;
            piece_left = piece_right;
        }
        laid.piece_count = pieces_.size() - laid.first_piece;
        lines_.push_back(laid);
    }
    top += (*metrics).height;
}

double LinkPage::text_width(gui_forms::TextMetricsProvider* provider, const std::string_view text,
                            const gui_forms::FontSpec font) {
    if (text.empty()) return 0.0;
    if (provider == nullptr) {
        const gui_forms::ResolvedTextLayout estimate = gui_forms::estimate_text_layout_utf8(text, font);
        return estimate.logical_size.width;
    }
    const gui_forms::TextExtent extent = (*provider).measure_text_extent_utf8(text, font);
    return extent.logical_size.width;
}

gui_forms::Point LinkPage::content_origin() const noexcept {
    const gui_forms::Rect viewport = viewport_rectangle();
    const gui_forms::Point scrolled = scroll_position();
    const gui_forms::Point origin{viewport.x - scrolled.x, viewport.y - scrolled.y};
    return origin;
}

std::size_t LinkPage::first_line_below(const double y) const noexcept {
    const AboveLineBottom above{};
    const std::vector<LaidLine>::const_iterator found = std::upper_bound(lines_.begin(), lines_.end(), y, above);
    const std::size_t index = static_cast<std::size_t>(found - lines_.begin());
    return index;
}

void LinkPage::on_paint(gui_forms::Painter& painter, const gui_forms::Rect local_damage) {
    gui_forms::ScrollableControl::on_paint(painter, local_damage);
    gui_forms::PartPaint paper{};
    paper.family = gui_forms::ControlVisualRole::panel;
    paper.role = gui_forms::PartRole::face;
    paper.bounds = client_rectangle();
    paper.corner_radius.set(0.0);
    gui_forms::paint_part(painter, *this, paper);
    if (!laid_out_) return;

    // Paint reads the kept layout: only the lines the damage and the
    // viewport cross are visited, found by a binary search, and nothing is
    // measured or flowed.
    const gui_forms::Rect viewport = viewport_rectangle();
    const gui_forms::Point origin = content_origin();
    const double visible_top = std::max(viewport.y, local_damage.y) - origin.y;
    const double visible_bottom = std::min(viewport.bottom(), local_damage.bottom()) - origin.y;
    painter.save();
    painter.clip_rect(viewport);
    for (std::size_t line = first_line_below(visible_top); line < lines_.size() && lines_[line].top < visible_bottom;
         ++line) {
        const LaidLine& laid = lines_[line];
        for (std::size_t index = laid.first_piece; index < laid.first_piece + laid.piece_count; ++index) {
            const LaidPiece& piece = pieces_[index];
            const std::string_view text =
                std::string_view(paragraphs_[piece.paragraph].text).substr(piece.begin, piece.end - piece.begin);
            const gui_forms::Rect bounds = offset_rect(piece.bounds, origin);
            gui_forms::PartPaint part{};
            part.role = gui_forms::PartRole::label;
            part.family = gui_forms::ControlVisualRole::panel;
            // The font the text was flowed in, so painted text matches the layout.
            part.font = layout_key_.font;
            part.text = gui_forms::lend_paint_text(text);
            if (piece.link != no_link) {
                part.family = gui_forms::ControlVisualRole::link;
                const stxlib::Optional<std::size_t> link = stxlib::Optional<std::size_t>::of(piece.link);
                if (hovered_link_ == link) part.states.insert(gui_forms::AnatomyState::hot);
                if (pressed_link_ == link) part.states.insert(gui_forms::AnatomyState::pressed);
            }
            gui_forms::paint_text_part_at(painter, *this, part, gui_forms::Point{bounds.x, piece.baseline + origin.y},
                                          bounds.width);
            if (piece.link != no_link) {
                gui_forms::PartPaint underline{};
                underline.family = part.family;
                underline.role = gui_forms::PartRole::glyph;
                underline.mark = gui_forms::PartMark::underline;
                underline.bounds =
                    gui_forms::Rect{bounds.x, bounds.y, bounds.width, piece.baseline + origin.y + 2.0 - bounds.y};
                underline.states = part.states;
                gui_forms::paint_part(painter, *this, underline);
            }
        }
    }
    painter.restore();
}

stxlib::Optional<std::size_t> LinkPage::link_at(const gui_forms::Point local) const noexcept {
    if (!laid_out_ || !viewport_rectangle().contains(local)) return stxlib::Optional<std::size_t>::none();
    const gui_forms::Point origin = content_origin();
    const gui_forms::Point content{local.x - origin.x, local.y - origin.y};
    const std::size_t line = first_line_below(content.y);
    if (line >= lines_.size() || lines_[line].top > content.y) return stxlib::Optional<std::size_t>::none();
    const LaidLine& laid = lines_[line];
    for (std::size_t index = laid.first_piece; index < laid.first_piece + laid.piece_count; ++index) {
        const LaidPiece& piece = pieces_[index];
        if (piece.link != no_link && piece.bounds.contains(content)) {
            return stxlib::Optional<std::size_t>::of(piece.link);
        }
    }
    return stxlib::Optional<std::size_t>::none();
}

void LinkPage::damage_link(const stxlib::Optional<std::size_t>& link) {
    const std::size_t* const index = link.value();
    if (index == nullptr || *index >= links_.size()) return;
    const gui_forms::Point origin = content_origin();
    const LaidLink& laid = links_[*index];
    for (std::size_t piece = laid.first_piece; piece < laid.first_piece + laid.piece_count; ++piece) {
        invalidate(offset_rect(pieces_[piece].bounds, origin));
    }
}

void LinkPage::set_hovered_link(const stxlib::Optional<std::size_t>& link) {
    // Only a real change repaints, and only the two links involved.
    if (link == hovered_link_) return;
    damage_link(hovered_link_);
    hovered_link_ = link;
    damage_link(hovered_link_);
    cursor = link.present() ? stxlib::Optional<gui_forms::CursorKind>::of(gui_forms::CursorKind::hand)
                            : stxlib::Optional<gui_forms::CursorKind>::none();
}

void LinkPage::on_mouse_move(gui_forms::MouseEventArgs& arguments) {
    // The scroll bars first, then the page. `location` is in this
    // control's coordinates; window_position is the window's and is not
    // used for hit testing.
    scroll_pointer(arguments);
    if (!arguments.handled) set_hovered_link(link_at(arguments.location));
    gui_forms::Control::on_mouse_move(arguments);
}

void LinkPage::on_mouse_down(gui_forms::MouseEventArgs& arguments) {
    scroll_pointer(arguments);
    if (!arguments.handled && arguments.button == gui_forms::PointerButton::primary) {
        pressed_link_ = link_at(arguments.location);
        if (pressed_link_.present()) {
            damage_link(pressed_link_);
            arguments.handled = true;
        }
    }
    gui_forms::Control::on_mouse_down(arguments);
}

void LinkPage::on_mouse_up(gui_forms::MouseEventArgs& arguments) {
    scroll_pointer(arguments);
    if (!arguments.handled && arguments.button == gui_forms::PointerButton::primary && pressed_link_.present()) {
        const stxlib::Optional<std::size_t> held = pressed_link_;
        pressed_link_.reset();
        damage_link(held);
        if (link_at(arguments.location) == held) {
            arguments.handled = true;
            const std::size_t link = *held.value();
            const LinkAddress address = link_addresses_[link];
            LinkActivation activation{};
            activation.link = link;
            activation.target = paragraphs_[address.paragraph].links[address.span].target;
            link_activated_.emit(activation);
        }
    }
    gui_forms::Control::on_mouse_up(arguments);
}

void LinkPage::on_mouse_leave() {
    scroll_pointer_left();
    set_hovered_link(stxlib::Optional<std::size_t>::none());
    gui_forms::Control::on_mouse_leave();
}

} // namespace link_page_example
