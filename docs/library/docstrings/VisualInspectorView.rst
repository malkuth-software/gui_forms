A diagnostic panel that shows the layout, state and paint record of one control.

A visual inspector view looks up a target control by stable ID in the
window's visual-inspection snapshot and lists what the window knows about it:
identity and runtime ID; visibility, enabled, focus, hover, press and capture
state; requested, arranged, absolute and visual bounds, the effective clip and
the child viewport; the authored surface material; the largest font it drew
and how that font resolved; a count of its recorded paint commands; whether
its display chunk is current; its theme; and the window's control and
pending-damage counts. It is for development builds and diagnostics panels.
Pair it with a `VisualInspectorOverlay` to see the same bounds drawn over the
target, and use `Window::visual_inspection_snapshot` directly to inspect
controls in code.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
target_stable_id
    The stable ID of the control to inspect, empty by default.

Properties
----------
target_stable_id
    The stable ID of the inspected control. When no control in the window has
    it, the view shows `Target not found:` and the ID.

Methods
-------
start_polling
    `start_polling(FrameInterval interval)` polls the inspected target every
    `interval` on a paint-gated `AnimationTimer` while the view is shown, and
    repaints only when what it shows has changed, so a settled view costs
    one inspection snapshot a poll and no paint. A hidden view or covered
    window holds no wake. An interval below 1 ms throws
    `std::invalid_argument`.
stop_polling
    Stops polling.
refresh
    Repaints the view and updates its semantics from a new snapshot. Call it
    after the target changes.

See Also
--------
VisualInspectorOverlay : Draws the target's bounds over the target itself.
MetricsView : Shows the window's runtime counters.
TypographySpecimen : Shows how one text run resolves.

Notes
-----
The view does not observe its target. It reads a snapshot when it paints and
when its semantics are queried, so what it draws is stale until it is
repainted: call `refresh` after changing the target, or keep it live with
`start_polling`. Each paint and each semantic query takes a
snapshot of the whole window, with up to 512 paint commands per control, so
a live inspector costs a full-window snapshot on every refresh.

The font row describes paint commands the target has already recorded. A
target that has not been painted yet shows `no committed text draw`, and its
chunk row shows `not painted`; a chunk recorded before the target last
changed is marked `STALE`.

The view publishes the accessibility role `group`, always named "Visual state
inspector", with a summary of the target's state, arranged bounds, clip and
paint commands as its value; it does not use `accessible_name`. It has no
input behaviour and takes no focus.

Examples
--------
Inspect a button, and refresh the view after the button changes:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("lab"));
   std::shared_ptr<gf::Button> apply =
       gf::make_control<gf::Button>(gf::StableId("lab.apply"), "Apply");
   std::shared_ptr<gf::VisualInspectorView> inspector =
       gf::make_control<gf::VisualInspectorView>(gf::StableId("lab.inspector"), "lab.apply");
   (*apply).set_requested_bounds({20.0, 20.0, 96.0, 32.0});
   (*inspector).set_requested_bounds({140.0, 0.0, 320.0, 460.0});
   (*root).add_child(apply);
   (*root).add_child(inspector);
   gf::Window window(root, gf::Size{460.0, 460.0});
   window.perform_layout();

   (*apply).set_enabled(false);
   (*inspector).refresh();
   gf::SemanticDescriptor summary = (*inspector).semantic_descriptor();
   // summary.value begins "lab.apply; visible · disabled"
