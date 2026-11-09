A wrapping row of compact editing modules, each with an enable switch, a few fields and a status line.

An instrument rack lays out modules described by `InstrumentModuleSpec`
records, such as the criteria of a search or the stages of a filter chain.
Each module shows a check box that enables it, one to eight fields edited
with drop-down choices or text boxes, a status line, and a remove button.
Modules flow left to right in the given order and wrap onto new lines, and
an optional action control fills the space after the last one. The rack
reports what the user does through its events and leaves the decisions to
the application, which calls `set_modules` again to add, remove or reorder
modules. Use a `PropertyList` instead for a long sheet of named settings.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
modules
    The modules as last set, updated as the user edits fields and toggles
    modules. See `set_modules` in the notes for the rules it checks.
action_content
    A control placed after the last module, at least the given minimum width
    wide (170 by default) and filling the rest of its line, for buttons such
    as "Add" and "Apply". It must not already have a parent, or
    `set_action_content` throws `std::invalid_argument`. Replacing it
    disposes the previous one. There is none by default.
module_width
    The width of each module, never more than the rack's width. It must be
    finite and positive, or the setter throws `std::invalid_argument`. The
    default is 210.
module_height
    The height of each module; a module whose fields are stacked may be
    taller. It must be finite and positive, or the setter throws
    `std::invalid_argument`. The default is 66.
rack_gap
    The space between modules and between lines. It must be finite and not
    negative, or the setter throws `std::invalid_argument`. The default is 5.
content_height
    The height of all the lines of modules as last laid out, including the
    padding.
scroll_offset
    How far the content is scrolled, clamped between 0 and `content_height`
    minus the visible height. A value that is not finite throws
    `std::invalid_argument`. Replacing the modules scrolls back to the top.

Events
------
field_changed
    Raised when the user changes a field. In a text field it is raised on
    every edit as the user types, with `committed` false. In a choice field
    it is raised when the selection changes, with `committed` true, and is
    followed immediately by `field_committed`. It carries an
    `InstrumentFieldChange` with the module and field IDs and the previous
    and current text.
field_committed
    Raised when the user commits a field: by pressing Enter in a text field,
    or by choosing an item in a choice field. Its previous value is the last
    committed one.
module_toggled
    Raised when the user checks or unchecks a module's enable box. It
    carries the module ID and the new state. `set_module_enabled` does not
    raise it.
remove_requested
    Raised when the user clicks a module's remove button. The rack does not
    remove the module; the application calls `set_modules` without it.
move_requested
    Raised when the user asks to move a module one place earlier or later.
    It carries the module ID, its current index and the requested index. It
    is not raised for a move past either end. The rack does not move the
    module; the application calls `set_modules` in the new order.

Methods
-------
field_editor
    The control that edits a field, a `ComboBox` or a `TextBox`, or null for
    unknown IDs.
module_bounds
    A module's bounds as last laid out, relative to the rack's content
    before scrolling, or no value for an unknown module ID.
preferred_height
    The height the rack needs to show every module at a given width. The
    width must be finite and positive, or it throws `std::invalid_argument`.
set_module_enabled
    Enables or disables a module, as its enable box would, without raising
    `module_toggled`. Returns false for an unknown module ID.
set_module_state
    Sets a module's `InstrumentModuleState` and status text. Returns false
    for an unknown module ID; text that is not valid UTF-8 throws
    `std::invalid_argument`.
set_field_value
    Sets a field's value and shows it in the field's editor, without raising
    an event. A choice field shows no selection when the value is not one of
    its choices. Returns false for unknown IDs; text that is not valid UTF-8
    throws `std::invalid_argument`.
set_field_validation
    Sets or clears a field's validation message, which becomes the
    accessible description of its editor and is kept in `modules`. The rack
    does not draw it and does not change the module's state; set the state
    with `set_module_state`. Returns false for unknown IDs.

See Also
--------
PropertyList : A scrolling sheet of named settings in groups.
PropertyGrid : Edits the registered properties of a control.
ComboBox : The editor of a choice field.
TextBox : The editor of a text field.

Notes
-----
`set_modules` checks the whole description before changing anything and
throws `std::invalid_argument` when it is wrong. All text must be valid
UTF-8. Every module needs a unique, nonempty ID and a name, and one to eight
fields; within a module every field needs a unique, nonempty ID and a name,
a positive `width_weight`, and, for a choice field, at least one choice.
A module whose ID was already present keeps its controls, and a field keeps
its editor as long as the module's fields have the same IDs and editor
kinds in the same order; otherwise the module's controls are rebuilt. When
the module holding the keyboard focus is removed, focus moves to the module
now at its position, or the last module, or else to the action content.

Each module is a group named after it. Its controls are named
``<module id>.enable`` for the enable box, ``<module id>.<field id>`` for
each field, ``<module id>.state`` for the status line and
``<module id>.remove`` for the remove button, so module IDs must be unique
within the window. The fields share the width between the enable box and
the remove button in proportion to their `width_weight`. When a module is
narrower than 190 units, its fields are stacked and the module grows to fit
them. The `priority` of a module is kept in `modules` but does not affect
the layout.

The module's state colours it: `InstrumentModuleState::staged`,
`InstrumentModuleState::pending` and `InstrumentModuleState::invalid` each
have their own colours, and a pending module disables its enable box,
fields and remove button. A disabled module disables its fields but keeps
its enable box usable. The remove button is shown only when the module is
`removable`. When a `required` text field is committed empty, the rack sets
its validation message to "<name> is required" and the module's state to
invalid; it does not undo either when a later commit is not empty.

Alt+Left and Alt+Right, pressed while the focus is anywhere inside a
module, raise `move_requested`, as do the module's
`SemanticAction::decrement` and `SemanticAction::increment` accessibility
actions. The mouse wheel scrolls the rack.

The rack publishes the accessibility role `group`, named "Editing instrument
rack" unless it has another accessible name, with the number of modules as
its value, for example "3 modules". Each module is a `group` whose
description is its status text followed by a hint about Alt+Left and
Alt+Right, and its status line is published as "Application state".

Examples
--------
Show two search criteria, one with a choice field and one with a text
field:

.. code-block:: cpp

   gf::InstrumentFieldSpec kind{.stable_id = "value", .name = "Kind",
                                .value = "Images",
                                .editor = gf::InstrumentFieldEditor::choice,
                                .choices = {"Images", "Documents"}};
   gf::InstrumentFieldSpec words{.stable_id = "value", .name = "Contains",
                                 .value = "Facade",
                                 .editor = gf::InstrumentFieldEditor::text,
                                 .required = true};
   gf::InstrumentModuleSpec by_kind{.stable_id = "search.kind",
                                    .name = "Kind criterion",
                                    .fields = {kind}, .status_text = "live"};
   gf::InstrumentModuleSpec by_content{.stable_id = "search.content",
                                       .name = "Content criterion",
                                       .fields = {words},
                                       .status_text = "staged · not applied",
                                       .state = gf::InstrumentModuleState::staged};
   std::shared_ptr<gf::InstrumentRack> rack =
       gf::make_control<gf::InstrumentRack>(gf::StableId("search.rack"));
   (*rack).set_modules({by_kind, by_content});

Apply the user's choices and remove modules on request. The rack only
reports; the owner decides and sets the new list:

.. code-block:: cpp

   struct Search final : gf::Component {
       std::shared_ptr<gf::InstrumentRack> rack =
           gf::make_control<gf::InstrumentRack>(gf::StableId("search.rack"));
       std::string kind = "Images";

       Search() {
           gf::InstrumentFieldSpec value{.stable_id = "value", .name = "Kind",
                                         .value = "Images",
                                         .editor = gf::InstrumentFieldEditor::choice,
                                         .choices = {"Images", "Documents"}};
           gf::InstrumentModuleSpec criterion{.stable_id = "search.kind",
                                              .name = "Kind criterion",
                                              .fields = {value},
                                              .status_text = "live"};
           (*rack).set_modules({criterion});
           gf::on((*rack).field_committed(), *this, &Search::on_field);
           gf::on((*rack).remove_requested(), *this, &Search::on_remove);
       }
       void on_field(const gf::InstrumentFieldChange& change) {
           if (change.module_id == "search.kind") kind = change.current_value;
       }
       void on_remove(const gf::InstrumentModuleRequest& request) {
           std::vector<gf::InstrumentModuleSpec> kept;
           for (const gf::InstrumentModuleSpec& criterion : (*rack).modules()) {
               if (criterion.stable_id != request.module_id) kept.push_back(criterion);
           }
           (*rack).set_modules(kept);
       }
   };

   void example() {
       Search search;
       std::shared_ptr<gf::ComboBox> value = std::dynamic_pointer_cast<gf::ComboBox>(
           (*search.rack).field_editor("search.kind", "value"));
       if (value) (*value).selected_index = std::size_t{1U};   // kind is "Documents"
   }
