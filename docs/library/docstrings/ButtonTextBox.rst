A text field with buttons in its frame, such as a path field with a browse
button or a value field with a reset button.

The control draws a well holding an inner TextBox (the `editor`, which takes
the focus) and the buttons, at its trailing end, its leading end or both.
Windows Forms has no counterpart. Each button is an `EditButton` record kept
by stable ID: a mark (`EditButtonKind`: ellipsis, drop-down chevron, close,
search) or its caption, at an `EditButtonPlacement`, enabled or not, shown
or hidden.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
text : default empty
    The text, a view onto the editor's text. A change raises
    `text_changed`.
placeholder_text : default empty
    The hint the empty field shows, a view onto the editor's placeholder;
    it describes the field to assistive technology.
read_only : default false
    Whether the user may select and copy the text but not change it, a view
    onto the editor's; the buttons still act.
editor
    The inner TextBox, owned as the field's only child.
buttons
    The buttons, in the order given; `set_buttons` replaces them and throws
    `std::invalid_argument` for an empty, invalid or repeated ID, invalid
    UTF-8, or more than `maximum_buttons` buttons, the old buttons then
    staying.

Events
------
text_changed
    Raised after the text changes, with the new text.
button_clicked
    Raised when a button acts (`EditButtonClick`, with the button's ID): a
    click released over the pressed button, F4 or Alt+Down in the field,
    the semantic press, or `perform_button_click`.

Methods
-------
set_button_enabled
    Enables or disables one button. Throws `std::out_of_range` for an
    unknown button.
set_button_visible
    Shows or hides one button. Throws `std::out_of_range` for an unknown
    button.
index_of
    The position of the button with an ID, or none.
button_at
    The visible button under a point in local coordinates, or none.
perform_button_click
    Raises `button_clicked` for a button as a click would (Windows Forms'
    PerformClick). Returns false, raising nothing, for a hidden or disabled
    button or a disabled field.

See Also
--------
TextBox : The editor the field holds.
SearchField : A query field with its own marks.

Notes
-----
Leading buttons run from the start edge in, trailing ones from the text out;
right to left the ends swap. A mark button's face is square, a caption
button's as wide as its caption with padding. F4 and Alt+Down act on the
first enabled trailing button, or the first enabled button when no trailing
one is. The well takes the theme's ``editor`` family and the buttons its
``button`` family, drawn without a frame. To assistive technology the field
is one ``text_box`` node whose value is the text (the editor is not
published separately) with one ``button`` node per visible button, named by
its caption (a mark button without one by its kind: More, Open, Clear,
Search) and described by its description; a drop-down or ellipsis button
reports a pop-up. The anatomy's ``button`` item system has the parts
``button``, ``button_glyph`` and ``button_label``.

Examples
--------
A folder field with a browse button:

.. code-block:: cpp

   std::shared_ptr<gf::ButtonTextBox> folder = gf::make_control<gf::ButtonTextBox>(gf::StableId("export.folder"));
   gf::EditButton browse;
   browse.stable_id = "browse";
   browse.caption = "Browse";
   (*folder).set_buttons({browse});
   (*folder).text = std::string{"Documents"};
   const bool acted = (*folder).perform_button_click("browse");
   // acted is true: button_clicked was raised with "browse"
