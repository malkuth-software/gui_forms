# Architecture decisions

Significant decisions are recorded as numbered architecture decision records.
Each record states the context, the decision, its consequences and the
options that lost. The records are kept in the
[`decisions/`](https://github.com/malkuth-software/notes/tree/gui_forms/decisions) directory of the project's notes
repository, on its `gui_forms` branch, and are edited there; the table links
to each one.

| ADR | Title | Status | Date |
|---|---|---|---|
| [ADR-001](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-001-portable-compatibility-controls.md) | Port handle-dependent compatibility controls to portable behaviour; no general Win32 emulation | accepted (recorded retroactively) | 2026-10-11 |
| [ADR-002](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-002-captured-facade-before-unchanged-binary.md) | Implement the captured interface-level facade first; an unchanged-binary load is a late experiment, not a gate | accepted (recorded retroactively) | 2026-10-11 |
| [ADR-003](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-003-observable-ordering.md) | Use one deterministic event order and preserve DML assignment order | accepted | 2026-08-04 |
| [ADR-004](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-004-one-text-rasterization-engine.md) | One text rasterization engine with identical output on every platform | accepted; implemented | 2026-10-11 |
| [ADR-006](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-006-independent-predecessors-and-negotiated-consumption.md) | GUI.Forms and Orchestrator are independent predecessors; consumption is negotiated through a published manifest | accepted (recorded retroactively) | 2026-10-11 |
| [ADR-013](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-013-panel-composition-reserved-to-file-manager.md) | Persistent side-panel composition is reserved to File Manager; demonstrations may compose panels | retired | 2026-10-13 |
| [ADR-014](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-014-lifecycle-contract.md) | One portable lifecycle contract with separate retained, session, native and lease identities | accepted (recorded retroactively) | 2026-10-11 |
| [ADR-015](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-015-text-prepared-and-owned-by-content.md) | Text is prepared ahead of painting and owned by its content | accepted | 2026-10-31 |
| [ADR-016](https://github.com/malkuth-software/notes/blob/gui_forms/decisions/ADR-016-retire-active-surfaces.md) | Animation runs on the motion clock and settling timers; active surfaces are retired | accepted; implemented | 2026-10-31 |

Numbers 005 and 007 through 012 are not assigned to GUI.Forms; the
numbering follows the wider programme's sequence, and records that belong to
other projects are kept there.

Smaller owner decisions, which do not take the form of a record, are indexed
in [`planning/DECISIONS_2026-11.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/DECISIONS_2026-11.md)
and in the stage briefs under `planning/stages/`.
