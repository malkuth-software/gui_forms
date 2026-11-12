#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/help/help_page/help_page.hpp"
#include "gui_forms/help/helpbook_section/helpbook_section.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
// Paint's docked help book). It shows one help page: the title over a rule,
// then the body's blocks on paper, with collapsible sections under topic
// bars, callouts on darker paper, stable-ID links and PNG images. It scrolls
// vertically; it never opens a link itself, but raises link_activated.
//
// The paper's look is the theme's (parts paper, title, rule, body, close,
// and the topic item system's topic_bar and expander); until the theme
// language gives help-book rules, the parts are drawn with the tooltip
// family (the paper), the menu-item family (topic bars), the card family
// (callouts) and the link families.
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
    // Topic targets shown as visited links.
    void set_visited(std::vector<std::string> targets);
    void mark_visited(std::string_view target);
    [[nodiscard]] bool visited(std::string_view target) const noexcept;

    // The link or topic bar keyboard focus is on, as an index into the
    // book's focus stops (links in reading order, bars among them), or none.
    [[nodiscard]] std::optional<std::size_t> focused_stop() const noexcept { return focused_stop_; }
    // Moves keyboard focus to the next (or previous) stop; false past the end.
    bool move_focus(bool forward);
    // Follows link `link` (an index into page().links) as a click would.
    bool activate_link(std::size_t link);

    [[nodiscard]] Event<HelpLinkActivation&>& link_activated() noexcept { return link_activated_; }
    [[nodiscard]] Event<>& close_requested() noexcept { return close_requested_; }
    [[nodiscard]] Event<const HelpSectionToggle&>& section_toggled() noexcept { return section_toggled_; }

    // The page's content height when laid out at the current width.
    [[nodiscard]] double content_height() const noexcept { return layout_.height; }
    // The block's top in content coordinates, or none when it is hidden.
    [[nodiscard]] std::optional<double> block_top(std::size_t block) const noexcept;
    // The link's box in local coordinates (scroll applied), or none when it
    // is not laid out.
    [[nodiscard]] std::optional<Rect> link_bounds(std::size_t link) const noexcept;
    // The topic bar of section `section` in local coordinates, or none.
    [[nodiscard]] std::optional<Rect> topic_bar_bounds(std::size_t section) const noexcept;

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
        bool highlighted{};
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
        std::optional<std::size_t> section{};
        std::size_t first_piece{};
        std::size_t piece_count{};
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
        double height{};
        Rect title{};
        double title_baseline{};
        Rect rule{};
        Rect close{};
        Rect body{};
        std::vector<LaidBlock> blocks{};
        std::vector<LaidPiece> pieces{};
        std::vector<LaidLine> lines{};
        // The layout's own texts, which pieces name by index (LaidPiece::own).
        std::vector<std::string> marker_texts{};
        std::vector<FocusStop> stops{};
    };
    // How a layout pass runs: on screen, or for print.
    struct LayoutMode final {
        bool print{};
    };

    void lay_out(double width, LayoutMode mode, LaidOut& laid) const;
    void lay_out_block(std::size_t block_index, double left, double width, double& top, LayoutMode mode,
                       LaidOut& laid) const;
    void lay_out_runs(std::size_t block_index, double left, double width, double& top, FontSpec base,
                      LayoutMode mode, LaidOut& laid) const;
    [[nodiscard]] bool block_shown(std::size_t block, LayoutMode mode) const noexcept;
    [[nodiscard]] FontSpec body_font() const noexcept;
    [[nodiscard]] FontSpec run_font(const HelpPageRun& run, FontSpec base) const noexcept;
    [[nodiscard]] FontSpec heading_font(std::size_t level) const noexcept;
    [[nodiscard]] double text_width(std::string_view text, FontSpec font) const;
    [[nodiscard]] const HelpBookImage* find_image(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view piece_text(const LaidOut& laid, std::size_t piece) const noexcept;
    void paint_laid(Painter& painter, const LaidOut& laid, Point offset, LayoutMode mode) const;
    void relayout();
    [[nodiscard]] std::optional<std::size_t> link_at(Point local) const noexcept;
    [[nodiscard]] std::optional<std::size_t> section_bar_at(Point local) const noexcept;
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
    std::optional<std::size_t> hovered_link_{};
    std::optional<std::size_t> hovered_section_{};
    std::optional<std::size_t> pressed_link_{};
    std::optional<std::size_t> pressed_section_{};
    std::optional<std::size_t> focused_stop_{};
    bool close_visible_{};
    bool close_hovered_{};
    bool focused_{};
    Event<HelpLinkActivation&> link_activated_;
    Event<> close_requested_;
    Event<const HelpSectionToggle&> section_toggled_;
};

} // namespace gui_forms
