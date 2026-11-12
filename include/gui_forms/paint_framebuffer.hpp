#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {
class ImageRegistry;

// Offscreen instance of the host's renderer, made on the UI thread
// (SurfaceRoot::create_framebuffer). One thread uses it at a time: the
// maker, or the one it hands it to whole, which then alone uses and
// destroys it (a sub-surface's thread, SubSurfaceConfiguration). Pixels are
// premultiplied sRGB BGRA8 on every host: blue, green, red and alpha bytes in
// memory order, the one format of every surface and raster. The buffer
// survives begin/end; callers may restore cached pixels before drawing damage.
// A painter and pixel span are borrowed only for this object's lifetime.
class GUI_FORMS_API PaintFramebuffer {
public:
    virtual ~PaintFramebuffer() = default;
    virtual bool begin(const ImageRegistry& images, Rect damage) = 0;
    virtual void end() = 0;
    virtual Painter& painter() noexcept = 0;
    virtual std::span<std::byte> pixels() noexcept = 0;
    virtual std::size_t row_bytes() const noexcept = 0;
    virtual std::uint32_t width() const noexcept = 0;
    virtual std::uint32_t height() const noexcept = 0;
};
} // namespace gui_forms
