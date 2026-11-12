A scrolling list of named settings in collapsible groups, each with a value and an optional editor.

A property list shows rows of name and value pairs under group headers, in
the manner of a settings sheet or an inspector. Each row is described by a
`PropertyRowSpec`: it can be read-only text, a text box, a drop-down of
choices, a True/False check box, or an editor the application supplies, and
it can carry a description, a validation message, a Reset button and child
rows behind a disclosure arrow. The list owns the editors and reports what
the user does through its events; the application keeps the data and
decides what a change means. Use `PropertyGrid` instead to edit the
registered properties of a control without describing the rows yourself.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
groups
    The groups and rows, as given to `set_groups` and updated as values
    and expansion states change. Setting it replaces every row and disposes
    every editor, including editors returned earlier by `editor`; see the
    notes for the rules it checks.
header_content
    A control shown above the first group, which scrolls with the rows.
    Set it with `set_header_content`. There is none by default.
header_height
    The height reserved for `header_content`, from 0 to 4096, and 0 when
    there is no header content; any other height throws
    `std::invalid_argument`. The default is 0.
label_width
    The width of the name column, from 40 to 320; any other width throws
    `std::invalid_argument`. The default is 76.
font
    The font of the rows. Setting it also sets the font of the stock editors
    and Reset buttons and overrides the group, name and validation fonts. The
    default is the content font at 9.5 points.
row_height
    The minimum height of a row; rows with an editor or a Reset button are
    five units taller, and a larger font, a stacked layout or a validation
    message adds more. It must be positive and at most 4096, or the setter
    throws `std::invalid_argument`. The default is 29.
scroll_offset
    How far the content is scrolled, clamped between 0 and
    `content_height` minus the visible height. A value that is not finite
    throws `std::invalid_argument`. Replacing the groups scrolls back to the
    top.
content_height
    The height of the header, the group headers and the visible rows.

Events
------
value_changed
    Raised when the user changes a value. In a text row it is raised on
    every edit as the user types, with `committed` false. In a choice or
    True/False row it is raised when the selection or the check changes,
    with `committed` true, and is followed immediately by `value_committed`.
    It carries a `PropertyValueChange` with the row ID and the previous and
    current text.
value_committed
    Raised when the user commits a value: by pressing Enter in a text row,
    or by changing a choice or a check. Its previous value is the last
    committed one. A text row raises it on Enter even when the text is
    unchanged.
group_changed
    Raised after a group expands or collapses, by a click on its header, an
    accessibility action or `set_group_expanded`. It carries the group ID and
    the new state.
row_expansion_changed
    Raised after a row with child rows expands or collapses, by a click on
    its disclosure arrow, an accessibility action or `set_row_expanded`.
reset_requested
    Raised when the user clicks a row's Reset button. It carries the row ID.
    The list does not change the value itself; the application restores the
    default and calls `set_value`.

Methods
-------
value
    Returns the current text of a row, including text the user is typing
    and has not committed, or no value for an unknown row ID.
set_value
    Sets the text of a row and shows it in the row's stock editor, without
    raising an event. A choice row accepts an empty value or one of its
    choices, and a True/False row accepts True, False, 1 or 0, in any case;
    other text throws `std::invalid_argument`. Returns false for an unknown
    row ID.
set_description
    Sets the description of a row, which becomes its editor's accessible
    description. Returns false for an unknown row ID.
set_validation
    Shows a message below a row, or removes it when the message is empty,
    and adds it to the editor's accessible description. Returns false for
    an unknown row ID.
set_reset_enabled
    Enables or disables a row's Reset button. Returns false when the row is
    unknown or has no Reset button.
set_group_expanded
    Expands or collapses a group. Returns false for an unknown group ID.
set_row_expanded
    Expands or collapses a row that has child rows. Returns false when the
    row is unknown or not expandable.
row_expanded
    Whether a row's child rows are shown, or no value when the row is
    unknown or not expandable.
editor
    The control that edits a row, or null for a read-only row or an unknown
    row ID. Stock editors are named ``<row id>.editor``.
replace_editor
    Replaces a row's editor with a control the application provides, which
    must be live and not yet in any parent or window, or it throws
    `std::invalid_argument`. The previous editor is disposed and the row
    becomes `PropertyEditorKind::custom`. The list does not observe the new
    editor's values; connect to it directly. Returns false for an unknown
    row ID.
reset_button
    The Reset button of a row, or null when the row has none. Reset buttons
    are named ``<row id>.reset``.

See Also
--------
PropertyGrid : Edits the registered properties of one or more controls.
ExpandableSections : Collapsible sections with arbitrary content.

Notes
-----
`set_groups` checks the whole description before changing anything and
throws `std::invalid_argument` when it is wrong. All text must be valid
UTF-8. Every group needs an ID and a title, and every row an ID and a name;
the IDs must be unique across all groups and rows. A row's `depth` must be
0 without a parent; a child row names an earlier expandable row of the same
group as its parent and has a depth one greater, up to 8. A row can be
`expanded` only if it is `expandable`, and `reset_enabled` only if it is
`resettable`. A choice row needs at least one choice, and its value must be
empty or one of them; a True/False row's value must be True or False, in
any case, or 1 or 0.

The editor of each row follows its `PropertyEditorKind`: a
`PropertyEditorKind::text` row gets a `TextBox`, a
`PropertyEditorKind::choice` row a `ComboBox`, and a
`PropertyEditorKind::boolean` row a `CheckBox` captioned True or False. A
`PropertyEditorKind::read_only` row shows its value as text, and a
`PropertyEditorKind::custom` row has no editor until `replace_editor`
supplies one. A `resettable` row also gets a Reset button. Each editor takes the row's name
and description as its accessible name and description, and a row with
`enabled` false has disabled controls. A group starts expanded unless its
`PropertyGroupSpec` sets `expanded` to false.

In a text row, Enter commits the text and Escape puts back the last
committed text without raising an event. When a `required` text row is
committed empty, the list shows the validation message "<name> is
required"; it does not remove the message when a later commit is not empty,
so clear it with `set_validation`.

A click on a group header expands or collapses the group, and a click on a
row's name or disclosure arrow does the same for its child rows. The mouse
wheel scrolls the list, and when the list itself has focus Home, End, Page
Up and Page Down scroll it. When an editor or Reset button takes focus, the
list scrolls its row into view. When the list is narrower than 230 units, or
than the name column plus 116 units if that is wider, each name is placed
above its value instead of beside it.

The control publishes the accessibility role `property_grid`, named
"Properties" unless it has an accessible name. Each group is published as a
`property_group` with the `expanded` state and an expand or collapse action,
and each read-only row as a `property_row` with its name, value and
description, followed by any validation message. Editors and Reset buttons
are published as the controls they are.

Examples
--------
Describe a short settings sheet with a read-only row, a text row and a
choice row:

.. code-block:: cpp

   std::shared_ptr<gf::PropertyList> sheet =
       gf::make_control<gf::PropertyList>(gf::StableId("file.sheet"));
   (*sheet).set_groups({
       {"file.identity", "IDENTITY", {
           {"file.kind", "Kind", "PNG image", "The file format"},
           {"file.name", "Name", "Facade Study.png", "The file name",
            gf::PropertyEditorKind::text},
           {"file.opens", "Opens with", "Preview", "The default application",
            gf::PropertyEditorKind::choice, {"Preview", "Image Laboratory"}},
       }},
   });
   (*sheet).set_value("file.name", "Facade Final.png");   // no event is raised

Keep application data in step with what the user commits. A choice row
commits as soon as its selection changes:

.. code-block:: cpp

   struct FileSheet final : gf::Component {
       std::shared_ptr<gf::PropertyList> sheet =
           gf::make_control<gf::PropertyList>(gf::StableId("file.sheet"));
       std::string opens_with = "Preview";

       FileSheet() {
           (*sheet).set_groups({
               {"file.identity", "IDENTITY", {
                   {"file.opens", "Opens with", "Preview", "The default application",
                    gf::PropertyEditorKind::choice, {"Preview", "Image Laboratory"}},
               }},
           });
           gf::on((*sheet).value_committed(), *this, &FileSheet::on_committed);
       }
       void on_committed(const gf::PropertyValueChange& change) {
           if (change.row_id == "file.opens") opens_with = change.current_value;
       }
   };

   void example() {
       FileSheet file;
       std::shared_ptr<gf::ComboBox> opens =
           std::dynamic_pointer_cast<gf::ComboBox>((*file.sheet).editor("file.opens"));
       if (opens) (*opens).selected_index = std::size_t{1U};   // opens_with is "Image Laboratory"
   }
