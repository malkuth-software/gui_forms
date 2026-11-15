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

## Components

| Component | Target | Kind | Use it for |
|---|---|---|---|
| `Threading` | `GUIForms::Threading` | shared | `Worker`, `CancellationFlag`, the atomic thread pool. Linked by `Core`, `Application` and `Audio`. |
| `Core` | `GUIForms::Core` | static | The retained tree, events, invalidation, scheduling, resources and the portable host protocol, with no renderer and no native window. |
| `Controls` | `GUIForms::Controls` | static | Stock controls over `Core`. Exports `Drawing`. |
| `Drawing` | `GUIForms::Drawing` | static | Renderer-free geometry, colour, paths, raster storage and command recording. |
| `Application` | `GUIForms::Application` | shared | The native host for your platform. Already contains `Core`, `Controls` and `Drawing`. |
| `Audio` | `GUIForms::Audio` | static | PCM clips, Ogg decoding and a device backend. Links `Threading` only. |
| `CAbi` | `GUIForms::CAbi` | shared | The C ABI (`gui_forms/c/gf_abi.h`), for C, C# and other languages. Links `Application`, so a process has one engine whether it uses C++, the C ABI or both. |
| `CAbiConformance` | `GUIForms::CAbiConformance` | static | The C ABI conformance kit ([Running the C ABI conformance kit](c-abi-conformance-kit)). |

Two consumer shapes follow from the table:

- **Renderer-free consumers** (tests, model code, headless automation) link
  `GUIForms::Controls` or `GUIForms::Core`. They construct and mutate a
  `Window` model but never open a native window.
- **Applications** link `GUIForms::Application` and nothing else from the
  retained set. A program that also calls the C ABI links `GUIForms::CAbi`
  beside it.

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
`GUIForms::PreparedText` (the prepared-text service) both need
`GUI_FORMS_ENABLE_HARFBUZZ_TEXT`.

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
[Content surfaces](content-surfaces.md)). GUI.Forms' text engine and its
pinned FreeType, HarfBuzz, SheenBidi and libunibreak are inside the
Application library and not exported from it: its export list holds only
GUI.Forms' declarations, so no `hb_` or `FT_` symbol of ours is visible to
your program, and your copies never replace ours. The static Core,
Controls and Drawing archives hold no text engine at all. The build checks
this on every shared GUI.Forms library (the `gui_forms_foreign_text_symbols_tests`
and `gui_forms_abi_foreign_text_symbols` tests), and
`tools/verify_install.py --native --upstream-text-stack <prefix>` builds
`tests/upstream_text_stack_consumer/`, which links the system's HarfBuzz and
FreeType through pkg-config beside Application and uses both.

The one exception is deliberate: `GUIForms::FreeType` and `GUIForms::HarfBuzz`
(`tools/build_sdk.py --features all`) install the pinned libraries under
their upstream names for a program that calls them directly. Link those or
your own copies, not both.

## Runtime files to ship

`Threading` and `Application` are shared libraries, and so is STXlib's
state (`stxlib_state`: STXlib's allocator with mimalloc, its thread
identities and its SIMD level), which the controls and `Application` use and
which must be one copy per process. Ship them beside your program and use
the same copy for every component:

| Platform | Files | Location |
|---|---|---|
| macOS | `libgui_forms_threading.0.dylib`, `libgui_forms_application.0.dylib` and their versioned targets, `libstxlib_state.dylib`, plus `libc++.1.dylib`, `libc++abi.1.dylib`, `libunwind.1.dylib` from the runtime prefix | `Your.app/Contents/Frameworks`, with `@executable_path/../Frameworks` rpaths; sign the libraries before the bundle |
| Linux | `libgui_forms_threading.so.0`, `libgui_forms_application.so.0` and the versioned files those symlinks resolve to, and `libstxlib_state.so` | the application's runtime library directory |
| Windows | `libgui_forms_threading.dll`, `libgui_forms_application.dll` and `stxlib_state.dll` (in the SDK's `bin/`); they embed the LLVM runtimes and winpthreads, so no runtime DLL comes with them. Your own program needs runtime DLLs only if you link it dynamically (link it with `-static`, as GUI.Forms' programs are, to need none) | beside the executable |

The reference examples stage Windows DLLs with `$<TARGET_RUNTIME_DLLS:...>`
in a post-build step; copy that pattern rather than listing DLL names.
`Application` and the controls carry `stxlib::state` in their link
interface, so it is in that list. In the SDK the state library sits in the
same `lib/` as GUI.Forms' libraries (they find it through their
`@loader_path` or `$ORIGIN` rpath), or in `bin/` on Windows.

## What the shared libraries export

`Application` and `Threading` export only the declarations their public
headers mark with the library's export macro (`GUI_FORMS_API`). On Windows the
mark is `dllexport`; on macOS and Linux each library links with an export
list generated from the marks (`cmake/exports/<library>.exports` and
`.map`, written by `tools/export_annotations.py`). A class's mark covers its
members, its vtable and its type information. A declaration without the mark,
such as anything in a `detail` namespace or a private header, is not exported,
and a program that names it fails to link against the shared library even
though its header compiles. The C ABI library exports only the functions its
allowlist names (`share/GUIForms/abi/gf_abi_exports.txt`).

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
