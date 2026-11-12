# Controls

GUI.Forms ships its controls as stock C++ types under
`include/gui_forms/controls/`. Each has a stable identity, retained state,
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
