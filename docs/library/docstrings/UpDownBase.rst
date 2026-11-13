The shared base of the up-down controls: a text field with up and down buttons.

`UpDownBase` is the base of `NumericUpDown` and `DomainUpDown`, as Windows
Forms names it. It holds the parts both are made of: an `editor` text box
and the column of spin buttons beside it. It cannot be created on its own;
create one of those controls instead, and use this type when code should
work with either, for example to reach the field of any up-down control.

Properties
----------
editor
    The `TextBox` that shows and edits the value, created by the derived
    control as ``<stable_id>.editor``. Use it to set the font or to move
    focus to the field; focus given to the control goes to it.

See Also
--------
NumericUpDown : A text field for a number, with up and down buttons.
DomainUpDown : A text field that steps through a list of strings.
