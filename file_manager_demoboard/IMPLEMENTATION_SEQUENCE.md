# Implementation sequence

Status: **consumer work order; does not authorize implementation of missing
GUI.Forms capabilities**.

The demoboard is built vertically. Each slice produces a runnable consumer,
headless trace, native capture, and honest capability report. Do not build all
view models first and postpone interaction/semantics until the end.

## General slice rule

For every slice:

1. name the control/state IDs from `CONTROL_AND_STATE_INVENTORY.md`;
2. name the public GUI.Forms facilities being consumed;
3. record any missing capability by FM requirement ID and owning parent
   milestone;
4. add or update deterministic fixtures;
5. create controls once with stable IDs;
6. bind shared commands and synchronous session mutations;
7. add headless input/focus/layout/semantic/damage traces;
8. run parent tests;
9. capture the native reference state if visually meaningful;
10. update capability report and evidence notes.

No slice may solve a missing parent primitive by hiding a reusable widget stack
under `Demoboard*` names.

## DEMO-D0 — package and installed-consumer proof

### Goal

Create the opt-in demoboard executable and test target without private parent
headers.

### Consume

- installed/public GUI.Forms target;
- Form/window;
- stable control IDs;
- headless host;
- resource lookup;
- diagnostics/capability query.

### Deliver

- empty File Manager Surface and plain Controller windows;
- compiled fixture-catalogue identity and reset command;
- public-include and symbol-boundary audit;
- startup/shutdown trace;
- explicit capability report listing all later demands unavailable rather than
  implied.

### Exit

- clean build from parent tree and installed package;
- open/close on the first native host;
- zero demoboard references to renderer/host private headers;
- parent tests unchanged.

## DEMO-D1 — fixed shell geometry and material specimen

### Goal

Reproduce the reference bands and default Sapphire/House Composite materials
with no domain interaction yet.

### Controls

`fm.window.primary`, title, ribbon bands, navigation, workspace, three panes,
splitters, status.

### Deliver

- 1450×850 logical layout trace;
- 40/23/66/40/flexible/24 row geometry;
- 218/3/fluid/3/288 workspace geometry;
- default relational color roles and material recipes;
- graphite seam specimen;
- controller reset/atmosphere/construction fields wired only when supported;
- headless layout snapshot and first native capture.

### Do not fake

- native custom chrome if host seam is absent;
- owned font metrics;
- accessibility publication;
- responsive priority collapse.

Label those unavailable and continue only with layout portions public GUI.Forms
can express honestly.

## DEMO-D2 — command, navigation, tree, and fixture object field

### Goal

Make Folder mode fully navigable with deterministic fixture data.

### Controls

ribbon tabs/groups/commands, shared menus, Back/Forward/Up, breadcrumb, search
editor shell, TreeView, object collection, Selection pane property projection,
status.

### Deliver

- Folder startup state;
- stable selection/focus and icon/details model;
- sort in ribbon/menu;
- no redundant content bar;
- context menus with fake commands;
- selection projection and preview/property fixture;
- keyboard spatial navigation and type-to-select;
- history/location trace;
- semantic snapshot if parent publisher/graph exists.

### Exit

Folder reference scenario passes pointer and keyboard flows; changing sort/view
preserves stable selection.

## DEMO-D3 — panes, editing, responsive behavior

### Goal

Exercise split/collapse mechanics, property editing, and window constraints.

### Deliver

- tree/right-pane resize and collapse/restore;
- preview-art disclosure;
- session-only Name and Opens-with editing;
- inline validation;
- window min 150×150;
- content-aware shrink and priority collapse traces at named sizes;
- large-text reflow/collapse states;
- state/location sound cue log or presentation if available.

### Exit

No window size traps access to location, complete commands, or one usable
content region. No nested Selection-pane scroller appears.

## DEMO-D4 — path matrix and text instrument

### Goal

Implement the owner-specified trailing `./` matrix and direct path editing.

### Deliver

- anchored overlay;
- full current drive-rooted stack;
- exactly five recent full stacks;
- open-tail conversion to text editor;
- fixture variable expansion and autocomplete generations;
- Tab acceptance, Enter navigation, two-stage Escape;
- malformed/invalid path faults;
- focus restoration and semantic relations;
- IME/caret/selection traces where text stack supports them.

### Exit

Every scenario in Interaction §4 passes without a second modal or private text
editor.

## DEMO-D5 — search correspondence surface

### Goal

Replace the historical table/search-preview pattern with the current expandable
correspondence design.

### Deliver

- seven logical fixture results with bounded realization;
- 45-to-126 height expansion;
- hover intent, focus expansion, one pinned row;
- variable-height scroll anchoring;
- inline excerpt, percentage, factual metadata, plugin lane;
- offline/stale evidence state;
- no Selection pane;
- result activation back to Folder fixture;
- quiet semantic announcements.

### Exit

Search native capture matches current composition; pointer never oscillates
because row expansion moves its target; no preview pane participates.

## DEMO-D6 — criteria virtual folder

### Goal

Exercise the predicate rack and reusable virtual object/selection projections.

### Deliver

- three default modules;
- enable/edit/remove/add interactions;
- live cheap versus staged expensive state;
- deterministic Apply progress/result generation;
- result object field sharing stable object model;
- Selection preview/properties retained;
- narrow and large-text module reflow;
- validation/focus/semantic trace.

### Exit

The rack reads as an instrument, not chip soup; staged state is explicit without
color; selection remains stable across fixture-result changes where identity
survives.

## DEMO-D7 — transfer, operation, and multi-window exercise

### Goal

Consume public clipboard/drag/operation primitives against deterministic peers.

### Deliver

- object and preview drag sources;
- tree/folder/background targets;
- optional second product window and participation states;
- synthetic external peer;
- same-volume silent/no-dialog success;
- cross-volume and collision task dialogs;
- complex-merger drawer;
- progress/cancel/retry fixture state;
- accepted/rejected cues and accessibility status.

### Stop condition

If public external drag/clipboard/owned-window behavior is absent, leave the
scenarios unavailable. Do not implement a demoboard host adapter.

## DEMO-D8 — native accessibility and accommodations

### Goal

Prove the product-shaped custom surface through native OS systems.

### Deliver

- stock/default semantic adapters plus custom composition adapters;
- explicit task order;
- virtual tree/list/result children;
- editable text ranges;
- action parity traces;
- high contrast, 125–225% text, reduced motion, sound-off;
- screen-reader smoke on each available host;
- focus-visible and keyboard-only complete scenarios.

### Exit

Narrator, VoiceOver, and Orca primary scenarios are either measured on their
hosts or explicitly open. Headless semantic snapshots are not treated as a
physical screen-reader substitute.

## DEMO-D9 — review laboratories

### Goal

Native versions of Palettes, Icons, Styles, and DNA using the same controls and
materials.

### Deliver

- twelve atmosphere cards and atomic product update;
- icon/provenance board with PNG resources and failure states;
- twelve construction cards with state preservation;
- DNA index/decision/audit/verdict interactions;
- keyboard/focus/semantic coverage;
- controller separation from product captures.

### Ordering

Do not polish eleven alternate construction families before House Composite and
the three daily surfaces pass.

## DEMO-D10 — closure corpus

### Goal

Make completion reproducible and bounded.

### Deliver

- all acceptance scenarios automated where appropriate;
- native capture matrix;
- installed-consumer build;
- capability report with no silent unavailable behavior;
- damage/layout/text/resource measurements;
- idle-zero proof;
- license/provenance inventory;
- known-delta and negative-results ledger;
- final report stating what the demoboard does not prove.

## Cross-slice test names

Suggested stable test scenario IDs:

```text
DB-SHELL-001 reference geometry
DB-SHELL-002 minimum-size recovery
DB-FOLDER-001 startup selection
DB-FOLDER-002 spatial keyboard navigation
DB-FOLDER-003 sort/view identity preservation
DB-PANE-001 tree collapse and focus transfer
DB-PANE-002 selection preview disclosure
DB-PATH-001 matrix browse navigation
DB-PATH-002 direct edit autocomplete
DB-PATH-003 invalid path
DB-SEARCH-001 hover/focus/pin expansion
DB-SEARCH-002 offline evidence
DB-SEARCH-003 result activation
DB-CRITERIA-001 cheap live change
DB-CRITERIA-002 expensive staged apply
DB-CRITERIA-003 module removal focus
DB-DRAG-001 object to tree
DB-DRAG-002 cross-window participation
DB-OP-001 same-volume no-dialog
DB-OP-002 collision task dialog
DB-A11Y-001 task order
DB-A11Y-002 virtual children
DB-A11Y-003 action parity
DB-ACCOM-001 225-percent text
DB-ACCOM-002 high contrast
DB-ACCOM-003 reduced motion and sound off
DB-IDLE-001 zero idle paints
```
