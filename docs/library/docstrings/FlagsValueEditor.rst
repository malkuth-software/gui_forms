A drop-down field that edits a flags enumeration as a list of check boxes.

A flags value editor shows the names of the flags set in its `value`, such
as "Top, Left", in a field with a drop-down arrow. Opening it shows a list
with a check box for each single flag; checking or unchecking one commits
the new combination at once, and the list stays open until the user clicks
outside it or presses Escape. The set of flags comes from its `descriptor`.
`PropertyGrid` uses it for flags properties such as `Control` anchoring. For
an enumeration where exactly one choice applies, use a `ComboBox`.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID. The drop-down list is
    created as ``<stable_id>.overlay.list`` while it is open.
initial_descriptor
    The flags enumeration to edit; it becomes `descriptor`.
initial_value
    The starting value; it becomes `value`.

Properties
----------
descriptor
    The enumeration: a type name and its choices. It must be a valid
    `PropertyEnumDescriptor`, with a nonempty type name and 1 to 256 choices
    whose names are nonempty and distinct; it must have `flags` set and at
    least one choice whose value is a single positive bit. Otherwise the
    constructor and a write throw `std::invalid_argument`. Setting a new descriptor converts the current
    value to it, and throws `std::invalid_argument` if the value has a flag
    the new descriptor lacks. An open drop-down is rebuilt.
value
    The current value as a `PropertyEnumValue`. Its type name must match the
    descriptor's, and only its numeric value counts: the name is rewritten
    as the name of a choice with exactly that value, or else as the names of
    the single-bit choices that are set, joined with ", ". A value with a
    bit that no choice declares throws `std::invalid_argument`. A write
    does not raise `value_changed`.
dropped_down : default ``false``
    Whether the list is open. Opening it needs the editor to be in a window;
    in a control that is not attached, writing true to `dropped_down` leaves it
    closed. In a window, opening it while the editor is hidden or disabled
    throws `std::logic_error`.
overlay_width : default ``240``
    The width of the list, from 80 to 1024; a width outside that range, or
    one that is not finite, throws `std::invalid_argument`. The list is never
    wider than the window. An open list is rebuilt at the new width.

Events
------
value_changed
    Raised when the user checks or unchecks a flag in the list and the value
    changes. It carries the new value. It is not raised by writes to `value` or
    `descriptor`.
drop_down_changed
    Raised after the list opens, with `true`, and after it closes, with
    `false`, however that happened.

See Also
--------
ComboBox : A drop-down list for choosing exactly one item.
CheckedListBox : A list of items with check boxes, the control the drop-down shows.
PropertyGrid : Edits the properties of a control, using this editor for flags.
ColorValueEditor : The editor `PropertyGrid` uses for colours.

Notes
-----
`value_changed` is raised only when the user commits an edit in the list,
never when a write to `value` changes it. An application that changes the
value itself already knows the new value; observe `value_changed` to learn
what the user chose.

The list shows the choice whose value is 0, if there is one, and each
choice whose value is a single bit; choices that combine several bits are
left out. It shows up to eight rows, below the field or above it when there
is no room below. Checking the zero choice clears every flag, and
unchecking it changes nothing. While the list is open it holds the keyboard
focus.

A press of the primary button on the field opens the list, and F4, Space
and Alt+Down open it while the field has focus. Escape closes an open list,
and so does a press anywhere outside it, including on the field; closing
never changes the value. Closing the list, or detaching the editor from its
window, discards the list.

The control publishes the accessibility role `combo_box`, with the value's
name as its value, the state `SemanticState::expanded` while the list is
open, and the actions `SemanticAction::focus` and either
`SemanticAction::expand` or `SemanticAction::collapse`.

Examples
--------
Edit which edges of a frame are drawn. The name is derived from the bits,
whatever name the starting value carries:

.. code-block:: cpp

   gf::PropertyEnumDescriptor edges;
   edges.type_name = "Edges";
   edges.flags = true;
   edges.choices = {{"None", 0}, {"Top", 1}, {"Left", 2}, {"Right", 4}, {"Bottom", 8}};
   std::shared_ptr<gf::FlagsValueEditor> editor =
       gf::make_control<gf::FlagsValueEditor>(
           gf::StableId("frame.edges"), edges, gf::PropertyEnumValue{"Edges", "", 3});
   // (*editor).value.read().name is "Top, Left"
   (*editor).value = gf::PropertyEnumValue{"Edges", "", 0};   // "None", no event

Open the list in a window and check a flag, as the user would. The change
commits at once and raises `value_changed`:

.. code-block:: cpp

   struct Frame final : gf::Component {
       std::shared_ptr<gf::FlagsValueEditor> edges;
       std::string drawn;

       explicit Frame(const gf::PropertyEnumDescriptor& descriptor)
           : edges(gf::make_control<gf::FlagsValueEditor>(
                 gf::StableId("frame.edges"), descriptor,
                 gf::PropertyEnumValue{"Edges", "", 3})) {
           gf::on((*edges).value_changed(), *this, &Frame::on_edges);
       }
       void on_edges(const gf::PropertyEnumValue& value) { drawn = value.name; }
   };

   void example() {
       gf::PropertyEnumDescriptor descriptor;
       descriptor.type_name = "Edges";
       descriptor.flags = true;
       descriptor.choices = {{"None", 0}, {"Top", 1}, {"Left", 2}, {"Right", 4}};
       Frame frame(descriptor);
       std::shared_ptr<gf::Panel> root =
           gf::make_control<gf::Panel>(gf::StableId("frame.root"));
       (*frame.edges).set_requested_bounds({10.0, 10.0, 200.0, 26.0});
       (*root).add_child(frame.edges);
       gf::Window window(root, {320.0, 240.0});
       window.perform_layout();
       (*frame.edges).dropped_down = true;
       std::shared_ptr<gf::CheckedListBox> list = std::dynamic_pointer_cast<gf::CheckedListBox>(
           window.find("frame.edges.overlay.list"));
       if (list) (*list).set_item_checked(3U, true);   // the "Right" row
       // drawn is "Top, Left, Right"
       (*frame.edges).dropped_down = false;
   }
