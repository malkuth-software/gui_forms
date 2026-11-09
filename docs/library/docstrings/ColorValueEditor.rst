A colour field: a swatch beside a text box that edits the colour as hexadecimal text.

A colour value editor shows its `value` as a swatch, drawn over a
checkerboard so that transparency shows, and as ``#RRGGBBAA`` text in an
`editor` text box. The user types a colour and presses Enter to commit it,
or Escape to put the current colour back. `PropertyGrid` uses it for
colour properties, and an application can place it anywhere a colour is
edited. There is no colour picker: the colour is edited only as text.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID. The text box is created
    as ``<stable_id>.text``.
initial_value
    The starting colour, which becomes `value`. The default, `Color` with no
    arguments, is opaque black.

Properties
----------
value
    The current colour. A write shows the new colour in the swatch and
    the text box and clears an invalid state left by a rejected edit; it
    does not raise `value_changed`.
editor
    The `TextBox` the user types into. It uses a monospace font.
swatch_width : default ``30``
    The width of the swatch at the left, from 12 to 96; a width outside that
    range, or one that is not finite, throws `std::invalid_argument`. The
    text box fills the rest of the control.

Events
------
value_changed
    Raised when the user commits text that names a different colour. It
    carries the new colour. It is not raised by a write to `value`.
edit_failed
    Raised when the user commits text that is not a colour. It carries a
    `PropertyEditorInputError` with the text as typed and the message
    "Color must be #RRGGBB or #RRGGBBAA". The value does not change.

Methods
-------
format_value
    Returns the canonical text for a colour: ``#`` followed by red, green,
    blue and alpha as two upper-case hexadecimal digits each, for example
    ``#2774B8FF``.
parse_value
    Reads ``#RRGGBB`` or ``#RRGGBBAA`` text, with digits in either case and
    surrounding white space ignored. ``#RRGGBB`` gives an opaque colour. It
    returns no value for any other text; for example, ``#RGB`` short forms
    and colour names are not accepted.
initialize_control_tree
    Creates the text box. `make_control` calls it; calling it again does
    nothing.

See Also
--------
PropertyGrid : Edits the properties of a control, using this editor for colours.
FlagsValueEditor : The editor `PropertyGrid` uses for flags enumerations.
TextBox : A field for free text.

Notes
-----
`value_changed` is raised only when the user commits an edit, never when
Writing `value` changes it. An application that changes the colour
itself already knows the new colour; observe `value_changed` to learn what
the user chose.

A commit happens when the user presses Enter in the text box. Valid text
becomes the value, the text is rewritten in canonical form, and
`value_changed` is raised if the colour differs; committing the current
colour again only rewrites the text. Invalid text marks the editor and its
text box invalid, raises `edit_failed`, and stays in the text box for the
user to correct. Escape restores the text of the current colour and clears
the invalid state without raising an event. Moving focus away commits
nothing, so typed text stays until the user presses Enter or Escape.

The control measures to the available width and a height of 28, scaled with
the window's text scale.

The control publishes the accessibility role `group`, with the canonical
colour text as its value and the state `SemanticState::invalid` after a
rejected edit. The text box is published inside it as an ordinary text
field.

Examples
--------
Parse colour text from a settings file, show it in an editor, and label it
with the canonical text. `parse_value` returns no value for text such as
``teal``:

.. code-block:: cpp

   std::shared_ptr<gf::ColorValueEditor> tint =
       gf::make_control<gf::ColorValueEditor>(gf::StableId("lamp.tint"));
   std::optional<gf::Color> parsed =
       gf::ColorValueEditor::parse_value(" #2a6fb4 ");   // opaque 42, 111, 180
   if (parsed) (*tint).value = *parsed;              // no event is raised
   std::shared_ptr<gf::Label> caption = gf::make_control<gf::Label>(
       gf::StableId("lamp.caption"),
       gf::ColorValueEditor::format_value((*tint).value.read()));   // "#2A6FB4FF"

React to colours the user commits. A write to `value` does not raise
`value_changed`, so the handler sees only the user's choices:

.. code-block:: cpp

   struct Lamp final : gf::Component {
       std::shared_ptr<gf::ColorValueEditor> tint =
           gf::make_control<gf::ColorValueEditor>(
               gf::StableId("lamp.tint"), gf::Color::rgba(39, 116, 184));
       gf::Color chosen = gf::Color::rgba(39, 116, 184);
       std::string problem;

       Lamp() {
           gf::on((*tint).value_changed(), *this, &Lamp::on_tint);
           gf::on((*tint).edit_failed(), *this, &Lamp::on_failed);
       }
       void on_tint(gf::Color color) { chosen = color; }
       void on_failed(const gf::PropertyEditorInputError& error) {
           problem = error.message;
       }
   };

   void example() {
       Lamp lamp;
       (*lamp.tint).value = gf::Color::rgba(16, 32, 48);   // chosen is unchanged
       (*lamp.tint).set_requested_bounds({0.0, 0.0, 200.0, 28.0});
       gf::Window window(lamp.tint, {200.0, 28.0});
       window.perform_layout();
       window.request_focus((*lamp.tint).editor());
       (*(*lamp.tint).editor()).select_all();
       window.dispatch_text({"#2A6FB4"});
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::enter});
       // chosen is now 42, 111, 180 and the text reads "#2A6FB4FF"
   }
