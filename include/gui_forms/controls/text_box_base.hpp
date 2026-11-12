#pragma once

#include "gui_forms/controls/panel.hpp"

namespace gui_forms {

// The text-editing controls' base, as Windows Forms' TextBoxBase: TextBox,
// MaskedTextBox and RichTextBox derive from it, so a TextBoxBase pointer
// holds any of them. It cannot be made on its own (its constructor is
// protected).
//
// It carries only what those three share today, and that is nothing beyond
// Panel: their text, read_only and the other common properties are cells
// each declares through its own capabilities (their tables and change
// events are the leaves'), and the editing surface (selection, undo, the
// clipboard) belongs to TextBox and RichTextBox only, since MaskedTextBox
// edits through the TextBox it owns. A member moves up here when all three
// have it, never by giving one of them a member it lacks. It declares no
// virtual function of its own: nothing varies between the leaves through it.
class TextBoxBase : public Panel {
public:
    ~TextBoxBase() override;

protected:
    explicit TextBoxBase(StableId stable_id);
};

} // namespace gui_forms
