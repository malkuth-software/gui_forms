# Consume the SDK

An application consumes GUI.Forms through the installed CMake package. There
are no private include paths, no archive filenames to list and no generated
glue: `find_package` resolves the components you ask for and CMake checks
that the combination is one the toolkit supports.

## Minimal consumer project

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyForms LANGUAGES CXX)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Application)

add_executable(my_forms main.cpp)
target_compile_features(my_forms PRIVATE cxx_std_20)
target_link_libraries(my_forms PRIVATE GUIForms::Application)
# The window class each form document generates, and the program's theme.
gui_forms_add_form(my_forms forms/greeter.form CLASS GreeterWindow NAMESPACE greeter
                   MODEL greeter::GreeterModel MODEL_HEADER greeter_model.hpp)
gui_forms_add_theme(my_forms themes/greeter FUNCTION greeter_theme NAMESPACE greeter
                    FORMS forms/greeter.form)
# Keep the selected SDK ahead of any older GUI.Forms installation in the
# compiler's normal include search path.
set_target_properties(my_forms PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
```

Configure it with the prefix you installed to and the same toolchain file the
SDK was built with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/path/to/sdk \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/gui_forms/cmake/llvm22.cmake
cmake --build build
```

`examples/reference/` in the repository is a complete consumer of this shape
and is built against the installed SDK by CI on every platform.

A control type written as a `.control` document (a composite control) is
generated the same way. `gui_forms_add_control` runs the form compiler's
control mode, which checks the document as the run-time loader does and
writes a header whose class derives from `gui_forms::CompositeControl`:

```cmake
gui_forms_add_control(my_forms controls/toggle-pill.control CLASS TogglePill NAMESPACE greeter)
```

Include `toggle_pill_control.hpp` and make one with
`gui_forms::make_control<greeter::TogglePill>(gui_forms::StableId("wifi"))`.

A form document names it once `gui_forms_add_form` lists it, with the same
class and header:

```cmake
gui_forms_add_form(my_forms forms/settings.form CLASS SettingsForm NAMESPACE greeter
                   COMPOSITES "controls/toggle-pill.control,greeter::TogglePill,toggle_pill_control.hpp")
```

### Rebuilding after a document changes

Each generated header is a build dependency of the objects that include it,
so a changed document rebuilds them even when the compiler's dependency
files spell the header another way (for example under `ccache` with
`CCACHE_BASEDIR` and a build tree behind a symlink). By default every source
of the target depends on every header it generates, which is always correct.
In a target with many forms, one edited document then rebuilds all of its
sources. `USED_BY` names the sources that include the header, directly or
through another header, and only they rebuild:

```cmake
gui_forms_add_form(my_forms forms/settings.form CLASS SettingsForm NAMESPACE greeter
                   USED_BY src/settings_page.cpp)
gui_forms_add_control(my_forms controls/toggle-pill.control CLASS TogglePill NAMESPACE greeter
                      USED_BY src/settings_page.cpp)
```

Name every source that includes the header: one left out of `USED_BY` may
not rebuild when the document changes. A control's header is included by
the form headers that name it, so its `USED_BY` lists those forms' sources.

## Components

| Component | Target | Kind | Use it for |
|---|---|---|---|
| `Threading` | `GUIForms::Threading` | static | `Worker`, `CancellationFlag`, the atomic thread pool. Linked by `Core`, `Application` and `Audio`. |
| `Core` | `GUIForms::Core` | static | The retained tree, events, invalidation, scheduling, resources and the portable host protocol, with no renderer and no native window. |
| `Controls` | `GUIForms::Controls` | static | Stock controls over `Core`. Exports `Drawing`. |
| `Drawing` | `GUIForms::Drawing` | static | Renderer-free geometry, colour, paths, raster storage and command recording. |
| `Application` | `GUIForms::Application` | static | The native host for your platform, linked into your executable. Already contains `Core`, `Controls` and `Drawing`. |
| `Audio` | `GUIForms::Audio` | static | PCM clips, Ogg decoding and a device backend. Links `Threading` only. |
| `CAbi` | `GUIForms::CAbi` | shared | The C ABI (`gui_forms/c/gf_abi.h`), for C, C# and other languages: one self-contained library holding its own engine, the drawing ABI and, on Windows, the Win32 compatibility shim. A process uses it or a statically linked C++ engine, never both. |
| `CAbiConformance` | `GUIForms::CAbiConformance` | static | The C ABI conformance kit ([Running the C ABI conformance kit](c-abi-conformance-kit)). |

Two consumer shapes follow from the table:

- **Renderer-free consumers** (tests, model code, headless automation) link
  `GUIForms::Controls` or `GUIForms::Core`. They construct and mutate a
  `Window` model but never open a native window.
- **C++ applications** link `GUIForms::Application` and nothing else from
  the retained set; GUI.Forms is linked statically into the one executable.
- **C and C# programs** link (or load) `GUIForms::CAbi` alone, the one
  self-contained `gui_forms_abi` library, and not `Application`.

[Install](install.md#package-components) lists the optional components
(`TextMasks`, `AudioLoopTransport`, `FreeType`, `HarfBuzz`, `GalleryModel`)
and the build options that provide them.

## The one linking rule

Never link `GUIForms::Core`, `Controls` or `Drawing` into a program that also
links `GUIForms::Application`, directly or through an adapter library.
`Application` already contains one copy of the retained engine; a second copy
means two sets of control registries, two dispatchers and undefined
behaviour. CMake enforces this at generation time and stops with a diagnostic
naming `ERROR_do_not_link_static_Core_with_Application`. That is a deliberate
rejection, not a missing component.

The rule has one practical consequence for component libraries. An audio
adapter that only needs `Worker` and `AudioClip` links `GUIForms::Audio`,
which exposes `Threading` without the core:

```cmake
add_library(audio_adapter STATIC audio_adapter.cpp)
target_link_libraries(audio_adapter PRIVATE GUIForms::Audio)
target_link_libraries(my_forms PRIVATE GUIForms::Application audio_adapter)
```

## Development text targets

`GUIForms::TextMasks` (the logical text-mask service) and
`GUIForms::PreparedText` (the prepared-text service) both use the text
engine, which every build has.

- `TextMasks` is the `TextMasks` package component of a native-host build
  configured with `GUI_FORMS_BUILD_TEXT_MASKS`, such as
  `tools/build_sdk.py --features all` (see
  [Package components](install.md#package-components)). Installing a
  hostless build with text masks stops with an error.
- `PreparedText` stays development-only. It exists only when your project
  adds the GUI.Forms source tree with `add_subdirectory` and turns
  `GUI_FORMS_BUILD_PREPARED_TEXT` on, and installing such a build stops with
  an error.

They follow the linking rule. In a build with a native host, each is an
interface target that links `GUIForms::Application` and nothing else; the
implementation is already inside the Application library, so link it beside
Application:

```cmake
target_link_libraries(my_forms PRIVATE GUIForms::Application GUIForms::TextMasks)
```

Adding `GUIForms::Core` to that program fails at generation time with a
Core-linkage diagnostic. With `PreparedText` enabled, Application also passes
the `GUI_FORMS_PREPARED_TEXT` definition to its consumers, so your code sees
the declarations the library was built with. In a build without a native host
the two targets carry a static Core instead, for hostless programs, and
cannot be combined with an Application library.
`tests/application_text_consumer/` in the repository is a consumer of this
shape.

## Your own HarfBuzz and FreeType

A program may link its own HarfBuzz and FreeType beside
`GUIForms::Application`, for content it lays out and renders itself (see
[Content surfaces](content-surfaces.md)). GUI.Forms contains neither: since
2026-11-16 it reads fonts, shapes and lays out text with STXlib (its font
tables, shaper and text layout, inside the stx package), so no `hb_` or
`FT_` symbol comes from GUI.Forms and nothing of yours can collide with it.

## Runtime files to ship

A program is one module (owner decision, 2026-11-15): it holds exactly one
copy of any routine and ships no GUI.Forms library beside it.

| Consumer | Links | Ships |
|---|---|---|
| C++ | `GUIForms::Application` (or `Core` and `Controls` for a headless program): static archives. The installed `Application` is `lib/libgui_forms_application.a` (the engine, renderer and host, linked whole because its controls and themes register themselves at static initialization) and `lib/libgui_forms_application_deps.a` (Skia and the text stack), with `Threading`, STXlib (its state included) and the stx archives beside them | the executable alone. On macOS add `libc++.1.dylib`, `libc++abi.1.dylib` and `libunwind.1.dylib` from the runtime prefix to `Contents/Frameworks` only while the pinned runtime has no static archives |
| C or C# | `GUIForms::CAbi`: the C ABI library, self-contained, with the drawing ABI (`gui_forms/drawing_c_api.h`) and its raster (installed by the drawing table's `raster_initialize`) in it | `gui_forms_abi1.dll` (which on Windows also exports the Win32 compatibility shim's user32 and gdi32 names), `libgui_forms_abi.so.1` or `libgui_forms_abi.1.dylib`, beside the executable (in `Contents/Frameworks` on macOS), and its `fonts/` licences |

A process uses either the C ABI library or a statically linked C++ engine,
never both: each holds its own STXlib state (allocator, thread identities and
SIMD level) and its own engine. Link your C++ program with `-static` on
Windows, as GUI.Forms' programs are, so it needs no runtime DLL.

## What is exported

A C++ program exports nothing of GUI.Forms: the archives are linked into it.
The public headers still mark their declarations with `GUI_FORMS_API` and
`GUI_FORMS_THREADING_API`; on Windows the marks are empty for the static
archives, and on macOS and Linux they are default visibility, so your own
export policy decides. The C ABI library exports exactly the `gf_*` entry
points of its allowlist (`share/GUIForms/abi/gf_abi_exports.txt`) and
`gd_get_api_v0`; there is no `gdr_*` export
and keeps libc++, STXlib, Skia and the text stack hidden inside it.

## Your macros and GUI.Forms' names

Your program may define any macro before it uses GUI.Forms, whether it is
your own or one from `<windows.h>`, Xlib, Qt or a predefined `linux`, with
one exception. These names are reserved to GUI.Forms; a program that
defines or undefines a macro with one of them, before or after using
GUI.Forms, has undefined results:

- identifiers that begin with `gf_`, `GF_`, `gd_`, `GD_`, `gui_forms`,
  `GUI_FORMS`, `gui_drawing`, `GUI_DRAWING`, `stxlib`, `STXLIB`, `stx_` or
  `STX_`;
- the names the C and C++ standard libraries declare, as the standard already
  rules for any program that includes a standard header. `min`, `max`, `emit`
  and `index` are the exceptions: GUI.Forms survives macros of those names.

Any other macro cannot change what GUI.Forms means. A test
(`tests/hostile_macros/`) defines every other identifier GUI.Forms' headers
use as a macro that fails the compile, then uses GUI.Forms, and must compile.

GUI.Forms' headers do not remove or change your macros. Your own code still
reads them: if `<windows.h>` has made `near` an empty macro, your code cannot
spell `HorizontalAlignment::near` either.

### `import gui_forms;`

With an SDK built with `GUI_FORMS_BUILD_MODULE=ON`, a C++20 program imports
GUI.Forms as a named module. The module's interface unit is compiled on its
own, with your compiler and your build's flags, so no macro of your source
files reaches it, and it exports no macro to you:

```cmake
cmake_minimum_required(VERSION 3.28)
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Application)
add_executable(my_forms main.cpp)
target_link_libraries(my_forms PRIVATE GUIForms::Module)
```

```cpp
#include <memory>   // the standard library from its own headers (or import std;)
import gui_forms;
```

It needs CMake 3.28 or later with Ninja or Visual Studio, and Clang 17+, GCC
14+ or MSVC 17.4+. The `GF_` macros of the C ABI are not exported: include
`gui_forms/c/gf_abi.h` for them.

### Without modules: the shield

A C program, or a C++ program built without modules, includes the headers.
`gui_forms/c/gf_abi.h`, `gui_forms/c/gf_abi.hpp` and
`gui_forms/drawing_c_api.h` protect themselves. Wrap other GUI.Forms
includes in the shield's two headers:

```cpp
#include <windows.h>
#include "gui_forms/gf_shield_begin.h"
#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/gf_shield_end.h"
```

`gf_shield_begin.h` saves and removes your macros for every name GUI.Forms'
headers use, and `gf_shield_end.h` restores them exactly. Nested and repeated
includes are handled: only the outermost begin and end act.

## Checked builds

An SDK built with checked borrowed views (`GUI_FORMS_CHECKED_BORROWS`, the
sanitizer tree) passes that definition to its consumers, so their code compiles
in the mode of the SDK they link; a library of your own that a consumer links
must be compiled the same way. See
[Checked borrowed views](install.md#checked-borrowed-views).

## Fonts

Controls never consult the host font catalogue; they draw only the bundled
faces. The faces are compiled into the GUI.Forms library as one font pack, so
a program needs no font files beside it. Native SDK builds install the faces'
licences and attribution at `GUIForms_FONT_DIR` (`share/GUIForms/fonts` under
the prefix); ship those licence files with your package. See
[Text engine](text-engine) for the fixed font set.

## Rebuild consumers with the SDK

The public C++ surface is pre-1.0. Object layouts of `Component`, `Event`
slots, `Control` and several controls change between releases, so a consumer
compiled against one SDK is not binary compatible with the next. Rebuild your
application whenever you install a new SDK, and keep the toolchain file the
same on both sides. Header-aware compiler caches invalidate stale objects
automatically; importing a cache does not repair an ABI mismatch.
