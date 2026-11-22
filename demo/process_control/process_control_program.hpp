#pragma once

// The process-control demonstrator's program steps every host's main takes
// (src/host/*/process_control_main.*): --check, then --headless --capture,
// which draws the window offscreen at --size (the demonstrator's own size
// otherwise) through the Gallery's headless capture
// (gallery_headless_capture.hpp), reduced motion included, so a failing
// lamp is captured steadily lit. Header-only, as program_options.hpp is.

#include "gallery_headless_capture.hpp"
#include "process_control.hpp"
#include "program_options.hpp"

#include <cstdio>

namespace gui_forms::process_control {

// Applies --check; false after printing the refusal.
[[nodiscard]] inline bool apply_process_control_staging(const demo::ProgramOptions& program, Window& window) {
    if (program.check.empty()) return true;
    if (check_process_control_box(window, program.check)) return true;
    std::fprintf(stderr, "cannot check process-control box: %s\n", program.check.c_str());
    return false;
}

// The headless capture at the demonstrator's size unless --size names one.
[[nodiscard]] inline int run_headless_process_control(demo::ProgramOptions program, Window& window) {
    if (!program.capture_size.has_value()) program.capture_size = window_size;
    const int result = gallery::run_headless_gallery_capture(program, window);
    return result;
}

} // namespace gui_forms::process_control
