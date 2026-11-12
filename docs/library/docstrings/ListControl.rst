The shared base of the list controls.

`ListControl` is the base of `ListBox` (and so of `CheckedListBox`) and
`ComboBox`, as Windows Forms names it. It cannot be created on its own;
create one of those controls instead, and use this type when code should
work with any of them. It carries the selected index and its change event;
each derived control holds its own `items` and the rest of its selection.

Properties
----------
selected_index
    The selected item's index, or no value when nothing is selected. For a
    `ListBox` it is the lowest selected index, which in `one` mode is the
    selected row. A `ComboBox` also declares it as a settable cell of the
    same name.

Events
------
selected_index_changed
    Raised with the new index, or no value, after the selected index
    changes. A `ListBox` raises it after `selection_changed`, only when its
    lowest selected index moved. A `ComboBox` raises it after a change by
    input, by the setter, or by an edit of `items` moving or clearing the
    selection; setting the current index again raises nothing.

See Also
--------
ListBox : A list the user selects one or more items from.
CheckedListBox : A list with a check box beside each item.
ComboBox : A field with a drop-down list of choices.
