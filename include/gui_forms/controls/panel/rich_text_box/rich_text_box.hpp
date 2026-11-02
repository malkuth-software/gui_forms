#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Command;
class ContextMenu;
struct MenuItemInvocation;

namespace detail {
class TextEditingEngine;

// One change a RichTextBox command makes to its source: [start, end)
// becomes text. A command's changes are sorted and do not overlap.
struct RichTextSourceChange final {
    std::size_t start{};
    std::size_t end{};
    std::string text{};
};
} // namespace detail

// The buttons of a RichTextBox's control bar, left to right.
enum class RichTextBarButton : std::uint8_t {
    bold,
    italic,
    heading,
    bulleted_list,
    numbered_list,
    link,
    code,
};

inline constexpr std::size_t rich_text_bar_button_count = 7U;

// Whether a format holds over a RichTextBox's selection: for all of it, for
// none of it, or for part of it.
enum class RichTextFormatState : std::uint8_t { off, on, mixed };

// The markdown formatting at a RichTextBox's selection, as its control bar
// shows it. A span style is on when every selected content character has
// it, mixed when some do, and off when none does; an empty selection reads
// the character before its caret (on or off). A line format reads the
// non-blank lines the selection touches the same way. heading is on when
// every touched line is a heading of one level, held in heading_level; it
// is mixed when the lines differ and at least one is a heading, and
// heading_level is then 0, as it is for body text.
struct RichTextFormat final {
    RichTextFormatState bold{RichTextFormatState::off};
    RichTextFormatState italic{RichTextFormatState::off};
    RichTextFormatState code{RichTextFormatState::off};
    RichTextFormatState link{RichTextFormatState::off};
    RichTextFormatState bulleted{RichTextFormatState::off};
    RichTextFormatState numbered{RichTextFormatState::off};
    RichTextFormatState heading{RichTextFormatState::off};
    std::size_t heading_level{};
};

// A rich text field whose document is markdown source (owner decision
// 2026-10-28): the stx_rtf2md dialect, read by parse_markdown. The box
// edits the source with TextBox's editing engine, so keys, selection,
// history, input methods and the clipboard are a multiline TextBox's, and
// shows it styled in place: headings, emphasis, code, links and the
// dialect's attribute spans take their style, and the markup itself (the
// heading, list and quote markers, the emphasis delimiters, link
// destinations, fences) stays visible, dimmed. A control bar above the text
// formats the selection by editing the markup: bold, italic, heading, the
// two lists, link and inline code.
//
// Positions are byte offsets into the source, on grapheme boundaries.
// text() is the plain text (the markup removed); markdown() is the source.
class RichTextBox final : public Panel {
public:
    explicit RichTextBox(StableId stable_id, std::string_view markdown = {});
    ~RichTextBox() override;

    // The source as edited.
    [[nodiscard]] std::string_view markdown() const noexcept;
    // Replaces the source and clears the history; the caret goes to the end.
    // Throws std::invalid_argument for invalid UTF-8 and std::length_error
    // past maximum_bytes.
    void set_markdown(std::string_view markdown);
    // The plain text: the source's blocks with the markup removed, joined by
    // line feeds (markdown_plain_text).
    [[nodiscard]] const std::string& text() const noexcept { return plain_text_; }
    // Replaces the document with plain text, written as markdown that reads
    // back as that text: punctuation escaped, each line feed a hard line
    // break. Leading white space on a line and trailing line feeds are not
    // kept. Throws as set_markdown does.
    void set_text(std::string_view text);
    // Replaces the document with RTF converted to markdown (import_rtf) and
    // clears the history. A failure, or markdown past the box's limits
    // (limit_exceeded), leaves the box as it was.
    [[nodiscard]] RtfConversionResult set_rtf(std::span<const std::byte> rtf);
    // Writes the document as RTF (export_rtf) into output.
    [[nodiscard]] RtfConversionResult rtf(std::vector<std::byte>& output) const;
    // Empties the document and clears the history.
    void clear();
    // The parsed source the box presents.
    [[nodiscard]] const MarkdownDocument& document() const noexcept { return document_; }
    // The largest source the box edits or loads, in UTF-8 bytes. Lines have
    // no bound of their own: a paragraph is one line of the source.
    static constexpr std::size_t maximum_bytes = 1024U * 1024U;

    [[nodiscard]] bool read_only() const noexcept;
    void set_read_only(bool read_only);
    // Lines wrap at the box's width (the default); without wrapping a line is
    // one row and the box scrolls horizontally.
    [[nodiscard]] bool word_wrap() const noexcept { return word_wrap_; }
    void set_word_wrap(bool enabled);
    // Whether Tab inserts a tab; off (the default), Tab moves the focus.
    [[nodiscard]] bool accepts_tab() const noexcept { return accepts_tab_; }
    void set_accepts_tab(bool enabled);
    // Whether the control bar is shown (the default). Its keys work without
    // it.
    [[nodiscard]] bool control_bar_visible() const noexcept { return control_bar_visible_; }
    void set_control_bar_visible(bool visible);
    // The body font. Headings scale it; code is set in the monospace role
    // at its size. Throws std::invalid_argument for an invalid FontSpec.
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    // The document's extent drives the inherited scrollbars (on by default).
    void set_auto_scroll(bool enabled) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] Point scroll_offset() const noexcept {
        if (auto_scroll()) return scroll_position();
        const Point result{.x = horizontal_offset_, .y = vertical_offset_};
        return result;
    }
    // The visual lines of the source as laid out at the box's width.
    [[nodiscard]] std::size_t visual_line_count();

    [[nodiscard]] TextSelection selection() const noexcept;
    // Throws std::out_of_range unless both offsets are grapheme boundaries.
    void select(Utf8Offset anchor, Utf8Offset caret);
    void select_all();
    // The selected source.
    [[nodiscard]] std::string selected_text() const;

    // The formatting at the selection, and the control bar's commands. Each
    // command edits the markup of the lines or text the selection covers as
    // one history step and returns whether it changed anything; read-only
    // refuses them all.
    [[nodiscard]] RichTextFormat selection_format() const;
    // Bold, italic and inline code wrap the selection's text on each line it
    // covers in ** , * or `, or remove the delimiters that already wrap it.
    // With an empty selection they insert a pair with the caret between.
    bool toggle_bold();
    bool toggle_italic();
    bool toggle_code();
    // Sets each touched line's ATX heading marker: 0 removes it, 1 to 6
    // writes that many #. Throws std::invalid_argument above 6.
    bool set_heading_level(std::size_t level);
    // Gives each touched non-blank line a list marker ("- ", or "1. ", "2. "
    // and so on), or removes the markers when every such line has one of
    // that kind already.
    bool toggle_bulleted_list();
    bool toggle_numbered_list();
    // Wraps the selection as a link's text, [selection](), and puts the
    // caret where the destination goes; with an empty selection it writes
    // [](), the caret in the brackets.
    bool insert_link();

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    bool undo();
    bool redo();
    void clear_undo_history();
    // Replaces the selection with source text. False when read-only or past
    // the box's limits.
    bool replace_selection(std::string_view replacement);
    bool delete_selection();
    // The clipboard holds the selected source.
    bool copy();
    bool cut();
    bool paste();

    // Raised after the plain text changes, with the plain text.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }
    // Raised after any change to the source, before text_changed.
    [[nodiscard]] Event<>& content_changed() noexcept { return content_changed_; }
    [[nodiscard]] Event<const TextSelection&>& selection_changed() noexcept {
        return selection_changed_;
    }
    // Raised with the target when a click with Ctrl (Cmd on macOS) lands on
    // a link's text. A plain click places the caret.
    [[nodiscard]] Event<const std::string&>& link_clicked() noexcept { return link_clicked_; }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    // The bar's buttons, as buttons (the heading button as a menu button).
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    // The visual lines of the source as painted, with each grapheme's box;
    // a line ending belongs to no line, as in a multiline TextBox.
    [[nodiscard]] SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout) override;
    [[nodiscard]] SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection) override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members: TextBox's parts and the bar.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry well;
        PartGeometry bar;
        PartGeometry content;
        PartGeometry caret;
        PartGeometry selection;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool read_only{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;
    // A bar button's parts: its face, its label and, on the heading button,
    // the drop-down arrow.
    struct ItemParts final {
        PartGeometry button;
        PartGeometry button_label;
        PartGeometry button_glyph;
    };
    struct ItemStates final {
        bool hot{};
        bool pressed{};
        bool checked{};
        // The button's format holds for part of the selection.
        bool indeterminate{};
        bool disabled{};
        bool open{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        RichTextBarButton button{RichTextBarButton::bold};
        ItemParts parts;
        ItemStates states;
    };
    // The bar's buttons while the bar is shown, left to right.
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_scroll_position_changed() override;
    void on_detached_from_window() noexcept override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    class EditingHost;
    // How one run of a row is set: its span style (none for markup), its
    // font, its rise above the baseline (negative below) and its line box.
    struct PieceStyle final {
        MarkdownSpanStyle style{};
        bool markup{};
        bool link{};
        FontSpec font{};
        double rise{};
        double ascent{};
        double line_height{};
        // The width of a tab stop: four spaces.
        double tab{};
    };
    // A source range of one style on a line.
    struct Run final {
        std::size_t start{};
        std::size_t end{};
        std::size_t style{};
    };
    // Text of one style drawn at x (content coordinates) on a row.
    struct Piece final {
        std::size_t start{};
        std::size_t end{};
        double x{};
        std::size_t style{};
    };
    // One visual line: its grapheme boundaries and their x, its pieces, and
    // its box (y from the document's top, baseline from the row's top).
    struct Row final {
        std::size_t line{};
        bool code_block{};
        double y{};
        double height{};
        double baseline{};
        std::vector<std::size_t> offsets{};
        std::vector<double> positions{};
        std::vector<Piece> pieces{};
    };
    // One grapheme of a line being laid out.
    struct Cell final {
        std::size_t start{};
        std::size_t end{};
        double advance{};
        std::size_t style{};
        std::size_t chunk{};
        bool space{};
        bool tab{};
    };
    // What place_row turns into a row: cells [begin, finish) of a line, the
    // byte the row starts at, and the line's block style and alignment.
    struct RowRequest final {
        std::size_t line{};
        bool code_block{};
        std::size_t begin{};
        std::size_t finish{};
        std::size_t start_byte{};
        std::size_t block_style{};
        MarkdownAlignment alignment{MarkdownAlignment::left};
        double width{};
    };
    // What a source line is: its block (none outside every block), the
    // block's font and alignment.
    struct LineBlock final {
        std::optional<std::size_t> block{};
        FontSpec font{};
        MarkdownAlignment alignment{MarkdownAlignment::left};
        bool code_block{};
    };
    // The colours a paint uses and the content origin in local coordinates.
    struct RowInks final {
        Point origin{};
        Color text{};
        Color muted{};
        Color selected_text{};
        Color link_text{};
        Color link_glyph{};
    };
    using SourceChange = detail::RichTextSourceChange;
    // The first and last source lines a selection touches.
    struct LineSpan final {
        std::size_t first{};
        std::size_t last{};
    };

    // Layout (rich_text_box_layout.cpp).
    void ensure_layout();
    void rebuild_rows(double width);
    // Lays out one source line; the cursors walk the blocks and spans.
    void layout_line(std::size_t line, double width, std::size_t& block_cursor, std::size_t& span_cursor,
                     double& y);
    [[nodiscard]] LineBlock line_block(Utf8Range range, std::size_t& block_cursor) const;
    [[nodiscard]] std::size_t add_piece_style(const MarkdownSpanStyle& style, bool markup, bool link,
                                              FontSpec block);
    void line_runs(Utf8Range range, const LineBlock& block, std::size_t& span_cursor);
    void measure_run(const Run& run, std::size_t& chunk);
    void place_row(const RowRequest& request, double& y);
    [[nodiscard]] double tab_advance(double x, std::size_t style) const;
    [[nodiscard]] double bar_height() const noexcept;
    [[nodiscard]] Rect text_rectangle() const noexcept;
    void set_scroll(Point position);
    void reveal_caret();
    [[nodiscard]] double document_height() const noexcept;
    [[nodiscard]] std::size_t caret_row() const;
    [[nodiscard]] std::size_t row_at_y(double y) const;
    [[nodiscard]] double boundary_x(const Row& row, Utf8Offset offset) const;
    [[nodiscard]] Utf8Offset position_in_row(std::size_t row, double x) const;
    [[nodiscard]] Utf8Offset position_at(double local_x, double local_y);
    [[nodiscard]] std::optional<std::string> link_at(double local_x, double local_y);
    [[nodiscard]] std::optional<Rect> selection_band(const Row& row) const;
    // Paints the visible rows and returns the caret's box (empty without one).
    [[nodiscard]] Rect paint_rows(Painter& painter);
    void paint_piece(Painter& painter, const Row& row, const Piece& piece, const RowInks& inks,
                     bool selected) const;

    // The control bar (rich_text_box_bar.cpp).
    [[nodiscard]] std::array<Rect, rich_text_bar_button_count> bar_buttons() const;
    [[nodiscard]] std::optional<std::size_t> bar_button_at(Point local) const;
    [[nodiscard]] ItemParts bar_item_parts(std::size_t item) const;
    [[nodiscard]] RichTextFormatState bar_button_state(RichTextBarButton button, const RichTextFormat& format) const;
    void paint_bar(Painter& painter) const;
    bool bar_pointer(PointerEvent& event, Point local);
    void invoke_bar_button(RichTextBarButton button);
    void open_heading_menu();
    void heading_chosen(const MenuItemInvocation& invocation);
    void heading_menu_open_changed(bool open);

    // Formatting (rich_text_box_format.cpp).
    // Wraps or unwraps with delimiters[0]; any of delimiters counts as
    // wrapping already.
    bool toggle_wrap(std::span<const std::string_view> delimiters);
    // Applies the changes as one history step and selects `selection`, or
    // the old selection carried through the changes.
    bool apply_changes(const std::vector<SourceChange>& changes, std::optional<TextSelection> selection);
    [[nodiscard]] std::size_t line_of(Utf8Offset offset) const;
    [[nodiscard]] LineSpan touched_lines() const;
    bool toggle_list(bool numbered);
    // Enter in a list item or a quote: the next line takes the same marker,
    // or an empty item ends the list. False when Enter is a plain line feed.
    [[nodiscard]] bool continue_list();

    // Editing and events (rich_text_box.cpp).
    void source_changed();
    void reparse();
    void adopt(std::string_view source);
    void set_selection(TextSelection selection, bool reveal = true);
    [[nodiscard]] bool vertical_key(const KeyEvent& event, bool extend, bool command);
    void reset_caret_blink();
    void schedule_caret_blink();
    // The caret blink timer's target. Observes the box; a destroyed one is
    // skipped.
    struct CaretTick final {
        std::weak_ptr<RichTextBox> box;
        void operator()(FrameTime now) const;
    };
    // One blink: toggles the caret and damages it.
    void toggle_caret();
    [[nodiscard]] BindingValue text_property_value() const;
    void set_text_property(std::string text);

    std::unique_ptr<EditingHost> editing_host_;
    std::unique_ptr<detail::TextEditingEngine> editing_;
    MarkdownDocument document_{};
    std::string plain_text_{};
    FontSpec font_{FontRole::content, 12.0, 400, false};
    bool word_wrap_{true};
    bool accepts_tab_{};
    bool control_bar_visible_{true};
    bool focused_{};
    bool selecting_{};
    bool caret_visible_{true};
    // The caret blink is paint-gated (ADR-016): a blink that finds the box
    // not painted since the last one (a covered window or a hidden box)
    // stops the timer, and the next paint starts it again. Before the first
    // blink there is no earlier one to have been painted.
    bool caret_painted_{true};
    bool caret_waiting_for_paint_{};
    bool reveal_pending_{true};
    bool caret_upstream_{};
    bool next_upstream_{};
    double preferred_x_{-1.0};
    double horizontal_offset_{};
    double vertical_offset_{};
    std::optional<std::size_t> hot_button_{};
    std::optional<std::size_t> pressed_button_{};
    bool heading_menu_open_{};

    std::vector<Row> rows_{};
    std::vector<PieceStyle> piece_styles_{};
    std::vector<Cell> cells_{};
    std::vector<Run> runs_{};
    double document_width_{};
    // Counts the box's source changes; the layout keys on it.
    std::uint64_t content_revision_{};
    std::uint64_t layout_revision_{};
    FontSpec layout_font_{};
    double layout_width_{-1.0};
    double layout_scale_{};
    const TextMetricsProvider* layout_provider_{};
    bool layout_wrap_{};
    Size layout_client_{-1.0, -1.0};
    bool layout_scroll_{};
    bool layout_bar_{};

    std::unique_ptr<ContextMenu> heading_menu_{};
    std::array<std::shared_ptr<Command>, 4> heading_commands_{};
    SubscriptionToken heading_invoked_{};
    SubscriptionToken heading_open_changed_{};
    FrameRequestToken caret_frame_{};
    Event<const std::string&> text_changed_{};
    Event<> content_changed_{};
    Event<const TextSelection&> selection_changed_{};
    Event<const std::string&> link_clicked_{};

    static constexpr double text_left_ = 6.0;
    static constexpr double text_top_ = 4.0;
    static constexpr std::size_t maximum_chunk_graphemes_ = 64U;
};

} // namespace gui_forms
