The control that runs a composite definition: a reusable control written as
a ``.control`` document of child controls, properties, rules, commands,
events and an optional look.

A composite's definition is compiled once, either at build time by
`gui_forms_add_control`, which generates a typed class deriving from
`CompositeControl`, or at run time by `load_composite_definition`. Each
instance makes its children from the definition, binds them, and runs the
definition's rules and commands. A form document names a composite by its
``.control`` name, and its content goes into the composite's slot
(the user guide's Forms page). Derive from `UserControl` instead for a
control written in C++ alone.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
definition
    The compiled definition the composite runs. A null definition throws
    `std::invalid_argument`.

Properties
----------
definition
    The definition the composite runs.
type_name
    The type's registered name, in snake case.
slot
    The control a form's content goes into, or null without one.
statistics
    The composite's counters (`CompositeStatistics`): rules fired and
    refused, commands run and refused, refused writes, part geometry
    passes, drags committed and restored, and the look's compilations.

Events
------
raised
    Raised when the composite raises one of its declared events, with a
    `CompositeEvent`: the event's name as written and its payload (null for
    none).

Methods
-------
initialize_control_tree
    Makes the children and their bindings; `make_control` calls it once,
    after construction. A refused child throws `std::invalid_argument`, and
    the composite then has no children.
perform
    Runs a command by name: the path a part, a key, an accessibility action
    and generated code share. False, changing nothing, for an unknown
    command or one that is not enabled.
command_enabled
    Whether a command is enabled now.
bind_command
    Binds a `Command` to a command-kind property: the children that bind
    that property take it, and the definition's ``<invoke>`` runs it. The
    command is borrowed: it must outlive the composite, or be unbound with a
    null command first. An unknown property, or one of another kind, throws
    `std::invalid_argument`.
child
    A child by its local ID (``title`` for ``@title``), or null.
part_bounds
    A part's bounds in local coordinates; empty when the part is absent or
    not declared.
presentational_value
    A presentational property's value, or null when unset.
paint_hook
    Draws a part the definition declares with ``paint="hook"``, in the
    part's place among the parts, with a `CompositePaintHook` naming the
    part, its bounds, its states and its text. The default draws nothing. A
    typed class overrides it; it runs inside the paint pass and may not
    change properties, the tree or the layout.

See Also
--------
UserControl : A control written in C++ alone.

Notes
-----
`load_composite_definition` reads, checks and compiles a document against
the base types and the child types it is given (`CompositeChildType`, made
with `composite_child_type`), and returns the definition or, with none,
every refusal as one line in the compilers' form. Rule firings nest at most
`maximum_composite_rule_depth` deep, and custom-state and command-enabled
conditions at most `maximum_composite_condition_depth`; a condition past
that depth reads as false. A look that reads only the engine's inputs and
built-in roles compiles once per definition; one that reads a theme's roles
compiles once per definition and theme, and the compilations of recently
shown themes are kept for reuse.
