# Touch demonstrators

The touch stage (notes: `planning/stages/TC-touch/README.md`) names two
demonstrators: the start screen, built here, and a small industrial panel
(section 6), outlined below. Both are built now with keyboard and mouse
working completely. The touch behaviour is added once the hosts report touch.
Every place it plugs in is marked `TODO(touch)` with a note of the intended
behaviour, so adding touch fills in those bodies without changing the design.

## 1. Touch start (`gui_forms_touch_start`, built)

The owner's addition of 2026-11-21: a start screen in the style of Windows 8,
as an original design. Its purpose is telling a flick (a pan) from a tap (a
selection), with no accidental launches.

- **Files:** `demo/touch_start/touch_start.{hpp,cpp}` (the tiles, the layout
  and the field), `touch_input.{hpp,cpp}` (the seams),
  `touch_start_program.hpp` (`--check`, `--headless --capture`), and
  `src/host/{linux,windows}/touch_start_main.cpp`,
  `src/host/macos/touch_start_main.mm`. The tests are in
  `gui_forms_tests/tests/touch_start_tests.cpp`.
- **Working now:** four groups of tiles (small, medium, wide and large) on a
  surface that pans sideways.
  - **Live tiles** turn their faces one at a time, every 2.5 s, with a slide.
  - **Pointer:** the wheel pans (a vertical wheel too). A mouse drag pans
    directly. A click selects a tile, and a click on the selected tile
    launches it.
  - **Keyboard:** the arrows move to the nearest tile in that direction and
    glide it into view. Home and End go to the first and last tiles, Page Up
    and Page Down pan, Enter or Space launches, and Escape returns.
  - **Launch:** the tile zooms to fill the window, and the page's type fades
    in. A right click also returns.
  - **Reduced motion** makes every motion instant.
  - **Accessibility:** each tile is a semantic list item with select and press
    actions.
  - **Staging:** `--check <tile>` selects a tile and pans it into view.
- **Seams** (`touch_input.hpp`):
  - **`GestureClassifier`** takes every press, move and release. It has a
    tap, a pan, a flick (with velocity), a press-and-hold and none. The mouse
    already gets the tap and the direct pan. `TODO(touch)` covers the velocity
    fit (`estimated_velocity`) and the hold on release. The touch slop is
    taken for `PointerKind::touch`.
  - **`PanInertia`** carries a flick on (`TODO(touch)`: exponential decay,
    time constant 325 ms). The field already calls it every frame.
  - **`settle_pan_offset` and `spring_back_target`:** the pan clamps hard
    now (`TODO(touch)`: a rubber band past the ends, then a spring back on
    lift).
  - **The field's handlers** mark where the pointer kind comes from the event
    and where a press-and-hold shows the tile's tool tip.
  - **The phase names** follow the gesture family's `GesturePhase`
    (`claude/magnify-input`). The demonstrator does not depend on that
    branch.

## 2. Touch panel (`gui_forms_touch_panel`, outlined)

TC-touch section 6 describes it as "a small industrial demonstration panel
(indicator lamps, hold-to-confirm buttons, steppers, a trend plot) built
from stock controls", to run on the Pi 4's capacitive and resistive panels.
It tests the press rule and the industrial extras, as the start screen tests
the pan.

- **Layout:** one window, 800 x 480 (the panels' size), in the industrial
  theme's touch variant once that exists.
  - **Top:** a row of `IndicatorLamp`s (pump, valve, heater, fault).
  - **Left:** two large steppers (`NumericUpDown`, set point and flow) with
    `SevenSegmentDisplay` readouts.
  - **Right:** a `PlotView` trend of the process value against the set
    point, fed by a simulated process.
  - **Bottom:** hold-to-confirm START and STOP keys, and a guarded PURGE
    (lift the cover, then press).
- **Files:** `demo/touch_panel/touch_panel.{hpp,cpp}`,
  `touch_panel_program.hpp`, and the three host mains. A model library
  `gui_forms_touch_panel_model` holds them, with
  `tests/touch_panel_tests.cpp`, all as for the start screen.
  `touch_input.hpp` moves to `demo/touch/` and both demonstrators share it.
- **Simulated process:** a first-order lag towards the set point, ticked on
  an `AnimationTimer` at 10 Hz. It is deterministic from a seed, so
  `--headless --capture` shows the same trend each time. `--check <lamp>`
  stages a lamp lit.
- **Working with keyboard and mouse first:** the steppers take the arrows
  and the wheel. A hold-to-confirm key confirms after the mouse or Space is
  held for its time (1.5 s), with a fill showing the progress. Letting go
  early cancels it, and the key has a semantic press action. Under reduced
  motion the fill is a stepped bar.
- **Seams to add** (to `touch_input.hpp`, marked `TODO(touch)`):
  - **`PressTracker`:** the press rule. A press commits on lift inside the
    touch area. The area that keeps a press alive is larger than the one
    that starts it (hysteresis), and a deliberate drag-off cancels. With the
    mouse the two areas are the same.
  - **`HoldTimer`:** hold-to-confirm and dwell-to-activate. The key starts
    it on the press and stops it on the lift or on a drag-off.
    `GestureClassifier`'s press-and-hold uses it too.
  - **Touch area:** each key's hit area extends past its drawn bounds to at
    least 9 mm, resolved against its neighbours. The demonstrator computes
    it with a `hit_test_local` override until the anatomies gain the touch
    inset (TC-touch section 4).
  - **Suppression hooks** (TC-touch section 3.3): palm and water rejection
    and resistive jitter. These are a pass-through for now, with a note on
    where the core's suppression slots in.
- **Tests:**
  - **Hold-to-confirm:** it confirms only after its time, an early release
    or a drag-off cancels it, and Space does the same as the pointer.
  - **Steppers:** they clamp at their limits.
  - **Trend:** it is deterministic from the seed.
  - **Staging:** `--check` lights the lamp it names.
  - **With touch:** the recorded panel traces (taps, wandering presses,
    drag-offs) replay against the press rule.
