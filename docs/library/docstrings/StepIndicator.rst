A step indicator: a row of numbered steps joined by connectors, each with
its name beneath.

The steps before `current_step` are complete and show a check; the current
step is highlighted; the steps after it are upcoming. A press on a completed
step raises `step_clicked` with its index; the indicator does not change
`current_step` itself, so its owner decides (a `Wizard` goes back to that
step). It takes no focus: the keyboard path is its owner's, such as a
wizard's Back button.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
current_step : default ``0``
    The step under way, counted from zero; the steps before it are
    complete. A step past the last throws `std::invalid_argument`; zero is
    always accepted.
steps
    The steps' names, in order. `set_steps` replaces them; a name that is
    not valid UTF-8 throws `std::invalid_argument` and keeps the old steps.
    A current step past the new last step moves to the last, or to zero
    when there are none.

Events
------
step_clicked
    Raised with a completed step's index when the user presses it.
current_step_changed
    Raised after `current_step` changes, with the new step.

Methods
-------
step_at
    The step whose chip or name contains a local point, or none.

See Also
--------
Wizard : Pages of a task, with a step indicator.

Notes
-----
To assistive technology the indicator is a group whose value reads, for
example, "Step 2 of 4: Details". A theme draws each step's ``chip`` in the
selection family (selected for the complete and current steps, checked for
the complete ones), with the ``glyph`` check or the step's number as its
label; ``divider`` connectors join the chips, selected between complete
steps; and ``caption`` is a step's name, ordinary for the current step and
secondary for the others.
