#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/text_box/auto_complete/auto_complete.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text.hpp"

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

// The suggestion list's events (list_box.hpp, anchored_popup_layer.hpp).
struct ListSelectionChange;
enum class PopupDismissReason : std::uint8_t;

namespace detail {
// TextBox's auto-complete state (src/controls/panel/text_box).
struct TextBoxAutoCompleteSession;
// The text editing engine TextBox edits through (src/controls/panel/text_editing).
class TextEditingEngine;
} // namespace detail

class TextBox final : public Panel {
public:
    explicit TextBox(StableId stable_id, std::string text = {});
    ~TextBox() override;

    [[nodiscard]] std::string_view text() const noexcept;
    void set_text(std::string text);
    [[nodiscard]] std::string_view placeholder_text() const noexcept {
        return placeholder_;
    }
    // The placeholder shows in the empty field. Without one, the field's
    // tooltip (Control::field_hint_text) shows there in the same light grey
    // while the field has focus.
    void set_placeholder_text(std::string text);
    [[nodiscard]] bool read_only() const noexcept;
    void set_read_only(bool read_only);
    // Opt-in plain-text document editing. Existing single-line behavior remains
    // the default. Stored line endings are preserved; Enter uses newline_sequence
    // (LF initially). Tab insertion is separately opt-in.
    [[nodiscard]] bool multiline() const noexcept { return multiline_; }
    void set_multiline(bool enabled);
    // Multiline opt-in: document extent drives the inherited scrollbars and
    // scroll_position. Wrapped documents expose only vertical overflow.
    void set_auto_scroll(bool enabled) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] bool word_wrap() const noexcept { return word_wrap_; }
    void set_word_wrap(bool enabled);
    [[nodiscard]] std::string_view newline_sequence() const noexcept { return newline_; }
    void set_newline_sequence(std::string sequence);
    [[nodiscard]] bool accepts_tab() const noexcept { return accepts_tab_; }
    void set_accepts_tab(bool enabled);
    // In a single-line box Enter is the box's own only while it has
    // something to accept: an open suggestion list or appended completion,
    // or an input-method composition. Otherwise Enter goes on to the
    // containers and the window's accept button. A box that is an edit
    // session (a search field, an in-place editor) opts in with
    // accepts_enter: plain Enter (no modifiers) with nothing else to accept
    // then raises committed() and stops there. Off by default. A multi-line
    // box always takes Enter to insert newline_sequence.
    [[nodiscard]] bool accepts_enter() const noexcept { return accepts_enter_; }
    void set_accepts_enter(bool enabled);
    // Escape is the box's own only while it has something to cancel: an open
    // suggestion list or appended completion, an input-method composition or
    // a selection drag. Otherwise Escape, with or without Shift, goes on to
    // the containers, menus and the window's cancel button. A box that is an
    // edit session (an in-place editor) opts in with accepts_escape: plain
    // Escape (no modifiers) with nothing else to cancel then raises
    // cancelled() and stops there. Off by default.
    [[nodiscard]] bool accepts_escape() const noexcept { return accepts_escape_; }
    void set_accepts_escape(bool enabled);
    // Provisional development bounds until cluster-position metrics replace
    // prefix measurement. Validation/refusal never truncates source text.
    static constexpr std::size_t maximum_multiline_bytes = 1024U * 1024U;
    static constexpr std::size_t maximum_multiline_line_bytes = 4096U;
    enum class MultilineValidation : std::uint8_t {
        valid, invalid_utf8, document_too_large, line_too_long
    };
    [[nodiscard]] static MultilineValidation validate_multiline_text(std::string_view text);
    [[nodiscard]] std::size_t visual_line_count();
    [[nodiscard]] Point scroll_offset() const noexcept {
        if (multiline_ && auto_scroll()) return scroll_position();
        const Point result{.x = horizontal_offset_, .y = vertical_offset_};
        return result;
    }
    [[nodiscard]] std::size_t maximum_length() const noexcept;
    // Zero admits any text that fits the TextStore. A positive limit applies
    // to user edits, paste, and semantic set-value without truncating a value
    // assigned programmatically through set_text().
    void set_maximum_length(std::size_t length);
    [[nodiscard]] char32_t password_character() const noexcept {
        return password_character_;
    }
    void set_password_character(char32_t character);
    [[nodiscard]] bool use_system_password_character() const noexcept {
        return use_system_password_character_;
    }
    void set_use_system_password_character(bool enabled);
    [[nodiscard]] bool password_protected() const noexcept {
        const bool result = use_system_password_character_ || password_character_ != U'\0';
        return result;
    }
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    [[nodiscard]] TextSelection selection() const noexcept;
    void select(Utf8Offset anchor, Utf8Offset caret);
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
    // MaskedTextBox does. It does not see set_text, undo or redo. A filter
    // may change this TextBox's text (with set_text) only when it refuses the
    // edit, to show a value it looked up, as a read-only DomainUpDown does;
    // an edit during whose filter call the text changed is abandoned whatever
    // the verdict. A rewrite is applied as one history step
    // and is still subject to maximum_length and the multiline limits. The
    // delegate does not own its target; clear it (an empty Delegate) before
    // the target goes away.
    void set_edit_filter(Delegate<TextEdit&> filter) noexcept;

    // Auto-complete, as Windows Forms' AutoCompleteMode, AutoCompleteSource
    // and AutoCompleteCustomSource. It acts only in a single-line, unmasked,
    // writable box with no edit filter (a composite that filters edits owns
    // the text); otherwise the settings are kept and do nothing. Matching is
    // by prefix, ignoring ASCII case unless auto_complete_case_sensitive.
    [[nodiscard]] AutoCompleteMode auto_complete_mode() const noexcept {
        return auto_complete_mode_;
    }
    void set_auto_complete_mode(AutoCompleteMode mode);
    [[nodiscard]] AutoCompleteSource auto_complete_source() const noexcept {
        return auto_complete_source_;
    }
    // list_items throws std::invalid_argument: a TextBox has no list (an
    // editable ComboBox offers its items through its own setting).
    void set_auto_complete_source(AutoCompleteSource source);
    // Never null: a new box owns an empty collection. Assigning one shares it
    // (see AutoCompleteStringCollection); null throws std::invalid_argument.
    [[nodiscard]] const std::shared_ptr<AutoCompleteStringCollection>&
    auto_complete_custom_source() const noexcept {
        return auto_complete_custom_source_;
    }
    void set_auto_complete_custom_source(std::shared_ptr<AutoCompleteStringCollection> source);
    [[nodiscard]] bool auto_complete_case_sensitive() const noexcept {
        return auto_complete_case_sensitive_;
    }
    void set_auto_complete_case_sensitive(bool case_sensitive);
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
    void on_frame(FrameTime now) override;
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
    [[nodiscard]] Rect paint_multiline(Painter& painter);
    void reveal_multiline_caret();
    [[nodiscard]] std::size_t caret_line() const;
    [[nodiscard]] Utf8Offset multiline_position_at(double x, double y);
    [[nodiscard]] Utf8Offset position_in_line(std::size_t line, double x) const;
    [[nodiscard]] double multiline_boundary_x(std::size_t line, Utf8Offset offset) const;

    [[nodiscard]] BindingValue text_property_value() const;

    // Auto-complete (text_box_auto_complete.cpp). The session holds the
    // typed text, the suggestions, a pending appended completion and the
    // open popup; it is created on first use and owned by this box.
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
    void show_completion_text(std::string_view text, TextSelection selection);
    void auto_complete_list_selection_changed(const ListSelectionChange& change);
    void auto_complete_list_activated(std::size_t index);
    void auto_complete_list_dismissed(PopupDismissReason reason);
    void auto_complete_popup_revoked();
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
    void retain_caret_damage(const Rect line_bounds, const Rect clip);
    [[nodiscard]] bool caret_damage_current() const;

    // The engine borrows the host, so the host is declared first and
    // destroyed last. Neither is ever null.
    std::unique_ptr<EditingHost> editing_host_;
    std::unique_ptr<detail::TextEditingEngine> editing_;
    std::string placeholder_{};
    FontSpec font_{FontRole::content, 12.0, 400, false};
    std::vector<double> layout_positions_{};
    std::vector<std::uint64_t> layout_offsets_{};
    double layout_text_scale_{};
    double horizontal_offset_{};
    bool multiline_{};
    bool word_wrap_{};
    bool accepts_tab_{};
    bool accepts_enter_{};
    bool accepts_escape_{};
    std::string newline_{"\n"};
    bool reveal_pending_{true};
    bool caret_upstream_{};
    bool next_upstream_{};
    double vertical_offset_{};
    double preferred_x_{-1.0};
    double line_height_{14.4};
    double line_ascent_{12.0};
    double document_width_{};
    std::vector<VisualLine> visual_lines_{};
    std::uint64_t multiline_revision_{};
    FontSpec multiline_font_{};
    double multiline_width_{-1.0};
    Size multiline_client_size_{-1.0, -1.0};
    bool multiline_scroll_enabled_{};
    double multiline_device_scale_{};
    const TextMetricsProvider* multiline_provider_{};
    char32_t password_character_{};
    bool use_system_password_character_{};
    bool focused_{};
    bool selecting_{};
    bool caret_visible_{true};
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
    AutoCompleteMode auto_complete_mode_{AutoCompleteMode::none};
    AutoCompleteSource auto_complete_source_{AutoCompleteSource::none};
    std::shared_ptr<AutoCompleteStringCollection> auto_complete_custom_source_;
    bool auto_complete_case_sensitive_{};
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
};

} // namespace gui_forms
