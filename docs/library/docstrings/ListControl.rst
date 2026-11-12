The shared base of the list controls.

`ListControl` is the base of `ListBox` (and so of `CheckedListBox`) and
`ComboBox`, as Windows Forms names it. It cannot be created on its own;
create one of those controls instead, and use this type when code should
work with any of them. It adds nothing to `Panel` yet: each derived control
holds its own `items` and selection.

See Also
--------
ListBox : A list the user selects one or more items from.
CheckedListBox : A list with a check box beside each item.
ComboBox : A field with a drop-down list of choices.
