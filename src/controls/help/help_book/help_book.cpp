#include "gui_forms/controls/help_book.hpp"

#include "../../scrollable_control/scrollable_anatomy.hpp"
#include "gui_forms/anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/typography/types/typography_types.hpp"
#include "gui_forms/window.hpp"

#include <stxlib/optional.hpp>
#include <stxlib/syntax.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Plan Paint's paper (help system study, section 3), in logical units.
constexpr double paper_left = 12.0;
constexpr double paper_top = 12.0;
constexpr double paper_right = 12.0;
constexpr double title_size = 18.0;
constexpr double body_size = 18.0;
constexpr double close_size = 24.0;
constexpr double close_title_left = 48.0;
constexpr double topic_bar_minimum = 24.0;
constexpr double topic_bar_padding = 4.0;
constexpr double expander_width = 16.0;
constexpr double list_indent = 24.0;
constexpr double quote_indent = 16.0;
constexpr double definition_indent = 22.0;
constexpr double face_padding = 8.0;
constexpr double focus_inset = 1.0;
constexpr double image_gap = 4.0;
constexpr std::size_t no_text = static_cast<std::size_t>(-1);

// The gaps between blocks: the ordinary one, before a heading of level 1 or
// 2 and of a lower level, after a heading, and between a term and its
// definition.
constexpr double block_gap = 8.0;
constexpr double major_heading_gap = 20.0;
constexpr double minor_heading_gap = 14.0;
constexpr double after_heading_gap = 6.0;
constexpr double definition_gap = 2.0;

// Line heights as multiples of the size: body text, headings, code.
constexpr double body_line_factor = 1.5;
constexpr double heading_line_factor = 1.3;
constexpr double code_line_factor = 1.45;

// The inline code chip: its padding beside the text, and how far it reaches
// above and below the baseline, as multiples of the code size (above the
// capitals, below the descenders), and its corner radius.
constexpr double chip_padding_factor = 0.25;
constexpr double chip_above_factor = 0.95;
constexpr double chip_below_factor = 0.3;
constexpr double chip_radius = 3.0;
// The code font's size against the text around it is kept within these, so
// a face with odd metrics cannot make it tiny or huge.
constexpr double minimum_code_scale = 0.8;
constexpr double maximum_code_scale = 1.1;

// Tables: the text against the body, the cell padding, and how wide one
// column's longest word may make it, as a share of the table.
constexpr double table_text_scale = 0.93;
constexpr double cell_padding_x = 8.0;
constexpr double cell_padding_y = 5.0;
// Cards: the narrowest card, the gap between cards, their padding, and the
// line under the title against the body.
constexpr double card_minimum_width = 220.0;
constexpr double card_gap = 8.0;
constexpr double card_padding_x = 10.0;
constexpr double card_padding_y = 7.0;
constexpr double card_line_scale = 0.92;
constexpr double card_radius = 4.0;
// A caption against the body, a signature's bar and padding, and the indent
// of a wrapped signature line in spaces.
constexpr double caption_scale = 0.85;
constexpr double signature_bar_width = 3.0;
constexpr double signature_padding = 6.0;
constexpr double wrapped_code_indent = 4.0;

constexpr std::array<double, 7> heading_scales{{1.0, 1.4, 1.25, 1.1, 1.0, 1.0, 1.0}};

// One stretch of a block's text as the line breaker reads it: page text (a
// run's bytes), or text of the layout's own (a list marker, a callout's
// label, printing's bracketed link target), each in one font. A chip run is
// inline code: it is padded at its ends and set on a chip.
struct LayoutRun final {
    std::string_view text{};
    FontSpec font{};
    std::size_t run{no_text};
    std::size_t joined_start{};
    bool link{};
    bool own{};
    bool chip{};
    double padding{};
};

[[nodiscard]] bool line_end_byte(char character) noexcept {
    return character == '\n';
}

[[nodiscard]] std::size_t trailing_space_start(std::string_view text, std::size_t begin, std::size_t end) noexcept {
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\n' || text[end - 1U] == '\t')) --end;
    return end;
}

// Whether byte `index` of `text` starts a code point (it is not a UTF-8
// continuation byte).
[[nodiscard]] bool code_point_start(std::string_view text, std::size_t index) noexcept {
    if (index >= text.size()) return true;
    const unsigned char byte = static_cast<unsigned char>(text[index]);
    return (byte & 0xC0U) != 0x80U;
}

[[nodiscard]] char fold_ascii(char character, bool match_case) noexcept {
    if (match_case || character < 'A' || character > 'Z') return character;
    return static_cast<char>(character - 'A' + 'a');
}

constexpr std::array<AnatomyState, 1> card_part_states{{AnatomyState::hot}};
// The match find stands on is the find_match part in the current state.
constexpr std::array<AnatomyState, 1> find_match_states{{AnatomyState::current}};

constexpr std::array<PartField<HelpBook::Parts>, 20> help_book_extra_fields{{
    {.declaration = {.name = "paper", .role = PartRole::face, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::paper},
    {.declaration = {.name = "title", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::title},
    {.declaration = {.name = "rule", .role = PartRole::divider, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::rule},
    {.declaration = {.name = "close", .role = PartRole::glyph, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::close},
    {.declaration = {.name = "body", .role = PartRole::content, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::body},
    {.declaration = {.name = "code_span", .role = PartRole::chip, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::code_span},
    {.declaration = {.name = "code_block", .role = PartRole::well, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::code_block},
    {.declaration = {.name = "signature", .role = PartRole::face, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::signature},
    {.declaration = {.name = "signature_bar", .role = PartRole::keyline, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::signature_bar},
    {.declaration = {.name = "table_header", .role = PartRole::face, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::table_header},
    {.declaration = {.name = "table_cell", .role = PartRole::face, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::table_cell},
    {.declaration = {.name = "card", .role = PartRole::face, .composed_from = {}, .states = card_part_states}, .member = &HelpBook::Parts::card},
    {.declaration = {.name = "find_match", .role = PartRole::selection, .composed_from = {}, .states = find_match_states}, .member = &HelpBook::Parts::find_match},
    {.declaration = {.name = "keyword", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::keyword},
    {.declaration = {.name = "type", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::type},
    {.declaration = {.name = "function", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::function},
    {.declaration = {.name = "string", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::string},
    {.declaration = {.name = "number", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::number},
    {.declaration = {.name = "comment", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::comment},
    {.declaration = {.name = "directive", .role = PartRole::label, .composed_from = {}, .states = {}}, .member = &HelpBook::Parts::directive},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 20> help_book_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(help_book_extra_fields));

constexpr std::array<AnatomyState, 4> topic_states{{AnatomyState::hot, AnatomyState::pressed, AnatomyState::open,
                                                    AnatomyState::closed}};
constexpr std::array<AnatomyState, 3> link_states{{AnatomyState::hot, AnatomyState::pressed,
                                                   AnatomyState::focus_visible}};

constexpr std::array<PartField<HelpBook::TopicParts>, 2> topic_part_fields{{
    {.declaration = {.name = "topic_bar", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &HelpBook::TopicParts::topic_bar},
    {.declaration = {.name = "expander", .role = PartRole::disclosure, .composed_from = {}, .states = {}},
     .member = &HelpBook::TopicParts::expander},
}};
constexpr std::array<PartField<HelpBook::LinkParts>, 1> link_part_fields{{
    {.declaration = {.name = "link_text", .role = PartRole::label, .composed_from = {}, .states = {}},
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
constexpr std::array<InteractionDeclaration, 4> help_book_interactions{{
    {.name = "link_activated", .payload = "HelpLinkActivation", .mutable_payload = true},
    {.name = "close_requested", .payload = "", .mutable_payload = false},
    {.name = "section_toggled", .payload = "HelpSectionToggle", .mutable_payload = false},
    {.name = "link_hovered", .payload = "HelpLinkHover", .mutable_payload = false},
}};

// The part a syntax token is drawn as, as the value of HelpBook's TokenPart
// (none 0, keyword 1, type 2, function 3, string 4, number 5, comment 6,
// directive 7): STXlib's kinds onto the book's parts, the kinds without a
// part of their own sharing the nearest (a control word is a keyword, a
// macro a directive, a character a string).
[[nodiscard]] std::uint8_t token_part_of(stxlib::syntax::TokenKind kind) noexcept {
    using stxlib::syntax::TokenKind;
    switch (kind) {
    case TokenKind::comment:
    case TokenKind::documentation:
        return 6U;
    case TokenKind::string:
    case TokenKind::character:
        return 4U;
    case TokenKind::number:
    case TokenKind::constant:
        return 5U;
    case TokenKind::keyword:
    case TokenKind::control:
        return 1U;
    case TokenKind::type:
        return 2U;
    case TokenKind::function:
        return 3U;
    case TokenKind::directive:
    case TokenKind::macro:
        return 7U;
    default:
        return 0U;
    }
}

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
    matches_.clear();
    current_match_ = 0U;
    pending_reveal_ = false;
    relayout();
    static_cast<void>(scroll_to(Point{0.0, 0.0}));
    if (!anchor.empty()) static_cast<void>(scroll_to_anchor(anchor));
    invalidate(Dirty::paint | Dirty::semantics);
}

bool HelpBook::show_record(const HelpBookSection& section, std::string_view stable_id, std::string_view anchor) {
    const stxlib::Optional<std::size_t> record = section.find(stable_id);
    if (record.absent()) return false;
    HelpPageModel page{};
    static_cast<void>(build_help_page(section, (*record.value()), page));
    show_page(std::move(page), anchor);
    return true;
}

bool HelpBook::open_sections_around(std::size_t block) {
    bool opened = false;
    stxlib::Optional<std::size_t> section = page_.blocks[block].section;
    while (section.present()) {
        if (open_sections_[(*section.value())] == 0U) {
            open_sections_[(*section.value())] = 1U;
            opened = true;
        }
        const std::size_t heading = page_.sections[(*section.value())].heading;
        section = page_.blocks[heading].section;
    }
    return opened;
}

bool HelpBook::scroll_to_anchor(std::string_view anchor) {
    const stxlib::Optional<std::size_t> block = page_.find_anchor(anchor);
    if (block.absent()) return false;
    // Open every section around the block, innermost outward.
    if (open_sections_around((*block.value()))) relayout();
    const stxlib::Optional<double> found_top = block_top((*block.value()));
    const double* const top = found_top.value();
    if (top == nullptr) return false;
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

// --- Find in page ----------------------------------------------------------------------

std::size_t HelpBook::find(std::string_view query, bool match_case) {
    matches_.clear();
    current_match_ = 0U;
    pending_reveal_ = false;
    if (!query.empty()) {
        for (std::size_t run = 0U; run < page_.runs.size(); ++run) {
            const std::string_view text(page_.runs[run].text);
            if (text.size() < query.size()) continue;
            std::size_t at = 0U;
            while (at + query.size() <= text.size()) {
                std::size_t matched = 0U;
                while (matched < query.size() &&
                       fold_ascii(text[at + matched], match_case) == fold_ascii(query[matched], match_case)) {
                    ++matched;
                }
                if (matched == query.size()) {
                    matches_.push_back(FindMatch{run, at, at + query.size()});
                    at += query.size();
                } else {
                    ++at;
                }
            }
        }
    }
    reveal_current_match();
    invalidate(Dirty::paint);
    return matches_.size();
}

void HelpBook::find_next() {
    if (matches_.empty()) return;
    current_match_ = (current_match_ + 1U) % matches_.size();
    reveal_current_match();
    invalidate(Dirty::paint);
}

void HelpBook::find_previous() {
    if (matches_.empty()) return;
    current_match_ = current_match_ == 0U ? matches_.size() - 1U : current_match_ - 1U;
    reveal_current_match();
    invalidate(Dirty::paint);
}

void HelpBook::clear_find() {
    matches_.clear();
    current_match_ = 0U;
    pending_reveal_ = false;
    invalidate(Dirty::paint);
}

std::size_t HelpBook::find_position() const noexcept {
    return matches_.empty() ? 0U : current_match_ + 1U;
}

stxlib::Optional<std::size_t> HelpBook::block_of_run(std::size_t run) const noexcept {
    for (std::size_t index = 0U; index < page_.blocks.size(); ++index) {
        const HelpPageBlock& block = page_.blocks[index];
        if (run >= block.first_run && run < block.first_run + block.run_count) {
            return stxlib::Optional<std::size_t>::of(index);
        }
        for (std::size_t cell = block.first_cell; cell < block.first_cell + block.cell_count; ++cell) {
            const HelpPageCell& page_cell = page_.cells[cell];
            if (run >= page_cell.first_run && run < page_cell.first_run + page_cell.run_count) {
                return stxlib::Optional<std::size_t>::of(index);
            }
        }
    }
    return stxlib::Optional<std::size_t>::none();
}

stxlib::Optional<Rect> HelpBook::match_rect(const FindMatch& match) const {
    stxlib::Optional<Rect> result{};
    for (const LaidPiece& piece : layout_.pieces) {
        if (piece.own != no_text || piece.run != match.run) continue;
        if (piece.end <= match.begin || piece.begin >= match.end) continue;
        const std::string_view text(page_.runs[piece.run].text);
        const std::size_t from = std::max(piece.begin, match.begin);
        const std::size_t to = std::min(piece.end, match.end);
        const double before = text_width(text.substr(piece.begin, from - piece.begin), piece.font);
        const double width = text_width(text.substr(from, to - from), piece.font);
        const Rect box{piece.bounds.x + before, piece.bounds.y, width, piece.bounds.height};
        const Rect* const so_far = result.value();
        const Rect united = so_far != nullptr ? Rect::united(*so_far, box) : box;
        result.set(united);
    }
    return result;
}

void HelpBook::reveal_current_match() {
    if (matches_.empty()) return;
    const FindMatch match = matches_[current_match_];
    const stxlib::Optional<std::size_t> block = block_of_run(match.run);
    if (block.present() && open_sections_around((*block.value()))) {
        relayout();
        // The page grew: state the scroll range now, so the scroll below
        // can reach the match.
        const Rect client = client_rectangle();
        if (client.width > 0.0 && client.height > 0.0) {
            arrange_scroll_viewport(Size{client.width, client.height}, Size{layout_.extent, layout_.height});
        }
        invalidate(Dirty::semantics);
    }
    const Rect viewport = viewport_rectangle();
    if (viewport.height <= 0.0 || viewport.width <= 0.0) {
        // No size yet: the next arrange scrolls to it.
        pending_reveal_ = true;
        return;
    }
    const stxlib::Optional<Rect> found = match_rect(match);
    const Rect* const box = found.value();
    if (box == nullptr) return;
    const Point scrolled = scroll_position();
    Point target = scrolled;
    if ((*box).y < scrolled.y || (*box).bottom() > scrolled.y + viewport.height) {
        target.y = std::max(0.0, (*box).y - viewport.height / 3.0);
    }
    if ((*box).x < scrolled.x || (*box).right() > scrolled.x + viewport.width) {
        target.x = std::max(0.0, (*box).x - viewport.width / 3.0);
    }
    if (target.x != scrolled.x || target.y != scrolled.y) static_cast<void>(scroll_to(target));
}

// --- Layout ---------------------------------------------------------------------------

FontSpec HelpBook::body_font() const noexcept {
    const FontSpec own{FontRole::control, body_size, 400, false};
    const FontSpec resolved = text_font(own);
    return resolved;
}

HelpBook::LayoutFonts HelpBook::layout_fonts() const {
    LayoutFonts fonts{};
    fonts.body = body_font();
    // Code is Portsmouth Mono, the house mono, over the monospace role's
    // faces for what it does not cover. It is sized so its x-height is the
    // body's: a mono face at the body's size looks larger and lighter.
    fonts.code = fonts.body;
    fonts.code.role = FontRole::monospace;
    fonts.code.family = portsmouth_mono_font_family;
    fonts.code.stack = FontFamilyStackId{};
    fonts.code.weight = 400;
    fonts.code.italic = false;
    fonts.code.letter_spacing = 0.0;
    const LineBoxMetrics body_metrics =
        line_box_metrics(resolve_text_layout_utf8("x", fonts.body), stxlib::Optional<double>::none());
    const LineBoxMetrics code_metrics =
        line_box_metrics(resolve_text_layout_utf8("x", fonts.code), stxlib::Optional<double>::none());
    if (body_metrics.x_height > 0.0 && code_metrics.x_height > 0.0) {
        fonts.code_scale =
            std::clamp(body_metrics.x_height / code_metrics.x_height, minimum_code_scale, maximum_code_scale);
    }
    // Half-unit sizes keep the code's glyphs on the same hinting steps as
    // the body's.
    fonts.code.size = std::round(fonts.body.size * fonts.code_scale * 2.0) / 2.0;
    return fonts;
}

FontSpec HelpBook::heading_font(std::size_t level) const noexcept {
    FontSpec font = body_font();
    const std::size_t clamped = std::min<std::size_t>(level, heading_scales.size() - 1U);
    font.size = font.size * heading_scales[clamped];
    font.weight = 700;
    return font;
}

FontSpec HelpBook::run_font(const HelpPageRun& run, FontSpec base, const LayoutFonts& fonts) const noexcept {
    FontSpec font = base;
    if (run.code) {
        // The code font at the size the base takes it to: inline code in a
        // heading is the heading's size, scaled as the body's code is.
        font = fonts.code;
        font.size = std::round(base.size * fonts.code_scale * 2.0) / 2.0;
    }
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
    stxlib::Optional<std::size_t> section = page_.blocks[block].section;
    while (section.present()) {
        if (open_sections_[(*section.value())] == 0U) return false;
        section = page_.blocks[page_.sections[(*section.value())].heading].section;
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

void HelpBook::lay_out_runs(const RunFlow& flow, const LayoutFonts& fonts, double& top, LayoutMode mode,
                            LaidOut& laid) const {
    // The layout runs: the page's runs in order, with printing's bracketed
    // target title after each link that ends.
    std::vector<LayoutRun> runs;
    runs.reserve(flow.run_count + 2U);
    std::string joined;
    for (std::size_t index = 0U; index < flow.run_count; ++index) {
        const std::size_t run_index = flow.first_run + index;
        const HelpPageRun& run = page_.runs[run_index];
        LayoutRun layout_run{};
        layout_run.text = run.line_break ? std::string_view("\n") : std::string_view(run.text);
        layout_run.font = run_font(run, flow.base, fonts);
        layout_run.run = run.line_break ? no_text : run_index;
        layout_run.joined_start = joined.size();
        layout_run.link = run.link.present();
        layout_run.chip = run.code && !run.line_break && !run.text.empty();
        layout_run.padding = layout_run.chip ? std::round(layout_run.font.size * chip_padding_factor) : 0.0;
        joined.append(layout_run.text);
        runs.push_back(layout_run);
        const bool link_ends = run.link.present() &&
                               (index + 1U == flow.run_count || page_.runs[run_index + 1U].link != run.link);
        if (mode.print && link_ends) {
            const HelpPageLink& link = page_.links[(*run.link.value())];
            if (!link.target_title.empty()) {
                LayoutRun suffix{};
                // The bracketed title is the layout's own text.
                laid.marker_texts.push_back(" [" + link.target_title + "]");
                suffix.text = std::string_view(laid.marker_texts.back());
                suffix.font = flow.base;
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
    // Every line is line_factor times the base size, its leading split above
    // and below, and every piece on it sits on its one baseline, whatever
    // its font: code and links share the body's baseline.
    const double line_height = std::round(flow.base.size * flow.line_factor);
    const ResolvedTextLayout base_layout = resolve_text_layout_utf8(" ", flow.base);
    const LineBoxMetrics metrics =
        line_box_metrics(base_layout, stxlib::Optional<double>::of(std::max(line_height, base_layout.ascent +
                                                                                             base_layout.descent)));
    const double code_size = std::round(flow.base.size * fonts.code_scale * 2.0) / 2.0;
    std::string folded;
    std::vector<std::string_view> words;
    double line_x = 0.0;
    std::size_t line_first_piece = laid.pieces.size();
    // Closes the open line: places its pieces at `top`, aligns them, and
    // sets each stretch of inline code on one chip.
    struct LineCloser final {
        MarkdownAlignment alignment{MarkdownAlignment::left};
        LaidOut* laid{};
        double left{};
        double width{};
        double height{};
        double baseline{};
        double chip_above{};
        double chip_below{};

        void operator()(double& line_top, double& x, std::size_t& first) const {
            const std::size_t end = (*laid).pieces.size();
            double used = 0.0;
            for (std::size_t piece = first; piece < end; ++piece) {
                used = std::max(used, (*laid).pieces[piece].bounds.right() - left);
            }
            double shift = 0.0;
            if (alignment == MarkdownAlignment::center) shift = std::max(0.0, (width - used) * 0.5);
            if (alignment == MarkdownAlignment::right) shift = std::max(0.0, width - used);
            for (std::size_t piece = first; piece < end; ++piece) {
                LaidPiece& laid_piece = (*laid).pieces[piece];
                laid_piece.bounds.x += shift;
                laid_piece.bounds.y = line_top;
                laid_piece.bounds.height = height;
                laid_piece.baseline = line_top + baseline;
            }
            // A chip spans the consecutive code pieces of one run on this
            // line, padded at the run's ends (the padding was set aside in
            // the line), from above the capitals to below the descenders.
            std::size_t piece = first;
            while (piece < end) {
                const LaidPiece& start = (*laid).pieces[piece];
                if (!start.chip) {
                    ++piece;
                    continue;
                }
                std::size_t last = piece;
                while (last + 1U < end && (*laid).pieces[last + 1U].chip &&
                       (*laid).pieces[last + 1U].run == start.run) {
                    ++last;
                }
                const LaidPiece& finish = (*laid).pieces[last];
                const double padding = std::round(start.font.size * chip_padding_factor);
                const Rect chip{start.bounds.x - padding, line_top + baseline - chip_above,
                                finish.bounds.right() - start.bounds.x + 2.0 * padding, chip_above + chip_below};
                (*laid).boxes.push_back(LaidBox{BoxKind::code_span, chip, {}});
                piece = last + 1U;
            }
            (*laid).lines.push_back(LaidLine{line_top, height});
            line_top += height;
            x = 0.0;
            first = end;
        }
    };
    const LineCloser close_line{flow.alignment,
                                &laid,
                                flow.left,
                                flow.width,
                                metrics.height,
                                metrics.baseline,
                                std::round(code_size * chip_above_factor),
                                std::round(code_size * chip_below_factor)};
    // The width of joined bytes `from` to `to`, over the runs they cross,
    // a chip run's padding counted at its ends.
    struct SpanWidth final {
        const std::vector<LayoutRun>* runs{};
        const HelpBook* book{};
        double operator()(std::size_t from, std::size_t to) const {
            double width = 0.0;
            for (const LayoutRun& run : *runs) {
                const std::size_t run_begin = run.joined_start;
                const std::size_t run_end = run.joined_start + run.text.size();
                const std::size_t begin = std::max(run_begin, from);
                const std::size_t stop = std::min(run_end, to);
                if (begin >= stop) continue;
                width += (*book).text_width(run.text.substr(begin - run_begin, stop - begin), run.font);
                if (run.chip && begin == run_begin) width += run.padding;
                if (run.chip && stop == run_end) width += run.padding;
            }
            return width;
        }
    };
    const SpanWidth span_width{&runs, this};
    const bool wraps = flow.wraps;
    std::size_t position = 0U;
    bool line_open = false;
    while (position < joined.size()) {
        // The segment up to the next break opportunity.
        std::size_t end = position + 1U;
        while (end < joined.size() && opportunities[end - 1U] != LineBreakOpportunity::allowed &&
               opportunities[end - 1U] != LineBreakOpportunity::mandatory) {
            ++end;
        }
        bool forced = opportunities[end - 1U] == LineBreakOpportunity::mandatory;
        std::size_t content_end = trailing_space_start(joined, position, end);
        std::size_t visible_end = end;
        while (visible_end > position && line_end_byte(joined[visible_end - 1U])) --visible_end;
        // The segment's content width (its trailing spaces hang), summed
        // over the runs it crosses, decides whether it fits.
        double content_width = span_width(position, std::min(content_end, visible_end));
        if (wraps && line_open && line_x + content_width > flow.width + 0.001) {
            close_line(top, line_x, line_first_piece);
            line_open = false;
        }
        if (wraps && !line_open && content_width > flow.width + 0.001 && content_end > position) {
            // A word wider than the whole line (a long name in a narrow
            // column) breaks between characters rather than overrun the
            // line: the longest run of whole code points that fits, and at
            // least one.
            std::size_t low = position + 1U;
            while (low < content_end && !code_point_start(joined, low)) ++low;
            while (low < content_end) {
                std::size_t next_point = low + 1U;
                while (next_point < content_end && !code_point_start(joined, next_point)) ++next_point;
                if (span_width(position, next_point) > flow.width + 0.001) break;
                low = next_point;
            }
            end = low;
            content_end = low;
            visible_end = low;
            forced = true;
        }
        for (const LayoutRun& run : runs) {
            const std::size_t run_begin = run.joined_start;
            const std::size_t run_end = run.joined_start + run.text.size();
            const std::size_t begin = std::max(run_begin, position);
            const std::size_t stop = std::min(run_end, visible_end);
            if (begin >= stop) continue;
            if (run.chip && begin == run_begin) line_x += run.padding;
            const double piece_width = text_width(run.text.substr(begin - run_begin, stop - begin), run.font);
            LaidPiece piece{};
            piece.font = run.font;
            piece.secondary = flow.secondary;
            piece.chip = run.chip;
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
            piece.bounds = Rect{flow.left + line_x, top, piece_width, metrics.height};
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
            if (run.chip && stop == run_end) line_x += run.padding;
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
}

double HelpBook::lay_out_code(std::size_t first_run, std::size_t run_count, HelpCodeLanguage language,
                              FontSpec font, double left, double width, double& top, LaidOut& laid) const {
    namespace sx = stxlib::syntax;
    const double line_height = std::round(font.size * code_line_factor);
    const ResolvedTextLayout base_layout = resolve_text_layout_utf8(" ", font);
    const LineBoxMetrics metrics = line_box_metrics(
        base_layout, stxlib::Optional<double>::of(std::max(line_height, base_layout.ascent + base_layout.descent)));
    const double space = text_width(" ", font);
    FontSpec comment_font = font;
    comment_font.italic = true;
    double right = left;
    std::array<sx::LineToken, 64> tokens{};
    sx::LineState state{};
    for (std::size_t run_index = first_run; run_index < first_run + run_count; ++run_index) {
        const std::string_view text(page_.runs[run_index].text);
        std::size_t line_start = 0U;
        while (line_start <= text.size()) {
            const std::size_t newline = text.find('\n', line_start);
            const std::size_t line_end = newline == std::string_view::npos ? text.size() : newline;
            // A final line feed ends the last line; it starts none.
            if (newline == std::string_view::npos && line_start == text.size() && line_start != 0U) break;
            std::string_view line = text.substr(line_start, line_end - line_start);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
            double x = left;
            double line_top = top;
            std::size_t column = 0U;
            sx::LineCursor cursor = sx::LineCursor::at_line_start(state);
            const sx::Language lexed = language == HelpCodeLanguage::cpp ? sx::Language::cpp : sx::Language::plain;
            while (!cursor.done) {
                const std::size_t count =
                    sx::lex_line(lexed, stxlib::View<char>(line.data(), line.size()), cursor,
                                 stxlib::MutableView<sx::LineToken>(tokens.data(), tokens.size()));
                if (count == 0U && !cursor.done) break;
                for (std::size_t index = 0U; index < count; ++index) {
                    const sx::LineToken& token = tokens[index];
                    // The lexer leaves the gaps between tokens untold: they
                    // are spaces.
                    if (token.column > column) x += space * static_cast<double>(token.column - column);
                    column = static_cast<std::size_t>(token.column) + token.length;
                    const std::string_view token_text = line.substr(token.column, token.length);
                    std::size_t spaces = 0U;
                    while (spaces < token_text.size() && token_text[spaces] == ' ') ++spaces;
                    if (spaces == token_text.size()) {
                        x += space * static_cast<double>(spaces);
                        continue;
                    }
                    const TokenPart part = static_cast<TokenPart>(token_part_of(token.kind));
                    const FontSpec& token_font = part == TokenPart::comment ? comment_font : font;
                    const double token_width = text_width(token_text, token_font);
                    if (width > 0.0 && x > left && x + token_width - left > width) {
                        // A wrapped signature line continues indented.
                        laid.lines.push_back(LaidLine{line_top, line_height});
                        line_top += metrics.height;
                        x = left + wrapped_code_indent * space;
                    }
                    LaidPiece piece{};
                    piece.kind = PieceKind::text;
                    piece.run = run_index;
                    piece.begin = line_start + token.column;
                    piece.end = piece.begin + token.length;
                    piece.font = token_font;
                    piece.token = part;
                    piece.secondary = part == TokenPart::comment;
                    piece.bounds = Rect{x, line_top, token_width, metrics.height};
                    piece.baseline = line_top + metrics.baseline;
                    laid.pieces.push_back(piece);
                    x += token_width;
                    right = std::max(right, x);
                }
            }
            state = cursor.state;
            laid.lines.push_back(LaidLine{line_top, metrics.height});
            top = line_top + metrics.height;
            if (newline == std::string_view::npos) break;
            line_start = newline + 1U;
        }
    }
    return right;
}

void HelpBook::lay_out_table(std::size_t first, std::size_t end, const LayoutFonts& fonts, double left,
                             double width, double& top, LayoutMode mode, LaidOut& laid) const {
    FontSpec text = fonts.body;
    text.size = std::round(fonts.body.size * table_text_scale * 2.0) / 2.0;
    FontSpec header = text;
    header.weight = 600;
    std::size_t columns = 1U;
    for (std::size_t index = first; index < end; ++index) columns = std::max(columns, page_.blocks[index].cell_count);
    // Each column's narrowest width (its longest word) and its natural
    // width (its longest cell on one line), padding included.
    std::vector<double> narrowest(columns, 2.0 * cell_padding_x);
    std::vector<double> natural(columns, 2.0 * cell_padding_x);
    for (std::size_t index = first; index < end; ++index) {
        const HelpPageBlock& row = page_.blocks[index];
        const FontSpec& row_font = row.header ? header : text;
        for (std::size_t column = 0U; column < row.cell_count; ++column) {
            const HelpPageCell& cell = page_.cells[row.first_cell + column];
            double line = 0.0;
            double word = 0.0;
            double longest = 0.0;
            for (std::size_t run = cell.first_run; run < cell.first_run + cell.run_count; ++run) {
                const HelpPageRun& page_run = page_.runs[run];
                const FontSpec font = run_font(page_run, row_font, fonts);
                const double padding = page_run.code ? 2.0 * std::round(font.size * chip_padding_factor) : 0.0;
                line += text_width(page_run.text, font) + padding;
                word += padding;
                const std::string_view run_text(page_run.text);
                std::size_t at = 0U;
                while (at < run_text.size()) {
                    std::size_t stop = run_text.find(' ', at);
                    if (stop == std::string_view::npos) stop = run_text.size();
                    word += text_width(run_text.substr(at, stop - at), font);
                    if (stop < run_text.size()) {
                        longest = std::max(longest, word);
                        word = 0.0;
                    }
                    at = stop + 1U;
                }
            }
            longest = std::max(longest, word);
            narrowest[column] = std::max(narrowest[column], std::ceil(longest) + 2.0 * cell_padding_x);
            natural[column] = std::max(natural[column], std::ceil(line) + 2.0 * cell_padding_x);
        }
    }
    // The widths, as a table sized to its content within the column: every
    // column at its natural width when they fit, and the spare shared by
    // natural width; else every column at least its narrowest, the rest
    // shared by how much more each wants; else the narrowest widths scaled
    // down, words breaking between characters (lay_out_runs) rather than
    // running over the next cell.
    double natural_total = 0.0;
    double narrowest_total = 0.0;
    for (std::size_t column = 0U; column < columns; ++column) {
        natural_total += natural[column];
        narrowest_total += narrowest[column];
    }
    std::vector<double> widths(columns, 0.0);
    if (natural_total <= width) {
        for (std::size_t column = 0U; column < columns; ++column) {
            widths[column] = natural[column] + (width - natural_total) * natural[column] / natural_total;
        }
    } else if (narrowest_total <= width) {
        const double wanted = natural_total - narrowest_total;
        for (std::size_t column = 0U; column < columns; ++column) {
            const double more = natural[column] - narrowest[column];
            widths[column] = narrowest[column] + (wanted > 0.0 ? (width - narrowest_total) * more / wanted : 0.0);
        }
    } else {
        for (std::size_t column = 0U; column < columns; ++column) {
            widths[column] = width * narrowest[column] / narrowest_total;
        }
    }
    // Whole units, the last column taking what rounding leaves.
    double used = 0.0;
    for (std::size_t column = 0U; column + 1U < columns; ++column) {
        widths[column] = std::floor(widths[column]);
        used += widths[column];
    }
    widths[columns - 1U] = std::max(2.0 * cell_padding_x, std::floor(width - used));
    double y = top;
    for (std::size_t index = first; index < end; ++index) {
        const HelpPageBlock& row = page_.blocks[index];
        LaidBlock laid_block{};
        laid_block.block = index;
        laid_block.first_piece = laid.pieces.size();
        laid_block.first_box = laid.boxes.size();
        laid.blocks.push_back(laid_block);
        // The cells' faces first, so they draw under the cells' chips.
        double x = left;
        const std::size_t first_face = laid.boxes.size();
        for (std::size_t column = 0U; column < columns; ++column) {
            // Neighbouring faces overlap by their edge, so cells share one
            // line between them.
            laid.boxes.push_back(LaidBox{row.header ? BoxKind::table_header : BoxKind::table_cell,
                                         Rect{x, y, widths[column] + 1.0, 0.0},
                                         {}});
            x += widths[column];
        }
        double row_bottom = y + std::round(text.size * body_line_factor) + 2.0 * cell_padding_y;
        x = left;
        for (std::size_t column = 0U; column < columns; ++column) {
            if (column < row.cell_count) {
                const HelpPageCell& cell = page_.cells[row.first_cell + column];
                RunFlow flow{};
                flow.first_run = cell.first_run;
                flow.run_count = cell.run_count;
                flow.left = x + cell_padding_x;
                flow.width = std::max(1.0, widths[column] - 2.0 * cell_padding_x);
                flow.base = row.header ? header : text;
                double cell_top = y + cell_padding_y;
                lay_out_runs(flow, fonts, cell_top, mode, laid);
                row_bottom = std::max(row_bottom, cell_top + cell_padding_y);
            }
            x += widths[column];
        }
        for (std::size_t face = first_face; face < first_face + columns; ++face) {
            laid.boxes[face].bounds.height = row_bottom - y + 1.0;
        }
        LaidBlock& done = laid.blocks.back();
        done.bounds = Rect{left, y, width, row_bottom - y};
        done.piece_count = laid.pieces.size() - done.first_piece;
        done.box_count = laid.boxes.size() - done.first_box;
        y = row_bottom;
    }
    top = y + 1.0;
}

void HelpBook::lay_out_cards(std::size_t first, std::size_t end, const LayoutFonts& fonts, double left,
                             double width, double& top, LayoutMode mode, LaidOut& laid) const {
    const std::size_t columns =
        std::max<std::size_t>(1U, static_cast<std::size_t>((width + card_gap) / (card_minimum_width + card_gap)));
    const double card_width =
        std::floor((width - card_gap * static_cast<double>(columns - 1U)) / static_cast<double>(columns));
    FontSpec line_font = fonts.body;
    line_font.size = std::round(fonts.body.size * card_line_scale * 2.0) / 2.0;
    double y = top;
    for (std::size_t row = first; row < end; row += columns) {
        const std::size_t row_end = std::min(end, row + columns);
        const std::size_t first_laid = laid.blocks.size();
        double row_bottom = y;
        for (std::size_t index = row; index < row_end; ++index) {
            const HelpPageBlock& block = page_.blocks[index];
            const double x = left + static_cast<double>(index - row) * (card_width + card_gap);
            LaidBlock laid_block{};
            laid_block.block = index;
            laid_block.first_piece = laid.pieces.size();
            laid_block.first_box = laid.boxes.size();
            laid.blocks.push_back(laid_block);
            stxlib::Optional<std::size_t> link{};
            for (std::size_t run = block.first_run; run < block.first_run + block.run_count; ++run) {
                if (page_.runs[run].link.present()) {
                    link = page_.runs[run].link;
                    break;
                }
            }
            laid.boxes.push_back(LaidBox{BoxKind::card, Rect{x, y, card_width, 0.0}, link});
            RunFlow flow{};
            flow.first_run = block.first_run;
            flow.run_count = block.run_count;
            flow.left = x + card_padding_x;
            flow.width = std::max(1.0, card_width - 2.0 * card_padding_x);
            flow.base = fonts.body;
            double card_top = y + card_padding_y;
            lay_out_runs(flow, fonts, card_top, mode, laid);
            if (block.cell_count > 0U) {
                const HelpPageCell& line = page_.cells[block.first_cell];
                flow.first_run = line.first_run;
                flow.run_count = line.run_count;
                flow.base = line_font;
                flow.secondary = true;
                lay_out_runs(flow, fonts, card_top, mode, laid);
            }
            row_bottom = std::max(row_bottom, card_top + card_padding_y);
            LaidBlock& done = laid.blocks.back();
            done.piece_count = laid.pieces.size() - done.first_piece;
            done.box_count = laid.boxes.size() - done.first_box;
        }
        // The cards of a row are as tall as the tallest.
        for (std::size_t laid_index = first_laid; laid_index < laid.blocks.size(); ++laid_index) {
            LaidBlock& laid_block = laid.blocks[laid_index];
            LaidBox& card = laid.boxes[laid_block.first_box];
            card.bounds.height = row_bottom - y;
            laid_block.bounds = card.bounds;
        }
        laid.lines.push_back(LaidLine{y, row_bottom - y});
        y = row_bottom + card_gap;
    }
    top = y - card_gap;
}

void HelpBook::lay_out_block(std::size_t block_index, const LayoutFonts& fonts, double left, double width,
                             double& top, LayoutMode mode, LaidOut& laid) const {
    const HelpPageBlock& block = page_.blocks[block_index];
    LaidBlock laid_block{};
    laid_block.block = block_index;
    laid_block.first_piece = laid.pieces.size();
    laid_block.first_box = laid.boxes.size();
    const double block_top = top;
    const double indent = static_cast<double>(block.quote_depth + block.indent) * quote_indent;
    double content_left = left + indent;
    double content_width = std::max(0.0, width - indent);
    stxlib::Optional<std::size_t> collapsible{};
    for (std::size_t index = 0U; index < page_.sections.size(); ++index) {
        if (page_.sections[index].heading == block_index) collapsible.set(index);
    }
    laid_block.section = collapsible;
    laid.blocks.push_back(laid_block);

    if (block.kind == HelpPageBlockKind::rule) {
        laid.blocks.back().bounds = Rect{content_left, top, content_width, 1.0};
        laid.lines.push_back(LaidLine{top, 1.0});
        top += 1.0;
        return;
    }
    if (block.kind == HelpPageBlockKind::image && block.image.present()) {
        const HelpPageImage& page_image = page_.images[(*block.image.value())];
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
    FontSpec base = fonts.body;
    double line_factor = body_line_factor;
    bool secondary = false;
    if (block.kind == HelpPageBlockKind::heading) {
        base = heading_font(block.level);
        line_factor = heading_line_factor;
    }
    if (block.kind == HelpPageBlockKind::caption) {
        base.size = std::round(fonts.body.size * caption_scale * 2.0) / 2.0;
        secondary = true;
    }
    if (block.kind == HelpPageBlockKind::definition) {
        content_left += definition_indent;
        content_width = std::max(0.0, content_width - definition_indent);
    }

    if (block.kind == HelpPageBlockKind::signature || block.kind == HelpPageBlockKind::code) {
        // Code on its face: a signature on a bar, wrapped at the page; a
        // code block in a well, never wrapped, as wide as its widest line
        // (the page scrolls sideways for it).
        const bool signature = block.kind == HelpPageBlockKind::signature;
        const double inset = signature ? signature_bar_width + signature_padding + 2.0 : face_padding;
        const double padding = signature ? signature_padding : face_padding;
        double code_top = top + padding;
        const double text_left = content_left + inset;
        const double wrap = signature ? std::max(1.0, content_width - inset - padding) : 0.0;
        const double right = lay_out_code(block.first_run, block.run_count, block.language, fonts.code, text_left,
                                          wrap, code_top, laid);
        const double face_width = std::max(content_width, right + padding - content_left);
        const Rect face{content_left, top, face_width, code_top + padding - top};
        LaidBlock& done = laid.blocks.back();
        if (signature) {
            laid.boxes.push_back(LaidBox{BoxKind::signature, face, {}});
            laid.boxes.push_back(
                LaidBox{BoxKind::signature_bar, Rect{face.x, face.y, signature_bar_width, face.height}, {}});
        } else {
            done.face = face;
        }
        laid.extent = std::max(laid.extent, face.right() + paper_right);
        top = face.bottom();
        done.bounds = face;
        done.piece_count = laid.pieces.size() - done.first_piece;
        done.box_count = laid.boxes.size() - done.first_box;
        return;
    }

    RunFlow flow{};
    flow.first_run = block.first_run;
    flow.run_count = block.run_count;
    flow.alignment = block.alignment;
    flow.base = base;
    flow.line_factor = line_factor;
    flow.secondary = secondary;

    if (collapsible.present() && !mode.print) {
        // The heading is the text of a topic bar: the expander, then the
        // heading's runs, at least topic_bar_minimum tall.
        const double bar_top = top;
        const double text_left = content_left + topic_bar_padding + expander_width + topic_bar_padding;
        double text_top = top + topic_bar_padding;
        flow.left = text_left;
        flow.width = std::max(0.0, content_width - (text_left - content_left));
        lay_out_runs(flow, fonts, text_top, mode, laid);
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
            for (std::size_t box = bar_block.first_box; box < laid.boxes.size(); ++box) {
                laid.boxes[box].bounds.y += slack * 0.5;
            }
        }
        bar_block.piece_count = laid.pieces.size() - bar_block.first_piece;
        bar_block.box_count = laid.boxes.size() - bar_block.first_box;
        top = bar_top + bar_height;
        return;
    }

    const bool faced = block.callout != HelpCalloutKind::none;
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
        FontSpec label_font = fonts.body;
        label_font.weight = 700;
        const double label_height = std::round(label_font.size * body_line_factor);
        const LineBoxMetrics metrics = line_box_metrics(resolve_text_layout_utf8(" ", label_font),
                                                        stxlib::Optional<double>::of(label_height));
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
        // The marker hangs in the indent: a bullet, or the item's number,
        // on the first line's baseline.
        const double item_indent = list_indent * static_cast<double>(block.level + 1U);
        std::string marker = block.marker == MarkdownListMarker::ordered ? std::to_string(block.ordinal) + "." : "\xE2\x80\xA2";
        const double marker_height = std::round(base.size * line_factor);
        const LineBoxMetrics metrics =
            line_box_metrics(resolve_text_layout_utf8(" ", base), stxlib::Optional<double>::of(marker_height));
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
    // The label and marker above belong to the block too: its pieces are
    // counted from first_piece.
    flow.left = content_left;
    flow.width = content_width;
    lay_out_runs(flow, fonts, top, mode, laid);
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
    done.piece_count = laid.pieces.size() - done.first_piece;
    done.box_count = laid.boxes.size() - done.first_box;
}

namespace {

// The gap above a block, after the block before it.
[[nodiscard]] double gap_before(const HelpPageBlock& previous, const HelpPageBlock& block) noexcept {
    if (block.kind == HelpPageBlockKind::heading) return block.level <= 2U ? major_heading_gap : minor_heading_gap;
    if (previous.kind == HelpPageBlockKind::heading) return after_heading_gap;
    if (block.kind == HelpPageBlockKind::definition &&
        (previous.kind == HelpPageBlockKind::term || previous.kind == HelpPageBlockKind::definition)) {
        return definition_gap;
    }
    if (block.kind == HelpPageBlockKind::caption && previous.kind == HelpPageBlockKind::image) return 0.0;
    return block_gap;
}

// Whether block `next` continues the table or card grid that `first` starts.
[[nodiscard]] bool continues_group(const HelpPageBlock& first, const HelpPageBlock& next) noexcept {
    return next.kind == first.kind && next.quote_depth == first.quote_depth && next.indent == first.indent &&
           next.section == first.section;
}

} // namespace

void HelpBook::lay_out(double width, LayoutMode mode, LaidOut& laid) const {
    laid.width = width;
    laid.extent = width;
    laid.blocks.clear();
    laid.pieces.clear();
    laid.boxes.clear();
    laid.lines.clear();
    laid.marker_texts.clear();
    laid.stops.clear();
    const LayoutFonts fonts = layout_fonts();
    const double content_width = std::max(0.0, width - paper_left - paper_right);
    double top = paper_top;
    // The close "x" at the upper left, then the title and its rule.
    const bool close_shown = close_visible_ && !mode.print;
    const double title_left = close_shown ? close_title_left : paper_left;
    FontSpec title_font = text_font(FontSpec{FontRole::control, title_size, 400, false});
    const LineBoxMetrics title_metrics =
        line_box_metrics(resolve_text_layout_utf8(" ", title_font), stxlib::Optional<double>::none());
    laid.close = close_shown ? Rect{paper_left, top, close_size, close_size} : Rect{};
    const double title_height = std::max(title_metrics.height, close_shown ? close_size : 0.0);
    laid.title = Rect{title_left, top, std::max(0.0, width - paper_right - title_left), title_height};
    laid.title_baseline = top + (title_height - title_metrics.height) * 0.5 + title_metrics.baseline;
    laid.lines.push_back(LaidLine{top, title_height});
    top += title_height + 2.0;
    laid.rule = Rect{paper_left, top, content_width, 1.0};
    top += 1.0 + block_gap;
    const double body_top = top;
    const HelpPageBlock* previous = nullptr;
    std::size_t index = 0U;
    while (index < page_.blocks.size()) {
        if (!block_shown(index, mode)) {
            ++index;
            continue;
        }
        const HelpPageBlock& block = page_.blocks[index];
        if (previous != nullptr) top += gap_before(*previous, block);
        std::size_t next = index + 1U;
        if (block.kind == HelpPageBlockKind::table_row || block.kind == HelpPageBlockKind::card) {
            while (next < page_.blocks.size() && continues_group(block, page_.blocks[next])) ++next;
            const double indent = static_cast<double>(block.quote_depth + block.indent) * quote_indent;
            const double group_width = std::max(0.0, content_width - indent);
            if (block.kind == HelpPageBlockKind::table_row) {
                lay_out_table(index, next, fonts, paper_left + indent, group_width, top, mode, laid);
            } else {
                lay_out_cards(index, next, fonts, paper_left + indent, group_width, top, mode, laid);
            }
        } else {
            lay_out_block(index, fonts, paper_left, content_width, top, mode, laid);
        }
        previous = &page_.blocks[next - 1U];
        index = next;
    }
    top += block_gap;
    laid.body = Rect{paper_left, body_top, content_width, std::max(0.0, top - body_top)};
    laid.height = top + paper_top;
    // The focus stops in reading order: each section's bar and each link
    // (once, at its first piece).
    std::vector<std::size_t> seen_links;
    for (const LaidBlock& laid_block : laid.blocks) {
        const std::size_t* const section = laid_block.section.value();
        if (section != nullptr && !mode.print) {
            laid.stops.push_back(FocusStop{true, *section});
        }
        for (std::size_t piece = laid_block.first_piece; piece < laid_block.first_piece + laid_block.piece_count;
             ++piece) {
            const LaidPiece& laid_piece = laid.pieces[piece];
            if (laid_piece.kind != PieceKind::link) continue;
            const std::size_t link = (*page_.runs[laid_piece.run].link.value());
            if (std::find(seen_links.begin(), seen_links.end(), link) != seen_links.end()) continue;
            seen_links.push_back(link);
            laid.stops.push_back(FocusStop{false, link});
        }
    }
}

void HelpBook::relayout() {
    const Rect viewport = viewport_rectangle();
    const double width = viewport.width > 0.0 ? viewport.width : client_rectangle().width;
    layout_font_ = body_font();
    const Window* owner = window();
    layout_provider_ = owner == nullptr ? nullptr : (*owner).text_metrics_provider();
    layout_scale_ = owner == nullptr ? 1.0 : (*owner).scale();
    const Size previous{layout_.extent, layout_.height};
    lay_out(width, LayoutMode{false}, layout_);
    ++layout_passes_;
    if (!arranging_ && (previous.width != layout_.extent || previous.height != layout_.height)) {
        invalidate(Dirty::arrange);
    }
    const std::size_t* const focused = focused_stop_.value();
    if (focused != nullptr && *focused >= layout_.stops.size()) focused_stop_.reset();
}

void HelpBook::relayout_if_changed() {
    const Rect viewport = viewport_rectangle();
    const double width = viewport.width > 0.0 ? viewport.width : client_rectangle().width;
    const Window* owner = window();
    const TextMetricsProvider* provider = owner == nullptr ? nullptr : (*owner).text_metrics_provider();
    const double scale = owner == nullptr ? 1.0 : (*owner).scale();
    if (std::abs(width - layout_.width) <= 0.5 && body_font() == layout_font_ && provider == layout_provider_ &&
        scale == layout_scale_) return;
    relayout();
}

void HelpBook::arrange(Rect final_bounds) {
    // The page has no child controls: it states its own extent, as RichTextBox
    // and TextBox do, instead of ScrollableControl::arrange, whose extent is
    // its children's and so would clamp the scroll position to nothing on
    // every arrange. The position is never put back with scroll_to here,
    // which would mark arrange again and leave the book dirty while it is
    // scrolled. The flow runs only when its inputs changed.
    arranging_ = true;
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{arranging_};
    arrange_self(final_bounds);
    const Size client{final_bounds.width, final_bounds.height};
    relayout_if_changed();
    arrange_scroll_viewport(client, Size{layout_.extent, layout_.height});
    // A vertical bar that appeared or went narrows the viewport: lay out
    // again at the width it leaves.
    relayout_if_changed();
    arrange_scroll_viewport(client, Size{layout_.extent, layout_.height});
    Control::arrange(final_bounds);
    if (pending_reveal_ && final_bounds.height > 0.0) {
        pending_reveal_ = false;
        reveal_current_match();
    }
}

Size HelpBook::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size result{requested.width > 0.0 ? requested.width : std::min(available.width, 480.0),
                      requested.height > 0.0 ? requested.height : std::min(available.height, 360.0)};
    return result;
}

stxlib::Optional<double> HelpBook::block_top(std::size_t block) const noexcept {
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (laid_block.block == block) return stxlib::Optional<double>::of(laid_block.bounds.y);
    }
    return stxlib::Optional<double>::none();
}

Point HelpBook::content_offset() const noexcept {
    const Rect viewport = viewport_rectangle();
    const Point scrolled = scroll_position();
    return Point{viewport.x - scrolled.x, viewport.y - scrolled.y};
}

stxlib::Optional<Rect> HelpBook::link_bounds(std::size_t link) const noexcept {
    const Point offset = content_offset();
    stxlib::Optional<Rect> result{};
    for (const LaidPiece& piece : layout_.pieces) {
        if (piece.kind != PieceKind::link || page_.runs[piece.run].link != stxlib::Optional<std::size_t>::of(link)) continue;
        const Rect moved{piece.bounds.x + offset.x, piece.bounds.y + offset.y, piece.bounds.width,
                         piece.bounds.height};
        const Rect* const so_far = result.value();
        const Rect united = so_far != nullptr ? Rect::united(*so_far, moved) : moved;
        result.set(united);
    }
    return result;
}

stxlib::Optional<Rect> HelpBook::topic_bar_bounds(std::size_t section) const noexcept {
    const Point offset = content_offset();
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (!holds(laid_block.section, section)) continue;
        const Rect bar{laid_block.bar.x + offset.x, laid_block.bar.y + offset.y, laid_block.bar.width,
                       laid_block.bar.height};
        return stxlib::Optional<Rect>::of(bar);
    }
    return stxlib::Optional<Rect>::none();
}

// --- Paint ----------------------------------------------------------------------------

namespace {

[[nodiscard]] std::string_view token_part_name(std::uint8_t token) noexcept {
    switch (token) {
    case 1U:
        return "keyword";
    case 2U:
        return "type";
    case 3U:
        return "function";
    case 4U:
        return "string";
    case 5U:
        return "number";
    case 6U:
        return "comment";
    case 7U:
        return "directive";
    default:
        return {};
    }
}

// The first match at or after run `run`, in run order.
struct MatchRunBefore final {
    template <typename Match>
    bool operator()(const Match& match, std::size_t run) const noexcept {
        return match.run < run;
    }
};

} // namespace

void HelpBook::paint_laid(Painter& painter, const LaidOut& laid, Point offset, LayoutMode mode) const {
    const std::size_t* const focused = focused_stop_.value();
    const std::size_t focused_link = focused != nullptr && !layout_.stops[*focused].section
                                         ? layout_.stops[*focused].index
                                         : static_cast<std::size_t>(-1);
    const std::size_t focused_section = focused != nullptr && layout_.stops[*focused].section
                                            ? layout_.stops[*focused].index
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
        title.key.set(PartKey{"title", {}, {}});
        paint_text_part_at(painter, *this, title, Point{laid.title.x + offset.x, laid.title_baseline + offset.y},
                           laid.title.width);
    }
    PartPaint rule{};
    rule.family = ControlVisualRole::tool_tip;
    rule.role = PartRole::divider;
    rule.bounds = Rect{laid.rule.x + offset.x, laid.rule.y + offset.y, laid.rule.width, laid.rule.height};
    rule.key.set(PartKey{"rule", {}, {}});
    paint_part(painter, *this, rule);
    if (!mode.print && close_visible_) {
        PartPaint close{};
        close.family = ControlVisualRole::tool_tip;
        close.role = PartRole::glyph;
        close.mark = PartMark::close;
        close.bounds = Rect{laid.close.x + offset.x, laid.close.y + offset.y, laid.close.width, laid.close.height};
        if (close_hovered_) close.states.insert(AnatomyState::hot);
        close.key.set(PartKey{"close", {}, {}});
        paint_part(painter, *this, close);
    }
    const bool marks_matches = !mode.print && !matches_.empty();
    for (const LaidBlock& laid_block : laid.blocks) {
        const HelpPageBlock& block = page_.blocks[laid_block.block];
        const std::size_t* const laid_section = laid_block.section.value();
        if (laid_block.bar.width > 0.0 && laid_section != nullptr) {
            const std::size_t section = *laid_section;
            AnatomyStateSet states{};
            states.insert(section_open(section) ? AnatomyState::open : AnatomyState::closed);
            if (holds(hovered_section_, section)) states.insert(AnatomyState::hot);
            if (holds(pressed_section_, section)) states.insert(AnatomyState::pressed);
            PartPaint bar{};
            bar.family = ControlVisualRole::menu_item;
            bar.role = PartRole::face;
            bar.bounds = Rect{laid_block.bar.x + offset.x, laid_block.bar.y + offset.y, laid_block.bar.width,
                              laid_block.bar.height};
            bar.states = states;
            bar.corner_radius.set(0.0);
            bar.key.set(PartKey{"topic_bar", "topic", stxlib::Optional<std::size_t>::of(section)});
            paint_part(painter, *this, bar);
            PartPaint expander{};
            expander.family = ControlVisualRole::menu_item;
            expander.role = PartRole::disclosure;
            expander.mark = section_open(section) ? PartMark::chevron_down : PartMark::chevron_right;
            expander.bounds = Rect{laid_block.expander.x + offset.x, laid_block.expander.y + offset.y,
                                   laid_block.expander.width, laid_block.expander.height};
            expander.states = states;
            expander.key.set(PartKey{"expander", "topic", stxlib::Optional<std::size_t>::of(section)});
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
                well.key.set(PartKey{"code_block", {}, {}});
                paint_part(painter, *this, well);
            } else {
                // A callout: darker paper with an edge (the card family
                // until the theme gives help-book callouts).
                PartPaint callout{};
                callout.family = ControlVisualRole::card;
                callout.role = PartRole::face;
                callout.bounds = face;
                callout.corner_radius.set(0.0);
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
        if (laid_block.image.width > 0.0 && block.image.present()) {
            const HelpBookImage* image = find_image(page_.images[(*block.image.value())].source);
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
        // The boxes behind the text: cells and cards, signatures, chips.
        for (std::size_t index = laid_block.first_box; index < laid_block.first_box + laid_block.box_count; ++index) {
            const LaidBox& box = laid.boxes[index];
            PartPaint face{};
            face.bounds = Rect{box.bounds.x + offset.x, box.bounds.y + offset.y, box.bounds.width, box.bounds.height};
            switch (box.kind) {
            case BoxKind::code_span:
                // A calm chip: a flat shade with even corners, no frame.
                face.family = ControlVisualRole::panel;
                face.role = PartRole::chip;
                face.frame = PartFrame::none;
                face.corner_radius.set(chip_radius);
                face.key.set(PartKey{"code_span", {}, {}});
                break;
            case BoxKind::signature:
                face.family = ControlVisualRole::card;
                face.role = PartRole::face;
                face.frame = PartFrame::none;
                face.corner_radius.set(0.0);
                face.key.set(PartKey{"signature", {}, {}});
                break;
            case BoxKind::signature_bar:
                face.family = ControlVisualRole::card;
                face.role = PartRole::keyline;
                face.accent.set(AccentIndex{0U});
                face.corner_radius.set(0.0);
                face.key.set(PartKey{"signature_bar", {}, {}});
                break;
            case BoxKind::table_header:
                face.family = ControlVisualRole::panel;
                face.role = PartRole::face;
                face.corner_radius.set(0.0);
                face.key.set(PartKey{"table_header", {}, {}});
                break;
            case BoxKind::table_cell:
                face.family = ControlVisualRole::card;
                face.role = PartRole::face;
                face.corner_radius.set(0.0);
                face.key.set(PartKey{"table_cell", {}, {}});
                break;
            case BoxKind::card:
                face.family = ControlVisualRole::card;
                face.role = PartRole::face;
                face.corner_radius.set(card_radius);
                if (box.link.present() && hovered_link_ == box.link && !mode.print) {
                    face.states.insert(AnatomyState::hot);
                }
                face.key.set(PartKey{"card", {}, {}});
                break;
            default:
                // Every kind is drawn above; an unknown one draws nothing.
                continue;
            }
            paint_part(painter, *this, face);
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
            if (marks_matches && piece.own == no_text) {
                // The find marks under this piece's text: its run's matches
                // that overlap it, the current one with a ring.
                const std::string_view run_text(page_.runs[piece.run].text);
                std::vector<FindMatch>::const_iterator match =
                    std::lower_bound(matches_.begin(), matches_.end(), piece.run, MatchRunBefore{});
                while (match != matches_.end() && (*match).run == piece.run) {
                    if ((*match).end > piece.begin && (*match).begin < piece.end) {
                        const std::size_t from = std::max(piece.begin, (*match).begin);
                        const std::size_t to = std::min(piece.end, (*match).end);
                        const double before = text_width(run_text.substr(piece.begin, from - piece.begin), piece.font);
                        const double width = text_width(run_text.substr(from, to - from), piece.font);
                        const bool current = static_cast<std::size_t>(match - matches_.begin()) == current_match_;
                        PartPaint mark{};
                        mark.family = ControlVisualRole::selection;
                        mark.role = PartRole::selection;
                        mark.bounds = Rect{bounds.x + before - 1.0, bounds.y + 1.0, width + 2.0, bounds.height - 2.0};
                        mark.states.insert(AnatomyState::selected);
                        if (current) mark.states.insert(AnatomyState::current);
                        mark.key.set(PartKey{"find_match", {}, {}});
                        paint_part(painter, *this, mark);
                        if (current) {
                            PartPaint ring{};
                            ring.family = ControlVisualRole::selection;
                            ring.role = PartRole::focus;
                            ring.bounds = mark.bounds;
                            ring.states.insert(AnatomyState::focus_visible);
                            paint_part(painter, *this, ring);
                        }
                    }
                    ++match;
                }
            }
            PartPaint part{};
            part.role = PartRole::label;
            part.font = piece.font;
            part.text = lend_paint_text(text);
            part.family = ControlVisualRole::tool_tip;
            part.tone = piece.secondary ? PartTextTone::secondary : PartTextTone::ordinary;
            const std::string_view token = token_part_name(static_cast<std::uint8_t>(piece.token));
            // Text is the body part, a coloured token its own part, and a
            // link the link item's text (below).
            part.key.set(PartKey{token.empty() ? std::string_view{"body"} : token, {}, {}});
            if (piece.kind == PieceKind::link) {
                const std::size_t link = (*page_.runs[piece.run].link.value());
                const bool was_visited = visited(page_.links[link].target);
                part.family = was_visited ? ControlVisualRole::visited_link : ControlVisualRole::link;
                if (holds(hovered_link_, link)) part.states.insert(AnatomyState::hot);
                if (holds(pressed_link_, link)) part.states.insert(AnatomyState::pressed);
                part.key.set(PartKey{"link_text", "link", stxlib::Optional<std::size_t>::of(link)});
            }
            paint_text_part_at(painter, *this, part, Point{bounds.x, piece.baseline + offset.y}, bounds.width);
            if (piece.kind == PieceKind::link) {
                const std::size_t link = (*page_.runs[piece.run].link.value());
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
    paper.corner_radius.set(0.0);
    paper.key.set(PartKey{"paper", {}, {}});
    paint_part(painter, *this, paper);
    const Rect viewport = viewport_rectangle();
    painter.save();
    painter.clip_rect(viewport);
    paint_laid(painter, layout_, content_offset(), LayoutMode{false});
    painter.restore();
    if (focused_ && focused_stop_.absent() && bounds.width > 4.0 && bounds.height > 4.0) {
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

stxlib::Optional<std::size_t> HelpBook::link_at(Point local) const noexcept {
    const Point offset = content_offset();
    const Point content{local.x - offset.x, local.y - offset.y};
    if (!viewport_rectangle().contains(local)) return stxlib::Optional<std::size_t>::none();
    for (const LaidPiece& piece : layout_.pieces) {
        if (piece.kind != PieceKind::link || !piece.bounds.contains(content)) continue;
        // A link piece's run always names its link (lay_out_runs).
        const std::size_t link = (*page_.runs[piece.run].link.value());
        return stxlib::Optional<std::size_t>::of(link);
    }
    // A card follows its title's link from anywhere on it.
    for (const LaidBox& box : layout_.boxes) {
        if (box.link.present() && box.bounds.contains(content)) return box.link;
    }
    return stxlib::Optional<std::size_t>::none();
}

stxlib::Optional<std::size_t> HelpBook::section_bar_at(Point local) const noexcept {
    const Point offset = content_offset();
    const Point content{local.x - offset.x, local.y - offset.y};
    if (!viewport_rectangle().contains(local)) return stxlib::Optional<std::size_t>::none();
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (laid_block.section.present() && laid_block.bar.contains(content)) return laid_block.section;
    }
    return stxlib::Optional<std::size_t>::none();
}

bool HelpBook::holds(const stxlib::Optional<std::size_t>& held, std::size_t index) noexcept {
    const std::size_t* const current = held.value();
    const bool result = current != nullptr && *current == index;
    return result;
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
    const stxlib::Optional<Rect> found_local = link_bounds(link);
    const Rect* const local = found_local.value();
    if (local != nullptr) {
        const ScaleFrame local_frame = window_frame();
        activation.bounds = frame_to_outer(local_frame, (*local));
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
    const std::size_t* const focused = focused_stop_.value();
    if (focused == nullptr) {
        next = forward ? 0U : layout_.stops.size() - 1U;
    } else if (forward) {
        if (*focused + 1U >= layout_.stops.size()) return false;
        next = *focused + 1U;
    } else {
        if (*focused == 0U) return false;
        next = *focused - 1U;
    }
    focused_stop_.set(next);
    const FocusStop& stop = layout_.stops[next];
    const Point offset = content_offset();
    const stxlib::Optional<Rect> found_local = stop.section ? topic_bar_bounds(stop.index) : link_bounds(stop.index);
    const Rect* const local = found_local.value();
    if (local != nullptr) {
        reveal_rect(Rect{(*local).x - offset.x, (*local).y - offset.y, (*local).width, (*local).height});
    }
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

// The scroll bars take the pointer first; what they leave is the book's
// links, section bars and close button. The event is raised after both.
void HelpBook::on_mouse_down(MouseEventArgs& arguments) {
    handle_mouse(arguments);
    Control::on_mouse_down(arguments);
}

void HelpBook::on_mouse_up(MouseEventArgs& arguments) {
    handle_mouse(arguments);
    Control::on_mouse_up(arguments);
}

void HelpBook::on_mouse_move(MouseEventArgs& arguments) {
    handle_mouse(arguments);
    Control::on_mouse_move(arguments);
}

void HelpBook::on_mouse_wheel(MouseEventArgs& arguments) {
    handle_mouse(arguments);
    Control::on_mouse_wheel(arguments);
}

void HelpBook::on_mouse_leave() {
    scroll_pointer_left();
    const bool left_link = hovered_link_.present();
    if (hovered_link_.present() || hovered_section_.present() || close_hovered_) {
        hovered_link_.reset();
        hovered_section_.reset();
        close_hovered_ = false;
        cursor = stxlib::Optional<CursorKind>::none();
        invalidate(Dirty::paint);
    }
    if (left_link) emit_hover();
    Control::on_mouse_leave();
}

void HelpBook::emit_hover() {
    HelpLinkHover hover{};
    hover.page = page_.stable_id;
    hover.link = hovered_link_;
    const std::size_t* const link = hovered_link_.value();
    if (link != nullptr && *link < page_.links.size()) {
        const HelpPageLink& page_link = page_.links[*link];
        hover.kind = page_link.kind;
        hover.target = page_link.target;
        hover.fragment = page_link.fragment;
    }
    link_hovered_.emit(hover);
}

void HelpBook::handle_mouse(MouseEventArgs& arguments) {
    scroll_pointer(arguments);
    if (arguments.handled) return;
    // The pointer in the book's own coordinates, where its links are.
    const Point local = arguments.location;
    if (arguments.action == PointerAction::move) {
        const stxlib::Optional<std::size_t> link = link_at(local);
        const stxlib::Optional<std::size_t> section =
            link.present() ? stxlib::Optional<std::size_t>::none() : section_bar_at(local);
        const bool over_close = close_visible_ && Rect{layout_.close.x + content_offset().x,
                                                       layout_.close.y + content_offset().y, layout_.close.width,
                                                       layout_.close.height}
                                                      .contains(local);
        if (link != hovered_link_ || section != hovered_section_ || over_close != close_hovered_) {
            const bool link_changed = link != hovered_link_;
            hovered_link_ = link;
            hovered_section_ = section;
            close_hovered_ = over_close;
            cursor = (link.present() || section.present() || over_close) ? stxlib::Optional<CursorKind>::of(CursorKind::hand)
                                                                             : stxlib::Optional<CursorKind>::none();
            // Hover repaints; it never lays the page out again.
            invalidate(Dirty::paint);
            if (link_changed) emit_hover();
        }
        return;
    }
    if (arguments.button != PointerButton::primary) return;
    if (arguments.action == PointerAction::down) {
        pressed_link_ = link_at(local);
        pressed_section_ = pressed_link_.present() ? stxlib::Optional<std::size_t>::none() : section_bar_at(local);
        if (pressed_link_.present() || pressed_section_.present() || close_hovered_) {
            arguments.handled = true;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (arguments.action == PointerAction::up) {
        const stxlib::Optional<std::size_t> link = link_at(local);
        const stxlib::Optional<std::size_t> section = section_bar_at(local);
        const stxlib::Optional<std::size_t> held_link = pressed_link_;
        const stxlib::Optional<std::size_t> held_section = pressed_section_;
        pressed_link_.reset();
        pressed_section_.reset();
        invalidate(Dirty::paint);
        const std::size_t* const pressed_link = held_link.value();
        if (pressed_link != nullptr && link == held_link) {
            arguments.handled = true;
            static_cast<void>(activate_link(*pressed_link));
            return;
        }
        const std::size_t* const pressed_section = held_section.value();
        if (pressed_section != nullptr && section == held_section) {
            arguments.handled = true;
            toggle_section(*pressed_section);
            return;
        }
        if (close_hovered_) {
            arguments.handled = true;
            close_requested_.emit();
        }
    }
}

void HelpBook::on_key_down(KeyEventArgs& arguments) {
    ScrollableControl::on_key_down(arguments);
    if (arguments.handled) return;
    const Rect viewport = viewport_rectangle();
    const bool shift = has_modifier(arguments.modifiers, Modifier::shift);
    const double line = body_size * 1.5;
    const Point scrolled = scroll_position();
    if (arguments.physical_key == PhysicalKey::tab && !has_modifier(arguments.modifiers, Modifier::control)) {
        if (move_focus(!shift)) arguments.handled = true;
        return;
    }
    const std::size_t* const focused = focused_stop_.value();
    if ((arguments.physical_key == PhysicalKey::enter || arguments.physical_key == PhysicalKey::space) &&
        focused != nullptr) {
        arguments.handled = true;
        const FocusStop stop = layout_.stops[*focused];
        activate_stop(stop);
        return;
    }
    if ((arguments.physical_key == PhysicalKey::left || arguments.physical_key == PhysicalKey::right) &&
        layout_.extent > viewport.width) {
        // Sideways, over a code block wider than the page.
        const double step = arguments.physical_key == PhysicalKey::left ? -line : line;
        const double across = std::clamp(scrolled.x + step, 0.0, std::max(0.0, layout_.extent - viewport.width));
        arguments.handled = true;
        static_cast<void>(scroll_to(Point{across, scrolled.y}));
        invalidate(Dirty::paint);
        return;
    }
    double target = scrolled.y;
    if (arguments.physical_key == PhysicalKey::down) target += line;
    else if (arguments.physical_key == PhysicalKey::up) target -= line;
    else if (arguments.physical_key == PhysicalKey::page_down) target += std::max(line, viewport.height - line);
    else if (arguments.physical_key == PhysicalKey::page_up) target -= std::max(line, viewport.height - line);
    else if (arguments.physical_key == PhysicalKey::home) target = 0.0;
    else if (arguments.physical_key == PhysicalKey::end) target = layout_.height;
    else return;
    arguments.handled = true;
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
    // A document (GAPS H3) named by its title, whose children are its
    // headings (with their level), images and links.
    descriptor.role = SemanticRole::document;
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
            // A collapsible section's heading is still a heading; it takes
            // expand or collapse and reports expanded while open.
            heading.role = SemanticRole::heading;
            for (std::size_t run = 0U; run < block.run_count; ++run) heading.name.append(page_.runs[block.first_run + run].text);
            heading.level.set(block.level);
            heading.bounds = Rect{laid_block.bounds.x + offset.x, laid_block.bounds.y + offset.y,
                                  laid_block.bounds.width, laid_block.bounds.height};
            heading.states = SemanticState::enabled | SemanticState::visible;
            const std::size_t* const section = laid_block.section.value();
            if (section != nullptr) {
                const bool open = section_open(*section);
                if (open) heading.states = heading.states | SemanticState::expanded;
                heading.actions.push_back(open ? SemanticAction::collapse : SemanticAction::expand);
            }
            children.push_back(std::move(heading));
        }
        if (block.kind == HelpPageBlockKind::image && block.image.present()) {
            SemanticNode image{};
            image.stable_id = prefix + ".image-" + std::to_string(laid_block.block);
            image.role = SemanticRole::image;
            image.name = page_.images[(*block.image.value())].alternative;
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
            if (piece.kind == PieceKind::link && page_.runs[piece.run].link == stxlib::Optional<std::size_t>::of(stop.index)) {
                link.name.append(page_.runs[piece.run].text.substr(piece.begin, piece.end - piece.begin));
            }
        }
        link.value = page_.links[stop.index].target;
        const stxlib::Optional<Rect> found_bounds = link_bounds(stop.index);
        const Rect* const bounds = found_bounds.value();
        if (bounds != nullptr) link.bounds = *bounds;
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
    // The content parts: each one's first laid instance.
    std::array<PartGeometry*, 6> box_parts{{&record.code_span, &record.signature, &record.signature_bar,
                                            &record.table_header, &record.table_cell, &record.card}};
    std::array<bool, 6> box_seen{};
    for (const LaidBox& box : layout_.boxes) {
        const std::size_t kind = static_cast<std::size_t>(box.kind);
        if (kind >= box_parts.size() || box_seen[kind]) continue;
        box_seen[kind] = true;
        *box_parts[kind] = PartGeometry::from_bounds(
            Rect{box.bounds.x + offset.x, box.bounds.y + offset.y, box.bounds.width, box.bounds.height});
    }
    for (const LaidBlock& laid_block : layout_.blocks) {
        if (laid_block.face.width <= 0.0 || page_.blocks[laid_block.block].kind != HelpPageBlockKind::code) continue;
        record.code_block = PartGeometry::from_bounds(Rect{laid_block.face.x + offset.x, laid_block.face.y + offset.y,
                                                           laid_block.face.width, laid_block.face.height});
        break;
    }
    std::array<PartGeometry*, 8> token_parts{{nullptr, &record.keyword, &record.type, &record.function,
                                              &record.string, &record.number, &record.comment, &record.directive}};
    std::array<bool, 8> token_seen{};
    for (const LaidPiece& piece : layout_.pieces) {
        const std::size_t token = static_cast<std::size_t>(piece.token);
        if (token == 0U || token >= token_parts.size() || token_seen[token]) continue;
        token_seen[token] = true;
        *token_parts[token] = PartGeometry::from_bounds(
            Rect{piece.bounds.x + offset.x, piece.bounds.y + offset.y, piece.bounds.width, piece.bounds.height});
    }
    if (!matches_.empty()) {
        const stxlib::Optional<Rect> current = match_rect(matches_[current_match_]);
        const Rect* const box = current.value();
        if (box != nullptr) {
            record.find_match = PartGeometry::from_bounds(
                Rect{(*box).x + offset.x, (*box).y + offset.y, (*box).width, (*box).height});
        }
    }
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
    if (item >= layout_.stops.size()) throw std::out_of_range("HelpBook realized item is past the focus stops");
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
    if (item >= layout_.stops.size()) throw std::out_of_range("HelpBook realized item is past the focus stops");
    AnatomyStateSet states{};
    const FocusStop& stop = layout_.stops[item];
    if (stop.section) {
        states.insert(section_open(stop.index) ? AnatomyState::open : AnatomyState::closed);
        if (holds(hovered_section_, stop.index)) states.insert(AnatomyState::hot);
        if (holds(pressed_section_, stop.index)) states.insert(AnatomyState::pressed);
    } else {
        if (holds(hovered_link_, stop.index)) states.insert(AnatomyState::hot);
        if (holds(pressed_link_, stop.index)) states.insert(AnatomyState::pressed);
        if (focused_ && holds(focused_stop_, item)) states.insert(AnatomyState::focus_visible);
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
            if (!holds(laid_block.section, stop.index)) continue;
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
    const stxlib::Optional<Rect> found_bounds = link_bounds(stop.index);
    const Rect* const bounds = found_bounds.value();
    if (bounds != nullptr) record.link_text = PartGeometry::from_bounds(*bounds);
    write_item_parts(help_book_item_systems[1], link_part_fields, record, geometry);
}

void HelpBook::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ScrollableControl::append_interactions(interactions);
    interactions.insert(interactions.end(), help_book_interactions.begin(), help_book_interactions.end());
}

} // namespace gui_forms
