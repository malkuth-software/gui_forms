# Interaction contract

Status: **GIVEN interface behavior for deterministic fixtures; host and control
implementation remain in GUI.Forms**.

All interactions run through public GUI.Forms event, command, focus, overlay,
text, drag, semantic, and retained-state contracts. Mouse-only success is
incomplete.

## 1. Global principles

1. One user action produces one command/state transition trace.
2. Pointer, keyboard, accessibility action, menu, and ribbon presentations meet
   at the same command.
3. Focus is not selection; hover is neither.
4. Navigation changes location immediately and records history.
5. Destructive-looking actions alter only session fixtures and are resettable.
6. No action depends on sound, color, hover, or animation alone.
7. Overlays close predictably and restore focus.
8. State updates synchronously on the UI thread; asynchronous-looking fixture
   outcomes use deterministic scheduled generations.
9. Motion may decorate a completed state change; it never delays authority.
10. Disabled explains “cannot act”; unavailable explains “source/capability is
    absent”; pending/staged explains “not yet applied.” Do not conflate them.

## 2. Startup and reset

On startup or `demo.reset`:

1. load immutable fixture catalogue;
2. create stable shell and all three daily retained surface subtrees;
3. set Sapphire/House Composite;
4. navigate to Projects without creating a history-back entry;
5. expand required tree ancestors;
6. select `Facade Study.png`;
7. project its selection properties;
8. set status and generation 86;
9. leave path matrix closed;
10. set result `result-invoice-text` pinned/expanded only when Search mode is
    selected;
11. set the three default criteria modules;
12. clear fake operations and diagnostics after the reset marker;
13. assign initial keyboard focus according to host convention, with explicit
    test focus available through the controller.

Reset is a state change and may emit the option-committed sound cue; its button
press itself does not.

## 3. History navigation

### Back

- pointer click, Alt+Left, menu/ribbon, and accessibility action call
  `nav.back`;
- unavailable at history start;
- closes path matrix and nonpinned transient inspection;
- commits/cancels active edits according to the edit's validation contract;
- changes location, tree selection, breadcrumb, object model, selection, title,
  search placeholder, and status in one session generation;
- restores stored selection/focus/scroll snapshot for that history entry;
- emits location-change sound and semantic location notification once.

### Forward

Same rules as Back using forward history. Any new navigation after Back removes
the forward branch.

### Up

- activates the fixture parent;
- disabled at the current volume root;
- records the old child as the preferred selection in the parent field;
- otherwise follows ordinary navigation transition rules.

### Tree and breadcrumb navigation

- single-click/selecting a tree node changes tree selection and navigates;
- keyboard arrows move tree focus; Enter/Right or an explicit activation
  navigates according to TreeView convention;
- clicking a breadcrumb segment navigates to that ancestor;
- activating the current final segment does not generate redundant history;
- navigating from Search returns to Folder for the destination unless a fixture
  result explicitly opens a virtual folder;
- Criteria remains a virtual-folder state until user chooses an ordinary
  location through tree/breadcrumb/history.

## 4. Path matrix state machine

States:

```text
closed
  -> open-browse
  -> open-editing
  -> resolving-valid | resolving-invalid
  -> closed after navigation/cancel
```

### Open

Activating the trailing `./` terminal:

- opens the anchored matrix if closed;
- closes it if it is already the active overlay;
- keeps the `./` control as overlay owner;
- exposes `aria-expanded`/native expanded state;
- initially focuses the current final segment or overlay container according to
  host overlay convention;
- displays the full drive-rooted current stack plus exactly five recent stacks;
- does not change location merely by opening;
- does not emit a sound merely because an overlay opened.

### Browse current/recent stacks

- clicking or pressing Enter on a current ancestor segment navigates there;
- activating a recent stack navigates to its complete path;
- each recent row is one location option but exposes its breadcrumb components
  as descriptive structure;
- navigation closes the overlay and restores focus to the most appropriate
  location control (`./` or breadcrumb current segment);
- navigation emits one location sound/status event.

### Enter direct edit

Activating the open tail immediately after the last current breadcrumb:

- replaces the visual current stack with a dark terminal-like text editor
  inside the same overlay identity;
- preloads the complete current path with trailing separator;
- moves the caret to the end and selects nothing;
- preserves overlay size/anchor;
- shows completion and resolution regions;
- does not open another modal or dialog.

### Editing and completion

- type, caret, selection, clipboard, undo, dead keys, IME, grapheme movement,
  Home/End, and platform word navigation use the real GUI.Forms editor;
- fixture variables expand for `~`, `$HOME`, `${HOME}`, `$PROJECTS`, and
  `${PROJECTS}`;
- completions are generated from the immutable fixture catalogue and carry a
  monotonically increasing request generation;
- stale completion generations are ignored;
- Down/Up moves completion focus/active descendant without destroying text
  focus;
- Tab accepts the current completion and keeps editing;
- Enter resolves and navigates when valid;
- Escape first leaves editing back to browse state; a second Escape closes the
  matrix;
- click-away closes the matrix and discards uncommitted text;
- invalid text stays in the editor with a non-color fault and exact resolution
  explanation; Enter does not navigate;
- no completion is a product recommendation; it is a direct fixture path
  match.

Default completions:

- `/Users/quentin/Work/Projects/Orchard Study/`
- `/Users/quentin/Work/Projects/North Shore/`
- `/Users/quentin/Work/Projects/Print Masters/`

## 5. Search entry and mode

### Enter Search

- focusing the shared search field does not change mode;
- typing a nonempty query schedules a deterministic fixture result generation;
- Enter or the fixture debounce deadline commits Search mode;
- the Folder right selection pane leaves layout and semantic task order;
- the tree remains available and represents scope/location;
- current query is `invoice quartz` in the reference scenario;
- no separate preview is created.

### Result expansion

Each result has independent `hovered`, `focused`, `pinned`, `selected`, and
`unavailable` state.

- pointer dwell/hover expands temporarily after the admitted hover-intent
  threshold;
- moving away collapses an unpinned row;
- keyboard focus expands without requiring hover;
- click or Space pins/unpins the active row;
- Enter invokes the result's default fake activation rather than toggling pin;
- only one row is pinned at a time;
- an unavailable row still expands and explains which stored evidence exists;
- expansion preserves list scroll anchor and does not cause pointer-target
  oscillation;
- the complete logical result list remains accessible even when only a bounded
  set of rows is realized.

### Result evidence

Expanded content exposes:

- path/location;
- exact reason(s) matched as one metadata string;
- inline excerpt or explicit absence;
- percentage match metric;
- plugin information and provenance;
- current/stale/offline catalogue state.

Percentage is factual evidence from the fixture. It never changes button
defaultness, uses persuasive copy, or says what the user should infer.

### Search selection/activation

- single click selects and may focus the result;
- Enter/double-click fake-opens the fixture object or navigates to its fixture
  location according to the result action;
- context menu supplies Open, Open containing location, Copy path, and fixture
  Properties presentations;
- activating a result may return to Folder and select its object;
- no real file or application opens.

### Clear Search

Escape in an empty/noncomposing search editor returns to Folder; a clear
command empties query and returns to Folder. Selection restoration uses the
stored Folder surface state.

## 6. Folder selection and activation

### Pointer

- single click selects one object and places collection focus appropriately;
- Control/Command click toggles selection;
- Shift click ranges from stable anchor in visual order;
- double-click/Enter activates folder navigation or fake file open;
- right click preserves an existing multiselection when clicking within it;
- right click outside selection selects the target before opening context menu;
- background click clears selection only when no modifier is active.

### Keyboard

- arrows use deterministic spatial navigation in icon mode;
- Home/End move to first/last object;
- Page Up/Page Down scroll and choose the nearest target by geometry;
- Space toggles/selects according to host convention;
- Enter activates focused selection;
- F2 or context command begins fake rename;
- Delete invokes fake delete command;
- platform Select All shortcut selects all logical objects;
- type-to-select remains separate from search and cycles fixture names.

### Selection projection

On selection change:

- update status summary;
- update Selection pane object/property projection;
- cancel a stale preview generation and bind the new fixture resource;
- maintain pane scroll rules without silently jumping if only a property value
  changes;
- emit one coalesced semantic selection notification;
- emit no sound for ordinary selection.

## 7. Inspection tooltip/popover

Hover or keyboard focus on eligible object `File Manager` can expose:

```text
Folder · 284 objects · 1.27 GB allocated · indexed 09:12
```

Rules:

- plain factual inspection appears after hover intent or focus help command;
- it is initially pointer-neutral;
- it has a keyboard/focus equivalent;
- it does not change selection, focus, or authority;
- it closes on target loss, Escape, navigation, or conflicting overlay;
- richer/pinned interaction requires the admitted inspection-popover path and
  is not approximated as a tooltip;
- no sound.

## 8. Pane splitting and collapse

### Resize

- pointer down inside invisible seam hit area captures pointer;
- drag changes pane extent within declared min/max;
- keyboard-focused splitter supports directional increments and a larger
  modified increment;
- extent is stored by stable pane ID;
- resize invalidates layout and affected paint only;
- no repeated sound during resize.

### Collapse/restore

- seam tab is visually small and low-contrast at rest;
- proximity/focus enlarges the tab without moving pane allocation;
- activation collapses or restores the pane to its remembered extent;
- collapsed pane leaves visual and semantic task order;
- focus inside a collapsing pane transfers to the nearest governing control;
- collapse/restore emits one state-change sound;
- reduced motion changes immediately;
- content-aware responsive collapse uses the same pane state machinery but
  records whether collapse is automatic or locally set.

System-set accommodation collapse follows the system-derived presentation
state. Locally set collapse remains a local explicit state. Restoring window
space does not override a user-collapsed pane.

## 9. Selection pane behavior

### Preview disclosure

- button toggles only preview image/frame, not the complete pane;
- property list moves up within the single scroll plane;
- expanded state/name are announced accessibly;
- emits one pane-content state sound;
- no sound for focusing/pressing the button itself.

### Editable name

- activating the Name value creates/activates a real single-line editor;
- Enter validates and commits a session-only rename;
- Escape cancels and restores the fixture name;
- empty, path separator, and fixture-duplicate cases show deterministic inline
  validation;
- successful rename updates object field, selection heading, properties,
  search fixture projection where applicable, and trace in one generation;
- the immutable catalogue remains unchanged and reset restores original name;
- success emits option/state-change sound.

### Opens with

- opens a GUI.Forms choice overlay with fixture handlers;
- changing the handler is session-only and emits option-committed sound;
- merely opening the menu is silent;
- no host application enumeration occurs.

### Preview copy/drag

- Copy places a fixture payload on the admitted clipboard test seam;
- drag source advertises the deterministic fixture reference and optional
  bounded preview bytes only when GUI.Forms host support exists;
- dragging from preview toward a tree folder produces target cues and fake
  operation handoff;
- no private drag implementation is permitted in the demoboard.

## 10. View and sort

- icon/details views are two presentations of one stable object model;
- changing view preserves selection, focus item, range anchor, and reasonable
  scroll anchor;
- view commands appear in ribbon/menu/status control;
- sort commands live in ribbon/menu, not a redundant content bar;
- sorting preserves stable selection IDs and recalculates spatial navigation;
- changing view/sort emits an option-committed sound once;
- context/status copy updates without separate content header.

## 11. Criteria interactions

### Enable/disable

- checkbox toggles module participation;
- cheap criteria apply live through deterministic fixture projection;
- expensive criterion enablement remains staged until Apply;
- module remains visible and explains disabled/staged state;
- selection/result projection updates while preserving stable identities where
  still present;
- commit emits option/state sound.

### Edit fields

- property/operator/value are real choice/editor controls;
- field changes validate typed combinations;
- cheap changes can update live after commit;
- expensive changes mark `staged · expensive · not applied`;
- no inference or actual search computation occurs;
- staged state is not represented solely by violet color.

### Add/remove

- `+ module` opens a bounded menu of fixture module types;
- adding produces a new stable session criterion ID from a deterministic
  counter;
- Remove removes only session state and is undoable through reset or fixture
  undo command where exposed;
- removing the focused module transfers focus predictably to next module or Add;
- removal emits a state-change sound.

### Apply

- visible/default only when staged expensive changes exist;
- activates deterministic scheduled progress (short fixture duration);
- disables duplicate Apply while pending but leaves cancel/status available;
- replaces result projection at completion generation;
- announces result count and clears staged state;
- emits start and completion sounds, not repeated progress sounds.

## 12. Context menus

Object context menu minimum:

```text
Open
Open with >
—
Cut
Copy
Move / copy >
—
Rename
Delete
—
Properties
```

Background context minimum:

```text
New folder
Paste (fixture availability)
—
View >
Sort by >
—
Properties
```

Rules:

- command state is snapshotted immediately before open;
- plugin-fixture entries, if shown, are clearly grouped and bounded;
- opening/closing menus is silent;
- Escape/click-away restores focus;
- no context command touches disk.

## 13. Drag and drop

Target exercise matrix:

| Source | Target | Fake result |
|---|---|---|
| object item | tree folder | same-volume move/copy session transition |
| object item | folder object | same-volume move/copy session transition |
| object item | content background | rejected/no-op when same location |
| preview | tree folder | copy fixture preview/object reference |
| primary window | second product window target | cross-window session transition |
| fixture external peer | tree/object/background | typed inbound fixture transfer |

During drag:

- non-key participating windows remain readable;
- approaching a valid secondary window restores destination emphasis without
  stealing keyboard focus;
- tree hover-expand and edge autoscroll are bounded/cancellable;
- accepted/rejected operation is indicated by cursor, local target material,
  and semantics—not color alone;
- drop negotiation is separate from fake operation outcome;
- same-volume no-conflict result shows no modal;
- cross-volume and collision fixtures open the appropriate deterministic task
  dialog;
- complex directory merger opens the operation drawer fixture;
- drag start/drop/completion may produce state sounds; mere target hover does
  not.

If public GUI.Forms external drag is unavailable, expose this scenario as
`unavailable`; do not simulate it with pointer events and claim conformance.

## 14. Fake operation surfaces

Normal same-volume fixture:

- immediate acceptance;
- status changes to operation progress if duration exceeds brief threshold;
- completion status and reversible fake outcome;
- no overlay.

Different-volume fixture:

- task dialog explains copy semantics and destination;
- default/cancel roles are explicit;
- acceptance starts hierarchical operation fixture.

Collision fixture:

- task dialog shows source/destination factual identity;
- choices: Replace, Keep both, Skip, Cancel;
- no manipulative recommendation styling.

Complex merger fixture:

- persistent edge drawer;
- parent and child operation progress;
- retry/skip/cancel fixture controls;
- collapsed drawer preserves accessible status and status-line authority.

## 15. Window participation

States:

- **key** — receives keyboard input, full identity emphasis;
- **secondary participating** — not key, still readable/usable for transfer;
- **drag proximate** — secondary plus local destination emphasis near pointer;
- **deactivated** — genuinely unavailable review/plugin board; strong recession.

Changing participation does not disable controls or erase semantic children.
Drag proximity never steals key status. A deactivated controller board is not a
model for ordinary non-key product windows.

## 16. Theme/construction switches

Controller selection performs one retained theme transaction:

1. validate selected resource/material pack;
2. keep control/model/command identities;
3. replace color/material roles;
4. invalidate style, measure only if the selected construction truly changes
   metrics, then paint;
5. preserve focus, selection, overlays when compatible;
6. record fallback faults;
7. emit one option-committed sound.

Atmosphere never changes geometry. Construction may change material geometry
only through declared role metrics; the default House Composite remains the
golden target.

## 17. Accessibility actions and announcements

Required quiet announcements:

- location changed;
- search results updated/count/unavailable source;
- result expanded/collapsed/pinned;
- criterion staged/applied/invalid;
- pane collapsed/restored;
- validation fault;
- operation started/completed/failed/cancelled;
- source became offline/stale.

Do not announce:

- every hover;
- every pointer movement during drag;
- decorative theme changes beyond the committed option result;
- repeated progress values more frequently than useful;
- button presses whose resulting state is already announced.

Screen-reader action, shortcut, ribbon, and pointer must produce the same
session transition trace.

## 18. Deterministic time

All hover intent, result expansion, fake search completion, operation progress,
tooltip delay, transition, and sound coalescing use an injected GUI.Forms clock.

Tests may:

- advance exactly to a deadline;
- assert no state before the deadline;
- interrupt/reverse an in-flight transition;
- enable reduced motion and assert immediate replacement;
- assert idle has no pending perpetual wakeup.

No `sleep`-based interaction test is accepted.
