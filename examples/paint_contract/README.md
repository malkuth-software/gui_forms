# Installed Paint contract consumer

This small program is built as a separate CMake project against an installed
GUI.Forms package. It is a semantic integration probe, not a painting program
or a Rainstar Paint port. It uses no source/private headers and has no
Rainstar Paint dependency.

It checks application-owned straight RGBA pixels (including hidden transparent
color), display premultiplication, a one-pixel edit's viewport damage, pointer
capture and release outside the canvas, zoom/pan coordinates, application-owned
undo, and controller subscription disposal. The native option additionally
creates and automatically closes a native window with `application.run` (an `Application` made by `Application::create`)
through the installed shared Application target, bundling the installed fonts
and notices. It runs on the selected macOS or Windows host.

```sh
cmake -S examples/paint_contract -B build/paint-contract \
  -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/paint-contract
ctest --test-dir build/paint-contract --output-on-failure
```

For a separate installation built with a native host:

```sh
cmake -S examples/paint_contract -B build/paint-contract-native \
  -DCMAKE_PREFIX_PATH=/absolute/native/install \
  -DGUI_FORMS_EXAMPLE_NATIVE=ON
cmake --build build/paint-contract-native
ctest --test-dir build/paint-contract-native --output-on-failure
```

Native execution needs an active graphical session (or a configured Windows
runner for cross-build tests). It is intentionally
automatic and short-lived. Visual review is performed separately with the
GUI.Forms Gallery, not inferred from this probe.
