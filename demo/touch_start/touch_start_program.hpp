#pragma once

// The touch start demonstrator's program steps every host's main takes
// (src/host/*/touch_start_main.*): --check, which selects the tile with
// that id segment (weather, map, settings, ...) and pans it into view, then
// --headless --capture, which draws the window offscreen at --size (the
// demonstrator's own size otherwise) through the Gallery's headless capture
// (gallery_headless_capture.hpp), reduced motion included, so the live
// tiles are captured on their first faces. Header-only, as
// program_options.hpp is.

#include "gallery_headless_capture.hpp"
#include "program_options.hpp"
#include "touch_start.hpp"

#include <cstdio>

namespace gui_forms::touch_start {

// Applies --check; false after printing the refusal.
[[nodiscard]] inline bool apply_touch_start_staging(const demo::ProgramOptions& program, Window& window) {
    if (program.check.empty()) return true;
    if (check_touch_start_tile(window, program.check)) return true;
    std::fprintf(stderr, "cannot select touch-start tile: %s\n", program.check.c_str());
    return false;
}

// The headless capture at the demonstrator's size unless --size names one.
[[nodiscard]] inline int run_headless_touch_start(demo::ProgramOptions program, Window& window) {
    if (!program.capture_size.has_value()) program.capture_size = window_size;
    const int result = gallery::run_headless_gallery_capture(program, window);
    return result;
}

} // namespace gui_forms::touch_start
