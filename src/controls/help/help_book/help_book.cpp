#include "gui_forms/controls/help_book.hpp"

#include "../../scrollable_control/scrollable_anatomy.hpp"
#include "gui_forms/anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/typography/types/typography_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Plan Paint's paper (help system study, section 3), in logical units.
constexpr double paper_left = 12.0;
constexpr double paper_top = 12.0;
constexpr double paper_right = 12.0;
constexpr double block_gap = 7.0;
constexpr double title_size = 18.0;
constexpr double body_size = 18.0;
constexpr double close_size = 24.0;
constexpr double close_title_left = 48.0;
constexpr double topic_bar_minimum = 24.0;
constexpr double topic_bar_padding = 4.0;
constexpr double expander_width = 16.0;
constexpr double list_indent = 24.0;
constexpr double quote_indent = 16.0;
constexpr double face_padding = 8.0;
constexpr double focus_inset = 1.0;
constexpr double image_gap = 4.0;
constexpr std::size_t no_text = static_cast<std::size_t>(-1);

constexpr std::array<double, 7> heading_scales{{1.0, 1.4, 1.25, 1.1, 1.0, 1.0, 1.0}};

// One stretch of a block's text as the line breaker reads it: page text (a
// run's bytes), or text of the layout's own (a list marker, a callout's
// label, printing's bracketed link target), each in one font.
struct LayoutRun final {
    std::string_view text{};
    FontSpec font{};
    std::size_t run{no_text};
    std::size_t joined_start{};
    bool link{};
    bool own{};
};

[[nodiscard]] bool line_end_byte(char character) noexcept {
    return character == '\n';
}

[[nodiscard]] std::size_t trailing_space_start(std::string_view text, std::size_t begin, std::size_t end) noexcept {
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\n' || text[end - 1U] == '\t')) --end;
    return end;
}

constexpr std::array<PartField<HelpBook::Parts>, 5> help_book_extra_fields{{
    {.declaration = {.name = "paper", .role = PartRole::face, .composed_from = {}}, .member = &HelpBook::Parts::paper},
    {.declaration = {.name = "title", .role = PartRole::label, .composed_from = {}}, .member = &HelpBook::Parts::title},
    {.declaration = {.name = "rule", .role = PartRole::divider, .composed_from = {}}, .member = &HelpBook::Parts::rule},
    {.declaration = {.name = "close", .role = PartRole::glyph, .composed_from = {}}, .member = &HelpBook::Parts::close},
    {.declaration = {.name = "body", .role = PartRole::content, .composed_from = {}}, .member = &HelpBook::Parts::body},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 5> help_book_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(help_book_extra_fields));

constexpr std::array<AnatomyState, 4> topic_states{{AnatomyState::hot, AnatomyState::pressed, AnatomyState::open,
                                                    AnatomyState::closed}};
constexpr std::array<AnatomyState, 3> link_states{{AnatomyState::hot, AnatomyState::pressed,
                                                   AnatomyState::focus_visible}};

constexpr std::array<PartField<HelpBook::TopicParts>, 2> topic_part_fields{{
    {.declaration = {.name = "topic_bar", .role = PartRole::face, .composed_from = {}},
     .member = &HelpBook::TopicParts::topic_bar},
    {.declaration = {.name = "expander", .role = PartRole::disclosure, .composed_from = {}},
     .member = &HelpBook::TopicParts::expander},
}};
constexpr std::array<PartField<HelpBook::LinkParts>, 1> link_part_fields{{
    {.declaration = {.name = "link_text", .role = PartRole::label, .composed_from = {}},
     .member = &HelpBook::LinkParts::link_text},
}};
constexpr std::array<PartDeclaration, 2> topic_parts = part_declarations(topic_part_fields);
constexpr std::array<PartDeclaration, 1> link_parts = part_declarations(link_part_fields);

constexpr std::array<ItemSystemDeclaration, 2> help_book_item_systems{{
    {.name = "topic", .parts = topic_parts, .states = topic_states, .custom_states = {}},
    {.name = "link", .parts = link_parts, .states = link_states, .custom_states = {}},
}};

constexpr ControlAnatomy help_book_anatomy{
    .control_id = "help_book",
    .parts = help_book_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = help_book_item_systems,
};

// The events HelpBook adds to those of ScrollableControl, in declaration
// order.
constexpr std::array<InteractionDeclaration, 3> help_book_interactions{{
    {.name = "link_activated", .payload = "HelpLinkActivation", .mutable_payload = true},
    {.name = "close_requested", .payload = "", .mutable_payload = false},
    {.name = "section_toggled", .payload = "HelpSectionToggle", .mutable_payload = false},
}};

} // namespace

HelpBook::HelpBook(StableId stable_id) : ScrollableControl(std::move(stable_id)) {
    auto_scroll = true;
    focusable = true;
    tab_stop = true;
}

// --- Content -------------------------------------------------------------------------

void HelpBook::show_page(HelpPageModel page, std::string_view anchor) {
    page_ = std::move(page);
    open_sections_.assign(page_.sections.size(), 0U);
    for (std::size_t index = 0U; index < page_.sections.size(); ++index) {
        open_sections_[index] = page_.sections[index].initially_open ? 1U : 0U;
    }
    hovered_link_.reset();
    hovered_section_.reset();
    pressed_link_.reset();
    pressed_section_.reset();
    focused_stop_.reset();
    relayout();
    static_cast<void>(scroll_to(Point{0.0, 0.0}));
    if (!anchor.empty()) static_cast<void>(scroll_to_anchor(anchor));
    invalidate(Dirty::paint | Dirty::semantics);
}

bool HelpBook::show_record(const HelpBookSection& section, std::string_view stable_id, std::string_view anchor) {
    const std::optional<std::size_t> record = section.find(stable_id);
    if (!record.has_value()) return false;
    HelpPageModel page{};
    static_cast<void>(build_help_page(section, *record, page));
    show_page(std::move(page), anchor);
    return true;
}

bool HelpBook::scroll_to_anchor(std::string_view anchor) {
    const std::optional<std::size_t> block = page_.find_anchor(anchor);
    if (!block.has_value()) return false;
    // Open every section around the block, innermost outward.
    bool opened = false;
    std::optional<std::size_t> section = page_.blocks[*block].section;
    while (section.has_value()) {
        if (open_sections_[*section] == 0U) {
            open_sections_[*section] = 1U;
            opened = true;
        }
        const std::size_t heading = page_.sections[*section].heading;
        section = page_.blocks[heading].section;
    }
    if (opened) relayout();
    const std::optional<double> top = block_top(*block);
    if (!top.has_value()) return false;
    static_cast<void>(scroll_to(Point{0.0, std::max(0.0, *top - paper_top)}));
    invalidate(Dirty::paint);
    return true;
}

void HelpBook::set_section_open(std::size_t section, bool open) {
    if (section >= open_sections_.size()) return;
    const std::uint8_t wanted = open ? 1U : 0U;
    if (open_sections_[section] == wanted) return;
    open_sections_[section] = wanted;
    relayout();
    invalidate(Dirty::paint | Dirty::semantics);
    const HelpSectionToggle toggle{section, open};
    section_toggled_.emit(toggle);
}

bool HelpBook::section_open(std::size_t section) const {
    return section < open_sections_.size() && open_sections_[section] != 0U;
}

void HelpBook::set_close_visible(bool visible) {
    if (close_visible_ == visible) return;
    close_visible_ = visible;
    relayout();
    invalidate(Dirty::paint | Dirty::semantics);
}

void HelpBook::set_images(std::vector<HelpBookImage> images) {
    images_ = std::move(images);
    relayout();
    invalidate(Dirty::paint);
}

void HelpBook::set_labels(HelpBookLabels labels) {
    labels_ = std::move(labels);
    relayout();
    invalidate(Dirty::paint | Dirty::semantics);
}

void HelpBook::set_highlight(std::vector<std::string> folded_words) {
    highlight_ = std::move(folded_words);
    relayout();
    invalidate(Dirty::paint);
}

void HelpBook::set_visited(std::vector<std::string> targets) {
    visited_ = std::move(targets);
    std::sort(visited_.begin(), visited_.end());
    invalidate(Dirty::paint);
}

void HelpBook::mark_visited(std::string_view target) {
    const std::vector<std::string>::iterator found =
        std::lower_bound(visited_.begin(), visited_.end(), target, std::less<>());
    if (found != visited_.end() && *found == target) return;
    visited_.insert(found, std::string(target));
    invalidate(Dirty::paint);
}

bool HelpBook::visited(std::string_view target) const noexcept {
    return std::binary_search(visited_.begin(), visited_.end(), target, std::less<>());
}

// --- Layout ---------------------------------------------------------------------------

FontSpec HelpBook::body_font() const noexcept {
    const FontSpec own{FontRole::control, body_size, 400, false};
    const FontSpec resolved = text_font(own);
    return resolved;
}

FontSpec HelpBook::heading_font(std::size_t level) const noexcept {
    FontSpec font = body_font();
    const std::size_t clamped = std::min<std::size_t>(level, heading_scales.size() - 1U);
    font.size = font.size * heading_scales[clamped];
    font.weight = 700;
    return font;
}

FontSpec HelpBook::run_font(const HelpPageRun& run, FontSpec base) const noexcept {
    FontSpec font = base;
    if (run.code) font.role = FontRole::monospace;
    if (run.bold) font.weight = 700;
    if (run.italic) font.italic = true;
    return font;
}

double HelpBook::text_width(std::string_view text, FontSpec font) const {
    if (text.empty()) return 0.0;
    const ResolvedTextLayout layout = resolve_text_layout_utf8(text, font);
    return layout.logical_size.width;
}

const HelpBookImage* HelpBook::find_image(std::string_view name) const noexcept {
    for (const HelpBookImage& image : images_) {
        if (image.name == name) return &image;
    }
    return nullptr;
}

bool HelpBook::block_shown(std::size_t block, LayoutMode mode) const noexcept {
    if (mode.print) return true;
    std::optional<std::size_t> section = page_.blocks[block].section;
    while (section.has_value()) {
        if (open_sections_[*section] == 0U) return false;
        section = page_.blocks[page_.sections[*section].heading].section;
    }
    return true;
}

std::string_view HelpBook::piece_text(const LaidOut& laid, std::size_t piece) const noexcept {
    const LaidPiece& laid_piece = laid.pieces[piece];
    if (laid_piece.own != no_text) {
        const std::string_view own(laid.marker_texts[laid_piece.own]);
        return own.substr(laid_piece.begin, laid_piece.end - laid_piece.begin);
    }
    const std::string_view text(page_.runs[laid_piece.run].text);
    return text.substr(laid_piece.begin, laid_piece.end - laid_piece.begin);
}

void HelpBook::lay_out_runs(std::size_t block_index, double left, double width, double& top, FontSpec base,
                            LayoutMode mode, LaidOut& laid) const {
    const HelpPageBlock& block = page_.blocks[block_index];
    LaidBlock& laid_block = laid.blocks.back();
    // The layout runs: the page's runs in order, with printing's bracketed
    // target title after each link that ends.
    std::vector<LayoutRun> runs;
    runs.reserve(block.run_count + 2U);
    std::string joined;
    for (std::size_t index = 0U; index < block.run_count; ++index) {
        const std::size_t run_index = block.first_run + index;
        const HelpPageRun& run = page_.runs[run_index];
        LayoutRun layout_run{};
        layout_run.text = run.line_break ? std::string_view("\n") : std::string_view(run.text);
        layout_run.font = run_font(run, base);
        layout_run.run = run.line_break ? no_text : run_index;
        layout_run.joined_start = joined.size();
        layout_run.link = run.link.has_value();
        joined.append(layout_run.text);
        runs.push_back(layout_run);
        const bool link_ends = run.link.has_value() &&
                               (index + 1U == block.run_count || page_.runs[run_index + 1U].link != run.link);
        if (mode.print && link_ends) {
            const HelpPageLink& link = page_.links[*run.link];
            if (!link.target_title.empty()) {
                LayoutRun suffix{};
                // The bracketed title is the layout's own text.
                laid.marker_texts.push_back(" [" + link.target_title + "]");
                suffix.text = std::string_view(laid.marker_texts.back());
                suffix.font = base;
                suffix.joined_start = joined.size();
                suffix.own = true;
                suffix.run = laid.marker_texts.size() - 1U;
                joined.append(suffix.text);
                runs.push_back(suffix);
            }
        }
    }
    // marker_texts may have grown: the views of own runs point into strings
    // whose storage moved only if the vector reallocated, so take them again.
    for (LayoutRun& run : runs) {
        if (run.own) run.text = std::string_view(laid.marker_texts[run.run]);
    }
    std::vector<LineBreakOpportunity> opportunities(joined.size(), LineBreakOpportunity::prohibited);
    if (!joined.empty()) {
        static_cast<void>(fill_line_break_opportunities_utf8(joined, std::span<LineBreakOpportunity>(opportunities)));
    }
    for (std::size_t index = 0U; index < joined.size(); ++index) {
        if (line_end_byte(joined[index])) opportunities[index] = LineBreakOpportunity::mandatory;
    }
    const bool wraps = block.kind != HelpPageBlockKind::code;
    const LineBoxMetrics metrics = line_box_metrics(resolve_text_layout_utf8(" ", base), std::nullopt);
    std::string folded;
    std::vector<std::string_view> words;
    double line_x = 0.0;
    std::size_t line_first_piece = laid.pieces.size();
    // Closes the open line: places its pieces at `top` and aligns them.
    struct LineCloser final {
        const HelpBook* book{};
        const HelpPageBlock* block{};
        LaidOut* laid{};
        double left{};
        double width{};
        double height{};
        double baseline{};

        void operator()(double& line_top, double& x, std::size_t& first) const {
            const std::size_t end = (*laid).pieces.size();
            double used = 0.0;
            for (std::size_t piece = first; piece < end; ++piece) {
                used = std::max(used, (*laid).pieces[piece].bounds.right() - left);
            }
            double shift = 0.0;
            if ((*block).alignment == MarkdownAlignment::center) shift = std::max(0.0, (width - used) * 0.5);
            if ((*block).alignment == MarkdownAlignment::right) shift = std::max(0.0, width - used);
            for (std::size_t piece = first; piece < end; ++piece) {
                LaidPiece& laid_piece = (*laid).pieces[piece];
                laid_piece.bounds.x += shift;
                laid_piece.bounds.y = line_top;
                laid_piece.bounds.height = height;
                laid_piece.baseline = line_top + baseline;
            }
            (*laid).lines.push_back(LaidLine{line_top, height});
            line_top += height;
            x = 0.0;
            first = end;
        }
    };
    const LineCloser close_line{this, &block, &laid, left, width, metrics.height, metrics.baseline};
    std::size_t position = 0U;
    bool line_open = false;
    while (position < joined.size()) {
        // The segment up to the next break opportunity.
        std::size_t end = position + 1U;
        while (end < joined.size() && opportunities[end - 1U] != LineBreakOpportunity::allowed &&
               opportunities[end - 1U] != LineBreakOpportunity::mandatory) {
            ++end;
        }
        const bool forced = opportunities[end - 1U] == LineBreakOpportunity::mandatory;
        const std::size_t content_end = trailing_space_start(joined, position, end);
        std::size_t visible_end = end;
        while (visible_end > position && line_end_byte(joined[visible_end - 1U])) --visible_end;
        // The segment's content width (its trailing spaces hang), summed
        // over the runs it crosses, decides whether it fits.
        double content_width = 0.0;
        for (const LayoutRun& run : runs) {
            const std::size_t run_begin = run.joined_start;
            const std::size_t run_end = run.joined_start + run.text.size();
            const std::size_t begin = std::max(run_begin, position);
            const std::size_t stop = std::min(run_end, visible_end);
            if (begin >= stop) continue;
            const std::size_t content_stop = std::min(stop, content_end);
            if (content_stop > begin) {
                content_width += text_width(run.text.substr(begin - run_begin, content_stop - begin), run.font);
            }
        }
        if (wraps && line_open && line_x + content_width > width + 0.001) {
            close_line(top, line_x, line_first_piece);
            line_open = false;
        }
        for (const LayoutRun& run : runs) {
            const std::size_t run_begin = run.joined_start;
            const std::size_t run_end = run.joined_start + run.text.size();
            const std::size_t begin = std::max(run_begin, position);
            const std::size_t stop = std::min(run_end, visible_end);
            if (begin >= stop) continue;
            const double piece_width = text_width(run.text.substr(begin - run_begin, stop - begin), run.font);
            LaidPiece piece{};
            piece.font = run.font;
            if (run.own) {
                piece.kind = PieceKind::label;
                piece.own = run.run;
                piece.begin = begin - run_begin;
                piece.end = stop - run_begin;
            } else {
                piece.kind = run.link ? PieceKind::link : PieceKind::text;
                piece.run = run.run;
                piece.begin = begin - run_begin;
                piece.end = stop - run_begin;
            }
            piece.bounds = Rect{left + line_x, top, piece_width, metrics.height};
            if (!highlight_.empty() && !run.own) {
                fold_help_text(run.text.substr(begin - run_begin, stop - begin), folded);
                split_help_words(folded, words);
                for (const std::string_view word : words) {
                    for (const std::string& wanted : highlight_) {
                        if (word.substr(0U, wanted.size()) == wanted) piece.highlighted = true;
                    }
                }
            }
            laid.pieces.push_back(piece);
            line_x += piece_width;
            line_open = true;
        }
        if (forced) {
            if (!line_open) {
                // An empty line still takes its height.
                laid.lines.push_back(LaidLine{top, metrics.height});
                top += metrics.height;
            } else {
                close_line(top, line_x, line_first_piece);
                line_open = false;
            }
        }
        position = end;
    }
    if (line_open) close_line(top, line_x, line_first_piece);
    laid_block.piece_count = laid.pieces.size() - laid_block.first_piece;
}

void HelpBook::lay_out_block(std::size_t block_index, double left, double width, double& top, LayoutMode mode,
                             LaidOut& laid) const {
    const HelpPageBlock& block = page_.blocks[block_index];
    LaidBlock laid_block{};
    laid_block.block = block_index;
    laid_block.first_piece = laid.pieces.size();
    const double block_top = top;
    const double indent = static_cast<double>(block.quote_depth) * quote_indent;
    double content_left = left + indent;
    double content_width = std::max(0.0, width - indent);
    std::optional<std::size_t> collapsible{};
    for (std::size_t index = 0U; index < page_.sections.size(); ++index) {
        if (page_.sections[index].heading == block_index) collapsible = index;
    }
    laid_block.section = collapsible;
    laid.blocks.push_back(laid_block);

    if (block.kind == HelpPageBlockKind::rule) {
        laid.blocks.back().bounds = Rect{content_left, top, content_width, 1.0};
        laid.lines.push_back(LaidLine{top, 1.0});
        top += 1.0;
        return;
    }
    if (block.kind == HelpPageBlockKind::image && block.image.has_value()) {
        const HelpPageImage& page_image = page_.images[*block.image];
        const HelpBookImage* image = find_image(page_image.source);
        if (image != nullptr && (*image).size.width > 0.0 && (*image).size.height > 0.0) {
            const double scale = std::min(1.0, content_width / (*image).size.width);
            const Rect box{content_left, top, (*image).size.width * scale, (*image).size.height * scale};
            laid.blocks.back().image = box;
            laid.blocks.back().bounds = box;
            laid.lines.push_back(LaidLine{top, box.height});
            top += box.height + image_gap;
            return;
        }
        // A missing image shows its alternative text as a paragraph.
    }
    FontSpec base = body_font();
    if (block.kind == HelpPageBlockKind::heading) base = heading_font(block.level);
    if (block.kind == HelpPageBlockKind::code) base.role = FontRole::monospace;

    if (collapsible.has_value() && !mode.print) {
        // The heading is the text of a topic bar: the expander, then the
        // heading's runs, at least topic_bar_minimum tall.
        const double bar_top = top;
        const double text_left = content_left + topic_bar_padding + expander_width + topic_bar_padding;
        double text_top = top + topic_bar_padding;
        lay_out_runs(block_index, text_left, std::max(0.0, content_width - (text_left - content_left)), text_top,
                     base, mode, laid);
        const double bar_height = std::max(topic_bar_minimum, text_top - bar_top + topic_bar_padding);
        LaidBlock& bar_block = laid.blocks.back();
        bar_block.bar = Rect{content_left, bar_top, content_width, bar_height};
        bar_block.expander =
            Rect{content_left + topic_bar_padding, bar_top + (bar_height - expander_width) * 0.5, expander_width,
                 expander_width};
        bar_block.bounds = bar_block.bar;
        // Centre the text in the bar.
        const double slack = bar_height - (text_top - bar_top + topic_bar_padding);
        if (slack > 0.0) {
            for (std::size_t piece = bar_block.first_piece; piece < laid.pieces.size(); ++piece) {
                laid.pieces[piece].bounds.y += slack * 0.5;
                laid.pieces[piece].baseline += slack * 0.5;
            }
        }
        top = bar_top + bar_height;
        return;
    }

    const bool faced = block.callout != HelpCalloutKind::none || block.kind == HelpPageBlockKind::code;
    if (faced) {
        content_left += face_padding;
        content_width = std::max(0.0, content_width - 2.0 * face_padding);
        top += face_padding;
    }
    if (block.callout != HelpCalloutKind::none) {
        // The callout's one-word label, bold, on its own line.
        std::string_view label = labels_.note;
        if (block.callout == HelpCalloutKind::tip) label = labels_.tip;
        if (block.callout == HelpCalloutKind::caution) label = labels_.caution;
        FontSpec label_font = body_font();
        label_font.weight = 700;
        const LineBoxMetrics metrics = line_box_metrics(resolve_text_layout_utf8(" ", label_font), std::nullopt);
        laid.marker_texts.push_back(std::string(label));
        LaidPiece piece{};
        piece.kind = PieceKind::label;
        piece.own = laid.marker_texts.size() - 1U;
        piece.begin = 0U;
        piece.end = label.size();
        piece.font = label_font;
        piece.bounds = Rect{content_left, top, text_width(label, label_font), metrics.height};
        piece.baseline = top + metrics.baseline;
        laid.pieces.push_back(piece);
        laid.lines.push_back(LaidLine{top, metrics.height});
        top += metrics.height;
    }
    if (block.kind == HelpPageBlockKind::list_item) {
        // The marker hangs in the indent: a bullet, or the item's number.
        const double item_indent = list_indent * static_cast<double>(block.level + 1U);
        std::string marker = block.marker == MarkdownListMarker::ordered ? std::to_string(block.ordinal) + "." : "\xE2\x80\xA2";
        const LineBoxMetrics metrics = line_box_metrics(resolve_text_layout_utf8(" ", base), std::nullopt);
        const double marker_width = text_width(marker, base);
        laid.marker_texts.push_back(std::move(marker));
        LaidPiece piece{};
        piece.kind = PieceKind::marker;
        piece.own = laid.marker_texts.size() - 1U;
        piece.begin = 0U;
        piece.end = laid.marker_texts.back().size();
        piece.font = base;
        piece.bounds = Rect{content_left + item_indent - marker_width - 6.0, top, marker_width, metrics.height};
        piece.baseline = top + metrics.baseline;
        laid.pieces.push_back(piece);
        content_left += item_indent;
        content_width = std::max(0.0, content_width - item_indent);
    }
    // lay_out_runs counts the block's pieces from first_piece; the label and
    // marker above belong to the block too.
    lay_out_runs(block_index, content_left, content_width, top, base, mode, laid);
    LaidBlock& done = laid.blocks.back();
    if (faced) {
        top += face_padding;
        done.face = Rect{left + indent, block_top, std::max(0.0, width - indent), top - block_top};
    }
    if (block.kind == HelpPageBlockKind::heading && block.level <= 1U) {
        // A level-1 heading is ruled like the title.
        done.face = Rect{left, top + 2.0, width, 1.0};
        top += 3.0;
    }
    done.bounds = Rect{left + indent, block_top, std::max(0.0, width - indent), top - block_top};
}

void HelpBook::lay_out(double width, LayoutMode mode, LaidOut& laid) const {
    laid.width = width;
    laid.blocks.clear();
    laid.pieces.clear();
    laid.lines.clear();
    laid.marker_texts.clear();
    laid.stops.clear();
    const double content_width = std::max(0.0, width - paper_left - paper_right);
    double top = paper_top;
    // The close "x" at the upper left, then the title and its rule.
    const bool close_shown = close_visible_ && !mode.print;
    const double title_left = close_shown ? close_title_left : paper_left;
    FontSpec title_font = text_font(FontSpec{FontRole::control, title_size, 400, false});
    const LineBoxMetrics title_metrics =
        line_box_metrics(resolve_text_layout_utf8(" ", title_font), std::nullopt);
    laid.close = close_shown ? Rect{paper_left, top, close_size, close_size} : Rect{};
    const double title_height = std::max(title_metrics.height, close_shown ? close_size : 0.0);
    laid.title = Rect{title_left, top, std::max(0.0, width - paper_right - title_left), title_height};
    laid.title_baseline = top + (title_height - title_metrics.height) * 0.5 + title_metrics.baseline;
    laid.lines.push_back(LaidLine{top, title_height});
    top += title_height + 2.0;
    laid.rule = Rect{paper_left, top, content_width, 1.0};
    top += 1.0 + block_gap;
    const double body_top = top;
    for (std::size_t index = 0U; index < page_.blocks.size(); ++index) {
        if (!block_shown(index, mode)) continue;
        lay_out_block(index, paper_left, content_width, top, mode, laid);
        top += block_gap;
    }
    laid.body = Rect{paper_left, body_top, content_width, std::max(0.0, top - body_top)};
    laid.height = top + paper_top;
    // The focus stops in reading order: each section's bar and each link
    // (once, at its first piece).
    std::vector<std::size_t> seen_links;
    for (const LaidBlock& laid_block : laid.blocks) {
        if (laid_block.section.has_value() && !mode.print) {
            laid.stops.push_back(FocusStop{true, *laid_block.section});
        }
        for (std::size_t piece = laid_block.first_piece; piece < laid_block.first_piece + laid_block.piece_count;
             ++piece) {
            const LaidPiece& laid_piece = laid.pieces[piece];
            if (laid_piece.kind != PieceKind::link) continue;
            const std::size_t link = *page_.runs[laid_piece.run].link;
            if (std::find(seen_links.begin(), seen_links.end(), link) != seen_links.end()) continue;
            seen_links.push_back(link);
            laid.stops.push_back(FocusStop{false, link});
        }
    }
}

void HelpBook::relayout() {
    const Rect viewport = viewport_rectangle();
    const double width = viewport.width > 0.0 ? viewport.width : client_rectangle().width;
    lay_out(width, LayoutMode{false}, layout_);
    if (focused_stop_.has_value() && *focused_stop_ >= layout_.stops.size()) focused_stop_.reset();
}

void HelpBook::arrange(Rect final_bounds) {
    ScrollableControl::arrange(final_bounds);
    relayout();
    arrange_scroll_viewport(Size{final_bounds.width, final_bounds.height}, Size{layout_.width, layout_.height});
    // A vertical bar that appeared or went narrows the viewport: lay out
    // again at the width it leaves.
    const Rect viewport = viewport_rectangle();
    if (viewport.width > 0.0 && std::abs(viewport.width - layout_.width) > 0.5) {
        relayout();
        arrange_scroll_viewport(Size{final_bounds.width, final_bounds.height}, Size{layout_.width, layout_.height});
    }
}

Size HelpBook::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size result{requested.width > 0.0 ? requested.width : std::min(available.width, 480.0),
                      requested.height > 0.0 ? requested.height : std::min(available.height, 360.0)};
    return result;
}

std::optional<double> HelpBook::block_top(std::size_t block) const noexcept {
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (laid_block.block == block) return laid_block.bounds.y;
    }
    return std::nullopt;
}

Point HelpBook::content_offset() const noexcept {
    const Rect viewport = viewport_rectangle();
    const Point scrolled = scroll_position();
    return Point{viewport.x - scrolled.x, viewport.y - scrolled.y};
}

std::optional<Rect> HelpBook::link_bounds(std::size_t link) const noexcept {
    const Point offset = content_offset();
    std::optional<Rect> result{};
    for (const LaidPiece& piece : layout_.pieces) {
        if (piece.kind != PieceKind::link || page_.runs[piece.run].link != link) continue;
        const Rect moved{piece.bounds.x + offset.x, piece.bounds.y + offset.y, piece.bounds.width,
                         piece.bounds.height};
        result = result.has_value() ? Rect::united(*result, moved) : moved;
    }
    return result;
}

std::optional<Rect> HelpBook::topic_bar_bounds(std::size_t section) const noexcept {
    const Point offset = content_offset();
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (laid_block.section != section) continue;
        const Rect bar{laid_block.bar.x + offset.x, laid_block.bar.y + offset.y, laid_block.bar.width,
                       laid_block.bar.height};
        return bar;
    }
    return std::nullopt;
}

// --- Paint ----------------------------------------------------------------------------

void HelpBook::paint_laid(Painter& painter, const LaidOut& laid, Point offset, LayoutMode mode) const {
    const std::size_t focused_link = focused_stop_.has_value() && !layout_.stops[*focused_stop_].section
                                         ? layout_.stops[*focused_stop_].index
                                         : static_cast<std::size_t>(-1);
    const std::size_t focused_section = focused_stop_.has_value() && layout_.stops[*focused_stop_].section
                                            ? layout_.stops[*focused_stop_].index
                                            : static_cast<std::size_t>(-1);
    // The title and its rule.
    if (!page_.title.empty()) {
        PartPaint title{};
        title.family = ControlVisualRole::tool_tip;
        title.role = PartRole::label;
        title.font = text_font(FontSpec{FontRole::control, title_size, 400, false});
        title.text = lend_paint_text(page_.title);
        // A fallback page's title is drawn in the secondary tone: the page
        // came, at least in part, from the program's built-in language.
        title.tone = page_.fallback ? PartTextTone::secondary : PartTextTone::ordinary;
        title.key = PartKey{"title", {}, {}};
        paint_text_part_at(painter, *this, title, Point{laid.title.x + offset.x, laid.title_baseline + offset.y},
                           laid.title.width);
    }
    PartPaint rule{};
    rule.family = ControlVisualRole::tool_tip;
    rule.role = PartRole::divider;
    rule.bounds = Rect{laid.rule.x + offset.x, laid.rule.y + offset.y, laid.rule.width, laid.rule.height};
    rule.key = PartKey{"rule", {}, {}};
    paint_part(painter, *this, rule);
    if (!mode.print && close_visible_) {
        PartPaint close{};
        close.family = ControlVisualRole::tool_tip;
        close.role = PartRole::glyph;
        close.mark = PartMark::close;
        close.bounds = Rect{laid.close.x + offset.x, laid.close.y + offset.y, laid.close.width, laid.close.height};
        if (close_hovered_) close.states.insert(AnatomyState::hot);
        close.key = PartKey{"close", {}, {}};
        paint_part(painter, *this, close);
    }
    for (const LaidBlock& laid_block : laid.blocks) {
        const HelpPageBlock& block = page_.blocks[laid_block.block];
        if (laid_block.bar.width > 0.0 && laid_block.section.has_value()) {
            const std::size_t section = *laid_block.section;
            AnatomyStateSet states{};
            states.insert(section_open(section) ? AnatomyState::open : AnatomyState::closed);
            if (hovered_section_ == section) states.insert(AnatomyState::hot);
            if (pressed_section_ == section) states.insert(AnatomyState::pressed);
            PartPaint bar{};
            bar.family = ControlVisualRole::menu_item;
            bar.role = PartRole::face;
            bar.bounds = Rect{laid_block.bar.x + offset.x, laid_block.bar.y + offset.y, laid_block.bar.width,
                              laid_block.bar.height};
            bar.states = states;
            bar.corner_radius = 0.0;
            bar.key = PartKey{"topic_bar", "topic", section};
            paint_part(painter, *this, bar);
            PartPaint expander{};
            expander.family = ControlVisualRole::menu_item;
            expander.role = PartRole::disclosure;
            expander.mark = section_open(section) ? PartMark::chevron_down : PartMark::chevron_right;
            expander.bounds = Rect{laid_block.expander.x + offset.x, laid_block.expander.y + offset.y,
                                   laid_block.expander.width, laid_block.expander.height};
            expander.states = states;
            expander.key = PartKey{"expander", "topic", section};
            paint_part(painter, *this, expander);
            if (focused_section == section && focused_) {
                PartPaint ring{};
                ring.family = ControlVisualRole::menu_item;
                ring.role = PartRole::focus;
                ring.bounds = Rect{bar.bounds.x + focus_inset, bar.bounds.y + focus_inset,
                                   bar.bounds.width - 2.0 * focus_inset, bar.bounds.height - 2.0 * focus_inset};
                ring.states.insert(AnatomyState::focus_visible);
                paint_part(painter, *this, ring);
            }
        }
        if (laid_block.face.width > 0.0 && laid_block.face.height > 0.0) {
            const Rect face{laid_block.face.x + offset.x, laid_block.face.y + offset.y, laid_block.face.width,
                            laid_block.face.height};
            if (block.kind == HelpPageBlockKind::heading) {
                PartPaint heading_rule{};
                heading_rule.family = ControlVisualRole::tool_tip;
                heading_rule.role = PartRole::divider;
                heading_rule.bounds = face;
                paint_part(painter, *this, heading_rule);
            } else if (block.kind == HelpPageBlockKind::code && block.callout == HelpCalloutKind::none) {
                PartPaint well{};
                well.family = ControlVisualRole::editor;
                well.role = PartRole::well;
                well.bounds = face;
                paint_part(painter, *this, well);
            } else {
                // A callout: darker paper with an edge (the card family
                // until the theme gives help-book callouts).
                PartPaint callout{};
                callout.family = ControlVisualRole::card;
                callout.role = PartRole::face;
                callout.bounds = face;
                callout.corner_radius = 0.0;
                paint_part(painter, *this, callout);
            }
        }
        if (block.kind == HelpPageBlockKind::rule) {
            PartPaint divider{};
            divider.family = ControlVisualRole::tool_tip;
            divider.role = PartRole::divider;
            divider.bounds = Rect{laid_block.bounds.x + offset.x, laid_block.bounds.y + offset.y,
                                  laid_block.bounds.width, laid_block.bounds.height};
            paint_part(painter, *this, divider);
        }
        if (laid_block.image.width > 0.0 && block.image.has_value()) {
            const HelpBookImage* image = find_image(page_.images[*block.image].source);
            if (image != nullptr) {
                PartPaint picture{};
                picture.family = ControlVisualRole::tool_tip;
                picture.role = PartRole::image;
                picture.image = (*image).image;
                picture.bounds = Rect{laid_block.image.x + offset.x, laid_block.image.y + offset.y,
                                      laid_block.image.width, laid_block.image.height};
                paint_part(painter, *this, picture);
            }
        }
        for (std::size_t index = laid_block.first_piece; index < laid_block.first_piece + laid_block.piece_count;
             ++index) {
            const LaidPiece& piece = laid.pieces[index];
            const std::string_view text = piece_text(laid, index);
            const Rect bounds{piece.bounds.x + offset.x, piece.bounds.y + offset.y, piece.bounds.width,
                              piece.bounds.height};
            if (piece.highlighted && !mode.print) {
                PartPaint marker{};
                marker.family = ControlVisualRole::selection;
                marker.role = PartRole::selection;
                marker.bounds = bounds;
                marker.states.insert(AnatomyState::selected);
                paint_part(painter, *this, marker);
            }
            PartPaint part{};
            part.role = PartRole::label;
            part.font = piece.font;
            part.text = lend_paint_text(text);
            part.family = ControlVisualRole::tool_tip;
            if (piece.kind == PieceKind::link) {
                const std::size_t link = *page_.runs[piece.run].link;
                const bool was_visited = visited(page_.links[link].target);
                part.family = was_visited ? ControlVisualRole::visited_link : ControlVisualRole::link;
                if (hovered_link_ == link) part.states.insert(AnatomyState::hot);
                if (pressed_link_ == link) part.states.insert(AnatomyState::pressed);
                part.key = PartKey{"link_text", "link", link};
            }
            paint_text_part_at(painter, *this, part, Point{bounds.x, piece.baseline + offset.y}, bounds.width);
            if (piece.kind == PieceKind::link) {
                const std::size_t link = *page_.runs[piece.run].link;
                PartPaint underline{};
                underline.family = part.family;
                underline.role = PartRole::glyph;
                underline.mark = PartMark::underline;
                underline.bounds = Rect{bounds.x, bounds.y, bounds.width, piece.baseline + offset.y + 2.0 - bounds.y};
                underline.states = part.states;
                paint_part(painter, *this, underline);
                if (focused_link == link && focused_ && !mode.print) {
                    PartPaint ring{};
                    ring.family = part.family;
                    ring.role = PartRole::focus;
                    ring.bounds = bounds;
                    ring.states.insert(AnatomyState::focus_visible);
                    paint_part(painter, *this, ring);
                }
            } else if (piece.own == no_text && piece.run < page_.runs.size()) {
                const HelpPageRun& run = page_.runs[piece.run];
                if (run.underline || run.strikethrough) {
                    PartPaint line{};
                    line.family = ControlVisualRole::tool_tip;
                    line.role = PartRole::glyph;
                    line.mark = PartMark::underline;
                    const double bottom = run.strikethrough ? bounds.y + bounds.height * 0.5 + 1.0
                                                            : piece.baseline + offset.y + 2.0;
                    line.bounds = Rect{bounds.x, bounds.y, bounds.width, bottom - bounds.y};
                    paint_part(painter, *this, line);
                }
            }
        }
    }
}

void HelpBook::on_paint(Painter& painter, Rect) {
    const Rect bounds = client_rectangle();
    PartPaint paper{};
    paper.family = ControlVisualRole::tool_tip;
    paper.role = PartRole::face;
    paper.bounds = bounds;
    paper.corner_radius = 0.0;
    paper.key = PartKey{"paper", {}, {}};
    paint_part(painter, *this, paper);
    const Rect viewport = viewport_rectangle();
    painter.save();
    painter.clip_rect(viewport);
    paint_laid(painter, layout_, content_offset(), LayoutMode{false});
    painter.restore();
    if (focused_ && !focused_stop_.has_value() && bounds.width > 4.0 && bounds.height > 4.0) {
        PartPaint ring{};
        ring.family = ControlVisualRole::tool_tip;
        ring.role = PartRole::focus;
        ring.bounds = Rect{1.5, 1.5, bounds.width - 3.0, bounds.height - 3.0};
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
}

// --- Printing ---------------------------------------------------------------------------

double HelpBook::layout_for_print(double width) {
    lay_out(width, LayoutMode{true}, print_layout_);
    return print_layout_.height;
}

void HelpBook::print_break_positions(std::vector<double>& positions) const {
    positions.clear();
    for (const LaidLine& line : print_layout_.lines) positions.push_back(line.top);
    std::sort(positions.begin(), positions.end());
    const std::vector<double>::iterator last = std::unique(positions.begin(), positions.end());
    positions.erase(last, positions.end());
}

void HelpBook::paint_print_band(Painter& painter, Point origin, double top, double height) const {
    painter.save();
    painter.clip_rect(Rect{origin.x, origin.y, print_layout_.width, height});
    paint_laid(painter, print_layout_, Point{origin.x, origin.y - top}, LayoutMode{true});
    painter.restore();
}

// --- Input ------------------------------------------------------------------------------

std::optional<std::size_t> HelpBook::link_at(Point local) const noexcept {
    const Point offset = content_offset();
    const Point content{local.x - offset.x, local.y - offset.y};
    if (!viewport_rectangle().contains(local)) return std::nullopt;
    for (const LaidPiece& piece : layout_.pieces) {
        if (piece.kind == PieceKind::link && piece.bounds.contains(content)) return page_.runs[piece.run].link;
    }
    return std::nullopt;
}

std::optional<std::size_t> HelpBook::section_bar_at(Point local) const noexcept {
    const Point offset = content_offset();
    const Point content{local.x - offset.x, local.y - offset.y};
    if (!viewport_rectangle().contains(local)) return std::nullopt;
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (laid_block.section.has_value() && laid_block.bar.contains(content)) return laid_block.section;
    }
    return std::nullopt;
}

void HelpBook::toggle_section(std::size_t section) {
    set_section_open(section, !section_open(section));
}

bool HelpBook::activate_link(std::size_t link) {
    if (link >= page_.links.size()) return false;
    const HelpPageLink& page_link = page_.links[link];
    HelpLinkActivation activation{};
    activation.page = page_.stable_id;
    activation.kind = page_link.kind;
    activation.target = page_link.target;
    activation.fragment = page_link.fragment;
    const std::optional<Rect> local = link_bounds(link);
    if (local.has_value()) {
        const Rect absolute = absolute_bounds();
        activation.bounds = Rect{absolute.x + (*local).x, absolute.y + (*local).y, (*local).width, (*local).height};
    }
    if (page_link.kind == HelpLinkKind::topic || page_link.kind == HelpLinkKind::popup) mark_visited(page_link.target);
    link_activated_.emit(activation);
    return activation.handled;
}

void HelpBook::activate_stop(const FocusStop& stop) {
    if (stop.section) {
        toggle_section(stop.index);
        return;
    }
    static_cast<void>(activate_link(stop.index));
}

void HelpBook::reveal_rect(Rect content_rect) {
    const Rect viewport = viewport_rectangle();
    const Point scrolled = scroll_position();
    double target = scrolled.y;
    if (content_rect.y < scrolled.y) target = content_rect.y;
    if (content_rect.bottom() > scrolled.y + viewport.height) target = content_rect.bottom() - viewport.height;
    if (target != scrolled.y) static_cast<void>(scroll_to(Point{scrolled.x, std::max(0.0, target)}));
}

bool HelpBook::move_focus(bool forward) {
    if (layout_.stops.empty()) return false;
    std::size_t next = 0U;
    if (!focused_stop_.has_value()) {
        next = forward ? 0U : layout_.stops.size() - 1U;
    } else if (forward) {
        if (*focused_stop_ + 1U >= layout_.stops.size()) return false;
        next = *focused_stop_ + 1U;
    } else {
        if (*focused_stop_ == 0U) return false;
        next = *focused_stop_ - 1U;
    }
    focused_stop_ = next;
    const FocusStop& stop = layout_.stops[next];
    const Point offset = content_offset();
    const std::optional<Rect> local = stop.section ? topic_bar_bounds(stop.index) : link_bounds(stop.index);
    if (local.has_value()) {
        reveal_rect(Rect{(*local).x - offset.x, (*local).y - offset.y, (*local).width, (*local).height});
    }
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void HelpBook::on_pointer(PointerEvent& event) {
    ScrollableControl::on_pointer(event);
    if (event.handled) return;
    const Point local = event.position;
    if (event.action == PointerAction::move) {
        const std::optional<std::size_t> link = link_at(local);
        const std::optional<std::size_t> section = link.has_value() ? std::nullopt : section_bar_at(local);
        const bool over_close = close_visible_ && Rect{layout_.close.x + content_offset().x,
                                                       layout_.close.y + content_offset().y, layout_.close.width,
                                                       layout_.close.height}
                                                      .contains(local);
        if (link != hovered_link_ || section != hovered_section_ || over_close != close_hovered_) {
            hovered_link_ = link;
            hovered_section_ = section;
            close_hovered_ = over_close;
            cursor = (link.has_value() || section.has_value() || over_close) ? std::optional<CursorKind>(CursorKind::hand)
                                                                             : std::optional<CursorKind>();
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::leave) {
        if (hovered_link_.has_value() || hovered_section_.has_value() || close_hovered_) {
            hovered_link_.reset();
            hovered_section_.reset();
            close_hovered_ = false;
            cursor = std::optional<CursorKind>();
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        pressed_link_ = link_at(local);
        pressed_section_ = pressed_link_.has_value() ? std::nullopt : section_bar_at(local);
        if (pressed_link_.has_value() || pressed_section_.has_value() || close_hovered_) {
            event.handled = true;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::up) {
        const std::optional<std::size_t> link = link_at(local);
        const std::optional<std::size_t> section = section_bar_at(local);
        const std::optional<std::size_t> pressed_link = pressed_link_;
        const std::optional<std::size_t> pressed_section = pressed_section_;
        pressed_link_.reset();
        pressed_section_.reset();
        invalidate(Dirty::paint);
        if (pressed_link.has_value() && link == pressed_link) {
            event.handled = true;
            static_cast<void>(activate_link(*pressed_link));
            return;
        }
        if (pressed_section.has_value() && section == pressed_section) {
            event.handled = true;
            toggle_section(*pressed_section);
            return;
        }
        if (close_hovered_) {
            event.handled = true;
            close_requested_.emit();
        }
    }
}

void HelpBook::on_key(KeyEvent& event) {
    ScrollableControl::on_key(event);
    if (event.handled || event.action != KeyAction::down) return;
    const Rect viewport = viewport_rectangle();
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    const double line = body_size * 1.5;
    const Point scrolled = scroll_position();
    if (event.physical_key == PhysicalKey::tab && !has_modifier(event.modifiers, Modifier::control)) {
        if (move_focus(!shift)) event.handled = true;
        return;
    }
    if ((event.physical_key == PhysicalKey::enter || event.physical_key == PhysicalKey::space) &&
        focused_stop_.has_value()) {
        event.handled = true;
        const FocusStop stop = layout_.stops[*focused_stop_];
        activate_stop(stop);
        return;
    }
    double target = scrolled.y;
    if (event.physical_key == PhysicalKey::down) target += line;
    else if (event.physical_key == PhysicalKey::up) target -= line;
    else if (event.physical_key == PhysicalKey::page_down) target += std::max(line, viewport.height - line);
    else if (event.physical_key == PhysicalKey::page_up) target -= std::max(line, viewport.height - line);
    else if (event.physical_key == PhysicalKey::home) target = 0.0;
    else if (event.physical_key == PhysicalKey::end) target = layout_.height;
    else return;
    event.handled = true;
    const double limit = std::max(0.0, layout_.height - viewport.height);
    static_cast<void>(scroll_to(Point{scrolled.x, std::clamp(target, 0.0, limit)}));
    invalidate(Dirty::paint);
}

void HelpBook::on_focus_changed(bool focused) {
    ScrollableControl::on_focus_changed(focused);
    focused_ = focused;
    if (!focused) focused_stop_.reset();
    invalidate(Dirty::paint);
}

// --- Semantics ---------------------------------------------------------------------------

SemanticDescriptor HelpBook::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    // There is no document role: the book is a pane named by its title,
    // whose children are its headings (static text with a level) and links.
    descriptor.role = SemanticRole::pane;
    descriptor.name = accessible_name.read().empty() ? page_.title : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> HelpBook::semantic_virtual_children() const {
    std::vector<SemanticNode> children = ScrollableControl::semantic_virtual_children();
    const Point offset = content_offset();
    const std::string prefix = std::string(stable_id().value());
    for (const LaidBlock& laid_block : layout_.blocks) {
        const HelpPageBlock& block = page_.blocks[laid_block.block];
        if (block.kind == HelpPageBlockKind::heading) {
            SemanticNode heading{};
            heading.stable_id = prefix + ".heading-" + std::to_string(laid_block.block);
            heading.role = laid_block.section.has_value() ? SemanticRole::button : SemanticRole::static_text;
            for (std::size_t run = 0U; run < block.run_count; ++run) heading.name.append(page_.runs[block.first_run + run].text);
            heading.level = block.level;
            heading.bounds = Rect{laid_block.bounds.x + offset.x, laid_block.bounds.y + offset.y,
                                  laid_block.bounds.width, laid_block.bounds.height};
            heading.states = SemanticState::enabled | SemanticState::visible;
            if (laid_block.section.has_value()) {
                heading.actions.push_back(section_open(*laid_block.section) ? SemanticAction::collapse
                                                                            : SemanticAction::expand);
            }
            children.push_back(std::move(heading));
        }
        if (block.kind == HelpPageBlockKind::image && block.image.has_value()) {
            SemanticNode image{};
            image.stable_id = prefix + ".image-" + std::to_string(laid_block.block);
            image.role = SemanticRole::image;
            image.name = page_.images[*block.image].alternative;
            image.bounds = Rect{laid_block.bounds.x + offset.x, laid_block.bounds.y + offset.y,
                                laid_block.bounds.width, laid_block.bounds.height};
            image.states = SemanticState::enabled | SemanticState::visible;
            children.push_back(std::move(image));
        }
    }
    for (const FocusStop& stop : layout_.stops) {
        if (stop.section) continue;
        SemanticNode link{};
        link.stable_id = prefix + ".link-" + std::to_string(stop.index);
        link.role = SemanticRole::link;
        for (const LaidPiece& piece : layout_.pieces) {
            if (piece.kind == PieceKind::link && page_.runs[piece.run].link == stop.index) {
                link.name.append(page_.runs[piece.run].text.substr(piece.begin, piece.end - piece.begin));
            }
        }
        link.value = page_.links[stop.index].target;
        const std::optional<Rect> bounds = link_bounds(stop.index);
        if (bounds.has_value()) link.bounds = *bounds;
        link.states = SemanticState::enabled | SemanticState::visible;
        link.actions.push_back(SemanticAction::press);
        children.push_back(std::move(link));
    }
    return children;
}

bool HelpBook::on_semantic_child_action(std::string_view child, SemanticAction action, std::string_view value) {
    const std::string prefix = std::string(stable_id().value());
    const std::string link_prefix = prefix + ".link-";
    const std::string heading_prefix = prefix + ".heading-";
    if (child.substr(0U, link_prefix.size()) == link_prefix && action == SemanticAction::press) {
        const std::string_view number = child.substr(link_prefix.size());
        std::size_t link = 0U;
        for (const char digit : number) {
            if (digit < '0' || digit > '9') return false;
            link = link * 10U + static_cast<std::size_t>(digit - '0');
        }
        static_cast<void>(activate_link(link));
        return true;
    }
    if (child.substr(0U, heading_prefix.size()) == heading_prefix &&
        (action == SemanticAction::expand || action == SemanticAction::collapse)) {
        const std::string_view number = child.substr(heading_prefix.size());
        std::size_t block = 0U;
        for (const char digit : number) {
            if (digit < '0' || digit > '9') return false;
            block = block * 10U + static_cast<std::size_t>(digit - '0');
        }
        for (std::size_t section = 0U; section < page_.sections.size(); ++section) {
            if (page_.sections[section].heading != block) continue;
            set_section_open(section, action == SemanticAction::expand);
            return true;
        }
        return false;
    }
    return ScrollableControl::on_semantic_child_action(child, action, value);
}

// --- Anatomy -------------------------------------------------------------------------------

const ControlAnatomy& HelpBook::anatomy() const noexcept {
    return help_book_anatomy;
}

void HelpBook::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();
    const Point offset = content_offset();
    record.paper = PartGeometry::from_bounds(client_rectangle());
    if (!page_.title.empty()) {
        record.title = PartGeometry::from_bounds(
            Rect{layout_.title.x + offset.x, layout_.title.y + offset.y, layout_.title.width, layout_.title.height});
    }
    record.rule = PartGeometry::from_bounds(
        Rect{layout_.rule.x + offset.x, layout_.rule.y + offset.y, layout_.rule.width, layout_.rule.height});
    if (close_visible_) {
        record.close = PartGeometry::from_bounds(
            Rect{layout_.close.x + offset.x, layout_.close.y + offset.y, layout_.close.width, layout_.close.height});
    }
    record.body = PartGeometry::from_bounds(
        Rect{layout_.body.x + offset.x, layout_.body.y + offset.y, layout_.body.width, layout_.body.height});
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), help_book_extra_fields, record, geometry);
}

HelpBook::Parts HelpBook::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record.scrollable);
    read_parts(anatomy(), help_book_extra_fields, published.parts(), record);
    return record;
}

std::size_t HelpBook::realized_item_count() const {
    std::size_t sections = 0U;
    std::size_t links = 0U;
    for (const FocusStop& stop : layout_.stops) {
        if (stop.section) {
            ++sections;
        } else {
            ++links;
        }
    }
    return sections + links;
}

std::string HelpBook::realized_item_stable_id(std::size_t item) const {
    if (item >= layout_.stops.size()) return {};
    const FocusStop& stop = layout_.stops[item];
    const std::string id = (stop.section ? "topic-" : "link-") + std::to_string(stop.index);
    return id;
}

ItemSystemIndex HelpBook::publish_realized_item_system(std::size_t item) const {
    const bool link = item < layout_.stops.size() && !layout_.stops[item].section;
    const ItemSystemIndex system{static_cast<std::uint8_t>(link ? 1U : 0U)};
    return system;
}

AnatomyStateSet HelpBook::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states{};
    if (item >= layout_.stops.size()) return states;
    const FocusStop& stop = layout_.stops[item];
    if (stop.section) {
        states.insert(section_open(stop.index) ? AnatomyState::open : AnatomyState::closed);
        if (hovered_section_ == stop.index) states.insert(AnatomyState::hot);
        if (pressed_section_ == stop.index) states.insert(AnatomyState::pressed);
    } else {
        if (hovered_link_ == stop.index) states.insert(AnatomyState::hot);
        if (pressed_link_ == stop.index) states.insert(AnatomyState::pressed);
        if (focused_ && focused_stop_ == item) states.insert(AnatomyState::focus_visible);
    }
    return states;
}

void HelpBook::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    if (item >= layout_.stops.size()) return;
    const FocusStop& stop = layout_.stops[item];
    if (stop.section) {
        TopicParts record;
        const Point offset = content_offset();
        for (const LaidBlock& laid_block : layout_.blocks) {
            if (laid_block.section != stop.index) continue;
            record.topic_bar = PartGeometry::from_bounds(Rect{laid_block.bar.x + offset.x, laid_block.bar.y + offset.y,
                                                              laid_block.bar.width, laid_block.bar.height});
            record.expander = PartGeometry::from_bounds(
                Rect{laid_block.expander.x + offset.x, laid_block.expander.y + offset.y, laid_block.expander.width,
                     laid_block.expander.height});
        }
        write_item_parts(help_book_item_systems[0], topic_part_fields, record, geometry);
        return;
    }
    LinkParts record;
    const std::optional<Rect> bounds = link_bounds(stop.index);
    if (bounds.has_value()) record.link_text = PartGeometry::from_bounds(*bounds);
    write_item_parts(help_book_item_systems[1], link_part_fields, record, geometry);
}

void HelpBook::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ScrollableControl::append_interactions(interactions);
    interactions.insert(interactions.end(), help_book_interactions.begin(), help_book_interactions.end());
}

} // namespace gui_forms
