# Control and state inventory

Status: **GIVEN demoboard composition; individual GUI.Forms controls remain
subject to their owning implementation gates**.

Stable IDs below are test, command, semantic, and DML identities. They must not
be regenerated from array positions, visible text, or fixture sort order.

## 1. Product-window retained tree

```text
fm.window.primary
├── fm.title.identity
├── fm.ribbon
│   ├── fm.ribbon.tabs
│   └── fm.ribbon.shelf
├── fm.navigation
│   ├── fm.nav.back
│   ├── fm.nav.forward
│   ├── fm.nav.up
│   ├── fm.path.breadcrumb
│   └── fm.search.editor
├── fm.workspace
│   ├── fm.tree.pane
│   ├── fm.tree.splitter
│   ├── fm.content.host
│   │   ├── fm.folder.surface
│   │   ├── fm.search.surface
│   │   └── fm.criteria.surface
│   ├── fm.selection.splitter
│   └── fm.selection.pane
├── fm.status
└── fm.path.matrix (owned anchored overlay)
```

Folder, Search, and Criteria retained subtrees are constructed once per window.
Inactive surfaces are hidden/nonparticipating, not destroyed and recreated on
every switch. Fixture model identities persist across projections.

## 2. Shell controls

| Stable ID | Type/role | Required state | Command/action | GUI.Forms demand |
|---|---|---|---|---|
| `fm.window.primary` | top-level Form/window | key, secondary, drag-proximate, deactivated; placement; scale | close/system actions | FM-W01–W04 |
| `fm.title.identity` | custom title content | location text, participation state | none | FM-W02, FM-W03 |
| `fm.title.icon` | image/presentational | resource generation | accessible window identity belongs to root | FM-V03, FM-R08 |
| `fm.ribbon.tabs` | command category strip | selected tab, focus, key tips | activate category | FM-C03, FM-C04 |
| `fm.ribbon.shelf` | command shelf | current category, collapse generation | present commands | FM-C03 |
| `fm.navigation` | horizontal composition | responsive mode | none | FM-LY01–LY04 |
| `fm.workspace` | split composition | pane extents/collapse | none | FM-LY05, FM-LY06 |
| `fm.status` | segmented status | surface/location/operation state | optional view commands | FM-S04, FM-C01 |

## 3. Ribbon and menu inventory

Tabs/categories:

| ID | Visible label | Contents |
|---|---|---|
| `fm.ribbon.file` | File | complete file-level menu entry; not a permanent open panel in the reference |
| `fm.ribbon.home` | Home | default selection/arrange/inspect groups |
| `fm.ribbon.edit` | Edit | selection/edit command presentations |
| `fm.ribbon.view` | View | object mode, sort, pane visibility, density |
| `fm.ribbon.go` | Go | history, Up, recent locations, path matrix |
| `fm.ribbon.commands` | Commands | complete command vocabulary and plugin-fed fixture entries |
| `fm.ribbon.help` | Help | keyboard help/inspection/about fixture |

Default Home shelf:

| ID | Presentation | Command | Group | Notes |
|---|---|---|---|---|
| `fm.ribbon.move_copy` | large split/dropdown | `selection.move_copy` | Selection | visible only when selection permits |
| `fm.ribbon.delete` | large | `selection.delete` | Selection | fake destructive outcome; context/menu equivalent |
| `fm.ribbon.view_mode` | large split/dropdown | `view.icons` / `view.details` | Arrange & inspect | current mode indicated |
| `fm.ribbon.sort` | labelled dropdown | `view.sort_name` etc. | Arrange & inspect | sort moved here from deleted redundant bar |
| `fm.ribbon.properties` | large | `view.properties` | Arrange & inspect | focuses/opens Selection pane |

`New folder`, `Rename`, and `Open with` are in context/complete menus, not
permanent Home shelf controls. The fixture can surface them through:

- `fm.context.background.new_folder`;
- `fm.context.object.rename`;
- `fm.context.object.open_with`.

Every ribbon item binds a shared command. Ribbon, menu, shortcut, context,
accessibility, and tests do not create separate execution paths.

## 4. Navigation controls

| Stable ID | Role | State/action |
|---|---|---|
| `fm.nav.back` | button | enabled when history index > 0; `nav.back` |
| `fm.nav.forward` | button | enabled when forward history exists; `nav.forward` |
| `fm.nav.up` | button | disabled only at fixture root; `nav.up` |
| `fm.path.breadcrumb` | segmented breadcrumb | ordered segment model, current segment, overflow |
| `fm.path.segment.<path-id>` | virtual segment child | activate ancestor location |
| `fm.path.open_tail` | terminal/editor actuator region | enters direct edit when activated |
| `fm.path.terminal` | trailing `./` button | opens/closes path matrix; expanded state |
| `fm.search.editor` | single-line editor | query, scope, clear, validation, composition |
| `fm.search.scope` | noneditable factual suffix | `this subtree`; collapses by responsive priority |

The open breadcrumb tail and `./` terminal are different objects. The tail
edits; `./` opens the full matrix.

## 5. Path matrix controls

| Stable ID | Role | Required behavior |
|---|---|---|
| `fm.path.matrix` | owned anchored overlay | click-away/Escape, focus scope, semantic parent, edge avoidance |
| `fm.path.matrix.heading` | label group | title plus factual description |
| `fm.path.matrix.current` | current-stack group | full drive-rooted stack |
| `fm.path.matrix.current.segment.<id>` | path segment | navigate immediately |
| `fm.path.matrix.current.tail` | edit actuator | converts current stack to editor |
| `fm.path.matrix.editor` | path text editor | selection, caret, IME, autocomplete, validation |
| `fm.path.matrix.completions` | virtual suggestion list | generation, selected item, keyboard navigation |
| `fm.path.matrix.completion.<id>` | option | accept on click/Enter/Tab according to contract |
| `fm.path.matrix.resolution` | status | exact resolved fixture path or fault |
| `fm.path.matrix.recents` | virtual list | five full stacks |
| `fm.path.matrix.recent.<id>` | option/group | contains semantic segment children; activates complete path |

## 6. Folder tree

| Stable ID | Role | State/action |
|---|---|---|
| `fm.tree.pane` | collapsible pane | extent, expanded/collapsed, scroll |
| `fm.tree.caption` | heading | `Folders`; mode `Home-rooted` |
| `fm.tree.view` | virtual TreeView | expanded IDs, selection, focus, lazy children |
| `fm.tree.node.<fixture-id>` | virtual tree item | name, icon, level, set position, expansion, selection, drop state |
| `fm.tree.splitter` | split seam | resize, proximity, collapse/restore, keyboard operation |

Required visible default nodes come from `FIXTURE_CATALOGUE.md`. Tree selection
navigates the content field. Merely focusing a tree row does not navigate.

## 7. Folder object field

| Stable ID | Role | State/action |
|---|---|---|
| `fm.folder.surface` | surface host | participates only in Folder mode |
| `fm.folder.objects` | virtual collection | icon/details projections, sort, selection, focus, scroll anchor |
| `fm.object.<fixture-id>` | virtual item | activate, select, context, label edit, drag source/target where eligible |
| `fm.object.<fixture-id>.icon` | image | presentational child of item |
| `fm.object.<fixture-id>.name` | item name/text | cluster-safe hit/selection, rename editor anchor |
| `fm.object.<fixture-id>.size_badge` | coarse factual decoration | separate accessible description, not focusable command |
| `fm.object.rename_editor` | in-place editor | stable target identity, commit/cancel/validation |

Selection and keyboard focus are independent. At startup, `Facade Study.png` is
selected while initial keyboard focus follows the host focus policy.

## 8. Search correspondence field

| Stable ID | Role | State/action |
|---|---|---|
| `fm.search.surface` | surface host | participates only in Search mode |
| `fm.search.results` | variable-height virtual collection | stable result IDs, scroll anchor, logical children |
| `fm.result.<id>` | correspondence item | compact/hover/focus/pinned, selected, unavailable |
| `fm.result.<id>.activate` | item default action | fake navigate/open result |
| `fm.result.<id>.percentage` | factual value | match evidence, not recommendation |
| `fm.result.<id>.metadata` | text | factual path and why matched |
| `fm.result.<id>.excerpt` | text range | inline excerpt with match attributes |
| `fm.result.<id>.plugins` | labelled information group | plugin indications and source facts |

There is no `fm.selection.pane` participation in Search mode. Result rows own
their inline evidence.

## 9. Criteria virtual folder

| Stable ID | Role | State/action |
|---|---|---|
| `fm.criteria.surface` | surface host | participates only in Criteria mode |
| `fm.criteria.console` | predicate-rack container | live/staged summary |
| `fm.criteria.module.<id>` | composite criterion | enable, fields, cost, applied state, remove |
| `fm.criteria.module.<id>.enable` | checkbox | toggle criterion |
| `fm.criteria.module.<id>.property` | choice field | property/producer selection |
| `fm.criteria.module.<id>.operator` | choice field | typed operator selection |
| `fm.criteria.module.<id>.value` | editor/choice | criterion value |
| `fm.criteria.module.<id>.remove` | button | remove module, reversible in session |
| `fm.criteria.add` | button | add deterministic fixture module |
| `fm.criteria.apply` | default action | apply staged expensive changes |
| `fm.criteria.objects` | virtual collection | same object-model projection as folder field |

Default modules:

- `criteria-kind-images` — live, inexpensive, applied;
- `criteria-modified-2026` — live, inexpensive, applied;
- `criteria-content-facade` — staged, expensive, not applied.

## 10. Selection and properties pane

| Stable ID | Role | State/action |
|---|---|---|
| `fm.selection.splitter` | split seam | resize/collapse/restore |
| `fm.selection.pane` | collapsible pane | absent in Search; present in Folder/Criteria |
| `fm.selection.caption` | heading | `Selection`; optional `properties` mode indicator |
| `fm.selection.object_name` | heading/text | selected object name |
| `fm.selection.preview_disclosure` | button | expand/collapse preview art |
| `fm.selection.preview` | trusted fixture preview | copy/drag source, semantic image description |
| `fm.selection.properties` | property list | one scroll plane, groups and rows |
| `fm.property.group.identity` | disclosure group | kind, location, size |
| `fm.property.group.image` | disclosure group | dimensions, profile, created |
| `fm.property.group.editable` | disclosure group | name, opens with |
| `fm.property.name.editor` | text editor | fake rename with validation |
| `fm.property.handler.choice` | choice/dropdown | fake handler selection |

Do not create a second nested scroller for properties. Preview and property
content participate in the pane's one scroll model.

## 11. Status line

| Stable ID | Cell | Required content |
|---|---|---|
| `fm.status.summary` | left/fluid | selection, count, operation, or current-surface summary |
| `fm.status.authority` | middle/auto | local navigation, index generation, offline/stale authority |
| `fm.status.view` | right/auto | icon/details toggle |

Default Folder:

```text
1 selected · 2.8 MB · 14 objects
Local navigation ready · index 86 current
```

Default Search:

```text
5 of 7 results shown · 1 unavailable source
Local navigation ready · result generation 86
```

Default Criteria:

```text
31 objects · 2 live criteria · 1 expensive criterion staged
Local navigation ready · index 86 current
```

This is where counts/index/local authority live. Do not recreate the deleted
content summary bar above the object field.

## 12. Review controller IDs

| Stable ID | Role |
|---|---|
| `demo.controller.window` | owned review tool window |
| `demo.surface.choice` | seven-way surface selector |
| `demo.atmosphere.choice` | twelve-way atmosphere selector |
| `demo.construction.choice` | twelve-way construction selector |
| `demo.fixture.condition` | deterministic condition choice |
| `demo.accommodation.text-scale` | text-scale selector |
| `demo.accommodation.contrast` | high-contrast toggle |
| `demo.accommodation.motion` | reduced-motion toggle |
| `demo.accommodation.sound` | sound enable toggle |
| `demo.participation.state` | window-state test choice |
| `demo.clock` | fake-clock controls |
| `demo.diagnostics` | counters/capability report |
| `demo.reset` | reset command |

## 13. Review-board IDs

Use stable identities:

- `demo.palette.board`, `demo.palette.<theme-id>`;
- `demo.icons.board`, `demo.icon_candidate.<candidate-id>`;
- `demo.styles.board`, `demo.style.<construction-id>`;
- `demo.dna.board`, `demo.dna.index`, `demo.dna.decision.<dna-id>`,
  `demo.dna.verdict.<verdict>`.

Review selection changes product presentation through one theme/material
transaction. It does not replace product state or command objects.

## 14. Focus scopes and order

Default product task order:

1. ribbon tabs;
2. active ribbon commands;
3. Back, Forward, Up;
4. breadcrumb segments, open tail, `./` terminal;
5. search editor;
6. tree pane if participating;
7. active content surface;
8. selection pane if participating;
9. status view toggle.

Overlay focus scope temporarily owns traversal and restores the invoking control
on close. Collapsed panes leave task order. Hidden inactive surfaces leave task
order. Visual draw order never determines accessibility/task order by accident.

Within object collections, arrows perform spatial/row navigation; Tab leaves the
collection rather than traversing every item. Within the criteria rack, Tab
traverses fields/actions in semantic order and arrows operate choices where
appropriate.

## 15. Semantic roles

At minimum:

- product window: window/application root;
- ribbon: toolbar with grouped commands and tabs;
- breadcrumb: grouped navigable path components;
- `./`: button with expanded state and overlay relation;
- path matrix: overlay/group with current path, editable text, suggestions, and
  recent location options;
- folder tree: outline/tree with virtual children;
- object field: list/grid/table according to view;
- result ledger: list of expandable correspondence items;
- percentage: value/text evidence, never a command;
- criteria rack: grouped editable predicate controls;
- selection pane: complementary information region with property name/value
  relations;
- preview: image/text object with copy/drag actions only when offered;
- status: quiet live-status regions with deduplication.

Size badges, evidence rails, textures, shadows, separators, and decorative
glows are presentational and do not become focusable semantic children.
