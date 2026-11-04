# Windows Forms divergences

Generated from `manifests/winforms-divergences.json` by
`python3 tools/winforms_divergences.py render`; do not edit. GUI.Forms matches the
documented observable behaviour of Windows Forms, not its internals or timings, and
records every difference here with its reason and the decision behind it.

## CheckBox

### back_color

| | |
|---|---|
| Windows Forms | BackColor paints the control's background; the check box's square stays white. |
| GUI.Forms | The control's own back_color also fills the check box's square. |
| Reason | Stage T4's Control colours work: a control's back colour is the fill of its own surfaces, the square included, so one colour themes the whole control. |
| Decision | coordinator, 2026-11-02 |

## DataGridView

### row_validating (focus leaving the grid)

| | |
|---|---|
| Windows Forms | When focus leaves the grid and a RowValidating handler cancels, the grid keeps focus and the current cell stays in the row. |
| GUI.Forms | When focus leaves the grid, the edit commits and the current row validates; a cancel skips row_validated, but focus is not held: it moves where the user sent it. |
| Reason | Focus moves before the grid learns of it (Window::focused_control_changed), and taking it back from the window's focus change would let one control veto another's focus, which no other control may do. |
| Decision | owner, 2026-11-02 |

## ToolStripManager

### merge_strips (MatchOnly)

| | |
|---|---|
| Windows Forms | MatchOnly merges the drop-down items of a matched ToolStripDropDownItem, creating its drop-down as needed. |
| GUI.Forms | match_only recurses only when the matched item already holds a menu (a drop-down or split button, a menu strip item, a submenu). |
| Reason | An item's kind fixes whether it holds a menu; a merge does not change a target item's kind. |
| Decision | coordinator, 2026-11-03 |

### merge_strips (emptied menus)

| | |
|---|---|
| Windows Forms | A drop-down left without items by a merge still shows an empty drop-down. |
| GUI.Forms | A menu a merge empties stays valid and is not shown until it has items again. |
| Reason | An empty menu has nothing to choose; showing it would be a dead end. |
| Decision | coordinator, 2026-11-03 |

### merge_strips (hosted items)

| | |
|---|---|
| Windows Forms | A moved ToolStripControlHost keeps its hosted control and that control's state. |
| GUI.Forms | A moved hosted item (combo box, text box, progress bar) is recreated in the target, so its control's state is not carried. |
| Reason | A hosted control is owned by its strip; moving it between owners is not part of the strip's item system. |
| Decision | coordinator, 2026-11-03 |

### merge_strips (matching by text)

| | |
|---|---|
| Windows Forms | Merge matches a source item to a target item by its Text, compared case-insensitively in the current culture. |
| GUI.Forms | merge_strips compares displayed text with ASCII-only case folding. |
| Reason | Culture-dependent comparison would make a merge depend on the user's locale; the strip ID and merge_index give exact matching where text is not enough. |
| Decision | owner, 2026-11-03 |

### merge_strips (repeated IDs)

| | |
|---|---|
| Windows Forms | Items carry no identity, so any merge result is accepted. |
| GUI.Forms | A merge whose result would repeat an item ID in the target is refused: merge_strips returns false and changes nothing. |
| Reason | Items are addressed by stable IDs (clicks, check state, semantics); a repeated ID would make them ambiguous. |
| Decision | coordinator, 2026-11-03 |

### merge_strips (strip kinds)

| | |
|---|---|
| Windows Forms | Merge accepts any two ToolStrips that allow merging, including a ToolStrip into a MenuStrip. |
| GUI.Forms | Only strips of the same kind merge: the ToolStrip family with the ToolStrip family, MenuStrip with MenuStrip. |
| Reason | A MenuStrip's top level holds menus only (MenuStripItemSpec), a ToolStrip's holds buttons, labels and hosted items; there is no item that is both. |
| Decision | coordinator, 2026-11-03 |
