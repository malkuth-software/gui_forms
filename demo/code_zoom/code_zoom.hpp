#pragma once

// The code-zoom demonstrator (owner, 2026-11-22): a dark editor window
// showing one of GUI.Forms' own C++ sources, embedded at build time
// (GUI_FORMS_CODE_ZOOM_SOURCE in CMakeLists.txt) and highlighted by STXlib's
// highlighter (stxlib/syntax.hpp), zoomed smoothly from the whole file as a
// minimap (0.1x) to a few characters filling the window (40x) with every
// frame's text rasterized at its true size (code_sheet.hpp). F toggles the
// frame-time readout. The program steps (--headless --capture's stills at
// several zooms, --bench's sweep) are code_zoom_program.hpp's.
//
// Demo code over public GUI.Forms vocabulary: no host, renderer or platform
// type appears here. The hosts' mains are src/host/*/code_zoom_main.*.

#include "code_sheet.hpp"

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window.hpp"

#include <memory>
#include <string_view>

namespace gui_forms::code_zoom {

inline constexpr std::string_view program_name = "GUI.Forms Code Zoom";
inline constexpr Size window_size{1280.0, 800.0};
inline constexpr Size minimum_window_size{480.0, 320.0};
inline constexpr std::string_view sheet_id = "code-zoom.sheet";

// The embedded source's path in the GUI.Forms tree and its bytes (static
// storage).
[[nodiscard]] std::string_view embedded_source_name() noexcept;
[[nodiscard]] std::string_view embedded_source_text() noexcept;

// The demonstrator's window over the embedded source, laid out, its keys
// subscribed.
[[nodiscard]] std::unique_ptr<Window> make_code_zoom();

// The window's sheet; null when `window` holds none.
[[nodiscard]] std::shared_ptr<CodeSheet> find_code_sheet(const Window& window);

} // namespace gui_forms::code_zoom
