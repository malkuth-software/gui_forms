# Controls

GUI.Forms ships its controls as stock C++ types under
`include/gui_forms/controls/`, one header per control
(`<gui_forms/controls/button.hpp>`). The grouped headers in
`include/gui_forms/` (`basic_controls.hpp`, `collection_controls.hpp` and the
rest) are compatibility umbrellas that include several of them; new code
includes the control's own header. Each has a stable identity, retained state,
properties written synchronously, typed change events, a default semantic adapter for
accessibility, and a page in this section. The set is organised the way
Forms developers already think about it: by what the control is *for*.

Every control is described from one registry, `manifests/controls.json`,
which records its header, type name, function category, Forms counterpart (or
"GUI.Forms-only"), status and gallery page. The by-function and alphabetical
indexes below are generated from that registry, so they cannot drift from the
headers; a test fails when a public control type is missing from the
registry or listed twice. The Gallery program builds its navigation from the
same file.

## Status labels

stable
: Construction, behaviour, event order, rendering, input, accessibility and
  disposal are tested on every CI platform, and the type is unlikely to
  change shape before 1.0.

provisional
: Implemented and demonstrated, but the API or visual recipe may still change;
  expect to rebuild and possibly adjust call sites.

lab
: Exists to exercise the engine (rendering experiments, measurement views).
  Usable, but not a product control and not a compatibility promise.

## Where to look

- **By function** answers "which control lets the user set a value / pick
  from a list / see progress?". It is the page to read first.
- **Alphabetical** is the lookup table when you know the name.
- **Control schema** lists each control's parts, states, item systems,
  properties and interactions: the names theme and form documents address.
- **Windows Forms divergences** lists every recorded difference from a
  Windows Forms control's observable behaviour, with its reason.
- **Member manifest** counts every control's properties, events and methods
  against Windows Forms: supported, deferred, excluded or not yet classified.
  Each control's API reference page lists its own.
- The [API reference](../api/index) lists every public type, member and
  enumeration, with one page per function category for the controls.

```{toctree}
:maxdepth: 1

../controls/generated/by-function
../controls/generated/alphabetical
../controls/generated/schema
../controls/generated/winforms-divergences
../controls/generated/members
```

## Conventions shared by every control

- A form document declares the controls a window starts with, and its
  generated class makes them ([Forms](forms)). A control made at run time is
  constructed with `make_control<T>(StableId, ...)`; the parent owns the
  result once you `add_child` it. A stable ID is one or more segments separated by
  `.`, each made of lowercase ASCII letters and digits joined by single `-`
  (`files.list-view`). A literal such as `StableId("files.list-view")` is
  checked when the program compiles; a `std::string` or `std::string_view`
  built at run time throws `std::invalid_argument` when it breaks the rule.
  The same rule covers the IDs a control builds from yours (menu rows, item
  and row nodes, hosted parts) and the semantic node IDs a control publishes
  for its parts, so item, row, column and page IDs you supply follow it too;
  the writes and calls that take them throw `std::invalid_argument` otherwise.
- Setting an equal value does nothing. State events fire for every actual
  change, whether it came from input or from a property write. Input events
  (`clicked`, `scroll`, `item_activated`) fire for user input only and are
  never synthesized by writes.
- Two editors are currently exceptions to the state-event rule:
  `ColorValueEditor` and `FlagsValueEditor` raise `value_changed` only when
  the user commits an edit, not when `set_value` changes the value.
- Collection controls (`ListBox`, `ComboBox`, `CommandBar`, `ChoiceGroup`,
  `ExpandableSections`) take item records with unique IDs; replacing the
  list reuses controls with the same ID and keeps your handlers connected.
- Appearance comes from named roles resolved by the host theme. `CommandBar`,
  `ChoiceGroup` and `ExpandableSections` expose their retained sub-controls
  through `part(name, index)` for custom recipes without subclassing.
- Accessibility metadata is optional; the default adapter publishes a
  sensible role and name, and authored labels enrich it.

## Scrolling

Scrollable controls (panels with scroll bars, lists, grids, trees and the
multiline text boxes) scroll the same way:

- A notched mouse wheel, a press on a scroll bar's arrow or track, and
  Page Up or Page Down glide to the new position over a short, fixed time.
  Input that arrives during a glide moves its target, so repeated notches
  never queue up.
- Precise deltas, such as a trackpad's or a high-resolution wheel's, apply at
  once: the device already supplies the motion.
- With reduced motion (`PresentationSettings::reduced_motion`, the host's
  preference) every scroll is instant.
- Moving the keyboard focus to a control scrolls it into view.

The `scroll` event fires when the input arrives, with the target position as
its new value; the position read during a glide is the one being shown.

## Large row sets

`VirtualRowsView` (`<gui_forms/controls/virtual_rows_view.hpp>`) shows a
very large number of owner-drawn rows of varying height, up to a billion.
Rows are not controls and the view keeps no row records: it keeps the row
count and each row's height in an internal height index (a sequence
B-tree), and asks the application to measure, draw and describe only the
rows it shows.

- **Callbacks.** `set_row_callbacks` takes a `VirtualRowsCallbacks` of three
  `Delegate`s: `measure` (a row's height at the content width), `paint` (one
  row, with its states, so it can draw theme parts) and `describe` (the
  row's accessible name and value). The view borrows the bound objects; they
  must outlive it or be replaced first. A callback may not change the view's
  rows or scroll.
- **Rows.** `reset_rows(count)`, `insert_rows(at, count)` and
  `erase_rows(at, count)` change the count; `refresh_rows` and
  `invalidate_rows` forget chosen heights. Rows start at
  `estimated_row_height` and are measured once, when they first come near
  the viewport. Heights are kept in 1/64 logical units, and every offset is
  the exact sum of the heights above it.
- **Scrolling.** The view anchors the first visible row, so rows that change
  height or are inserted above it do not move the content on screen.
  `set_follow_tail(true)` keeps a view at the end as rows are appended.
  `scroll_to_row` takes a `VirtualRowAlignment`.
- **Selection.** `set_selection_mode` takes ListBox's modes; the extended
  mode keeps sorted ranges (`selected_ranges`). `selection_changed`,
  `row_activated` and `scrolled` report changes.
- **Accessibility.** The list publishes only the realized rows, with the
  full row count as its size, and a live region that announces appended
  rows once per settle ("<n> new rows, <total> in all").
- **Bounded work.** A settle measures at most
  `maximum_measured_per_settle` rows, and a paint draws only the rows that
  meet the viewport. `set_measured_row_budget` bounds the index's memory by
  coarsening rows far from the viewport. `statistics()` reports the
  measured rows, the index size and the repaints.

`TreeListView` keeps its row heights in the same index.

## Node graphs

`NodeGraph` (`<gui_forms/controls/node_graph.hpp>`) is the model of a node
editor: nodes, their ports and the links between ports, held as records,
never as child controls. Identities (`NodeId`, `LinkId`) carry a
generation, so an identity kept past its record's removal is refused with
`GraphStatus::stale_identity`. Every change goes through a named operation
(`add_node`, `move_nodes`, `connect`, `disconnect` and the rest) that
validates its arguments before changing anything, so a refused operation
leaves the graph as it was.

Each document operation is one undo step, or one step together with the
operations between `begin_undo_group` and `end_undo_group`.
`rollback_undo_group` cancels an open group and leaves no step. `undo` and
`redo` restore the exact state, and the log is bounded by `UndoLimits`.
`save` writes a versioned text document into caller storage, and `load`
validates the whole document before changing anything and names the line
and column of the first error. Port lamps (`set_port_lamp`) are evaluation
state: they are neither recorded for undo nor saved.

`NodeCanvas` (`<gui_forms/controls/node_canvas.hpp>`) shows and edits a
graph it borrows through `set_graph`. It pans and zooms, draws through the
theme, connects ports with magnetic link drags, selects, moves with grid
snap and has a full keyboard path. Each gesture is one `DragTracker`
session and one undo group, which Escape rolls back.

## Indicator lamps and danger buttons

`IndicatorLamp` (`<gui_forms/controls/indicator_lamp.hpp>`) is a round lens
in a bezel that shows `state`: `IndicatorLampState::off`, `on`, `error` or
`fault`, with an optional legend in `text`. A lamp in `fault` is failing:
lit in the error colour, it flickers and stutters as a breaking lamp does,
the same way in every run (seeded by its stable id), under three flashes a
second, and steadily lit under reduced motion. It takes no focus and passes
the pointer through. To assistive technology it is static text whose value
is the state ("off", "on", "error" or "fault"), so the state never rests on
colour alone. Themes style it through
`indicator-lamp::part(lens):state(on)`. `light_planes` names a baked
light-plane set from the window's resources, and `lamp_color` sets the
colour the bake is lit in. A `RadioButton` in the lamp look takes a
`lamp_color` too.

A `Button` that marks a dangerous or irreversible action sets `danger`:

```cpp
(*delete_button).danger = true;
```

Its parts then take the custom state `danger`, a warning mark leads its
content, and a theme that styles the state (the default theme's
`button::part(face):state(danger)`) lights the face from inside with a slow
pulse. The pulse holds steady under reduced motion, while disabled and
outside a window.
