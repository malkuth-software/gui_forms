# The browser host

GUI.Forms in a web page, built with Emscripten (owner decisions, 2026-11-15).
The first program is Plan Paint, at malkuthsoft.com/paint.

## Shape

- **Threads.** The program runs on a worker. It is linked with
  `PROXY_TO_PTHREAD`, so `main` runs on a pthread. Its event loop blocks there
  as a desktop loop does, and so do the nested loops of modal windows and
  dialogs. The browser's main thread holds the DOM, the canvas and WebGL2. It
  never waits on the program.
- **Two halves.** The program half (`application/`) is C++. The page half
  (`page/web_host.js`, an Emscripten `--js-library`) is JavaScript.
  - The page posts DOM events to the program through the `gf_web_post_*`
    exports (`page/web_page_exports.cpp`). They become the window manager's
    portable input records (`../surface/input/surface_input.hpp`).
  - The program calls `gf_web_js_*`, which Emscripten proxies to the main
    thread.
- **The window manager.** The canvas is the one surface of GUI.Forms' own
  window manager (`../surface/`, shared with the planned framebuffer
  appliance). `WebSurfaceHost` (`application/web_surface_host.hpp`)
  supplies the surface, the input queue and the present call. The manager
  does the windows, frames, states, focus, modal blocking and compositing.
- **Presentation (WebGL2 only).** The manager composes the damaged part of
  the scene into the surface on the CPU and presents its damaged
  rectangles. The host hands them over in the present record
  (`page/web_present.hpp`). On the next animation frame the page uploads
  those rectangles into its one texture, draws it, and posts `presented`.
  The manager writes no pixel of the surface while a present is with the
  page, so the page reads stable pixels without a copy. The windows keep
  painting into their own rasters meanwhile.
  - An `OffscreenCanvas` on the program's thread was not used: a thread that
    blocks in its own loop never commits its frames.
  - This is the one written exception to "no GPU" in `AGENTS.md`.
- **Files.**
  - Opening uses the browser's file chooser, or a drop on the canvas. The
    chosen files are copied into the program's file system under
    `/web/files/<request>/` (drops under `/web/drops/<n>/`).
  - Saving returns a path under `/web/save/<request>/`. When the program
    closes the file it wrote there, the page writes it to the file the
    browser's save chooser picked (`showSaveFilePicker`, where offered), or
    starts a download.
  - The folder dialog is unsupported.
  - Message boxes are drawn by the host.
- **Clipboard.** A browser grants no synchronous read. The page keeps the
  text of every paste event and of the program's own writes. A paste key
  waits for its paste event, so the program reads the pasted text when it
  handles the key. Writes go to `navigator.clipboard.writeText`.
- **Fonts.** The program's bundled interface fonts live in
  `/gui_forms/fonts`, from the page's required font pack. `GUI_FORMS_FONT_DIR`
  overrides the location. Lazy font groups arrive later and are announced to
  the program. A face that was looked up and missing before its group
  arrived stays missing for the run: the bundled font set caches failures.
  This is an open decision (see the queue entry).

What a page cannot do is reported in `web_capabilities()`, not imitated:
- accessibility publication;
- frame capture;
- sound cues;
- clipboard images;
- tray icons;
- window materials;
- window state reports;
- the splash screen before the first window.

## Building

```sh
cmake -S . -B .build/web -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/emscripten.cmake \
      -DGUI_FORMS_EMSDK=/path/to/emsdk-6.0.13 -DCMAKE_BUILD_TYPE=Release
cmake --build .build/web --target gui_forms_application
```

- The toolchain file pins emsdk 6.0.13.
- It adds `-pthread -msimd128 -fwasm-exceptions` to every unit, the stx
  package's included.
- stx_skia takes its WebAssembly baseline profile: SIMD128 through Skia's
  SSE4.1 paths.
- A program links `GUIForms::Application`. That library carries the host's
  link options (`--js-library`, `PROXY_TO_PTHREAD`, the exports the page
  calls). The program adds its own settings, for example:

```text
-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=512MB -sMAXIMUM_MEMORY=4GB
-sSTACK_SIZE=8MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB -sPTHREAD_POOL_SIZE=8
```

## The page

`web/paint/` is the hosted shell:
- `index.html`, `shell.css` and `shell.js`;
- `page.json`, listing the program, its asset packs (file_packager
  `--js-output`) and its font groups;
- `.htaccess`, for COOP/COEP, MIME types and caching;
- `coi-serviceworker.js`, the fallback where the front proxy drops the
  isolation headers.

## Tests

- `tests/web_host_rules_tests.cpp` covers the plain C++ parts on every
  platform: the DOM values turned into records, the present record, and the
  dialogs' accept lists and save names.
- The window manager's own tests are listed in `../surface/README.md`.
- The Emscripten parts need a browser.
