# Effect verification

Effect programs (`gui_forms/engine/effect/effect.hpp`) are bounded by
construction. The second pass of the GUI.Forms Clang plugin, the one that
also records [localizable literals](localizable-literals.md), checks this
during the normal compile of every file that registers an effect (each
`make_effect_entry<E>()`):

- from `E::run` and `E::setup` it walks every function they reach, and
  rejects `while`, `do` and `goto`; any `for` other than a range-for over
  `bounded_range<N>(count)`, an `std::array` or an `EffectArray`, or a counted
  `for` with constant bounds whose variable the body does not write;
  recursion; calls through pointers, virtual calls and `std::function`; calls
  into functions whose bodies it cannot see; allocation; writes to globals
  and reads of globals that are not `constexpr`; I/O, clocks, threads,
  atomics, inline assembly, exceptions, `dynamic_cast`, `typeid`, `<cmath>`
  and library randomness. A call goes either to effect vocabulary (marked
  `GUI_FORMS_EFFECT_VOCABULARY(fixed, per_pixel)`, such as the canvas's
  drawing calls and the `effect_*` arithmetic) or to a function that obeys
  the same rules;
- it checks the effect's state: trivially copyable, with no pointer or
  reference members, and no larger than its declared `state_budget`;
- it bounds the operations of one `run` (each operator 1, each call to a
  checked function 1 plus its body, each vocabulary call its declared fixed
  cost, loops multiplied by their bounds, the larger branch of each
  condition) and fails the compile when the bound passes the declared
  `cost.fixed`, listing each loop's share; `setup` is bounded the same way
  against `setup_cost`;
- for each effect that passes, it writes a verification record (its name,
  the two bounds and its state's size) into the object file. The effect
  registry looks the record up when the effect is registered: an effect is
  `verified` only when the pass really ran on it.

Every rejection is a compiler error at the construct, followed by a note
naming the effect.

On CI, the `effect-pass` job (`.github/workflows/effect-pass.yml`) builds
the plugin on Linux with Clang 22's development headers and runs the effect
tests with the pass on, for pull requests that touch the effect engine, the
plugin, the effect tests or a file that registers effects. The native jobs
build without those headers, so their effects are unverified.

## Turning it off

The `GUI_FORMS_EFFECT_VERIFY` option is on by default, and turning it off is
not recommended. With it off, or with a toolchain that cannot load the plugin
(see [Turning it on and off](localizable-literals.md#turning-it-on-and-off)),
effects still build and run, unverified:

- configure prints a warning naming each target whose effects are not
  checked;
- each file that registers effects prints a compiler `#warning` naming them
  (CMake defines `GUI_FORMS_EFFECTS_UNVERIFIED` for it);
- the registry labels them unverified (`EffectEntry::verified` is false) and
  counts them (`EffectRegistry::unverified_count()`).

A file of your own that registers effects is listed with
`gui_forms_verify_effects(<target> SOURCES <files>)` in the toolkit's build.
The plugin's `effects=off` argument
(`-fplugin-arg-gui_forms_localizable-effects=off`) skips the pass for a unit
compiled with the plugin for its literals.
