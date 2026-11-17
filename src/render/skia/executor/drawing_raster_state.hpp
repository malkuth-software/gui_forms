#pragma once

// The raster's per-thread state, shared by GUI.Drawing's two raster
// entries: the C ABI's raster service (src/abi/drawing_skia_c_api.cpp) and
// the C++ execute_recorder (recorder_raster.cpp). Each thread that
// rasterizes has one STXlib thread record holding its SkiaExecutor (which
// is thread-affine and owns its typefaces) and the font registrations
// made on it; the record lives and dies with the thread. One copy per
// program: the C ABI library and a C++ program each link this archive
// (gui_drawing_skia) once.

#include "gui_forms/drawing/font_substitution/font_substitution.hpp"
#include "gui_forms/drawing/graphics_recorder/graphics_recorder.hpp"
#include "skia_executor.hpp"

#include <cstdint>
#include <exception>
#include <string_view>

namespace gui_drawing::render {

// Stage F: one family request's answer on this thread, registered for
// drawing. The request's own name is not kept; the table's views are static.
struct FontRequest final {
    FontFamilyResolution resolution{};
    bool face_loaded{};
};

// Why the thread's record is unavailable: its allocation failed (a later
// call tries again), or the thread's records are ending or have ended
// (STXlib disposed, or the record type unregistered).
enum class RasterRecordRefusal : std::uint8_t {
    out_of_memory = 0,
    not_available = 1,
};

// Thrown where a raster entry needs the thread's record and cannot have
// it. Each entry answers with its own refusal code and nothing more.
class RasterRecordUnavailable final : public std::exception {
public:
    explicit RasterRecordUnavailable(RasterRecordRefusal why) noexcept : refusal(why) {}
    const char* what() const noexcept override {
        return "the drawing raster has no record on this thread";
    }
    RasterRecordRefusal refusal;
};

// The calling thread's executor, made on first use. Throws
// RasterRecordUnavailable, or what the executor's construction throws.
SkiaExecutor& raster_executor();

// Resolves `family` in `style` once per thread and registers its face with
// the thread's executor (the substitution table, then a host face named
// exactly so, then the content family). The reference stays valid for the
// thread's life. Throws RasterRecordUnavailable.
const FontRequest& ensure_font(std::string_view family, std::uint32_t style);

// Registers the packaged last-resort faces on this thread, once. Throws
// RasterRecordUnavailable.
void register_packaged_fonts();

// Resolves and registers the fonts of `recorder`'s text commands from
// `first_command` on. Throws RasterRecordUnavailable.
void ensure_recorder_fonts(const GraphicsRecorder& recorder, std::uint64_t first_command);

} // namespace gui_drawing::render
