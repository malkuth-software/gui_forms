# File Manager Demoboard worker instructions

This directory is a standalone first-party consumer project inside GUI.Forms.
Its job is to reproduce and fully exercise the accepted File Manager prototype
as a native retained GUI.Forms application backed only by deterministic fixture
models.

Read, in order, before changing code or specifications:

1. `README.md`
2. `PROJECT_SPECIFICATION.md`
3. `VISUAL_CONSTRUCTION_SPECIFICATION.md`
4. `CONTROL_AND_STATE_INVENTORY.md`
5. `INTERACTION_CONTRACT.md`
6. `FIXTURE_CATALOGUE.md`
7. `IMPLEMENTATION_SEQUENCE.md`
8. `STATUS.md`
9. `ACCEPTANCE_GATES.md`
10. `reference/README.md`
11. parent `../AGENTS.md`

## Code style

Code here follows the house style (`HOUSE_STYLE.md` in `malkuth-software/notes`), as everywhere in
GUI.Forms; see "Code style (mandatory)" in the parent `../AGENTS.md`. The
`house-style` CI job checks this directory.

## Mission

Build a native, interactive File Manager interface specimen that looks and
behaves like the reference prototype. It is evidence about interface
composition, state, visual grammar, input, retained rendering, and framework
consumption. It is not the File Manager frontend and it is not a new GUI.Forms
capability tranche.

## Hard boundaries

- Use only public GUI.Forms headers and installed/resource-pack seams. Never
  include a private renderer, host, Skia, AppKit, Win32, GTK, or browser type.
- Do not use HTML, CSS, JavaScript, a browser engine, Dear ImGui, or an
  immediate-mode control tree at runtime. The copied HTML is reference evidence
  only.
- Do not read the real filesystem, run Engine, call Orchestrator, enumerate
  host applications, load plugins, or mutate user data. Every object, path,
  result, criterion, operation, and failure is an in-memory fixture.
- Do not add reusable controls or host capabilities here. When the demoboard
  needs a missing GUI.Forms facility, record the need by consumer-profile ID,
  expose an honest unavailable state, and stop that slice. Implement the
  facility in its owning GUI.Forms milestone only under separate authority.
- A demoboard-only composite may arrange public controls and translate fixture
  state. It may not become a private TreeView, ribbon, overlay manager, text
  editor, drag/drop stack, accessibility graph, or renderer.
- Preserve the retained imperative model: create stable controls once, attach
  stable fixture identities, mutate properties synchronously on the UI thread,
  and invalidate only declared effects.
- PNG is the only runtime image format. SVG in the reference HTML is not an
  admitted runtime asset.
- Use Portsmouth Rapids only for admitted control/title roles. Body typography
  uses the selected bundled product body pack when available; never silently
  substitute an arbitrary host font.
- Native accessibility publication is required for completion. Authored labels
  enrich stock semantics; they do not switch accessibility on.
- Minimum user-resizable window size is 150 by 150 logical pixels. Responsive
  collapse is deterministic and priority-authored.
- Default presentation is Sapphire Dusk plus House Composite. The other eleven
  atmospheres and eleven alternate construction families are review modes, not
  product defaults.
- The review controller is demoboard instrumentation. It is never painted into
  the product window and is excluded from product screenshot goldens.

## Epistemic discipline

Use repository labels exactly: GIVEN, OBSERVED, MEASURED, HYPOTHESIS,
CANDIDATE, REJECTED, and DECIDED. A visually close screenshot is OBSERVED
evidence, not proof of keyboard, accessibility, damage, or platform behavior.

Reference precedence is:

1. owner corrections recorded in this package and parent Design DNA verdicts;
2. the current copied concept-atlas HTML;
3. the four historical screenshots;
4. personal taste or platform defaults.

When a screenshot conflicts with a later correction, implement the correction
and record the visual delta. Important known deltas are listed in
`reference/README.md`.

## Required worker behavior

- Begin each slice by naming the control/state IDs it covers.
- Keep fixture state separate from view state and GUI.Forms retained state.
- Add headless interaction traces before relying on a native screenshot.
- Test pointer, keyboard, focus, large text, reduced motion, high contrast, and
  sound-off equivalents for every completed surface.
- Capture reference-size and constrained-size native screenshots after a slice
  becomes stable.
- Never label a control complete merely because it paints.
- Preserve unrelated work and do not freeze a public ABI from demoboard code.

## Stop and report

Stop the current slice rather than improvising when:

- public GUI.Forms cannot express the required behavior without a new reusable
  primitive or private-header access;
- the body font or an icon cannot be used under a recorded redistribution
  license;
- a requested interaction would touch the real filesystem or another real
  application without an admitted fixture/host test seam;
- the visual reference conflicts with a later GIVEN verdict not reconciled in
  `reference/README.md`;
- accessibility, focus, or input order can only be approximated by paint;
- the clean parent GUI.Forms tests fail before the demoboard change;
- or exact renderer/host behavior would require reopening an accepted ADR.

## Completion report

Report:

- surfaces and state IDs completed;
- public GUI.Forms controls/capabilities consumed;
- unavailable capability IDs and their owning milestone;
- headless traces and native captures produced;
- accessibility and accommodation results;
- measured damage/layout/resource counters;
- known visual deltas from the reference;
- and what the evidence does not establish.
