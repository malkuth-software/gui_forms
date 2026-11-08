A dialog for choosing one of GUI.Forms' bundled fonts.

A font dialog lists the bundled faces by the role they serve (Portsmouth
Rapids for controls, Carlito for content, Cousine for monospace), the styles
the chosen face has, and a size, and previews the result. GUI.Forms never
uses arbitrary host fonts, so these are the only choices; the result is a
`FontSpec`. `show` opens the dialog over its owner's window; the program
learns the outcome from `closed`. Use it to let the user restyle text the
application owns, such as an editor or a log view.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Its controls are
    ``<stable_id>.face-list``, ``.style-list``, ``.size``, ``.ok``,
    ``.cancel`` and ``.apply``; the layer under an open dialog is
    ``<stable_id>.layer``.

Properties
----------
font : default 9-unit Carlito, regular
    The font OK or Apply last took. A write sets it and resets the
    dialog's controls to it; an invalid `FontSpec` throws
    `std::invalid_argument`. Letter spacing is kept as given.
selection
    The font the dialog's controls show now: weight 400 or 700 (a set font
    takes the nearer), italic only for a face with italics, size within the
    size range.
minimum_size : default ``8``
    The smallest size offered. `set_size_range` sets it with `maximum_size`;
    a non-positive or reversed range throws `std::invalid_argument`, and the
    selection is clamped into it.
maximum_size : default ``72``
    The largest size offered.
show_apply : default ``true``
    Whether the Apply button is shown.
preview_text : default ``"AaBbYyZz 0123"``
    The text the preview shows. Invalid UTF-8 throws `std::invalid_argument`.
is_open
    Whether the dialog is open over a window.
face_list
    The `ListBox` of faces.
style_list
    The `ListBox` of styles: Regular and Bold, and Italic and Bold Italic for
    a face with italics.
size_box
    The `NumericUpDown` of sizes, in logical units with one decimal place.
ok_button
    The OK button: takes the selection as `font` and closes.
cancel_button
    The Cancel button: closes and keeps `font`.
apply_button
    The Apply button: takes the selection as `font` and raises `apply`.

Events
------
apply
    Raised by Apply, with the new `font`.
closed
    Raised after the dialog closes, with a `FontDialogResult`: the
    `DialogResult` (ok or cancel) and `font`. Escape, `close` and the owner
    going away (hidden, disabled or disposed) close it as a cancel.

Methods
-------
show
    Opens the dialog centred over the owner's window, above a layer that
    takes presses outside it, and holds the keyboard focus inside it until it
    closes. The controls start from `font`. Returns false when the owner is
    not attached or the dialog already has a parent (it is placed in a
    layout). There is no modal loop: `show` returns at once.
close
    Closes the open dialog with a `DialogResult`; OK takes the selection as
    `font`. Raises `closed`. Does nothing when the dialog is not open.
set_size_range
    Sets `minimum_size` and `maximum_size`.
initialize_control_tree
    Creates the dialog's controls. `make_control` calls it; do not call it
    yourself.

See Also
--------
Label : Text whose font a dialog result can set.
TypographySpecimen : The bundled faces and text roles, side by side.

Notes
-----
`bundled_font_faces` lists the faces the dialog offers. The Noto faces
GUI.Forms also bundles are fallbacks for scripts and emoji and are not
offered. Portsmouth Rapids has no italic face, so the dialog offers no italic
style for the control role. Faces have regular and bold weights only.

Windows Forms members with no meaning here are left out rather than faked:
the script list (`FontRole` faces cover their scripts through fallback
faces), effects (strikeout and underline: `FontSpec` has neither), colour
(text colour belongs to the theme), fixed-pitch-only and vector-only
filters, and the choice of arbitrary installed fonts.

Placed in a layout instead of shown, the dialog still works: OK and Cancel
raise `closed` without closing anything.

The dialog paints its face, title and preview through the by-role paint
path; its lists, size box and buttons are the stock controls. It publishes
the accessibility role `dialog` named "Font", `modal` while open, valued
with the selection, such as ``Carlito Bold 12``.

Examples
--------
Restyling a label from the dialog:

.. code-block:: cpp

   std::shared_ptr<gf::FontDialog> dialog =
       gf::make_control<gf::FontDialog>(gf::StableId("log.font"));
   (*dialog).font = gf::FontSpec{gf::FontRole::monospace, 11.0, 400, false};
   (*dialog).set_size_range(8.0, 24.0);
   const gf::FontSpec chosen = (*dialog).selection();   // 11-unit Cousine
   // (*dialog).show(owner) opens it; its closed event carries the result.
