# Running the C ABI conformance kit

The conformance kit checks that the GUI.Forms C ABI (`gui_forms/c/gf_abi.h`)
behaves as its header promises in your process, built with your compiler and
your flags. It is a set of C11 checks of ownership, threads, callbacks,
events, values, the control schema, themes, host services, the control
families and the window run. A product that consumes the C ABI runs it in
its own test suite; GUI.Forms runs the same kit in its own tests.

## What is installed

The SDK installs the kit with the C ABI:

| | |
| --- | --- |
| CMake component | `CAbiConformance` (target `GUIForms::CAbiConformance`) |
| header | `include/gui_forms/c/gf_conformance.h` |
| library | `lib/libgui_forms_abi_conformance_kit.a` (static, C11) |

The target links `GUIForms::CAbi` and the platform's threads library, so
linking it is all a program needs.

## Running it

The kit has one entry point:

```c
uint32_t gf_conformance_run(FILE* out, gf_conformance_flags flags);
```

It runs every check on the calling thread and returns the number of failed
checks. Each failure is one line on `out`, followed by a summary; pass
`NULL` for no output.

Two rules apply to the call:

- **Run it first.** The calling thread becomes the library's UI thread, and
  no GUI.Forms object may exist when the kit starts. Give it a process of its
  own, which is what a test program is, and do not create GUI.Forms objects
  before it in that process.
- **Pick the window flag.** `GF_CONFORMANCE_FLAGS_WINDOW` adds the window
  checks, which show a window briefly and need a display. They include a
  threaded sub-surface (`GF_TABLE_SUBSURFACES`, since 1.1): its build and
  posted callbacks on its own thread, objects owned by the thread that made
  them, and stale identities once it stops. They also make binding sources
  over `windows.run`'s root and a control of its tree from the run's ready
  callback (since 1.1).
  The rules table (`GF_TABLE_RULES`, since 1.1) is checked on a created
  window in both modes: an interaction trigger with set, toggle and callback
  actions, a negated condition, consume, duplicates, refusals, stale rules
  and freed tokens. The data grid table (`GF_TABLE_DATA_GRID`, since 1.1) is
  checked on a created window's root in both modes: columns, rows in bulk,
  cells by row ID, the bulk read into caller storage, the current cell, the
  selection, an edit and the change events.
  `GF_CONFORMANCE_FLAGS_NONE` leaves them out, for headless CI.

A complete test program, as the SDK's own installed example builds it
(`examples/reference/c_abi_conformance.c`):

```c
#include <gui_forms/c/gf_conformance.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    gf_conformance_flags flags = GF_CONFORMANCE_FLAGS_WINDOW;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--no-window") == 0) {
            flags = GF_CONFORMANCE_FLAGS_NONE;
        }
    }
    return gf_conformance_run(stdout, flags) == 0u ? 0 : 1;
}
```

```cmake
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS CAbiConformance)
add_executable(c_abi_conformance c_abi_conformance.c)
set_target_properties(c_abi_conformance PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(c_abi_conformance PRIVATE GUIForms::CAbiConformance)
enable_testing()
add_test(NAME c_abi_conformance COMMAND c_abi_conformance --no-window)
set_tests_properties(c_abi_conformance PROPERTIES TIMEOUT 120)
```

Point `CMAKE_PREFIX_PATH` at the SDK, as for any other component
({doc}`consume-the-sdk`). At run time the process must find the C ABI
library (`gui_forms_abi1.dll` beside the program or on `PATH` on Windows,
`libgui_forms_abi.so.1` on Linux, `libgui_forms_abi.1.dylib` on macOS).

## Reading a result

A passing run ends with its summary, for example
`gf_conformance: 695 checks, 0 failed`, and the program exits 0. Each
failed check is a line naming the check and, for a call that returned the
wrong status, the status it got. Every failure is a defect, either in
GUI.Forms or in how the product builds against it, and is never a flake to
rerun. Report it with the line, the SDK version (`runtime.version` in
`gf_abi.h`, or the SDK's package version) and the compiler.

## Under the sanitizers

Run the kit in your sanitized builds as well. Under AddressSanitizer and
UndefinedBehaviorSanitizer, link the sanitized SDK. A sanitized program
loading an unsanitized C ABI library checks only your side. Under
ThreadSanitizer (Linux and macOS) the kit's thread checks are the useful
part. GUI.Forms runs its own TSan pass with `tools/abi_tsan.py`
({doc}`../building/index`, "ThreadSanitizer").

## What the kit does not cover

- Visual Studio (MSVC) programs: the kit is a MinGW/Clang static library and
  uses pthreads. MSVC consumers are covered by GUI.Forms' own
  `gui_forms_abi_msvc_consumer` test (the layout probe, the reference
  consumer and the C++ wrappers compiled with `cl`).
- The C++ wrappers (`gui_forms/c/gf_abi.hpp`) and the C# interop have their
  own tests in GUI.Forms; the kit exercises the C header alone.
