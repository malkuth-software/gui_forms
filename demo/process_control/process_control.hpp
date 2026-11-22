#pragma once

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The process-control demonstrator (owner, 2026-11-22; skeudoindustrial
// surfaces, phase 3): the owner's process-control dialog
// (notes:experiments/industrial-surfaces/demo.html) as a program of its
// own, built from the library's general primitives and nothing special to
// it:
// - the dialog's blasted enamel and its heavier title band
//   (`finish(blasted-enamel-glint, ...)`), with a picker for the ГОСТ paint
//   under the coat and the seed of its noise;
// - its three status lamps, jewels lit from the shipped indicator bake,
//   each switched off, on, error or fault (failing: it flickers and
//   stutters, steady under reduced motion) from the side strip;
// - its CANCEL and OK keys, buttons the program's theme draws from the
//   shipped key bakes (`primitive: light-map`), pressed while held, OK lit
//   with the status lamps and CANCEL always;
// - the lamp radio row (RadioButton's lamp look) with its colour set at run
//   time, and the coolant flow pinpoint.
// The program's theme is one sheet set on the root (Control::
// set_theme_sheet), rebuilt when the paint or the seed changes.
//
// Demo code over public GUI.Forms vocabulary: no host, renderer or
// platform type appears here. The hosts' mains are src/host/*/
// process_control_main.*; every one takes the demonstration programs'
// options (program_options.hpp), --headless --capture and --check
// included.
namespace gui_forms::process_control {

inline constexpr std::string_view program_name = "GUI.Forms Process Control";
// The window's logical size: the study's 500 x 310 dialog and the strip.
inline constexpr Size window_size{840.0, 440.0};

// One ГОСТ paint the enamel can be laid over (demo/themes/gost_paints,
// ГОСТ 22133-86's colorimetry).
struct EnamelPaint final {
    std::string_view name{};
    std::string_view body{};
    std::string_view band{};
};

// The paints, the study's first: ГОСТ 397 blue-green under ГОСТ 832's band.
inline constexpr std::array<EnamelPaint, 3> enamel_paints{{
    {.name = "ГОСТ 397 blue-green", .body = "#4d676d", .band = "#656f74"},
    {.name = "ГОСТ 365 grey-green", .body = "#80948a", .band = "#4d676d"},
    {.name = "ГОСТ 832 dark grey", .body = "#656f74", .band = "#4d676d"},
}};

// The study's seed.
inline constexpr std::uint32_t study_seed = 11U;

// The program's theme sheet for the paint at `paint` (an index into
// enamel_paints, wrapped) and `seed`: the enamel and title band classes and
// the lit keys.
[[nodiscard]] std::string process_control_sheet(std::size_t paint, std::uint32_t seed);

// The demonstrator's window, laid out and themed.
[[nodiscard]] std::unique_ptr<Window> make_process_control();

// Sets the check box with stable id `id` as a click would (capture
// staging, --check); false when the window holds no such check box.
[[nodiscard]] bool check_process_control_box(Window& window, std::string_view id);

} // namespace gui_forms::process_control
