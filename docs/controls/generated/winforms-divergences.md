# Windows Forms divergences

Generated from `manifests/winforms-divergences.json` by
`python3 tools/winforms_divergences.py render`; do not edit. GUI.Forms matches the
documented observable behaviour of Windows Forms, not its internals or timings, and
records every difference here with its reason and the decision behind it.

## Application

### dark mode

| | |
|---|---|
| Windows Forms | Dark mode follows Application.SetColorMode (.NET 9 and later): SystemColors are replaced by a dark system palette. |
| GUI.Forms | Dark mode is a theme variant. A theme declares color-scheme: light dark. ApplicationResources::set_automatic_colour_scheme, off by default, follows the host's preference. A theme with no dark variant is shown as GUI.Forms' own dark theme, with warning W403. |
| Reason | Themes are the single home of colour; the fallback gives every application a working dark mode without per-control colours (owner 2026-11-04). |
| Decision | owner, 2026-11-04 |

### default colours (SystemColors)

| | |
|---|---|
| Windows Forms | A control's default colours are SystemColors (Control, ControlText, Window, Highlight), read from the system. |
| GUI.Forms | A control's default colours come from the application's theme. The system's accent and highlight colours are only hints, which a theme can read with env(system-accent, ...) and env(system-highlight, ...) or ignore. |
| Reason | Colour belongs to the theme, so that one theme looks the same on every host (T4 theme variants). |
| Decision | owner, 2026-11-04 |

### high contrast (Windows contrast themes)

| | |
|---|---|
| Windows Forms | Under a Windows contrast theme, controls paint with SystemColors, which take the contrast theme's palette. |
| GUI.Forms | Windows' contrast palette is not taken. GUI.Forms switches to its own high-contrast mode, with its own palette, swapped in at paint time. |
| Reason | The owner judged the host contrast palettes unreliable; GUI.Forms' own palette meets its 7:1 contrast gate in every state (T4 D2). |
| Decision | owner, 2026-11-04 |

## CheckBox

### back_color

| | |
|---|---|
| Windows Forms | BackColor paints the control's background; the check box's square stays white. |
| GUI.Forms | The control's own back_color also fills the check box's square. |
| Reason | Stage T4's Control colours work: a control's back colour is the fill of its own surfaces, the square included, so one colour themes the whole control. |
| Decision | coordinator, 2026-11-02 |

## Control

### back_color, fore_color

| | |
|---|---|
| Windows Forms | BackColor and ForeColor apply in every presentation and are ordinary practice. |
| GUI.Forms | They apply under the application's theme, and the first time each is set on a control it writes the warning W404 inline_colour. While a window shows GUI.Forms' own theme as a fallback for a missing dark or light variant, they are suspended. |
| Reason | A colour set on a control cannot adapt to a theme's light and dark variants, and painting a light-mode colour over the dark fallback breaks the look. Colour belongs in theme rules (T4 theme variants, stage A). |
| Decision | owner, 2026-11-04 |

### click, mouse_click (broken sequence)

| | |
|---|---|
| Windows Forms | Click and MouseClick are raised whatever an earlier handler did, unless the control was destroyed. |
| GUI.Forms | When a handler disposes the control, opens a modal window or a modal layer or menu, or the capture is lost before the release, the sequence's remaining derived events (Click, MouseClick, the activation) are dropped. |
| Reason | The owner's input order, rule 5. |
| Decision | owner, 2026-11-12 |

### key_down (handled)

| | |
|---|---|
| Windows Forms | Handled on KeyDown does not stop KeyPress; only SuppressKeyPress does. |
| GUI.Forms | A KeyDown left handled, by the window or the control, cancels the KeyPress and the key's text, as suppress_key_press does; an input method's commit while a composition is open is kept. |
| Reason | The owner's input order, rule 3. |
| Decision | owner, 2026-11-12 |

### mouse_up (handled)

| | |
|---|---|
| Windows Forms | MouseEventArgs has no Handled; nothing a MouseUp handler does cancels Click. |
| GUI.Forms | A MouseUp left handled cancels Click, MouseClick, DoubleClick, MouseDoubleClick and the activation. |
| Reason | The owner's input order, rule 3. |
| Decision | owner, 2026-11-12 |

### mouse_up, click, mouse_click (order)

| | |
|---|---|
| Windows Forms | A release over the pressed control raises Click and MouseClick, then MouseUp last. |
| GUI.Forms | Physical events come first: MouseUp, then Click and MouseClick (or DoubleClick and MouseDoubleClick), on the pressed control only. At MouseUp the button is up and the capture released; MouseCaptureChanged comes last. |
| Reason | The owner's input order (ADR-003 amendment): physical events first in causal order and derived events after the completing physical event, with state already updated, for fewer ordering bugs (rules 1, 2 and 4). |
| Decision | owner, 2026-11-12 |

## DataGridView

### row_validating (focus leaving the grid)

| | |
|---|---|
| Windows Forms | When focus leaves the grid and a RowValidating handler cancels, the grid keeps focus and the current cell stays in the row. |
| GUI.Forms | When focus leaves the grid, the edit commits and the current row validates; a cancel skips row_validated, but focus is not held: it moves where the user sent it. |
| Reason | Focus moves before the grid learns of it (Window::focused_control_changed), and taking it back from the window's focus change would let one control veto another's focus, which no other control may do. |
| Decision | owner, 2026-11-02 |

## SplitContainer

### seam drag (cancel, keyboard)

| | |
|---|---|
| Windows Forms | Escape during a splitter drag cancels it and the distance stays where it was before the drag; losing the capture ends the drag. The focused splitter moves by SplitterIncrement with the arrow keys. |
| GUI.Forms | Escape, losing the pointer capture and the window's deactivation cancel the drag and put back the distance from before it (SplitChangeReason::cancel). Besides the focused arrows, the begin_keyboard_drag rule action on splitter_control runs a keyboard drag (arrows by keyboard_increment, Ctrl ten steps, Shift a tenth; Enter keeps, Escape restores). |
| Reason | The seam drag runs on the drag engine, as the Splitter's does, and the engine's cancels never leave a model changed (before the engine a lost capture kept the dragged distance). The keyboard drag is the engine's accessibility rule. |
| Decision | coordinator, 2026-11-08 |

## Splitter

### drag (live resize, cancel, keyboard)

| | |
|---|---|
| Windows Forms | Dragging draws a split line and resizes the target on release; Escape during the drag cancels and the target keeps its size. The splitter has no keyboard input. |
| GUI.Forms | Dragging resizes the target live (splitter_moving before each step). Escape, losing the pointer capture and the window's deactivation cancel and put the target back at its size before the drag. The begin_keyboard_drag rule action moves the bar from the keyboard (arrows by 10, Ctrl 100, Shift 1; Enter keeps, Escape restores). |
| Reason | The drag runs on the drag engine, whose cancels never leave a model changed; the observable end of Escape is Windows Forms'. Live resizing was already GUI.Forms' behaviour, and the keyboard path is the engine's accessibility rule (every drag has a keyboard equivalent). |
| Decision | coordinator, 2026-11-08 |

## TableLayoutPanelCompat

### column_span / row_span (automatic children)

| | |
|---|---|
| Windows Forms | SetColumnSpan and SetRowSpan apply to any child; a child without an explicit cell is auto-placed into the first free run of cells its span needs. |
| GUI.Forms | A span needs an explicit cell: writing a span other than 1 to an automatically placed child is refused, and automatic placement gives each child one cell. |
| Reason | The table holds the spans as anonymous GridSpan objects anchored at a child's cell (study 10.9 section 5); an automatic child has no anchor. Coordinator for cp20: automatic placement ignores spans. |
| Decision | coordinator, 2026-11-05 |

### column_span / row_span / cell (overlap)

| | |
|---|---|
| Windows Forms | Spans may overlap other spans or explicitly placed children; the layout then moves the later child on (or leaves it out under FixedSize). |
| GUI.Forms | A span that would overlap another span or cover another child's cell, and a cell inside another child's span, are refused with std::invalid_argument when written. |
| Reason | The table's spans follow the GridSpan conflict rules (study 10.9 section 1, rules a and d), so a fused region never holds a second placed child. |
| Decision | owner, 2026-11-05 |

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
