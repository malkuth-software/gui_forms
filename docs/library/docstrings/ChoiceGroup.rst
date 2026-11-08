A row of buttons from which the user picks exactly one choice.

A choice group presents a few mutually exclusive options, such as a view
mode, as a row of buttons of equal width; the chosen button is drawn
selected. Items are `ChoiceItem` records with an integer ID, a label, and
optionally a textual glyph, an image and an accessible name. The selection
is the index of the chosen item, or -1 for none, held in the group's own
`Value` and usually bound to an application `Value<int>` with `bind`. The
user chooses by clicking an item, pressing Space or Enter on it, pressing
its mnemonic, or moving with the arrow keys. Use `RadioButton` controls
when the choices are laid out individually, and a `ComboBox` when there are
too many to show at once.

Parameters
----------
id
    Identity of the group, unique within its window. Logs, automation and
    semantic actions address the group by this ID, and each item's button
    is named ``<id>.item.<item id>``.

Properties
----------
presentation : default ``ChoiceGroupPresentation::buttons``
    How the choices are drawn. ``buttons`` is a row of separate buttons,
    the chosen one drawn selected. ``segmented`` is a segmented control:
    one track (part ``track``), the items as segments inside a 2-unit
    inset, and a selection indicator (part ``selection``) behind the
    chosen segment; the group then carries the custom state ``segmented``.
    Selection, keyboard and semantics are the same in both. The indicator
    is a keyed part, so the theme's engine may move it between segments,
    and does not when motion is reduced.
parts
    The published part geometry: Panel's parts, then ``track`` and
    ``selection``, which are absent in the buttons presentation (and the
    selection while nothing is chosen).
items
    The choices, in display order. Empty by default. Item IDs must be
    unique and every text valid UTF-8, or `set_items` throws
    `std::invalid_argument`. Each item's button shows its glyph and label
    separated by a space, and its image; its accessible name is the item's
    `accessible_name`, or else the label. Replacing the items reuses the
    button of every item whose ID remains, so its styling and focus
    survive, and removes the buttons of the others. The selected index is
    kept if it is still in range, even if a different item now has it, and
    otherwise becomes -1. Calling `set_items` while a `set_items` call is
    notifying throws `std::logic_error`.
selected_index : default ``-1``
    The index of the selected item, or -1 when nothing is selected. A value
    below -1 or past the last item throws `std::out_of_range`. While the
    group is bound, the bound value's own validators are consulted too, and
    an exception from them leaves both unchanged.

Events
------
changed
    Raised after `selected_index` changes, by input, by the setter, by the
    bound `Value`, or by `set_items` clearing an index that no longer
    exists. It carries the new index. It is the `changed` event of
    `selection`.
activated
    Raised when the user chooses an item: by a click, Space or Enter on it,
    its mnemonic, an arrow key, or `perform_click` on its button. It
    carries the item's index and comes after the `changed` the choice
    caused. Unlike `changed`, it is raised even when the item was already
    selected, and never by a setter.

Methods
-------
bind
    Keeps `selected_index` and an application `Value<int>` in step. The
    value wins when the binding starts, so it must already be -1 or a valid
    index for the current items, or `std::out_of_range` is thrown. After
    that the user's choice updates the value, a change to the value updates
    the group, and setting the value to an index the group cannot show
    throws `std::out_of_range` and leaves the value unchanged. A new
    binding replaces the previous one. Configure the items before binding,
    and unbind before replacing them with fewer.
unbind
    Ends the binding. The group keeps its current selection.
selection
    The group's own `Value<int>`, which holds `selected_index`.
item
    The `Button` of the item at an index, for visual recipes, typography
    and content layout. An index outside the items throws
    `std::out_of_range`. Change the selection through the group, not
    through the button's selected flag.
part
    ``part("item", index)`` returns the same control as `item`; any other
    part name returns a null pointer.
arrange
    Gives each item button an equal share of the width, in one row at the
    full height.
on_key
    Handles the arrow keys described under Notes. The window calls it.
on_key_bubble
    Handles the arrow keys pressed while an item's button has focus.
semantic_descriptor
    Publishes the role `radio_group`.

See Also
--------
RadioButton : A single choice placed on its own among others.
ComboBox : One choice from a list that drops down.
CheckBox : An independent on/off choice.

Notes
-----
The group itself never takes focus; its item buttons do. Only one item is
a tab stop: the focused item, otherwise the selected item, or the first
when nothing is selected, skipping disabled and hidden items. With an item
focused, Left and Up choose the previous item and Right and Down the next,
wrapping at the ends and skipping disabled or hidden items; the chosen
item takes the focus, and `changed` and `activated` are raised as for a
click. When nothing is selected the keys count from the first item, so in
a group of several items Right chooses the second item and Left the last.

The items share the width of the group equally, in one row, at the full
height of the group.

A `&` in a label marks the mnemonic, as on any button, but the label is
also used unchanged as the default accessible name, ampersand included.
Give such items an `accessible_name`.

The group publishes the accessibility role `radio_group`, and each item
button the role `radio_button`, with the state `checked` when it is
selected.

Examples
--------
Bind a choice group to an application setting. The value wins when the
binding starts, and afterwards each side follows the other:

.. code-block:: cpp

   gf::Value<int> view_mode(0);
   std::shared_ptr<gf::ChoiceGroup> modes =
       gf::make_control<gf::ChoiceGroup>(gf::StableId("view.mode"));
   (*modes).set_items({{.id = 1, .label = "Icons"}, {.id = 2, .label = "Details"}});
   (*modes).bind(view_mode);   // Icons is selected
   view_mode.set(1);           // and now Details

Tell state changes from user choices. A setter raises only `changed`; a
choice by the user raises `changed` when the selection moves, then
`activated`:

.. code-block:: cpp

   struct ModeSwitch final : gf::Component {
       std::shared_ptr<gf::ChoiceGroup> modes =
           gf::make_control<gf::ChoiceGroup>(gf::StableId("view.mode"));
       int shown = -1;
       int choices = 0;

       ModeSwitch() {
           (*modes).set_items({{.id = 1, .label = "Icons"}, {.id = 2, .label = "Details"}});
           gf::on((*modes).changed(), *this, &ModeSwitch::on_changed);
           gf::on((*modes).activated(), *this, &ModeSwitch::on_activated);
       }
       void on_changed(int index) { shown = index; }
       void on_activated(int index) {
           if (index >= 0) ++choices;
       }
   };

   void example() {
       ModeSwitch view;
       (*view.modes).set_selected_index(1);     // shown is 1, choices is 0
       (*view.modes).item(0).perform_click();   // shown is 0, choices is 1
   }
