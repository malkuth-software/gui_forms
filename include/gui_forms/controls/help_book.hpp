#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/help/help_page/help_page.hpp"
#include "gui_forms/help/helpbook_section/helpbook_section.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <stxlib/optional.hpp>

namespace gui_forms {

// A PNG resource a help body may show (`![alt](name.png)`): its name in the
// body, the registered image and its size in logical units.
struct GUI_FORMS_API HelpBookImage final {
    std::string name{};
    ImageId image{};
    Size size{};
};

// A link was activated (click, Enter, or the semantic press). `page` is the
// page's stable ID; `bounds` the link's box in window coordinates, where a
// popup anchors. A handler that acts sets `handled`; the viewer handles
// topic and popup links itself and offers external ones to the application.
struct GUI_FORMS_API HelpLinkActivation final {
    std::string page{};
    HelpLinkKind kind{HelpLinkKind::external};
    std::string target{};
    std::string fragment{};
    Rect bounds{};
    bool handled{};
};

// The pointer moved onto a link, or off every link. `link` indexes the
// page's links, or is none once the pointer is over no link; `kind`,
// `target` and `fragment` are that link's (empty for none). An owner shows
// the target in its status bar.
struct GUI_FORMS_API HelpLinkHover final {
    std::string page{};
    stxlib::Optional<std::size_t> link{};
    HelpLinkKind kind{HelpLinkKind::external};
    std::string target{};
    std::string fragment{};
};

// A collapsible section was opened or closed.
struct GUI_FORMS_API HelpSectionToggle final {
    std::size_t section{};
    bool open{};
};

// The words the book shows for its callouts: a one-word label each. They are
// the help system's own text, translated with the program's.
struct GUI_FORMS_API HelpBookLabels final {
    GUI_FORMS_LOCALIZABLE(text) std::string note{"Note"};
    GUI_FORMS_LOCALIZABLE(text) std::string tip{"Tip"};
    GUI_FORMS_LOCALIZABLE(text) std::string caution{"Caution"};
    GUI_FORMS_LOCALIZABLE(accessible_name) std::string close{"Close help"};
};

// HelpBook: the paper topic pane (help system study, sections 3 and 6; Plan
// Paint's docked help book; Pocket Reference's page). It shows one help
// page: the title over a rule, then the body's blocks on paper, with
// collapsible sections under topic bars, callouts on darker paper, tables,
// definition lists, code blocks and signatures coloured by stxlib::syntax,
// linked cards, stable-ID links and PNG images. It scrolls vertically, and
// sideways when a code block is wider than the page; it never opens a link
// itself, but raises link_activated, and link_hovered as the pointer moves
// over links. Find in page marks a query's matches.
//
// Text is set with leading: body lines are 1.5 times the body size,
// headings 1.3 times, and every run on a line, code and links included,
// sits on the line's one baseline. Inline code is in Portsmouth Mono, sized
// so its x-height matches the body's, on a chip.
//
// The book lays its page out again only when the page, the open sections,
// the font, the text metrics, the device scale or the wrap width change;
// hover, presses and scrolling repaint. It states its own scroll extent in
// arrange, so re-arranging keeps the scroll position.
//
// The paper's look is the theme's (parts paper, title, rule, body, close, the
// content parts code_span, code_block, signature, signature_bar, table_header,
// table_cell, card, find_match (the match find stands on in the current state)
// and the syntax parts keyword, type, function, string, number, comment and
// directive, and the topic item system's topic_bar and expander); until the
// theme language gives help-book rules, the parts are drawn with the tooltip
// family (the paper and text), the menu-item family (topic bars), the card
// family (callouts, cells and cards), the panel family (header cells and code
// chips), the selection family (find marks) and the link families.
//
// Keyboard: Tab and Shift+Tab move through the links and topic bars (past
// the last, focus leaves the book), Enter and Space follow or toggle,
// arrows, Page Up, Page Down, Home and End scroll.
class GUI_FORMS_API HelpBook final : public ScrollableControl {
public:
    explicit HelpBook(StableId stable_id);

    // Shows `page` scrolled to `anchor` (a heading's anchor or a listed
    // control's stable ID), or to the top. Collapsible sections take their
    // initial state, except one holding the anchor, which opens.
    void show_page(HelpPageModel page, std::string_view anchor = {});
    // Shows record `stable_id` of `section` (build_help_page). Returns false,
    // changing nothing, when the section has no such record.
    bool show_record(const HelpBookSection& section, std::string_view stable_id, std::string_view anchor = {});
    [[nodiscard]] const HelpPageModel& page() const& noexcept { return page_; }
    const HelpPageModel& page() const&& = delete;
    // Scrolls so the anchored block is at the top, opening the sections
    // around it. Returns false when no block has the anchor.
    bool scroll_to_anchor(std::string_view anchor);

    void set_section_open(std::size_t section, bool open);
    [[nodiscard]] bool section_open(std::size_t section) const;
    // Whether the close "x" shows at the upper left (the docked book).
    void set_close_visible(bool visible);
    [[nodiscard]] bool close_visible() const noexcept { return close_visible_; }
    void set_images(std::vector<HelpBookImage> images);
    void set_labels(HelpBookLabels labels);
    [[nodiscard]] const HelpBookLabels& labels() const noexcept { return labels_; }
    // Words to mark with the search highlight, folded (fold_help_text).
    void set_highlight(std::vector<std::string> folded_words);
    // Find in page: marks every match of `query` in the page's text (ASCII
    // letters folded unless `match_case`), within one run of text, and makes
    // the first the current match, scrolled into view and its sections
    // opened. An empty query marks nothing. Returns the number of matches.
    std::size_t find(std::string_view query, bool match_case);
    // Makes the next (or previous) match current, wrapping, and scrolls to it.
    void find_next();
    void find_previous();
    void clear_find();
    [[nodiscard]] std::size_t find_count() const noexcept { return matches_.size(); }
    // The current match, counted from 1, or 0 when there is none.
    [[nodiscard]] std::size_t find_position() const noexcept;
    // Topic targets shown as visited links.
    void set_visited(std::vector<std::string> targets);
    void mark_visited(std::string_view target);
    [[nodiscard]] bool visited(std::string_view target) const noexcept;

    // The link or topic bar keyboard focus is on, as an index into the
    // book's focus stops (links in reading order, bars among them), or none.
    [[nodiscard]] stxlib::Optional<std::size_t> focused_stop() const noexcept { return focused_stop_; }
    // Moves keyboard focus to the next (or previous) stop; false past the end.
    bool move_focus(bool forward);
    // Follows link `link` (an index into page().links) as a click would.
    bool activate_link(std::size_t link);

    [[nodiscard]] Event<HelpLinkActivation&>& link_activated() noexcept { return link_activated_; }
    [[nodiscard]] Event<>& close_requested() noexcept { return close_requested_; }
    [[nodiscard]] Event<const HelpSectionToggle&>& section_toggled() noexcept { return section_toggled_; }
    [[nodiscard]] Event<const HelpLinkHover&>& link_hovered() noexcept { return link_hovered_; }

    // The page's content height when laid out at the current width.
    [[nodiscard]] double content_height() const noexcept { return layout_.height; }
    // The page's content width: the width it is laid out at, or more when a
    // code block is wider.
    [[nodiscard]] double content_width() const noexcept { return layout_.extent; }
    // How many times the screen layout has been made (a diagnostic counter):
    // hover and scrolling leave it unchanged.
    [[nodiscard]] std::uint64_t layout_passes() const noexcept { return layout_passes_; }
    // The block's top in content coordinates, or none when it is hidden.
    [[nodiscard]] stxlib::Optional<double> block_top(std::size_t block) const noexcept;
    // The link's box in local coordinates (scroll applied), or none when it
    // is not laid out.
    [[nodiscard]] stxlib::Optional<Rect> link_bounds(std::size_t link) const noexcept;
    // The topic bar of section `section` in local coordinates, or none.
    [[nodiscard]] stxlib::Optional<Rect> topic_bar_bounds(std::size_t section) const noexcept;

    // Printing (help system study, section 8). Lays the page out at `width`
    // with every section open, no close and no paper ground, links followed
    // by their target's title in brackets; returns the content height. The
    // print layout is kept apart from the screen's.
    double layout_for_print(double width);
    // Where the print layout may break pages: the top of every line, in
    // content coordinates, ascending. Filled from the last layout_for_print.
    void print_break_positions(std::vector<double>& positions) const;
    // Paints the print layout's content from `top` (content coordinates) to
    // `top + height` with its top at `origin` on `painter`, clipped to that
    // band.
    void paint_print_band(Painter& painter, Point origin, double top, double height) const;

    void arrange(Rect final_bounds) override;
    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

    // Stage T1 anatomy, as dot members.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry paper;
        PartGeometry title;
        PartGeometry rule;
        PartGeometry close;
        PartGeometry body;
        // The content parts repeat; each publishes its first laid instance,
        // or nothing when the page has none.
        PartGeometry code_span;
        PartGeometry code_block;
        PartGeometry signature;
        PartGeometry signature_bar;
        PartGeometry table_header;
        PartGeometry table_cell;
        PartGeometry card;
        PartGeometry find_match;
        PartGeometry keyword;
        PartGeometry type;
        PartGeometry function;
        PartGeometry string;
        PartGeometry number;
        PartGeometry comment;
        PartGeometry directive;
    };
    [[nodiscard]] Parts parts() const;
    // One topic item: a collapsible section's bar and expander, or one
    // link's text.
    struct GUI_FORMS_API TopicParts final {
        PartGeometry topic_bar;
        PartGeometry expander;
    };
    struct GUI_FORMS_API LinkParts final {
        PartGeometry link_text;
    };
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;

    [[nodiscard]] bool paints_scrolled_content() const noexcept override { return true; }
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    // The mouse raisers' handling: the scroll bars', then the book's.
    void handle_mouse(MouseEventArgs& arguments);
    // What a laid-out piece of text is.
    enum class PieceKind : std::uint8_t { text, link, marker, label };
    // A code piece's syntax part (stxlib::syntax kinds onto the parts), or
    // none for text that is not coloured.
    enum class TokenPart : std::uint8_t { none, keyword, type, function, string, number, comment, directive };
    // What a laid-out box behind text is.
    enum class BoxKind : std::uint8_t { code_span, signature, signature_bar, table_header, table_cell, card };
    // One laid-out piece of text on one line: a byte range of a run, or of
    // one of the layout's own texts (`own`, an index into
    // LaidOut::marker_texts: a list marker, a callout's label, a printed
    // link's bracketed target), its box and baseline in content
    // coordinates, and the font it is set in.
    struct LaidPiece final {
        PieceKind kind{PieceKind::text};
        std::size_t run{static_cast<std::size_t>(-1)};
        std::size_t own{static_cast<std::size_t>(-1)};
        std::size_t begin{};
        std::size_t end{};
        Rect bounds{};
        double baseline{};
        FontSpec font{};
        // Drawn in the muted ink (a caption, a card's line, a comment).
        bool secondary{};
        TokenPart token{TokenPart::none};
        // Inline code, set on a chip (LaidOut::boxes).
        bool chip{};
        bool highlighted{};
    };
    // A box painted behind a block's text: a code chip, a signature's face
    // and bar, a table cell or a card, in content coordinates. A card's
    // `link` is its title's link: the card follows it and is hot with it.
    struct LaidBox final {
        BoxKind kind{BoxKind::code_span};
        Rect bounds{};
        stxlib::Optional<std::size_t> link{};
    };
    // One laid-out block: its box, the callout or code face around it, a
    // collapsible heading's bar and expander, an image's box, and its pieces
    // (LaidOut::pieces from first_piece, piece_count of them).
    struct LaidBlock final {
        std::size_t block{};
        Rect bounds{};
        Rect face{};
        Rect bar{};
        Rect expander{};
        Rect image{};
        stxlib::Optional<std::size_t> section{};
        std::size_t first_piece{};
        std::size_t piece_count{};
        // Its boxes: LaidOut::boxes from first_box, box_count of them.
        std::size_t first_box{};
        std::size_t box_count{};
    };
    // One line's top and height, for print pagination.
    struct LaidLine final {
        double top{};
        double height{};
    };
    // A focus stop: a link (by link index) or a section's bar.
    struct FocusStop final {
        bool section{};
        std::size_t index{};
    };
    // The page laid out at one width. Each pass refills it; storage is kept
    // between passes so a steady page allocates nothing.
    struct LaidOut final {
        double width{};
        // The content's width: `width`, or more when a code block is wider.
        double extent{};
        double height{};
        Rect title{};
        double title_baseline{};
        Rect rule{};
        Rect close{};
        Rect body{};
        std::vector<LaidBlock> blocks{};
        std::vector<LaidPiece> pieces{};
        std::vector<LaidBox> boxes{};
        std::vector<LaidLine> lines{};
        // The layout's own texts, which pieces name by index (LaidPiece::own).
        std::vector<std::string> marker_texts{};
        std::vector<FocusStop> stops{};
    };
    // How a layout pass runs: on screen, or for print.
    struct LayoutMode final {
        bool print{};
    };
    // The fonts of one layout pass, resolved once: the body, and the code
    // font sized so its x-height matches the body's (code_scale times the
    // size of the text around it).
    struct LayoutFonts final {
        FontSpec body{};
        FontSpec code{};
        double code_scale{1.0};
    };
    // One flow of runs into lines: HelpPageModel::runs from first_run,
    // run_count of them, set in `base` from `left`, wrapped at `width`
    // unless `wraps` is false, in lines of `line_factor` times the base
    // size.
    struct RunFlow final {
        std::size_t first_run{};
        std::size_t run_count{};
        MarkdownAlignment alignment{MarkdownAlignment::left};
        double left{};
        double width{};
        FontSpec base{};
        double line_factor{1.5};
        bool secondary{};
        bool wraps{true};
    };
    // A find match: bytes begin to end of run `run`.
    struct FindMatch final {
        std::size_t run{};
        std::size_t begin{};
        std::size_t end{};
    };

    void lay_out(double width, LayoutMode mode, LaidOut& laid) const;
    void lay_out_block(std::size_t block_index, const LayoutFonts& fonts, double left, double width, double& top,
                       LayoutMode mode, LaidOut& laid) const;
    void lay_out_runs(const RunFlow& flow, const LayoutFonts& fonts, double& top, LayoutMode mode,
                      LaidOut& laid) const;
    // Places the code of runs first_run to first_run + run_count, line by
    // line, coloured as `language`, from (`left`, `top`); wraps at `width`
    // when it is above zero. Returns the right edge of the widest line.
    double lay_out_code(std::size_t first_run, std::size_t run_count, HelpCodeLanguage language, FontSpec font,
                        double left, double width, double& top, LaidOut& laid) const;
    // Lays out the table rows from block first to block end, or the cards.
    void lay_out_table(std::size_t first, std::size_t end, const LayoutFonts& fonts, double left, double width,
                       double& top, LayoutMode mode, LaidOut& laid) const;
    void lay_out_cards(std::size_t first, std::size_t end, const LayoutFonts& fonts, double left, double width,
                       double& top, LayoutMode mode, LaidOut& laid) const;
    [[nodiscard]] bool block_shown(std::size_t block, LayoutMode mode) const noexcept;
    [[nodiscard]] FontSpec body_font() const noexcept;
    [[nodiscard]] LayoutFonts layout_fonts() const;
    [[nodiscard]] FontSpec run_font(const HelpPageRun& run, FontSpec base, const LayoutFonts& fonts) const noexcept;
    [[nodiscard]] FontSpec heading_font(std::size_t level) const noexcept;
    [[nodiscard]] double text_width(std::string_view text, FontSpec font) const;
    [[nodiscard]] const HelpBookImage* find_image(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view piece_text(const LaidOut& laid, std::size_t piece) const noexcept;
    void paint_laid(Painter& painter, const LaidOut& laid, Point offset, LayoutMode mode) const;
    // Lays the page out now (the page or what shows of it changed). Outside
    // arrange, a layout whose size changed states the new scroll range at
    // once and marks arrange, so a scroll right after reaches the page.
    void relayout();
    // Lays the page out only when what the layout is made from changed since
    // the last one: the wrap width, the body font (the theme's), the text
    // metrics provider or the device scale. A change of the page or of what
    // shows of it lays out at once (relayout), so arrange, hover and pointer
    // moves cost no layout while these hold.
    void relayout_if_changed();
    [[nodiscard]] stxlib::Optional<std::size_t> link_at(Point local) const noexcept;
    // The block whose runs or cells hold run `run`, or none.
    [[nodiscard]] stxlib::Optional<std::size_t> block_of_run(std::size_t run) const noexcept;
    // Opens every collapsed section around block `block`; true when one opened.
    bool open_sections_around(std::size_t block);
    // The current match's box in content coordinates, or none when it is not
    // laid out.
    [[nodiscard]] stxlib::Optional<Rect> match_rect(const FindMatch& match) const;
    void reveal_current_match();
    void emit_hover();
    [[nodiscard]] stxlib::Optional<std::size_t> section_bar_at(Point local) const noexcept;
    // Whether `held` (a hovered, pressed or focused index) holds `index`.
    [[nodiscard]] static bool holds(const stxlib::Optional<std::size_t>& held, std::size_t index) noexcept;
    [[nodiscard]] Point content_offset() const noexcept;
    void toggle_section(std::size_t section);
    void activate_stop(const FocusStop& stop);
    void reveal_rect(Rect content_rect);

    HelpPageModel page_{};
    // One flag per section of the page: 1 open, 0 closed.
    std::vector<std::uint8_t> open_sections_{};
    std::vector<HelpBookImage> images_{};
    std::vector<std::string> highlight_{};
    std::vector<std::string> visited_{};
    HelpBookLabels labels_{};
    // The screen layout; owned and used on the UI thread only.
    LaidOut layout_{};
    // The print layout (layout_for_print).
    LaidOut print_layout_{};
    stxlib::Optional<std::size_t> hovered_link_{};
    stxlib::Optional<std::size_t> hovered_section_{};
    stxlib::Optional<std::size_t> pressed_link_{};
    stxlib::Optional<std::size_t> pressed_section_{};
    stxlib::Optional<std::size_t> focused_stop_{};
    // Find in page: the matches in run order, the current one, and a reveal
    // asked for before the book had its size, made at the next arrange.
    std::vector<FindMatch> matches_{};
    std::size_t current_match_{};
    bool pending_reveal_{};
    // What the screen layout was made from, so arrange lays out again only
    // when it changed.
    FontSpec layout_font_{};
    const TextMetricsProvider* layout_provider_{};
    double layout_scale_{};
    // True while arrange runs: a layout there states its extent itself, so
    // it marks no arrange.
    bool arranging_{};
    std::uint64_t layout_passes_{};
    bool close_visible_{};
    bool close_hovered_{};
    bool focused_{};
    Event<HelpLinkActivation&> link_activated_;
    Event<> close_requested_;
    Event<const HelpSectionToggle&> section_toggled_;
    Event<const HelpLinkHover&> link_hovered_;
};

} // namespace gui_forms
