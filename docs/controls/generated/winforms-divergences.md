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
