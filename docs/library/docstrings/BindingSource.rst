A list of rows with a current position, to which controls are bound.

A binding source shows the data that a form edits: the rows of a
`BindingProvider`, each with a stable ID, a value per column of the
provider's `BindingSchema`, an editable flag and optional error messages,
and a current position. Controls bound to it with `Binding` show the fields
of the current row and write their changes back, straight to the
provider's data; moving the position moves every bound control to another
row at once. The source offers navigation, insertion and removal, sorting
and filtering, edit transactions with cancellation, and the events a list
view or navigator needs. A new source holds an empty `BindingTable`, the
built-in column store; `set_provider` binds any other provider. Use a
`Value` instead to share a single scalar between controls.

Parameters
----------
window
    The window whose UI thread the source belongs to. Construct the source on
    that thread, with `std::make_shared`, since bindings hold it by
    `std::shared_ptr`.

Properties
----------
provider
    The `BindingProvider` whose rows the source shows. `set_provider(provider)`
    binds another (null binds a new empty `BindingTable`): the previous one
    is detached and its `BindingProviderLink` revoked, a sort or filter whose
    columns the new schema lacks is dropped, and the first row becomes
    current. It raises `data_source_changed`, then `list_changed` with
    `reset`, then the position events if the current row changed.
table
    The provider when it is a `BindingTable`, otherwise null. Changing the
    table changes the source: `assign` replaces every row (the row with the
    previous current stable ID stays current, otherwise the first row
    becomes current), and `append`, `insert`, `remove`, `move` and
    `set_value` raise the matching list changes.
schema
    The provider's columns, as last read: on binding and on every reset.
    `column_of(name)` finds a column by any spelling of its name.
generation
    A counter that every change the provider reports advances. A value read
    with `value_at(generation, row, column)` is refused, as an empty
    optional, once the generation has moved on.
count
    The number of rows shown: the provider's, or those the filter keeps.
position : default ``-1``
    The index of the current record, or -1 when there is none. Setting it
    first ends any edit in progress, then raises `list_changed` with
    `BindingListChangeKind::position_changed`, `position_changed`,
    `current_changed` and `current_item_changed`, in that order, after the
    bound controls have been updated. It returns false, raising nothing,
    when the position is unchanged, and throws `std::out_of_range` outside
    -1 to `count` - 1.
current_stable_id
    The current row's stable ID, or an empty string when there is none.
sort
    The sort keys, each a column name and a `BindingSortDirection`.
    `set_sort(keys)` sorts the shown rows and `remove_sort` restores the
    provider's order. A provider that sorts is asked to; otherwise the
    source builds an index of the keys, kept up to date as rows are
    inserted, removed and changed. Equal keys keep the provider's order,
    and text compares ignoring case, by code point. An unknown column
    throws `std::invalid_argument` and changes nothing.
filter
    The `BindingFilter`, a list of `BindingPredicate` values that every
    shown row satisfies. `set_filter(filter)` and `remove_filter` set and
    clear it, asking the provider or keeping an index as for `sort`.
view_mode
    How the rows are ordered: `provider_order` with no sort or filter,
    `provider_view` when the provider sorts or filters itself (its
    `BindingProviderCapabilities` say so), and `index` when the source keeps
    its own index of the sort keys and filter memberships.
bindings
    The bindings currently connected to the source.
allow_edit : default ``true``
    Whether fields may be changed. While false, `set_current_field` and
    `begin_edit` return false, and turning it off ends any edit in progress.
allow_new : default ``true``
    Whether rows may be added; while false, `add` and `insert` throw
    `std::logic_error`.
allow_remove : default ``true``
    Whether records may be removed; while false, `remove_at`,
    `remove_current` and `clear` on a nonempty list throw `std::logic_error`.
binding_suspended : default ``false``
    Whether changes are being withheld from bound controls; see
    `suspend_binding`.
raise_list_changed_events : default ``true``
    Whether `list_changed` is raised. Bound controls are updated either way.
data_member : default ``""``
    A name stored for consumers of the source. Changing it raises
    `data_member_changed` and refreshes the bindings; the source itself
    does not use it to look up fields.
currency_manager
    The `CurrencyManager` of the source: the same rows and position
    behind the `BindingManagerBase` interface, as returned by
    `BindingContext::manager`.
snapshot
    A `BindingSourceSnapshot` with the count, position and current stable ID,
    the generation, a revision counter, counts of resets, field changes, position changes and
    suspended mutations, and whether binding is suspended, an edit is in
    progress and `list_changed` is raised.

Events
------
list_changed
    Raised after every change to the list, with a `BindingListChange` giving
    its `kind`: `reset` after `set_provider`, a provider's reset, a change
    of more than 64 rows at once, `clear`, `resume_binding` or a reset
    request; `item_added` with the index and stable ID and `item_removed`
    with the index; `item_moved` (Windows Forms' `ItemMoved`) with the
    index the row left and the `to_index` it now stands at, one per row
    whose shown position a provider's move changed; `item_changed` with the
    index, stable ID and, for a field change, the field name; and
    `position_changed`. The changes one provider notification makes are
    raised in order, each relative to the list the ones before it left. It
    is raised after the bound controls have been updated, and is not raised
    while `raise_list_changed_events` is false.
position_changed
    Raised with the current position whenever the position or the current
    row changes: when `position` is set or moved, and when `set_provider`,
    a provider's change, `insert`, `remove_at` or `clear` shift or replace
    the current row.
    The index may be unchanged, when removing the current record makes the
    next one current.
current_changed
    Raised just after each `position_changed`.
current_item_changed
    Raised just after each `current_changed`, and also when a field of the
    current record changes, when an edit of it is cancelled and when it is
    reset with `reset_item`.
data_error
    Raised with the error text when a binding fails to transfer a value.
data_source_changed
    Raised by `set_provider`, before its `list_changed`.
data_member_changed
    Raised after `data_member` changes, with the new value.
binding_complete
    Raised after a bound transfer completes, for bindings whose options have
    `formatting_enabled`, just after the binding's own `binding_complete`.
disposed_event
    Raised when the source is disposed, before its events are disconnected.
    Bindings and binding contexts use it to let go of the source.

Methods
-------
move_first
    Makes the first record current. Returns false when the list is empty or
    the first record is already current.
move_last
    Makes the last record current. Returns false when the list is empty or
    the last record is already current.
move_next
    Makes the next record current. Returns false at the end of the list.
move_previous
    Makes the previous record current. Returns false at the start of the
    list.
value
    `value(row, column)` returns a cell of a shown row, or an empty optional
    when the provider gave no valid answer for it (`error` says why).
    `field(row, name)` finds the column by name. Every read goes through a
    bounded cache of recently read rows, so painting a window of a large
    provider reads only the rows it shows.
stable_id
    `stable_id(row)` returns a shown row's stable ID.
error
    `error(row, column)` returns the provider's error text for a cell, or
    for the row with `binding_whole_row`, or why its answer was refused, or
    an empty string.
read_only
    `read_only(row, column)` is true unless the source, the column, the row
    and the provider's per-cell flag all allow writing.
write
    `write(row, column, value)` writes a cell straight to the provider and
    returns its `BindingWriteResult`, accepted or refused with a reason.
current_field
    `current_field(name)` returns the value of a field of the current row,
    or an empty optional when there is no current row or no such field.
current_error
    `current_error(name)` returns the current record's error message for a
    field, or for the record as a whole when the name is empty, or an empty
    string.
set_current_field
    `set_current_field(name, value)` writes a field of the current row and
    raises `list_changed` with `item_changed` and then
    `current_item_changed`. The built-in `BindingTable` grows a column for
    a field it has not got; another provider's columns are fixed. It begins
    an edit first, so `cancel_edit` can undo it. Returns false, changing
    nothing, when there is no current row, editing is not allowed, the row
    or cell is read-only, the value is not of the column's kind, the value
    is unchanged or the provider refuses it.
add
    Appends a row through the provider; see `insert`.
add_rows
    Appends several rows through the provider, all or none: when the
    provider refuses one, the rows added before it are removed again and
    `std::invalid_argument` names the refusal. Several rows need a provider
    that removes rows (`std::logic_error` otherwise, before anything
    changes); a provider that refuses the undo too leaves its rows, the
    source resets to them, and `std::runtime_error` says so.
insert
    `insert(index, row)` asks the provider to insert a `BindingRow` (values
    by column position) before the row shown at `index`, and returns the
    new row's position, or an empty optional when the filter hides it. When
    there was no current row, the first row becomes current; otherwise the
    current row stays current. Throws `std::out_of_range` past the end,
    `std::logic_error` when the provider cannot insert, and
    `std::invalid_argument` when it refuses the row (a duplicate stable ID,
    for one).
remove_at
    Removes the row at an index through the provider and returns true, or
    returns false past the end. When the current row is removed, the row
    that takes its index becomes current, or the new last row.
remove_current
    Removes the current row. Returns false when there is none.
clear
    Removes every row and raises `list_changed` with `reset`. The position
    becomes -1.
find
    `find(name, value)` returns the position of the first row whose field
    equals the value, or an empty optional, reading every row until it
    finds one. `find_stable_id(id)` finds a row by its stable ID.
begin_edit
    Takes a snapshot of the current row so that `cancel_edit` can restore
    it. Returns false when there is no current row, editing is not allowed
    or every cell of the row is read-only. Calling it again during the same
    edit keeps the first snapshot.
cancel_edit
    Writes the snapshot taken by `begin_edit` back to the provider and
    raises `item_changed`, and `current_item_changed` when it is the current
    row. Does nothing when no edit is in progress.
end_edit
    Keeps the changes and discards the snapshot.
suspend_binding
    Withholds changes from bound controls. The list still changes, and
    `list_changed` is still raised; the bound controls keep their values.
resume_binding
    Ends suspension; suspension does not nest. When anything changed
    meanwhile, the bound controls are updated once and `list_changed` is
    raised with `reset`.
reset_bindings
    `reset_bindings(metadata_changed = false)` raises a `reset` so that every
    bound control shows its field again.
reset_current_item
    `reset_item` for the current record.
reset_item
    `reset_item(index)` raises `item_changed` for a row, so that controls
    bound to it show it again, and returns false past the end.

See Also
--------
Binding : Connects one control property to one field of a source.
BindingTable : The built-in provider, a column store.
BindingProvider : The interface any data a source shows implements.
BindingContext : The currency managers of a window's binding sources.
ErrorProvider : Shows the errors that rows carry for bound fields.

Notes
-----
Column names are trimmed and compared ignoring ASCII case:
"Name", "name" and " NAME " are the same field. An empty field name passed to
`current_field`, `set_current_field` or `find` throws
`std::invalid_argument`.

A provider is untrusted data: an answer of the wrong kind, past the bounds,
or an exception it throws becomes a cell error, never a failure of the
source. Misuse of the source itself (another thread, an index past
`count`, an unknown column in a sort) throws. A provider may report changes
from any thread through its `BindingProviderLink`; they are applied on the
source's UI thread.

Edits are snapshots, not transactions with the bound controls: moving to
another row ends the edit and keeps its changes, as does `end_edit`.
Changing `allow_edit`, `allow_new` or `allow_remove` raises a `reset` with
`metadata_changed` set, so that bound views can update their commands.

Use the source only on its window's UI thread. Its mutating members throw
`std::logic_error` from another thread, after disposal and after the window
has shut down. Disposal raises `disposed_event`, deactivates the bindings,
revokes the provider's link and empties the source; destroying an undisposed source disposes it.

Examples
--------
Fill a source and move through it:

.. code-block:: cpp

   std::shared_ptr<gf::Control> root = gf::make_control<gf::Control>(gf::StableId("app"));
   gf::Window window(root, gf::Size{200.0, 100.0});
   std::shared_ptr<gf::BindingSource> cities = std::make_shared<gf::BindingSource>(window);

   gf::BindingTable& table = *(*cities).table();
   static_cast<void>(table.add_column(gf::BindingColumn{"name", gf::BindingValueKind::text}));
   table.assign({gf::BindingRow{"city.oslo", {std::string("Oslo")}},
                 gf::BindingRow{"city.lima", {std::string("Lima")}}});
   // position() is 0, the first row is current
   static_cast<void>((*cities).move_next());
   std::optional<gf::BindingValue> name = (*cities).current_field("Name");
   // name holds std::string("Lima")

Follow the current position with an owned handler, and undo an edit:

.. code-block:: cpp

   struct Navigator final : gf::Component {
       std::ptrdiff_t shown = -1;
       void on_position(std::ptrdiff_t position) { shown = position; }
   };

   void example() {
       std::shared_ptr<gf::Control> root = gf::make_control<gf::Control>(gf::StableId("app"));
       gf::Window window(root, gf::Size{200.0, 100.0});
       std::shared_ptr<gf::BindingSource> notes = std::make_shared<gf::BindingSource>(window);
       Navigator navigator;
       gf::on((*notes).position_changed(), navigator, &Navigator::on_position);

       static_cast<void>((*(*notes).table()).add_column(gf::BindingColumn{"title"}));
       static_cast<void>((*notes).add(gf::BindingRow{"note.1", {std::string("Draft")}}));
       // navigator.shown is 0: the first row became current
       static_cast<void>((*notes).set_current_field("title", std::string("Final")));
       (*notes).cancel_edit();
       // the title is "Draft" again
   }
