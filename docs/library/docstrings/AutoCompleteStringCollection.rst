An ordered list of completion candidates for a text box or combo box.

An auto-complete string collection holds the strings a `TextBox` or an
editable `ComboBox` offers when its `auto_complete_source` is
`custom_source`, as Windows Forms' AutoCompleteStringCollection. The
strings are kept in the order given, and that is the order the suggestion
list shows them in; duplicates are kept, and the list shows each text once.

A control holds its collection through a `std::shared_ptr`. A new control
owns an empty one, which a program fills through
`auto_complete_custom_source`; assigning another collection shares it, so
one collection can serve several fields and lives while any of them, or the
program, holds it. Use it on the UI thread.

Properties
----------
items
    The strings, in order. The view is valid until the next change.
size
    The number of strings.
empty
    Whether there are none.

Events
------
changed
    Raised once after each change, by any mutator. A control whose
    suggestion list is open matches the typed text again, keeping the
    highlight on the same text when it is still offered.

Methods
-------
at
    The string at an index; an index past the end throws
    `std::out_of_range`.
contains
    Whether a string equal to the given one is held.
index_of
    The first index holding the given string, or none.
add
    Appends a string and returns its index.
add_range
    Appends several strings, raising `changed` once.
insert
    Inserts a string before an index; the size itself appends.
set
    Replaces the string at an index.
remove
    Removes the first string equal to the given one, and returns false when
    there is none.
remove_at
    Removes the string at an index.
clear
    Removes every string; it raises `changed` only when there were some.

See Also
--------
TextBox : Its auto_complete_custom_source reads a collection.
ComboBox : The same, for the drop_down style's editor.

Notes
-----
Every mutator validates before changing anything: a string that is not
valid UTF-8 throws `std::invalid_argument`, and an index past the end
`std::out_of_range`, and the collection is left as it was. `add_range`
checks every string before adding any.

Examples
--------
One collection shared by two fields:

.. code-block:: cpp

   std::shared_ptr<gf::AutoCompleteStringCollection> towns =
       std::make_shared<gf::AutoCompleteStringCollection>();
   (*towns).add_range({"Dartmouth", "Dover", "Durham"});
   std::shared_ptr<gf::TextBox> from =
       gf::make_control<gf::TextBox>(gf::StableId("trip.from"));
   std::shared_ptr<gf::TextBox> to =
       gf::make_control<gf::TextBox>(gf::StableId("trip.to"));
   (*from).auto_complete_custom_source = towns;
   (*to).auto_complete_custom_source = towns;
   (*towns).add("Derby");   // both fields offer it from now on
