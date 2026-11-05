# Native File Manager Demoboard

Status: **GIVEN first-party interface-consumer project; implementation active;
D0–D6 measured partial**.

The Native File Manager Demoboard is a deterministic GUI.Forms application
that reproduces the latest File Manager prototype as a fully interactive native
surface. It exists so GUI.Forms workers can exercise the complete interface as
the library grows without dogfooding an unfinished framework inside the real
frontend and without inventing application backends.

The demoboard is intentionally ambitious in presentation and intentionally
fake in domain authority:

- every visible folder, file, property, search result, criterion, index state,
  transfer, failure, and recent path comes from versioned in-memory fixtures;
- every interaction is real GUI.Forms input, focus, command, overlay, layout,
  retained-state, accessibility, and drawing behavior;
- no operation touches the real filesystem;
- no Engine or Orchestrator process is started;
- no plugin code executes;
- no browser is bundled or used at runtime.

The target is not a nonfunctional screenshot. The target is an interface that
can be navigated, resized, focused, edited, expanded, collapsed, selected,
dragged, sorted, inspected, themed, and accessibility-tested while remaining a
closed deterministic model.

## Start here

| Document | Authority |
|---|---|
| `AGENTS.md` | worker scope, safety, and stop rules |
| `PROJECT_SPECIFICATION.md` | product boundary, project structure, architecture, and definition of done |
| `VISUAL_CONSTRUCTION_SPECIFICATION.md` | exact geometry, materials, typography, icons, density, responsive behavior |
| `CONTROL_AND_STATE_INVENTORY.md` | stable control IDs, ownership, commands, state, and GUI.Forms dependencies |
| `INTERACTION_CONTRACT.md` | pointer, keyboard, focus, path, search, criteria, preview, drag, motion, and sound behavior |
| `FIXTURE_CATALOGUE.md` | exact deterministic paths, objects, results, properties, failures, and operations |
| `IMPLEMENTATION_SEQUENCE.md` | dependency-ordered worker slices and evidence products |
| `STATUS.md` | live milestone/capability/evidence ledger; implementation starts here after reading the specifications |
| `ACCEPTANCE_GATES.md` | functional, visual, accessibility, accommodation, and performance gates |
| `reference/README.md` | copied evidence, hashes, reference precedence, and known screenshot deltas |

## Deliverable

The finished project supplies two native windows:

1. **File Manager Surface** — the actual product-shaped specimen, with native
   host participation and custom GUI.Forms content.
2. **Demoboard Controller** — a small developer/review tool window used to
   choose surface, fixture condition, atmosphere, construction family,
   accommodation, clock, and diagnostics. It never appears in product goldens.

The product-shaped surface provides three daily modes:

- Folder
- Search
- Criteria virtual folder

The controller additionally exposes four review boards derived from the atlas:

- palette laboratory;
- icon/material laboratory;
- construction-family laboratory;
- Design DNA decision laboratory.

These boards exist to exercise the same GUI.Forms material and controls. They
are not File Manager product navigation destinations.

## Reference composition

At the 1450 by 850 logical-pixel reference client size, the product window is:

```text
┌──────────────── Watercolor identity / native title region ────────────────┐ 40
├ File | Home | Edit | View | Go | Commands | Help ─────────────────────────┤ 23
├ Office Pearl command shelf / ribbon groups ────────────────────────────────┤ 66
├ Back | Forward | Up | breadcrumb........................| ./ | Search ──────┤ 40
├───────────────┬─┬──────────────────────────────┬─┬────────────────────────┤
│ folder tree   │ │ white object / result field  │ │ selection information  │
│ 218 logical px│3│ fluid                        │3│ 288 logical px          │ flexible
├───────────────┴─┴──────────────────────────────┴─┴────────────────────────┤
│ local status and counts          │ authority/index │ view mode             │ 24
└─────────────────────────────────────────────────────────────────────────────┘
```

Search removes the right selection pane and folds its information into
expandable correspondence rows. Criteria retains the right selection pane
because it is a virtual-folder representation.

## Non-goals

- shipping frontend;
- real filesystem semantics;
- real indexing or search relevance;
- real plugin execution;
- system file-operation dialogs;
- implementing missing GUI.Forms controls inside this directory;
- browser-perfect raster equality;
- all twelve construction families at production quality before the default
  House Composite passes;
- or an ABI decision.

## Intended eventual build contract

Once implementation begins, the parent build should expose an opt-in target
equivalent to:

```text
cmake -S gui_forms -B gui_forms/build -DGUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD=ON
cmake --build gui_forms/build --target gui_forms_file_manager_demoboard
ctest --test-dir gui_forms/build -R file_manager_demoboard --output-on-failure
gui_forms/build/.../gui_forms_file_manager_demoboard
```

For deterministic native review, the executable also accepts:

```text
--product-only
--capture-state=folder
--capture-state=path-matrix-browse
--capture-state=path-matrix-editing
--capture-state=search-pinned
--capture-state=search-offline-expanded
--capture-state=criteria-default
--capture-state=criteria-staged-progress
```

`--product-only` omits the separate controller window so native accessibility
and Computer Use inspect the product at full resolution instead of returning a
gathered multi-window overview. Capture states enter through the same public
semantic actions as assistive technology; they do not mutate private product
state.

The exact target spelling may follow parent conventions. The application must
also be consumable against an installed GUI.Forms package so build-tree private
headers cannot become accidental dependencies.
