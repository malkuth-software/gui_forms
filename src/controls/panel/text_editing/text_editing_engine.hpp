#pragma once

#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/text.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace gui_forms::detail {

// The text editing engine shared by the text controls (owner decision
// 2026-10-28): the text and its selection, user edits with their edit
// filter and limits, undo and redo history, input-method replacement and
// composition, the clipboard, the editing keys and the word rule. It knows
// nothing of layout or painting: the control that owns it lays the text
// out, paints it, and moves the caret between lines.
//
// The engine tells its control what happened through a TextEditingHost and
// never publishes an event itself, so the control keeps the order of its
// cache updates, invalidation and events.

// What a control does around the engine's changes. Every call is made on
// the control's thread, from inside an engine operation.
class TextEditingHost {
public:
    virtual ~TextEditingHost() = default;

    // The control's own mutation check (Control::require_mutable), made at
    // the start of every operation that may change the text.
    virtual void editing_require_mutable() const = 0;
    // Undo or redo is about to replace the text with a history entry.
    virtual void editing_restoring() = 0;
    // The text and the selection changed together (an edit, an undo or a
    // redo): the control drops its layout, shows the caret, invalidates and
    // publishes its text and selection changes, in that order.
    virtual void editing_text_changed() = 0;
    // Moves the selection, revealing the caret, without changing the text.
    virtual void editing_select(TextSelection selection) = 0;
    // A key is about to move the caret or select everything.
    virtual void editing_navigating() = 0;
    // A key or text input changed the text with an edit of kind; composing
    // when the input was an unfinished composition.
    virtual void editing_user_edit(TextEditKind kind, bool composing) = 0;
    // The window's host services for the clipboard, or null without one.
    [[nodiscard]] virtual HostServices* editing_host_services() const = 0;
    // Whether the text is masked: masked text is never copied or cut.
    [[nodiscard]] virtual bool editing_password_protected() const = 0;
};

class TextEditingEngine final {
public:
    // host must outlive the engine; the engine keeps a reference to it.
    TextEditingEngine(TextEditingHost& host, std::string_view text);
    TextEditingEngine(const TextEditingEngine&) = delete;
    TextEditingEngine& operator=(const TextEditingEngine&) = delete;

    [[nodiscard]] const TextStore& store() const noexcept { return store_; }
    [[nodiscard]] std::string_view text() const noexcept { return store_.utf8(); }
    [[nodiscard]] TextSelection selection() const noexcept { return selection_; }
    // Records a selection the control has validated; no host call.
    void set_selection_value(TextSelection selection) noexcept { selection_ = selection; }

    [[nodiscard]] bool read_only() const noexcept { return read_only_; }
    void set_read_only(bool read_only) noexcept { read_only_ = read_only; }
    [[nodiscard]] std::size_t maximum_length() const noexcept { return maximum_length_; }
    void set_maximum_length(std::size_t length) noexcept { maximum_length_ = length; }
    // The most UTF-8 bytes an edit may leave; 0 (the default) sets no bound.
    [[nodiscard]] std::size_t maximum_bytes() const noexcept { return maximum_bytes_; }
    void set_maximum_bytes(std::size_t bytes) noexcept { maximum_bytes_ = bytes; }
    // A multiline engine holds every text to TextBox::validate_multiline_text.
    [[nodiscard]] bool multiline() const noexcept { return multiline_; }
    void set_multiline(bool multiline) noexcept { multiline_ = multiline; }
    void set_edit_filter(Delegate<TextEdit&> filter) noexcept { edit_filter_ = filter; }
    [[nodiscard]] bool has_edit_filter() const noexcept {
        const bool present = static_cast<bool>(edit_filter_);
        return present;
    }
    // The last change of the text, whichever way it came (an edit, undo or
    // redo, a filter's rewrite, set_text or show_text): the bytes it
    // replaced and how many took their place. Decorations follow it
    // (TextDecorations::shift_for_edit). Before any change it is empty.
    [[nodiscard]] TextChange last_change() const noexcept { return last_change_; }

    // Whether the last text input was an unfinished composition with text.
    [[nodiscard]] bool composing() const noexcept { return composing_; }
    void end_composition() noexcept { composing_ = false; }

    // Replaces the whole text without history: the caret goes to the end,
    // the history and any composition end. No host call.
    void set_text(std::string_view text);
    // Shows text the control supplies (an auto-complete completion) with
    // selection, outside the history. Returns whether the text changed; the
    // control does the rest. No host call.
    [[nodiscard]] bool show_text(std::string_view text, TextSelection selection);

    [[nodiscard]] bool can_undo() const noexcept {
        const bool result = !undo_.empty();
        return result;
    }
    [[nodiscard]] bool can_redo() const noexcept {
        const bool result = !redo_.empty();
        return result;
    }
    bool undo();
    bool redo();
    // A new history boundary: both histories empty.
    void clear_history() noexcept;

    // A user edit: replace [start, end) with replacement, through the read-
    // only state, the boundaries, the edit filter and the limits, as one
    // history step. Returns whether it was applied (or only moved the caret).
    bool replace(Utf8Offset start, Utf8Offset end, std::string_view replacement, TextEditKind kind);
    bool replace_selection(std::string_view replacement);
    bool delete_selection();
    [[nodiscard]] std::string selected_text() const;
    bool copy();
    bool cut();
    bool paste();

    // The word rule (Ctrl+Left and Ctrl+Right).
    [[nodiscard]] Utf8Offset previous_word_boundary(Utf8Offset offset) const;
    [[nodiscard]] Utf8Offset next_word_boundary(Utf8Offset offset) const;
    // Adds the word starts of a host's text layout query: 0, each later word
    // start, and the end. Empty text has none.
    void fill_word_starts(SemanticTextLayout& layout) const;

    // The editing keys, after the control's own (line movement, Tab, Enter,
    // Escape): select all, the clipboard, undo and redo, Left and Right by
    // grapheme, word or line, Home and End of the text, Backspace and Delete.
    void handle_key(KeyEvent& event);
    // Text input, including an input method's replacement range and
    // composition. The control checks focus and enablement first.
    void handle_text_input(TextInputEvent& event);

private:
    struct Snapshot final {
        std::string text{};
        TextSelection selection{};
    };

    [[nodiscard]] Snapshot snapshot() const;
    void apply_snapshot(Snapshot snapshot);
    void push_history(std::deque<Snapshot>& history, Snapshot snapshot);
    void clear_redo() noexcept;
    // Replaces the whole text with a filter's rewrite, as one history step.
    bool apply_rewrite(std::string_view rewritten, Utf8Offset caret);

    // Replaces the whole text, recording the change from the old one.
    void replace_whole_text(std::string_view text);

    TextEditingHost& host_;
    TextStore store_{};
    TextChange last_change_{};
    TextSelection selection_{};
    std::deque<Snapshot> undo_{};
    std::deque<Snapshot> redo_{};
    std::size_t history_bytes_{};
    Delegate<TextEdit&> edit_filter_{};
    std::size_t maximum_length_{};
    std::size_t maximum_bytes_{};
    bool read_only_{};
    bool multiline_{};
    bool composing_{};

    static constexpr std::size_t maximum_history_entries_ = 128U;
    static constexpr std::size_t maximum_history_bytes_ = 8U * 1024U * 1024U;
};

} // namespace gui_forms::detail
