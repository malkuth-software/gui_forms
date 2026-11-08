A list box with a check box on each row.

A checked list box lets the user turn each item on or off independently,
for example to choose which features to install, while keeping the
selection, scrolling and keyboard behaviour of a `ListBox`. A row can be
unchecked, checked or indeterminate. By default the first click on a row
selects it and a further click on the selected row toggles its check;
with `check_on_click` the first click toggles too. Space toggles the
active row. The check state is separate from the selection. Use a
`ListBox` when rows have no on/off state, and `CheckBox` controls for a
few fixed options.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID, and its rows are named
    after it.

Properties
----------
checked_indices
    The indices of the rows whose state is `CheckState::checked`, in
    ascending order. Indeterminate rows are not included.
check_on_click : default ``false``
    Whether a click on a row that is not yet selected also toggles its
    check. When false, the first click only selects the row.
indicator_size : default ``14.0``
    Side of the square check indicator in logical pixels; the row text
    starts after it. Values outside 8 to 32, or not finite, throw
    `std::out_of_range`.

Events
------
item_checking
    Raised before a row's check state changes, by input or by a method. It
    carries a mutable `ItemCheckEvent` with the row `index`, its
    `current_state` and the proposed `new_state`. A handler may set
    `cancel` to keep the current state, or replace `new_state`; a
    `new_state` that is not a valid `CheckState` throws
    `std::invalid_argument` after the handlers return.
item_check_state_changed
    Raised after a row's check state changes, immediately after
    `item_checking`. It carries the row index and the new state, which the
    row already has. It is not raised when the change was cancelled or
    left the state as it was.

Methods
-------
add_item
    Appends a row in the given `CheckState`. A state that is not a valid
    `CheckState` throws `std::invalid_argument`. The `items` edits keep
    the check states with their rows: an insertion adds unchecked rows, an
    erasure removes the rows' states, a replacement keeps the row's state,
    and an assignment of every row makes every row unchecked.
item_check_state
    The check state of a row. An index outside the items throws
    `std::out_of_range`.
item_checked
    Whether a row's state is `CheckState::checked`. Indeterminate counts as
    not checked.
set_item_check_state
    Changes a row's check state, raising `item_checking` and then, unless a
    handler cancels, `item_check_state_changed`. Setting the current state
    again raises nothing. An index outside the items throws
    `std::out_of_range`, and an invalid state `std::invalid_argument`.
set_item_checked
    Sets a row to `CheckState::checked` or `CheckState::unchecked`, as
    `set_item_check_state` does.
toggle_item
    Sets a checked row to unchecked, and an unchecked or indeterminate row
    to checked, as `set_item_check_state` does. This is what a click or
    Space does.
on_pointer
    Handles the pointer input described under Notes. The window calls it.
on_key
    Toggles the active row on Space and passes every other key to
    `ListBox`. The window calls it.
semantic_virtual_children
    Publishes the rows as `ListBox` does, with the role `check_list_item`,
    the value ``checked``, ``unchecked`` or ``indeterminate``, and the
    state `checked` or `mixed`.
on_semantic_child_action
    A semantic press on a row focuses the list, selects the row and toggles
    its check, without raising `item_activated`. Other actions behave as in
    `ListBox`.

See Also
--------
ListBox : The same list without check boxes.
CheckBox : A single independent on/off choice.
FlagsValueEditor : A drop-down editor for a flags value, built on a checked list.

Notes
-----
A primary press on a row first selects it, as in `ListBox`, and then
toggles its check when `check_on_click` is true or the row was already
selected before the press. The toggle happens on the press, anywhere on the
row; releasing the button on the same row then raises the inherited
`item_activated`. So a click that toggles raises `selection_changed` (if
the selection changed), `item_checking`, `item_check_state_changed` and
finally `item_activated`.

Space toggles the active row when the list has focus. Enter raises
`item_activated`, and the arrow, Home, End and page keys move the selection
as in `ListBox`. A disabled list ignores pointer input.

The list publishes the accessibility role `list`, and each row is a
`check_list_item`. Semantic press on a row works only while the rows use
their index-derived IDs; after `set_item_stable_ids` it is not handled.

Examples
--------
Build a list with initial check states and toggle a row:

.. code-block:: cpp

   std::shared_ptr<gf::CheckedListBox> features =
       gf::make_control<gf::CheckedListBox>(gf::StableId("install.features"));
   (*features).add_item("Core files", gf::CheckState::checked);
   (*features).add_item("Documentation", gf::CheckState::unchecked);
   (*features).add_item("Samples", gf::CheckState::indeterminate);
   (*features).toggle_item(1U);   // Documentation is now checked
   // (*features).checked_indices() is {0, 1}; Samples stays indeterminate

Refuse a change before it happens. The handler on `item_checking` cancels
any attempt to uncheck the required first row:

.. code-block:: cpp

   struct FeatureRules final : gf::Component {
       std::shared_ptr<gf::CheckedListBox> features =
           gf::make_control<gf::CheckedListBox>(gf::StableId("install.features"));
       std::size_t last_checked = 0;

       FeatureRules() {
           (*features).add_item("Core files", gf::CheckState::checked);
           (*features).add_item("Samples", gf::CheckState::unchecked);
           gf::on((*features).item_checking(), *this, &FeatureRules::on_checking);
           gf::on((*features).item_check_state_changed(), *this,
                  &FeatureRules::on_checked);
       }
       void on_checking(gf::ItemCheckEvent& change) {
           if (change.index == 0U && change.new_state == gf::CheckState::unchecked) {
               change.cancel = true;
           }
       }
       void on_checked(std::size_t index, gf::CheckState state) {
           if (state == gf::CheckState::checked) last_checked = index;
       }
   };

   void example() {
       FeatureRules rules;
       (*rules.features).set_item_checked(0U, false);   // cancelled: still checked
       (*rules.features).set_item_checked(1U, true);    // rules.last_checked is 1
   }
