# Native File Manager Demoboard project specification

Date: 2026-08-05

Status: **GIVEN consumer specification; implementation and ABI remain gated**.

## 1. Question answered by this project

Can the public retained GUI.Forms system express the complete File Manager
interface—its material depth, dense layout, stable objects, keyboard and pointer
behavior, overlays, virtual collections, editing, accessibility, responsive
collapse, and bounded motion—without a browser, immediate mode, real product
services, or application-local widget framework?

The demoboard answers by becoming a serious native consumer. It does not answer
whether filesystem, indexing, search, preview isolation, plugins, or transfers
are correct or performant in the product.

## 2. Relationship to the parent projects

### GUI.Forms owns

- window and host participation;
- retained control identity and lifetime;
- layout, focus, commands, input, capture, overlays, scrolling, and virtual rows;
- text editing/shaping/rasterization;
- drawing/material/resource primitives;
- drag/drop and clipboard presentation seams;
- semantic graph and native accessibility publication;
- timers, motion deadlines, sound presentation hook, and diagnostics.

### Demoboard owns

- immutable fixture catalogue;
- small mutable fixture session state;
- product-shaped composition of public GUI.Forms controls;
- command bindings to deterministic fake outcomes;
- product-specific copy, ordering, and visual roles;
- review-controller settings;
- headless scenarios and native captures proving consumption.

### Demoboard does not own

- filesystem identity or operations;
- index/search authority;
- Orchestrator negotiation or plugin supervision;
- preview decoding or hostile-content isolation;
- a replacement for any reusable GUI.Forms primitive;
- cross-project C ABI shape;
- or final File Manager frontend architecture.

## 3. Executable shape

The eventual project should contain ordinary C++ sources under this directory
and link only the public GUI.Forms package. Recommended internal layout:

```text
file_manager_demoboard/
  AGENTS.md
  README.md
  PROJECT_SPECIFICATION.md
  VISUAL_CONSTRUCTION_SPECIFICATION.md
  CONTROL_AND_STATE_INVENTORY.md
  INTERACTION_CONTRACT.md
  FIXTURE_CATALOGUE.md
  IMPLEMENTATION_SEQUENCE.md
  ACCEPTANCE_GATES.md
  reference/
  include/file_manager_demoboard/
    fixture_model.hpp
    session_state.hpp
    commands.hpp
    surface_ids.hpp
  src/
    app.cpp
    controller_window.cpp
    product_window.cpp
    folder_surface.cpp
    search_surface.cpp
    criteria_surface.cpp
    path_matrix.cpp
    selection_pane.cpp
    review_boards.cpp
    fixture_model.cpp
    session_state.cpp
  resources/
    demoboard_resources.manifest
    icons/
    previews/
    sounds/
  tests/
    fixture_tests.cpp
    command_trace_tests.cpp
    layout_trace_tests.cpp
    interaction_trace_tests.cpp
    semantic_snapshot_tests.cpp
    resource_boundary_tests.cpp
```

This layout is guidance, not a public ABI. Files may be regrouped if parent
conventions demand it, but the model/view/resource/test separations remain.

## 4. Windows and surfaces

### 4.1 File Manager Surface

One native top-level window named `File Manager — Demoboard`. It uses native
host behavior for move, resize, close, minimize, maximize/zoom, system menu, snap
where available, scale changes, and accessibility root identity. GUI.Forms may
draw title content only through its admitted custom-chrome seam; the demoboard
must not manually counterfeit host resize or caption behavior.

The window owns one stable shell. Folder, Search, and Criteria replace the
center surface through stable retained compositions. The shared title, ribbon,
navigation, tree where applicable, selection pane where applicable, and status
line retain identity across mode switches.

Default startup:

- mode: Folder;
- location: `/Users/quentin/Work/Projects`;
- selected object: `obj-facade-study`;
- atmosphere: Sapphire Dusk;
- construction: House Composite;
- tree expanded through `quentin / Work / Projects`;
- right pane expanded;
- preview image expanded;
- icon view;
- sort by name ascending;
- sounds on;
- ordinary motion;
- nominal text scale and 1× logical scale unless host provides otherwise.

### 4.2 Demoboard Controller

A separate owned tool window, never embedded in the product surface. It contains:

- surface selector: Folder, Search, Criteria, Palettes, Icons, Styles, DNA;
- atmosphere selector: twelve families, Sapphire default;
- construction selector: twelve families, House Composite default;
- fixture condition: normal, offline volume, stale index, preview unavailable,
  transfer running, conflict, empty folder, million-item virtualization;
- accommodation toggles driven through the same host/theme input seam: nominal,
  high contrast, 125/150/200/225% text, reduced motion, sounds off;
- window participation selector for test-only key/secondary/deactivated states;
- deterministic clock controls: play, pause, advance 16 ms, 100 ms, 1 s;
- reset-to-reference button;
- diagnostic counters and capability report;
- capture-state selector for automated goldens.

The controller may use plain stock GUI.Forms styling. It must not distort the
product window's layout or claim product visual authority.

### 4.3 Optional second product window

The controller can create one additional File Manager Surface bound to a second
fixture session. This exists only to exercise participating secondary windows,
cross-window selection/drag destinations, focus independence, and shared theme
updates. It is not a tab or dual-pane mode.

## 5. Model architecture

### 5.1 Immutable catalogue

`FixtureCatalogue` is loaded from compiled/static data and never reads disk at
runtime. It owns stable IDs and immutable descriptions for:

- volumes and roots;
- folders and file-like objects;
- object properties;
- thumbnails/preview resource IDs;
- search result evidence;
- criteria definitions and result sets;
- recent paths and completions;
- operation scenarios;
- plugin-information facts;
- status/failure variants.

### 5.2 Mutable session

`DemoboardSession` owns only transient fake state:

- current mode and location;
- history index and forward/back stacks;
- expanded tree nodes;
- selection set, focus item, range anchor;
- view mode and sort order;
- pane extents/collapse state;
- path matrix/editor/completion state;
- search query, hovered row, pinned row, keyboard-active row;
- criterion enabled/removed/edited/staged state;
- preview/property disclosure state;
- fake transfer/operation progress;
- local theme/construction choice;
- sound/motion/accommodation choices;
- controller fixture condition.

No session mutation changes the immutable catalogue. Destructive-looking
commands produce a fake result record and visible reversible state, then reset
on application restart or controller reset.

### 5.3 View projection

Surface models expose immutable snapshots to controls. A command mutation:

1. validates against fixture state;
2. creates a new session generation;
3. updates only affected view properties/models;
4. declares layout/paint/text/semantic effects;
5. posts any semantic status and sound cue associated with the state change;
6. records a deterministic trace entry.

There is no polling loop and no per-frame rebuilding of the control hierarchy.

## 6. Capability truthfulness

The demoboard maintains a visible and machine-readable capability report. Each
required feature is one of:

- `supported` — public GUI.Forms implementation is exercised;
- `simulated-domain` — GUI behavior is real but the product operation/result is
  deterministic fixture data;
- `unavailable` — GUI.Forms primitive is absent; the slice displays a labelled
  unavailable state;
- `incompatible` — current public behavior violates the specified contract.

Empty success and visual-only impersonation are forbidden. For example:

- painting breadcrumb-looking boxes without keyboard/path semantics is not
  breadcrumb support;
- changing a row height on hover without focus/pin/virtual-height correction is
  not correspondence-row support;
- drawing a dotted rectangle without semantic focus is not focus support;
- logging “drag started” without a typed host session is not external drag;
- drawing text in a bitmap is not accessible text.

## 7. Public GUI.Forms dependency map

The target consumes the parent consumer-profile requirements as follows:

| Demoboard area | Primary requirements |
|---|---|
| native shell | FM-W01–W07, FM-LY01–LY05 |
| ribbon and menus | FM-C01–C08, FM-W09 |
| breadcrumb/path matrix | FM-N01–N07, FM-C07, FM-LY09 |
| folder tree and object field | FM-O01–O10, FM-LY07, FM-LY08 |
| selection pane | FM-O11, FM-O12, FM-LY06 |
| search correspondence rows | FM-C09, FM-C10, FM-S01, FM-LY08 |
| criteria virtual folder | FM-S02, FM-S03, FM-O02, FM-O11, FM-O12 |
| transfer fixtures | FM-D01–D10, FM-S04–S06 |
| accessibility | FM-A01–A12 |
| typography | FM-T01–T09 |
| House material | FM-R01–R12, FM-V01–V04 |
| motion and sound | FM-LY10, FM-V05–V07 |
| inspection and tests | FM-X01–X07, FM-X09 |

This table is a demand map, not permission to implement those capabilities
inside this project.

## 8. Commands and fake outcomes

Every visible action binds a stable command object. Shared commands appear in
ribbon, menu/context presentation, shortcuts, accessibility actions, and tests
without duplicating execution.

Minimum commands:

```text
nav.back                 nav.forward              nav.up
nav.open_location        nav.open_path_matrix     nav.edit_path
nav.accept_path          nav.cancel_path          nav.accept_completion
view.icons               view.details             view.sort_name
view.sort_modified       view.toggle_tree         view.toggle_selection
view.toggle_preview_art  view.properties
selection.open           selection.move_copy      selection.delete
selection.copy           selection.rename         selection.select_all
search.focus             search.activate_result   search.pin_result
search.clear_pin         criteria.add             criteria.remove
criteria.toggle          criteria.edit            criteria.apply_staged
properties.edit_name     properties.choose_handler
demo.reset               demo.open_second_window  demo.set_condition
```

`New folder`, `Rename`, and `Open with` remain contextual/menu commands, not
permanent ribbon commands. The demoboard may expose them in a context-menu test
even though no real filesystem operation occurs.

Fake outcomes must look ordinary. Do not show a modal for a same-volume mock
move/copy with no conflict. Use dialogs only for the explicit cross-volume,
collision, blocking-error, or complex-merger fixture.

## 9. Review boards

The copied atlas contains palette, icon, style, and DNA surfaces. In the native
demoboard these are review boards reached from the controller. They reuse the
same material, controls, typography, and state semantics, and must remain fully
keyboard accessible.

### Palette board

Twelve atmosphere cards with identical geometry. Selecting one atomically
updates the product window's relational color roles. It must not change
construction geometry.

### Icon board

Shows the approved direction and provenance metadata using runtime PNG assets.
Candidate libraries remain evidence, not automatically bundled dependencies.
Missing icon resources show explicit fallbacks and provenance faults.

### Style board

Twelve construction-family cards with Sapphire held constant. Selecting one
rebuilds material recipes only; content state, selection, focus, layout identity,
and commands survive.

### DNA board

One decision at a time: identity, principle, specimen, audit, direction,
evidence, failure/reversal, and local review verdict. Verdicts remain in the
demoboard session and never become ADRs.

## 10. Instrumentation

The controller and tests can query:

- retained control count and stable-ID map;
- layout pass count and reason;
- display chunk count;
- damaged region count and pixels;
- paint count by control ID;
- shaped run, glyph, and cache counts;
- realized/virtual item counts;
- overlay and capture owner;
- focused control and selection identities;
- semantic node/proxy counts;
- pending deadlines and sound cues;
- resource bytes and fallback events;
- command trace and current fixture generation.

No diagnostic requires a private renderer or host type in demoboard code.

## 11. Definition of done

The project is complete only when:

1. all three daily surfaces pass their interaction scenarios using public
   GUI.Forms;
2. all four review boards are native and keyboard accessible;
3. visual captures match the specified composition and material within declared
   native typography/profile tolerances;
4. responsive behavior works down to 150×150 without trapping the user;
5. high contrast, text scaling, reduced motion, and sounds-off retain complete
   meaning and actions;
6. semantic snapshots and at least one physical host screen-reader smoke pass;
7. the fake domain boundary is auditable and no real user file can be touched;
8. idle produces zero paint loop;
9. every missing public capability is recorded honestly rather than privately
   reimplemented;
10. clean install consumption succeeds without private parent headers.
