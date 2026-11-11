# Architecture and extension model

GUI.Forms is one native retained engine with thin layers around it. The
engine owns identity, state, layout, painting, input, accessibility
publication and disposal. A host adapter translates one platform's windows
and events into the portable host protocol, and a private CPU renderer turns
`Painter` commands into pixels. This page describes how those pieces are
split into libraries, which objects you extend, and the rules every
extension follows. All of it is pre-1.0; see
[Compatibility and versioning](#compatibility-and-versioning).

## Library layers

The installed CMake package exposes six components. Their link-time use and
the one linking rule are in [Consume the SDK](../user/consume-the-sdk); this
is how they stack:

```text
your application
  └─ GUIForms::Application   shared; one native host per build
       contains the public layers
         Controls ─┬─ Core ── Threading (shared, shipped beside it)
                   └─ Drawing
       and, privately,
         host adapter       src/host/macos | windows | linux
         CPU renderer       src/render/skia on every host (Windows presents its
                            frames from DIB sections, src/host/windows/raster)
         text engine        HarfBuzz, FreeType, SheenBidi

renderer-free code (tests, models, automation)
  └─ GUIForms::Controls ── Core, Drawing

audio adapters
  └─ GUIForms::Audio ── Threading
```

`Drawing` depends on nothing else in the toolkit, `Core` depends only on
`Threading`, and `Controls` adds the stock controls over `Core` and
`Drawing`. `Application`
exists only in a build with a native host enabled and already contains the
retained set, which is why an application links it and nothing else from
that set.

Some build targets are never installed. The headless host
(`src/host/headless/`) is the deterministic reference adapter the tests link.
The experimental drawing C ABI (`src/abi/drawing_*`) is built and tested in
the source tree; its library is not part of the package. (The experimental
0.x C ABI, `gui_forms_abi0`, was retired before 1.0; the sealed C ABI,
`gui_forms_abi1`, is GUI.Forms' one C surface.) The development text services refuse
to install: configuring with them enabled makes `cmake --install` stop.

### Boundaries the build enforces

The layering is checked, not only described. Audit targets and tests in the
build inspect symbols, imports and sources:

- `cmake/check_renderer_boundaries.cmake`: no Skia, CoreGraphics, CoreText,
  AppKit, Metal or OpenGL symbol in the renderer-free core.
- `cmake/check_host_boundaries.cmake`, `cmake/check_windows_boundaries.cmake`:
  AppKit symbols stay out of the core and headless host, Win32 and COM types
  out of portable sources, and the Windows Gallery imports no GPU or managed
  runtime.
- `cmake/check_drawing_boundaries.cmake`: no renderer or host dependency in
  the drawing libraries.
- `cmake/check_core_linkage.cmake`, run by the reference examples: a project
  that links static `Core` beside `Application` fails at generation time.

One Apple-framework file lives outside `src/host/macos/`:
`src/render/skia/executor/core_text_font_bytes.cpp`, inside the private
renderer, recovers a named face's bytes for the drawing C API so the shared
text engine can load it. Nothing it does crosses the portable contract.

## Extension objects

There are several ways to put your own behaviour into a window. Pick the
smallest one that does the job.

| You need | Use | You implement |
|---|---|---|
| Custom drawing in one region, with no input of its own | `DrawingSurface` | `set_paint_callback`, called as `(Painter&, Rect bounds, Rect local_damage)`; optionally a semantic role, hit-test visibility and background |
| An application-owned bitmap shown with zoom and pan | `RasterCanvas` | own and edit a `gui_drawing::Bitmap`; the control handles sampling, transparency and local damage |
| A producer that writes whole frames (video, visualizer, game board) | `LiveSurface` | publish complete frames through a write lease from any thread; a control draws it with `Painter::draw_live_surface` |
| Stock controls composed into a reusable unit | `UserControl` | build children and handle their events; `loaded()` fires once, `attachment_count()` counts presentations |
| Your own layout, paint, input or semantics | a `Control` subclass | the overrides below |
| Nonvisual behaviour: handler owners, providers | a `Component` subclass, held by a `ComponentContainer` | `on_dispose`; observe events with `on(event, owner, &Owner::method)` |

A `Control` subclass participates fully. The hooks are `measure` and
`arrange` (with the protected `arrange_self` and `set_child_layout` for
custom containers, see [Layout](../user/layout)); `on_paint(Painter&, Rect)`,
`on_paint_overlay` and `visual_outsets`; `hit_test_local`; the preview,
target and bubble pointer, key and drag hooks plus `on_text_input`;
`semantic_descriptor`,
`semantic_virtual_children` and `on_semantic_action`; and the attachment
hooks. A setter on a subclass declares what it dirties with
`invalidate_declared` ([Update scopes and dirtiness](../user/update-scopes-and-dirtiness)).

Two rules hold for every extension. It sees only the renderer-neutral
`Painter`: no extension can acquire the private renderer, a native window or
a platform drawing context. And it publishes meaning through the semantic
tree: a custom control returns a descriptor and, for parts that are not
controls, virtual children, so it is reachable by accessibility and
automation without authored metadata. Extender providers (`ToolTip`,
`ErrorProvider`, `HelpProvider`) enrich the final descriptor of any control,
stock or custom.

## Ownership and thread rules

The details are in [Retained tree](../user/retained-tree),
[Lifecycle](../user/lifecycle) and [Threading](../user/threading). In short:

- A parent owns its attached children strongly; a child refers to its parent
  weakly. Detaching a control does not destroy it while you hold a pointer.
- Nonvisual components belong to a `ComponentContainer` or to you, never to
  the visual tree.
- Subscriptions are tokens owned by a `Component`. Disposing the owner
  revokes them and its owned dispatcher work synchronously.
- Attached state changes only on the window's UI thread. A wrong-thread
  setter throws and is counted; it never partially applies. Other threads
  post with `begin_invoke` or block with `invoke`.
- Faults are contained where they arise. A throwing paint callback restores
  the previous chunks, a throwing frame or timer callback is disconnected and
  counted while its peers continue, and a dispatched callback's exception
  stays on its operation. The AppKit and Win32 scheduler callbacks contain any
  exception that still escapes, and the experimental C ABI turns exceptions
  into error results.

## Host-owned appearance

Controls supply content and semantic intent; the window's theme decides how
it looks. A `Theme` is an immutable record of named visual roles (window,
panel, card, button, accent and command buttons, choice, editor, menu item,
selection, progress), each with recipes per surface state and a high-contrast
set. `Window::set_theme` replaces the theme in one transaction;
`Control::set_theme_override` scopes a different one to a subtree.

A custom control should resolve `effective_theme().resolve(role, context)`
rather than hard-code colours. Neither the toolkit nor its hosts read
platform theme handles, system colours or OS appearance settings. The
default theme is GUI.Forms' own, `gui-forms`, after the design-language
specification; the Windows-familiar themes (`windows-professional` and the
familiar themes) are written in the toolkit's own materials and are not
skins of any operating system. Composite controls such as
`CommandBar`, `ChoiceGroup` and `ExpandableSections` expose their parts
through `part(name, index)` so a recipe can address them without
subclassing. Text uses only the bundled faces; see
[Text engine](../user/text-engine).

## Compatibility and versioning

The package is version 0.2.0. `find_package(GUIForms 0.2)` accepts any 0.2.x
installation and rejects other minor versions, and the shared libraries carry
`SOVERSION 0`. Within that range there is still no binary promise: object
layouts change between releases, so rebuild your application against each
new SDK ([Consume the SDK](../user/consume-the-sdk)).

Contracts are versioned separately rather than under one number. The host
protocol reports its own `protocol_version` in `HostCapabilities`
([Hosts and capabilities](../user/hosts-and-capabilities)), and the
experimental C ABIs negotiate their own 0.x versions, which carry no
compatibility promise.

GUI.Forms is Forms-shaped, not a reimplementation of all of Windows Forms.
ActiveX, browser hosting, printing, MDI, obsolete control families and
window-handle or message emulation are outside its scope. The decisions
behind these boundaries, event order and the lifecycle contract are in
[Architecture decisions](decisions); what has to be true before 1.0 is on the
[Roadmap](roadmap).
