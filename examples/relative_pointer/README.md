# Orbit toy: relative pointer mode

A minimal test game for relative pointer mode
(`gui_forms/events/relative_pointer/relative_pointer.hpp`). It is not a
PlaySuite game: those keep the pointer visible. It exercises the capability.

- A primary press on the view asks for the pointer **confined** to the view
  (confinement before a hard lock), hidden, with raw deltas. Each motion
  sample turns the camera; the wheel moves it in and out; a press fires.
- The shell's **capsule** (the button at the top left) is the window's
  release region: when the pointer reaches it the mode is suspended, the
  ordinary pointer reappears and clicks it, and moving back over the view
  resumes the mode.
- **Escape** always gives the pointer back, as do focus leaving the view,
  the window's deactivation and any dialog or menu.
- `--visible` asks for the visible-cursor variant: free mode, raw deltas,
  every sample, the pointer shown and unlocked.

The same scene runs headless in `tests/relative_pointer_example_tests.cpp`
(scripted motion, the capsule's suspension and click, Escape).

```sh
cmake -S examples/relative_pointer -B build/orbit-toy -DCMAKE_PREFIX_PATH="$PWD/build/install" \
  -DGUI_FORMS_EXAMPLE_NATIVE=ON
cmake --build build/orbit-toy
build/orbit-toy/orbit_toy
```
