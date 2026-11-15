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
  - The page posts records to the program through the `gf_web_post_*`
    exports (`page/web_page_exports.cpp`) into the page queue
    (`page/web_page_queue.hpp`).
  - The program calls `gf_web_js_*`, which Emscripten proxies to the main
    thread.
- **Presentation (WebGL2 only).** The program paints each window into its
  CPU Skia raster. It hands the frame over in the present record
  (`page/web_present.hpp`). On the next animation frame the page uploads
  the damaged rectangles straight from the rasters into textures and draws
  the layers. It then posts `presented`. The program paints nothing while a
  frame is with the page, so the page reads stable pixels without a copy.
  - An `OffscreenCanvas` on the program's thread was not used: a thread that
    blocks in its own loop never commits its frames.
  - This is the one written exception to "no GPU" in `AGENTS.md`.
- **One canvas, many windows.** Every window of the run is a layer of
  `#gui-forms-canvas` (`desk/web_desk.hpp`):
  - The primary window fills the canvas.
  - Owned, tool and floating windows have a frame the host draws: a title
    bar and a close box. They can be dragged and resized. The frame's title
    strip is a small GUI.Forms window painted into a raster of its own
    (`FrameChrome`).
  - Popups and tooltips are placed where the program puts them.
  - The desk keeps the stacking, the active window and modal blocking.
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

`tests/web_desk_tests.cpp` covers the plain C++ parts on every platform:
- the desk's stacking, hit tests, modal blocking and frame drags;
- the input rules;
- the page queue's order, bound, waits and wakes;
- the present record's device rectangles.

The Emscripten parts need a browser.
