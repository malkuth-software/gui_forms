# Content boundary examples

These examples go with [Content surfaces](../../docs/user/content-surfaces.md).
Each one drives its window headless and runs as a test in the repository's
own build.

- `freezing_renderer.cpp` (`gui_forms_example_freezing_renderer`) runs a
  renderer on its own thread. The renderer draws a few frames, then hangs
  and never checks its stop token again. The interface keeps answering: a
  counter keeps counting, and the watchdog marks the surface stale while
  keeping its last frame under a veil. Terminate is then offered, and
  pressing it abandons the hung thread after the bounded grace.
- `hosted_crash.cpp` (`gui_forms_example_hosted_crash`, macOS and Linux)
  starts `crashing_program.cpp` in a process of its own. That program draws
  frames through `HostedClient` and then aborts. Only its process ends: the
  surface reports `failed` with the signal, the application keeps counting,
  and Restart runs the program again.

The content boundary is an integrity boundary, not a security boundary.
Content cannot stop or corrupt the interface, but it is not confined.
