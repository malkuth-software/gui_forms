Read-only text that names, describes or reports something.

A label displays text on one line, or wrapped at words to its width, in the
theme's typography for its `text_style_role`. It never takes focus, and
pointer input passes through it to the control underneath. A mnemonic in the
text moves focus to the next control in tab order, so a label such as
`"&Name:"` placed before a `TextBox` gives the field an Alt+N shortcut. Use a
`LinkLabel` for text the user clicks, and a read-only `TextBox` for text the
user should be able to select and copy.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
text
    Text to display. `&` marks the next character as the mnemonic; write
    `&&` for a literal ampersand. A `\n` starts a new line.

Properties
----------
text : default ``""``
    The text, including any `&` mnemonic marker. Setting a different value
    raises `text_changed`.
font
    The font in use: the one given to `set_font`, or else the theme's font
    for `text_style_role`. It is scaled by the window's text scale. An invalid
    specification throws `std::invalid_argument`; `clear_font` returns to the
    theme's font.
text_style_role : default ``TextStyleRole::body``
    Which theme font the label uses when no font is set: body, control,
    caption, heading, title or monospace.
foreground
    The text colour: the one given to `set_foreground`, or else the theme's
    text colour. `clear_foreground` returns to the theme's colour.
alignment : default ``HorizontalAlignment::near``
    Horizontal position of each line: near (left), center or far (right).
vertical_alignment : default ``VerticalAlignment::center``
    Vertical position of the block of lines within the label.
text_wrapping : default ``TextWrapping::no_wrap``
    `TextWrapping::word` wraps lines at spaces to fit the label's width, and
    collapses runs of spaces to one. A word wider than the label, and text
    without spaces such as Chinese or Japanese, breaks between characters.
    Explicit line breaks are kept either way.
line_spacing : default ``1.25``
    Line height as a multiple of the font size, between 0.75 and 3.0; other
    values throw `std::invalid_argument`.
text_case_transform : default ``TextCaseTransform::none``
    Draws ASCII letters in upper or lower case without changing `text`.
maximum_lines : default ``0``
    Largest number of lines to measure and draw; zero keeps every line. The
    accessible name still holds the whole text. Values above 4096 throw
    `std::out_of_range`.
use_mnemonic : default ``true``
    Whether `&` in `text` marks a mnemonic. When false the ampersand is drawn
    and Alt shortcuts are ignored.

Events
------
text_changed
    Raised after `text` changes. It carries the new text.

Methods
-------
clear_font
    Removes the font given to `set_font`, so the label follows the theme.
clear_foreground
    Removes the colour given to `set_foreground`, so the label follows the
    theme.

See Also
--------
LinkLabel : Text that the user clicks.
TextBox : Editable, selectable text.
GroupBox : A captioned frame around related controls.

Notes
-----
Alt with the label's mnemonic letter moves focus to the next control after
the label, in tab order, that is a tab stop; the label itself is never
focused. If no such control follows the label, focus stays where it is.

When the label has both a requested width and a requested height, it keeps
that size even with `auto_size`, and unless `auto_size` is on a text change
only repaints it without laying out its container again. This makes
fixed-size labels cheap for values that change often, such as a transfer
rate. A dimension left at zero is measured from the text, and then a text
change also updates layout.

The control publishes the accessibility role `static_text`. Its name is the
accessible name, or else the text without the mnemonic marker and without
`text_case_transform`. The character after the mnemonic marker is its access
key. A label with an empty name is not exposed.

Examples
--------
Give a text field a caption and an Alt shortcut:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> form = gf::make_control<gf::Panel>(gf::StableId("form"));
   std::shared_ptr<gf::Label> caption =
       gf::make_control<gf::Label>(gf::StableId("form.name.caption"), "&Name:");
   std::shared_ptr<gf::TextBox> name =
       gf::make_control<gf::TextBox>(gf::StableId("form.name"));
   (*caption).set_requested_bounds({8.0, 8.0, 60.0, 24.0});
   (*name).set_requested_bounds({72.0, 8.0, 160.0, 24.0});
   (*name).accessible_name = "Name";
   (*form).add_child(caption);
   (*form).add_child(name);
   gf::Window window(form, gf::Size{240.0, 40.0});
   window.dispatch_key(gf::KeyEvent{gf::KeyAction::down, gf::PhysicalKey::n,
                                    gf::Modifier::alt});   // the text box now has focus

A wrapped description limited to two lines, in the theme's caption font:

.. code-block:: cpp

   std::shared_ptr<gf::Label> note = gf::make_control<gf::Label>(
       gf::StableId("export.note"),
       "Exported files keep their names; existing files are never overwritten.");
   (*note).set_text_style_role(gf::TextStyleRole::caption);
   (*note).set_text_wrapping(gf::TextWrapping::word);
   (*note).set_maximum_lines(2U);
   (*note).set_requested_bounds({0.0, 0.0, 180.0, 0.0});   // fixed width, measured height
