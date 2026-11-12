The shared base of the text-editing controls.

`TextBoxBase` is the base of `TextBox`, `MaskedTextBox` and `RichTextBox`, as
Windows Forms names it. It cannot be created on its own; create one of those
controls instead, and use this type when code should work with any of them.
It adds nothing to `Panel` yet: each derived control declares its own text,
read-only state and events, and only `TextBox` and `RichTextBox` edit through
a selection with undo and the clipboard.

See Also
--------
TextBox : A field for free text.
MaskedTextBox : A text field that takes input through a mask.
RichTextBox : A field for styled text, kept as markdown.
