#pragma once
#include "gui_forms/types/cursor_image/cursor_image.hpp"
#include <X11/Xcursor/Xcursor.h>
namespace gui_forms::host::linux_detail {
// Caller provides an already validated immutable cursor and UI-owned display.
inline ::Cursor create_image_cursor(Display* display, const CursorImages& images, const CursorImage& image) {
  XcursorImage* pixels = XcursorImageCreate(image.width, image.height);
  if (pixels == nullptr) return None;
  (*pixels).xhot = images.hotspot_x(image); (*pixels).yhot = images.hotspot_y(image);
  for (std::size_t i = 0; i < image.rgba.size(); ++i) {
    const Color c = image.rgba[i];
    (*pixels).pixels[i] = (std::uint32_t(c.alpha) << 24) |
        (((std::uint32_t(c.red) * c.alpha + 127) / 255) << 16) |
        (((std::uint32_t(c.green) * c.alpha + 127) / 255) << 8) |
         ((std::uint32_t(c.blue) * c.alpha + 127) / 255);
  }
  const ::Cursor cursor = XcursorImageLoadCursor(display, pixels);
  XcursorImageDestroy(pixels);
  return cursor;
}
} // namespace gui_forms::host::linux_detail
