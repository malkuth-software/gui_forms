#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/text_box/auto_complete/auto_complete.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/panel/text_box/text_box_capabilities/text_box_capabilities.hpp"
#include "gui_forms/controls/panel/text_box/text_decorations/text_decorations.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

struct TextSelection final {
    Utf8Offset anchor{};
    Utf8Offset caret{};

    [[nodiscard]] Utf8Offset start() const noexcept {
        const std::size_t first = std::min(anchor.value(), caret.value());
        const Utf8Offset result(first);
        return result;
    }
    [[nodiscard]] Utf8Offset end() const noexcept {
        const std::size_t last = std::max(anchor.value(), caret.value());
        const Utf8Offset result(last);
        return result;
    }
    [[nodiscard]] std::size_t length() const noexcept {
        const Utf8Offset first = start();
        const Utf8Offset last = end();
        const std::size_t result = last.value() - first.value();
        return result;
    }
    [[nodiscard]] bool empty() const noexcept {
        const bool result = anchor == caret;
        return result;
    }
    friend constexpr bool operator==(const TextSelection& left,
                                     const TextSelection& right) noexcept {
        const bool result = left.anchor == right.anchor && left.caret == right.caret;
        return result;
    }
};

// The kind of user edit a TextBox offers its edit filter.
enum class TextEditKind : std::uint8_t {
    typing,    // text input (including composition), Tab and Enter in a multiline box, replace_selection
    deletion,  // Backspace, Delete, cut, delete_selection
    paste,     // paste from the clipboard
    value,     // SemanticAction::set_value: the whole text
};

// What an edit filter decides about a proposed edit.
enum class TextEditVerdict : std::uint8_t {
    accept,   // apply the edit as proposed
    refuse,   // apply nothing
    rewrite,  // replace the whole text with rewritten_text instead
};

// One user edit, offered to the edit filter before the TextBox changes
// anything: replace [start, end) of text() with `replacement`. The filter
// reads the proposal and writes its verdict; with `rewrite` it also writes
// the whole new text and where the caret goes in it. `replacement` and the
// caret offsets are borrowed for the call. For a deletion with an empty
// selection the range is the grapheme Backspace or Delete would remove.
struct TextEdit final {
    TextEditKind kind{TextEditKind::typing};
    Utf8Offset start{};
    Utf8Offset end{};
    std::string_view replacement{};
    TextEditVerdict verdict{TextEditVerdict::accept};
    std::string rewritten_text{};
    // A byte offset in rewritten_text; moved forward to a grapheme boundary.
    Utf8Offset rewritten_caret{};
};

// The suggestion list's events (list_box.hpp, anchored_overlay_layer.hpp).
struct ListSelectionChange;
struct PartPaint;
enum class OverlayDismissReason : std::uint8_t;

namespace detail {
// TextBox's auto-complete state (src/controls/panel/text_box).
struct TextBoxAutoCompleteSession;
// The text editing engine TextBox edits through (src/controls/panel/text_editing).
class TextEditingEngine;
} // namespace detail

class TextBox final : public Panel, public TextBoxCapabilities<TextBox> {
public:
    explicit TextBox(StableId stable_id, std::string initial_text = {});
    ~TextBox() override;

    // Multiline opt-in: document extent drives the inherited scrollbars and
    // scroll_position. Wrapped documents expose only vertical overflow.
    void arrange(Rect final_bounds) override;
    // Provisional development bounds until cluster-position metrics replace
    // prefix measurement. Validation/refusal never truncates source text.
    static constexpr std::size_t maximum_multiline_bytes = 1024U * 1024U;
    static constexpr std::size_t maximum_multiline_line_bytes = 4096U;
    enum class MultilineValidation : std::uint8_t {
        valid, invalid_utf8, document_too_large, line_too_long
    };
    [[nodiscard]] static MultilineValidation validate_multiline_text(std::string_view source);
    [[nodiscard]] std::size_t visual_line_count();
    [[nodiscard]] Point scroll_offset() const noexcept {
        if (multiline.read() && auto_scroll.read()) return scroll_position();
        const Point result{.x = horizontal_offset_, .y = vertical_offset_};
        return result;
    }
    [[nodiscard]] bool password_protected() const noexcept {
        const bool result = use_system_password_character.read() || password_character.read() != U'\0';
        return result;
    }
    [[nodiscard]] TextSelection selection() const noexcept;
    void select(Utf8Offset anchor, Utf8Offset caret);
    // The UTF-16 position of a byte offset in the text, and the byte offset
    // of a UTF-16 position, as the box's text store converts them (a
    // Windows Forms selection is in UTF-16 units). Throw std::out_of_range
    // past the text, std::invalid_argument inside a scalar or between the
    // halves of a surrogate pair.
    [[nodiscard]] Utf16Offset utf16_offset(Utf8Offset position) const;
    [[nodiscard]] Utf8Offset utf8_offset(Utf16Offset position) const;
    void select_all();
    [[nodiscard]] std::string selected_text() const;
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    bool undo();
    bool redo();
    // Establish a new history boundary without replacing text, moving the
    // selection/viewport, or publishing text/selection change events.
    void clear_undo_history();
    bool replace_selection(std::string_view replacement);
    bool delete_selection();
    bool copy();
    bool cut();
    bool paste();
    // The edit filter sees every user edit (TextEditKind) after the TextBox's
    // own checks (read-only, UTF-8, grapheme boundaries) and before it is
    // applied; a composite control uses it to refuse or rewrite input, as
    // MaskedTextBox does. It does not see text assignments, undo or redo. A filter
    // may change this TextBox's text (by assigning text) only when it refuses the
    // edit, to show a value it looked up, as a read-only DomainUpDown does;
    // an edit during whose filter call the text changed is abandoned whatever
    // the verdict. A rewrite is applied as one history step
    // and is still subject to maximum_length and the multiline limits. The
    // delegate does not own its target; clear it (an empty Delegate) before
    // the target goes away.
    void set_edit_filter(Delegate<TextEdit&> filter) noexcept;

    // Whether the source can offer candidates: accepted for none and
    // custom_source; for a host source, the window's host services' answer
    // (unsupported from every host today, and without a host).
    [[nodiscard]] HostServiceStatus auto_complete_source_status();
    // The control the suggestion list opens under: this box unless set (an
    // editable ComboBox sets itself). It must contain this box; it is
    // observed, not owned, and a stale or unrelated anchor falls back to
    // this box. A null anchor restores the default.
    void set_auto_complete_anchor(const Control::Ptr& anchor);
    // Whether the suggestion list is open, the suggestions it shows (in
    // source order, each text once, at most maximum_auto_complete_suggestions),
    // and the highlighted one.
    [[nodiscard]] bool auto_complete_open() const noexcept;
    [[nodiscard]] std::span<const std::string> auto_complete_suggestions() const noexcept;
    [[nodiscard]] std::optional<std::size_t> auto_complete_highlighted() const noexcept;
    // Closes the suggestion list and ends a pending appended completion,
    // keeping the text as it is.
    void close_auto_complete();

    // Styled text runs (text_box_decorations.cpp): decoration layers over
    // the text, for syntax highlighting, diagnostics and search matches
    // (see TextDecorations). A multiline box draws them; a single-line box
    // keeps them but draws its text in one style. The text never changes.
    // Ranges are UTF-8 offsets into text(); a range past the text throws
    // std::out_of_range, and the collection's own refusals
    // (std::invalid_argument) apply, before anything changes. Decorations
    // follow every edit, undo, redo and assignment of the text.
    // A change of colours, underlines or grounds repaints only the rows of
    // the range it touched; a change of weight or italics lays the text out
    // again, so the caret, hit testing and the accessibility layout keep to
    // the glyphs. The selection band and its ink stay over decorations;
    // underlines show through it.
    void set_decorations(TextDecorationLayer layer, std::span<const TextDecoration> decorations);
    // Replaces `layer`'s decorations within `range` only (an incremental
    // highlighter's update; TextDecorations::replace_range).
    void replace_decorations(TextDecorationLayer layer, Utf8Range range,
                             std::span<const TextDecoration> decorations);
    void clear_decorations(TextDecorationLayer layer);
    void clear_all_decorations();
    [[nodiscard]] const TextDecorations& decorations() const noexcept { return decorations_; }
    // The composed runs over `range` (cleared first), as the box draws them.
    void styled_runs(Utf8Range range, std::vector<StyledTextRun>& runs);

    [[nodiscard]] Event<const std::string&>& text_changed() noexcept {
        return text_changed_;
    }
    [[nodiscard]] Event<const TextSelection&>& selection_changed() noexcept {
        return selection_changed_;
    }
    // Raised by plain Enter in a single-line box that accepts_enter, when
    // the box has nothing else to accept. It carries the text.
    [[nodiscard]] Event<const std::string&>& committed() noexcept {
        return committed_;
    }
    // Raised by plain Escape in a box that accepts_escape, when the box has
    // nothing else to cancel.
    [[nodiscard]] Event<>& cancelled() noexcept { return cancelled_; }
    // Raised with true after the suggestion list opens and with false after
    // it closes, whichever way it closed.
    [[nodiscard]] Event<bool>& auto_complete_open_changed() noexcept {
        return auto_complete_open_changed_;
    }
    // Raised after the user accepts a completion (a suggestion chosen with
    // Enter or the pointer, or an appended completion taken with Enter or
    // Tab), once the text holds it and the list has closed. It carries the
    // accepted text.
    [[nodiscard]] Event<const std::string&>& auto_complete_accepted() noexcept {
        return auto_complete_accepted_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    // The layout as painted: one line for a single-line box, the visual
    // (wrapped) lines for a multiline one; graphemes at their painted
    // positions after scrolling; word starts by next_word_boundary (the
    // Ctrl+Right rule). A password box publishes no text: no_text.
    [[nodiscard]] SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout) override;
    [[nodiscard]] SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection) override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry well;
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

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_scroll_position_changed() override;
    [[nodiscard]] bool paints_scrolled_content() const noexcept override { return true; }
    // Multiline opt-in: the document extent drives the inherited scroll bars;
    // turning auto scroll off keeps the document where it was.
    void on_auto_scroll_changed(Point previous_position) override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // What the empty field shows in light grey: the placeholder, else the
    // field hint while focused, else nothing.
    [[nodiscard]] std::string empty_field_text() const;

    struct VisualRun final {
        std::size_t start{};
        std::size_t end{};
        double x{};
        // The font the run was measured in: the content font, or a
        // decoration's weight and italics over it.
        FontSpec font{};
    };
    struct VisualLine final {
        std::vector<std::size_t> offsets{};
        std::vector<double> positions{};
        std::vector<VisualRun> runs{};
    };
    void ensure_multiline_layout();
    void rebuild_multiline_rows(double width);
    // Fill the lines and graphemes of a host's text layout query (A-T1);
    // the multiline form needs current visual rows. The editing engine adds
    // the word starts.
    void fill_single_line_text_layout(SemanticTextLayout& layout) const;
    void fill_multiline_text_layout(SemanticTextLayout& layout) const;
    [[nodiscard]] Rect multiline_text_rectangle() const noexcept;
    void set_multiline_scroll(Point position);
    [[nodiscard]] AnatomyStateSet text_states() const;
    [[nodiscard]] PartPaint text_part_template(FontSpec part_font, bool selected) const;
    void paint_selection_band(Painter& painter, Rect band) const;
    void paint_caret(Painter& painter, Rect caret) const;
    [[nodiscard]] Rect paint_multiline(Painter& painter);
    void reveal_multiline_caret();
    [[nodiscard]] std::size_t caret_line() const;
    [[nodiscard]] Utf8Offset multiline_position_at(double x, double y);
    [[nodiscard]] Utf8Offset position_in_line(std::size_t line, double x) const;
    [[nodiscard]] double multiline_boundary_x(std::size_t line, Utf8Offset offset) const;

    // Decorations (text_box_decorations.cpp).
    // The font of the styled run holding `offset` (composed into
    // styled_scratch_ for the logical line being laid out) and the offset
    // where that font ends, at most `limit`.
    struct RunFont final {
        FontSpec font{};
        std::size_t end{};
    };
    [[nodiscard]] RunFont run_font_at(std::size_t offset, std::size_t limit, FontSpec content_font) const;
    // Checks a caller's range against the text (std::out_of_range).
    void require_decoration_range(Utf8Range range) const;
    // Repaints what a decoration change touched: the rows of its range, or
    // the whole box (and its layout) when a font changed or the rows are
    // not current.
    void decorations_changed(const TextDecorationChange& change);
    // The text rectangle's part showing the rows of `range`, or none when
    // the rows are not current.
    [[nodiscard]] std::optional<Rect> multiline_range_damage(Utf8Range range) const;
    // One visual row with decorations: grounds, then the selection band,
    // then each styled run's glyphs (the selection's ink inside the band),
    // then underlines.
    struct DecoratedRow final {
        std::size_t line{};
        double origin{};
        double y{};
        bool selected{};
        Rect band{};
        double clip_left{};
        double clip_right{};
    };
    void paint_decorated_row(Painter& painter, const DecoratedRow& row, const PartPaint& text_part,
                             const PartPaint& selected_part);
    [[nodiscard]] PartPaint decorated_text_part(const PartPaint& base, const TextDecorationStyle& style) const;
    [[nodiscard]] Color decoration_underline_color(const PartPaint& part, const TextDecorationStyle& style) const;

    // Auto-complete (text_box_auto_complete.cpp). The session holds the
    // typed text, the suggestions, a pending appended completion and the
    // open overlay; it is created on first use and owned by this box.
    [[nodiscard]] bool auto_complete_active() const noexcept;
    [[nodiscard]] detail::TextBoxAutoCompleteSession& auto_complete_session();
    // After a user edit of `kind` that changed the text. Composition input
    // (`composing`) never appends and closes the list.
    void auto_complete_after_edit(TextEditKind kind, bool composing);
    // Up, Down, Enter, Tab and Escape while a completion is pending.
    [[nodiscard]] bool auto_complete_key(const KeyEvent& event);
    void collect_auto_complete_candidates(std::string_view typed,
                                          std::vector<std::string>& matches);
    void open_auto_complete_list();
    void place_auto_complete_list();
    void highlight_auto_complete(std::optional<std::size_t> index);
    void accept_auto_complete(std::string accepted);
    void restore_auto_complete_typed_text();
    void end_auto_complete(bool restore_typed);
    void close_auto_complete_list();
    // Forgets the open list without raising events (detachment).
    void discard_auto_complete_list() noexcept;
    [[nodiscard]] bool auto_complete_completion_pending() const noexcept;
    // Shows completion text without an edit-filter call or a history step:
    // the completion belongs to the user edit that produced it, so undo
    // removes both.
    void show_completion_text(std::string_view shown_text, TextSelection selection);
    void auto_complete_list_selection_changed(const ListSelectionChange& change);
    void auto_complete_list_activated(std::size_t index);
    void auto_complete_list_dismissed(OverlayDismissReason reason);
    void auto_complete_overlay_revoked();
    void auto_complete_popup_closed(std::size_t slot);
    void auto_complete_collection_changed();

    // How the editing engine reaches this box (text_box.cpp).
    class EditingHost;

    void set_selection(TextSelection selection, bool reveal_caret = true);
    // The engine changed the text and the selection together.
    void text_edited();
    [[nodiscard]] Utf8Offset position_at(double local_x) const noexcept;
    [[nodiscard]] double boundary_x(Utf8Offset offset) const noexcept;
    [[nodiscard]] std::string display_text() const;
    void reset_caret_blink();
    void schedule_caret_blink();
    // The caret blink timer's target. Observes the box; a destroyed one is
    // skipped.
    struct CaretTick final {
        std::weak_ptr<TextBox> box;
        void operator()(FrameTime now) const;
    };
    // One blink: toggles the caret and damages it.
    void toggle_caret();
    void retain_caret_damage(const Rect line_bounds, const Rect clip);
    [[nodiscard]] bool caret_damage_current() const;

    // The engine borrows the host, so the host is declared first and
    // destroyed last. Neither is ever null.
    std::unique_ptr<EditingHost> editing_host_;
    std::unique_ptr<detail::TextEditingEngine> editing_;
    std::vector<double> layout_positions_{};
    std::vector<std::uint64_t> layout_offsets_{};
    double layout_text_scale_{};
    double horizontal_offset_{};
    bool reveal_pending_{true};
    // A paging key moved the caret: the next caret reveal scrolls smoothly
    // (ScrollableControl::glide_to) instead of at once. Taken by that reveal.
    bool glide_next_reveal_{};
    bool caret_upstream_{};
    bool next_upstream_{};
    double vertical_offset_{};
    double preferred_x_{-1.0};
    double line_height_{14.4};
    double line_ascent_{12.0};
    double document_width_{};
    std::vector<VisualLine> visual_lines_{};
    // One logical line of the text the rows were last built from: its
    // content's bytes, the next line's start (the text's size for the last
    // line) and its visual rows. An edit re-lays out only the lines it
    // changed and copies the others' rows (audit P3).
    struct LaidLine final {
        std::size_t start{};
        std::size_t end{};
        std::size_t next{};
        std::size_t first_row{};
        std::size_t row_count{};
    };
    // The text the rows were built from (a copy, as long as the document)
    // and its logical lines; empty until a full build commits them.
    std::string laid_text_{};
    std::vector<LaidLine> laid_lines_{};
    // Scratch for the next laid_lines_ and the previous build's rows, kept
    // for their capacity.
    std::vector<LaidLine> laying_lines_{};
    std::vector<VisualLine> previous_visual_lines_{};
    std::uint64_t multiline_revision_{};
    // The decorations' layout revision the rows were measured with.
    std::uint64_t multiline_decoration_revision_{};
    TextDecorations decorations_{};
    // Composition scratch for layout and paint, reused across rows.
    std::vector<StyledTextRun> styled_scratch_{};
    FontSpec multiline_font_{};
    double multiline_width_{-1.0};
    Size multiline_client_size_{-1.0, -1.0};
    bool multiline_scroll_enabled_{};
    double multiline_device_scale_{};
    const TextMetricsProvider* multiline_provider_{};
    bool focused_{};
    bool selecting_{};
    bool caret_visible_{true};
    // The caret blink is paint-gated (ADR-016): a blink that finds the box
    // not painted since the last one (a covered window or a hidden box)
    // stops the timer, and the next paint starts it again. Before the first
    // blink there is no earlier one to have been painted.
    bool caret_painted_{true};
    bool caret_waiting_for_paint_{};
    // Last successfully recorded geometry, including the invisible blink phase.
    // Dirty state rejects other pending visual changes before this is reused.
    struct CaretDamage final {
        Rect rectangle{};
        Rect bounds{};
        FontSpec font{};
        TextSelection selection{};
        Point scroll{};
        const TextMetricsProvider* provider{nullptr}; // Identity only; never dereferenced.
        std::uint64_t revision{0};
        double scale{0.0};
        bool valid{false};
    };
    CaretDamage caret_damage_{};
    FrameRequestToken caret_frame_{};
    Control::WeakPtr auto_complete_anchor_{};
    std::unique_ptr<detail::TextBoxAutoCompleteSession> auto_complete_session_;
    Event<const std::string&> text_changed_{};
    Event<const TextSelection&> selection_changed_{};
    Event<const std::string&> committed_{};
    Event<> cancelled_{};
    Event<bool> auto_complete_open_changed_{};
    Event<const std::string&> auto_complete_accepted_{};

    static constexpr double text_left_ = 6.0;
    static constexpr std::size_t maximum_configured_length_ = 16U * 1024U * 1024U;
    // property_codemod begin TextBox (tools/property_families/text_box.json; generated, do not edit)
public:
    // TextBox's property table: Panel's entries, then its own.
    static constexpr std::size_t text_box_property_count = Panel::panel_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, text_box_property_count>& text_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_text_changed();
    virtual void on_read_only_changed();
    virtual void on_multiline_changed();
    virtual void on_accepts_tab_changed();
    virtual void on_font_changed();

private:
    friend struct TextBoxTextCapability<TextBox>;
    friend struct TextBoxPlaceholderTextCapability<TextBox>;
    friend struct TextBoxReadOnlyCapability<TextBox>;
    friend struct TextBoxMultilineCapability<TextBox>;
    friend struct TextBoxWordWrapCapability<TextBox>;
    friend struct TextBoxNewlineSequenceCapability<TextBox>;
    friend struct TextBoxAcceptsTabCapability<TextBox>;
    friend struct TextBoxAcceptsEnterCapability<TextBox>;
    friend struct TextBoxAcceptsEscapeCapability<TextBox>;
    friend struct TextBoxMaximumLengthCapability<TextBox>;
    friend struct TextBoxPasswordCharacterCapability<TextBox>;
    friend struct TextBoxUseSystemPasswordCharacterCapability<TextBox>;
    friend struct FontCapability<TextBox>;
    friend struct TextBoxAutoCompleteModeCapability<TextBox>;
    friend struct TextBoxAutoCompleteSourceCapability<TextBox>;
    friend struct TextBoxAutoCompleteCustomSourceCapability<TextBox>;
    friend struct TextBoxAutoCompleteCaseSensitiveCapability<TextBox>;
    friend struct TextBoxTextAlignmentCapability<TextBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(text)& cell) const;
    void prepare_write(const decltype(placeholder_text)& cell, std::string& incoming);
    void written(const decltype(placeholder_text)& cell);
    void write_view(const decltype(read_only)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(read_only)& cell) const;
    void prepare_write(const decltype(multiline)& cell, bool& incoming);
    void written(const decltype(multiline)& cell);
    void prepare_write(const decltype(word_wrap)& cell, bool& incoming);
    void written(const decltype(word_wrap)& cell);
    void prepare_write(const decltype(newline_sequence)& cell, std::string& incoming);
    void written(const decltype(newline_sequence)& cell);
    void prepare_write(const decltype(accepts_tab)& cell, bool& incoming);
    void written(const decltype(accepts_tab)& cell);
    void prepare_write(const decltype(accepts_enter)& cell, bool& incoming);
    void written(const decltype(accepts_enter)& cell);
    void prepare_write(const decltype(accepts_escape)& cell, bool& incoming);
    void written(const decltype(accepts_escape)& cell);
    void write_view(const decltype(maximum_length)& cell, std::size_t incoming);
    [[nodiscard]] std::size_t read_view(const decltype(maximum_length)& cell) const;
    void prepare_write(const decltype(password_character)& cell, char32_t& incoming);
    void written(const decltype(password_character)& cell);
    void prepare_write(const decltype(use_system_password_character)& cell, bool& incoming);
    void written(const decltype(use_system_password_character)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(auto_complete_mode)& cell, AutoCompleteMode& incoming);
    void written(const decltype(auto_complete_mode)& cell);
    void prepare_write(const decltype(auto_complete_source)& cell, AutoCompleteSource& incoming);
    void written(const decltype(auto_complete_source)& cell);
    void prepare_write(const decltype(auto_complete_custom_source)& cell, std::shared_ptr<AutoCompleteStringCollection>& incoming);
    void written(const decltype(auto_complete_custom_source)& cell);
    void prepare_write(const decltype(auto_complete_case_sensitive)& cell, bool& incoming);
    void written(const decltype(auto_complete_case_sensitive)& cell);
    void prepare_write(const decltype(text_alignment)& cell, HorizontalAlignment& incoming);
    void written(const decltype(text_alignment)& cell);
    // property_codemod end TextBox
};

} // namespace gui_forms
