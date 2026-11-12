# Installed SDK references

Configure this directory with the installed GUI.Forms prefix and the provider's
LLVM 22 toolchain file, then build and run CTest. These examples use exported
targets rather than private source-tree includes or archives.

Every example with a window is written the way GUI.Forms recommends: a form
document in `forms/` says what exists and where (`gui_forms_add_form`
generates a `gui_forms::Window` subclass with the controls as typed
members), a model header names what the document binds (`value: bind(volume)`,
`command: bind(run)`), and the program attaches behaviour with owned
handlers. This directory is GUI.Forms' reference application. Every windowed program
makes its `Application` first, with `Application::create`, and its window
joins it (`Window(application, ...)`, the generated class's `Application&`
overload). `collection_gallery` is the complete application: its
application compiles its theme package (`themes/collection_gallery`,
embedded by `gui_forms_add_theme`) and runs its generated window. The
others drive their windows headless: making an application needs no host;
only `run` does. A window constructed with no current application makes
its own resources, which is how GUI.Forms' own tests run, not how a program
starts.

`cooperative_worker` links Threading alone. `audio_worker.cpp` is a static adapter
that links only Audio. Its named Worker entry borrows the context and passes the
entry's CancellationFlag straight to `AudioClip::load_ogg`. A small atomic gate
makes cancellation before decode deterministic; `join` publishes the result
before the caller reads it. The normal successful decode first verifies the
input. `ogg_worker` exercises the adapter without Core, and
`application_audio_worker` uses the same adapter beside shared Application.

`tone.ogg` is the repository's author-generated 0.12-second, 440 Hz, stereo 48 kHz
Vorbis fixture (libvorbis quality 2), identical to the hexadecimal fixture in
`tests/audio/audio_service_tests.cpp`. It is test data, not third-party music.
The audio service tests additionally request cancellation between actual decode
blocks, check no partial publication, and verify allocation/quota behavior.

The three `reject_mixed_core_*` tests require CMake generation to reject direct,
private static-adapter and interface-adapter combinations of Core and Application.
See `docs/COMPILER_CACHE.md` for the consumer linking and runtime packaging rule.

`mouse_events` drives the WinForms mouse events headless: an owning component
hears a button's MouseDown, MouseUp, Click, MouseClick and activation in order
(`manifests/abi/interaction_order.json`),
a panel's DoubleClick on the second release and a keyboard context request,
and a consuming rule withholds the button's Click
(`docs/user/mouse-events.md`).
