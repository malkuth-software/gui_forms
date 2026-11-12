The shared base of the text-editing controls.

`TextBoxBase` is the base of `TextBox`, `MaskedTextBox` and `RichTextBox`, as
Windows Forms names it. It cannot be created on its own; create one of those
controls instead, and use this type when code should work with any of them.
It carries the editing surface the three share: the selection, `select` and
`select_all`, the selected text, undo, and the clipboard. Each works on the
derived control's own text: a `TextBox`'s text, a `RichTextBox`'s markdown
source, and for a `MaskedTextBox` the text of the field it edits through.
The text itself, `read_only`, `redo`, `replace_selection`,
`delete_selection` and the change events stay on each derived control.

Properties
----------
selection
    The selection as `anchor` and `caret` UTF-8 byte offsets into the text,
    on grapheme boundaries; they are equal when nothing is selected. Change
    it with `select` or `select_all`.
selected_text
    A copy of the selected text, which is empty when nothing is selected.
can_undo
    Whether `undo` would restore an earlier state. Always false for a
    `MaskedTextBox`, which keeps no undo history, as in Windows Forms.

Methods
-------
select
    Sets the selection from an anchor and a caret offset. Both must lie on
    grapheme boundaries of the text, or it throws `std::out_of_range`.
select_all
    Selects the whole text.
undo
    Restores the text and selection before the last edit (in a
    `RichTextBox`, the last edit or formatting command), and returns false
    when there is nothing to undo or the control is read-only. Does nothing
    in a `MaskedTextBox`.
clear_undo_history
    Forgets all undo and redo steps without changing the text or the
    selection or raising events, for example after saving. Does nothing in
    a `MaskedTextBox`.
copy
    Copies the selection to the clipboard. It returns false when nothing is
    selected, the text is masked, or the window has no host clipboard.
cut
    Copies the selection and then deletes it. It returns false when nothing
    is selected, the control is read-only or masked, or the copy fails.
paste
    Replaces the selection with clipboard text (in a `RichTextBox`, as
    markdown source). It returns false when the control is read-only, the
    window has no host clipboard, the clipboard holds no text, or the edit
    is refused.

See Also
--------
TextBox : A field for free text.
MaskedTextBox : A text field that takes input through a mask.
RichTextBox : A field for styled text, kept as markdown.
