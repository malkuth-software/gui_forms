# Acceptance gates

Status: **required evidence contract; thresholds needing a renderer/font profile
are OPEN until that profile is admitted**.

Painting a close-looking window is not completion. Acceptance is layered so a
visual success cannot conceal broken focus, semantics, retained state, or fake
backend leakage.

## Gate A — scope and build purity

Pass only if:

- demoboard links the public/installed GUI.Forms package;
- no private renderer/host headers or symbols appear;
- no browser/web engine, HTML runtime, .NET, Java, Godot, or immediate-mode
  dependency appears;
- no runtime read/write of real user filesystem paths occurs;
- Engine and Orchestrator are not linked or launched;
- fixture catalogue is the only domain authority;
- parent tests pass before and after;
- capability report identifies unsupported requirements honestly.

Audit command/symbol details should follow parent build conventions.

## Gate B — reference composition

At 1450×850 logical client and nominal text/scale:

- row bands resolve to 40/23/66/40/flexible/24 within the selected font/profile
  tolerance;
- Folder/Criteria workspace resolves to 218/3/fluid/3/288;
- Search removes right pane and seam;
- object grid uses 86-minimum columns and 78 rows;
- tree/selection captions are 27 nominal;
- path `./` is at the end of breadcrumb well;
- deleted redundant content summary bar is absent;
- Folder/Criteria Selection pane is labelled `Selection`;
- Search has no Selection/Preview pane;
- criteria uses predicate rack, not historical left facets;
- default atmosphere/construction are Sapphire/House Composite.

Layout goldens inspect logical geometry and stable IDs, not only pixels.

## Gate C — material and visual hierarchy

Pass visual review if:

- title identity is bounded Watercolor sapphire/coral, not a full-window glow;
- ribbon is pearl/light with Office-like grouped geography;
- below-ribbon chassis is subtle graphite, not black;
- seams are visually about three logical pixels with larger invisible hit zones;
- Studio-like information panes are dense and factual;
- object field is white/near-white and ordinary files have no cards;
- selection and focus are distinct;
- window secondary state remains readable;
- icons show real size-specific depth rather than flat font glyphs;
- decoration never hides failure, unavailable, focus, or selection state;
- no screenshot correction listed in `reference/README.md` regresses.

## Gate D — folder interaction

Required scenarios:

1. startup location, selected object, status, and properties match fixtures;
2. pointer and keyboard selection preserve independent focus;
3. arrow navigation is deterministic in icon/details modes;
4. Back/Forward/Up/tree/breadcrumb share one navigation trace;
5. view/sort preserve stable selection and focus identity;
6. context menu commands bind shared command state;
7. fake rename validates and updates all projections atomically;
8. no real file is opened, renamed, deleted, or created;
9. coarse badge exposes exact inspection text without becoming a command;
10. properties use one scroll plane.

## Gate E — path matrix

Pass only if:

- `./` toggles one anchored overlay;
- complete current drive-rooted stack appears;
- exactly five recent full stacks appear with repeated roots;
- open tail converts the current stack directly to editor;
- autocomplete uses generation/cancellation semantics;
- Tab accepts completion, Enter navigates, Escape stages are correct;
- invalid path is visibly and semantically explained;
- overlay closes on click-away and restores focus;
- editable path exposes real caret/selection/IME/text ranges;
- location change updates title, breadcrumb, tree, content, status, and history
  in one transition.

## Gate F — search

Pass only if:

- seven logical results exist and realization remains bounded;
- compact rows are 45 nominal and expanded rows 126 nominal;
- hover intent, keyboard focus, and pinning are distinct;
- one pinned row persists through temporary hover elsewhere;
- variable-height expansion preserves scroll anchor and pointer stability;
- match percentage and evidence are factual/nonprescriptive;
- metadata is a string, not colorful criterion chips;
- plugin tags appear only in the plugin-information lane;
- excerpt is inline and matched substrings have non-color semantics;
- offline/stale result explains available evidence;
- Search has no right preview pane;
- screen-reader/keyboard can reach and activate every logical result.

## Gate G — criteria

Pass only if:

- three default modules match fixtures;
- enable/property/operator/value/remove are real controls;
- cheap changes project live fixture generations;
- expensive changes remain explicitly staged;
- Apply produces deterministic progress and final generation;
- module add/remove moves focus predictably;
- staged/invalid state is not color-only;
- result field shares stable collection/object model;
- Selection preview remains present;
- narrow/large-text layouts remain usable.

## Gate H — panes, responsive geometry, and accommodation

Test at least these logical client sizes:

```text
1450×850 reference
1200×760 common
960×680 narrow
720×520 compact
480×360 severe
300×240 extreme
150×150 enforced minimum
```

At each size:

- one useful current-location/content path remains;
- complete command vocabulary remains reachable through menu/overflow;
- automatic versus user collapse is distinguishable and reversible;
- no zero-size focusable control traps focus;
- no essential text draws outside bounds;
- seams remain operable through keyboard and enlarged hit target;
- overlays stay on screen or choose an admitted alternate placement.

Text-scale matrix: 100, 125, 150, 200, 225 percent. Display-scale matrix where
available: 1.0, 1.25, 1.5, 2.0. High contrast and reduced motion are separate
inputs.

## Gate I — accessibility

### Headless

- deterministic semantic snapshot keyed by stable IDs;
- explicit task order independent from paint/child order;
- correct role/name/value/state/action/relations;
- virtual tree/list/result children without one proxy per million logical items;
- text ranges, caret, selection, composition, and bounds for editors;
- collapsed/hidden surfaces leave task order;
- screen-reader action equals pointer/keyboard command trace;
- quiet, deduplicated live status.

### Physical hosts

Primary scenario on each available host:

1. identify window/location;
2. navigate ribbon/menu to Folder tree;
3. choose Projects and inspect selected Facade Study;
4. open and edit path matrix;
5. search `invoice quartz`, inspect match evidence, activate a result;
6. edit a Criteria module and Apply;
7. collapse/restore Selection pane;
8. complete one fake operation/conflict;
9. exit without touching disk.

Record VoiceOver, Narrator, and Orca evidence separately. A headless pass does
not imply physical screen-reader success.

## Gate J — drag, clipboard, and operations

When the parent capabilities exist:

- typed outbound and inbound fixture payloads negotiate copy/move/link/none;
- preview and object sources work;
- tree/folder/background/second-window targets work;
- drag proximity does not steal key focus;
- hover-expand/autoscroll are bounded and cancellable;
- rejected target is clear without color alone;
- same-volume no-conflict outcome opens no dialog;
- cross-volume and collision fixtures open correct task dialogs;
- merger uses drawer with hierarchical progress;
- external/synthetic peers prove host session behavior.

If absent, gate is OPEN/unavailable, not visually simulated.

## Gate K — motion and sound

- all transitions use injected clock;
- interruption/reversal leaves correct final state;
- reduced motion substitutes immediate complete state;
- no input waits for animation;
- idle has no perpetual deadline;
- cues occur only on listed state/location events;
- button/menu/hover/focus/ordinary selection are silent;
- sounds-off yields identical state and semantic traces;
- repeated events coalesce according to parent service policy.

## Gate L — retained rendering and bounds

Required measurements at reference scale:

- idle: zero repeated layout and zero repeated paints after quiescence;
- one object selection: damage remains bounded to old/new item, affected status,
  and Selection projection rather than whole window absent a measured renderer
  limitation;
- correspondence expansion: only affected rows/scroll region and necessary
  shadow bounds damage;
- caret blink: bounded editor damage and deadline;
- pane resize: bounded changed workspace region with no control recreation;
- theme transaction: one declared batch, no intermediate broken states;
- million-item fixture: realized controls/items and semantic proxies remain
  bounded by viewport/overscan policy;
- resource bytes remain within declared fixture pack bounds.

Record p50/p95/p99 and worst relevant stall for startup, reference layout,
selection, navigation, search projection, correspondence expansion, criteria
apply projection, and theme switch. Do not call the result fast without a
baseline and environment.

## Gate M — native capture matrix

Required product-window captures, excluding controller:

```text
native-folder-sapphire-house-1450x850.png
native-folder-sapphire-house-960x680.png
native-folder-sapphire-house-150x150.png
native-folder-sapphire-house-text225.png
native-folder-high-contrast.png
native-path-matrix-browse.png
native-path-matrix-editing.png
native-search-compact-and-pinned.png
native-search-offline-expanded.png
native-criteria-default.png
native-criteria-staged-progress.png
native-selection-preview-collapsed.png
native-secondary-window-participating.png
native-drag-proximate-window.png
native-collision-dialog.png
native-merger-drawer.png
native-palette-board.png
native-icon-board.png
native-style-board.png
native-dna-board.png
```

Each capture records OS, GUI.Forms revision, demoboard catalogue revision, font
pack/hash, renderer profile, atmosphere, construction, display scale, text
scale, contrast, motion setting, and fixture generation.

## Gate N — visual comparison method

Compare at native zoom. Use:

1. logical geometry snapshot;
2. stable semantic snapshot;
3. native screenshot side by side with reference;
4. optional pixel diff only within the same OS/font/renderer profile;
5. human review of hierarchy, material, crispness, state, and density.

Browser text pixels are not native goldens. Exact native profile goldens become
authoritative only after font and raster profile admission.

Initial provisional tolerances before exact profile acceptance:

- structural band/column edges: ±1 logical pixel at 1×;
- control/icon anchor positions: ±2 logical pixels;
- text baseline/advance: measured and reported, not hand-waved into a broad
  screenshot tolerance;
- gradients/shadows: role and bounded extent must match; platform compositor
  differences are qualified;
- color: compare in declared sRGB/profile, not screenshot-tool transformed
  values.

## Gate O — resource and license provenance

- all runtime assets are PNG or admitted built-in data forms;
- every asset has source, revision, license, hash, dimensions, and derivative
  record;
- production Portsmouth rights are recorded before non-evaluation distribution;
- selected body/font fallback packs have license and coverage manifests;
- MIT icon candidates are pinned per file/revision;
- no copied screenshot pixels become product icon assets;
- malformed/oversized resource fixtures fail safely;
- missing assets select explicit built-in fallback or labelled unavailable state.

## Gate P — final report

The completion report must say:

- which daily surfaces and review boards are complete;
- which public GUI.Forms capabilities were exercised;
- which remain unavailable or incompatible;
- visual deltas and why;
- physical accessibility/accommodation coverage;
- resource/license status;
- performance/damage measurements with environment;
- negative results;
- and explicitly that the demoboard does not prove real filesystem, Engine,
  Orchestrator, plugin, search-quality, or hostile-preview behavior.
