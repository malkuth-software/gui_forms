#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gui_forms {

struct GUI_FORMS_API TextSelection final {
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

namespace detail {
// The text editing engine the editing controls edit through
// (src/controls/panel/text_editing).
class TextEditingEngine;
} // namespace detail

// The text-editing controls' base, as Windows Forms' TextBoxBase: TextBox,
// MaskedTextBox and RichTextBox derive from it, so a TextBoxBase pointer
// holds any of them. It cannot be made on its own (its constructor is
// protected).
//
// It carries the editing surface the three share (owner-accepted base
// proposal, CP29 I5): the selection, select and select_all, the selected
// text, undo, and the clipboard. Each works on the leaf's editing engine;
// MaskedTextBox edits through the TextBox it owns, so its surface works on
// that field's engine. MaskedTextBox keeps no undo history of its own, as in
// Windows Forms: can_undo reports false, and undo and clear_undo_history do
// nothing. Text, read_only and the other common properties stay cells each
// leaf declares through its own capabilities, and Text and TextChanged stay
// per leaf. Redo, replace_selection and delete_selection stay on TextBox and
// RichTextBox. Its virtual functions are the two hooks the surface needs
// from a leaf: its engine, and how it shows a selection.
class GUI_FORMS_API TextBoxBase : public Panel {
public:
    ~TextBoxBase() override;

    // The selection, as byte offsets into the text (empty, at 0, before the
    // leaf made its engine).
    [[nodiscard]] TextSelection selection() const noexcept;
    // Selects [anchor, caret); throws std::out_of_range unless both offsets
    // are grapheme boundaries of the text, std::logic_error before the leaf
    // made its engine.
    void select(Utf8Offset anchor, Utf8Offset caret);
    // Selects the whole text (nothing before the leaf made its engine).
    void select_all();
    // A copy of the selected text.
    [[nodiscard]] std::string selected_text() const;
    // Whether undo would change the text (always false for MaskedTextBox).
    [[nodiscard]] bool can_undo() const noexcept;
    // Undoes the last edit; returns whether the text changed.
    bool undo();
    // Ends the history without changing the text, the selection or the
    // view, and raises nothing.
    void clear_undo_history();
    // The clipboard: copy writes the selected text, cut writes it and
    // deletes it, paste replaces the selection with the clipboard's text.
    // Each returns whether it did anything (read-only refuses cut and
    // paste; a password box refuses copy and cut).
    bool copy();
    bool cut();
    bool paste();

protected:
    // Whether the leaf keeps an undo history.
    enum class UndoHistory : std::uint8_t {
        kept,
        none,
    };

    explicit TextBoxBase(StableId stable_id, UndoHistory history = UndoHistory::kept);

    // The engine the surface edits, or null before the leaf made it. The
    // leaf owns it (MaskedTextBox: its field does).
    [[nodiscard]] virtual detail::TextEditingEngine* editing_engine() const noexcept = 0;
    // Shows `selection`, already checked against the text, the leaf's way
    // (its caret, scrolling and selection_changed).
    virtual void apply_selection(TextSelection selection) = 0;
    // The engine of another editing control, for a leaf that edits through
    // one it owns.
    [[nodiscard]] static detail::TextEditingEngine* engine_of(const TextBoxBase& other) noexcept;

private:
    UndoHistory undo_history_;
};

} // namespace gui_forms
