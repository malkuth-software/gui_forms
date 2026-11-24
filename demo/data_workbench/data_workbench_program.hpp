#pragma once

// The data-workbench demonstrator's program steps every host's main takes
// (src/host/*/data_workbench_main.*): --headless --capture, which draws the
// window offscreen at --size (the demonstrator's own size otherwise)
// through the Gallery's headless capture (gallery_headless_capture.hpp).
// The demonstrator has no check boxes to stage, so --check is refused.
// Header-only, as program_options.hpp is.

#include "data_workbench.hpp"
#include "gallery_headless_capture.hpp"
#include "program_options.hpp"

#include <cstdio>

namespace gui_forms::data_workbench {

// Refuses --check, which names nothing here; false after printing why.
[[nodiscard]] inline bool apply_data_workbench_staging(const demo::ProgramOptions& program) {
    if (program.check.empty()) return true;
    std::fprintf(stderr, "the data workbench has no check box to set: %s\n", program.check.c_str());
    return false;
}

// The headless capture at the demonstrator's size unless --size names one.
[[nodiscard]] inline int run_headless_data_workbench(demo::ProgramOptions program, Window& window) {
    if (!program.capture_size.has_value()) program.capture_size = window_size;
    const int result = gallery::run_headless_gallery_capture(program, window);
    return result;
}

} // namespace gui_forms::data_workbench
