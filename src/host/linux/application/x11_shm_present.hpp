#pragma once
// The X11 host's shared-memory presentation image (MIT-SHM; audit G4/N67):
// the window's native image lives in a System V shared memory segment the
// X server reads directly, so a present no longer writes the frame through
// the X socket. On a remote display, a server without the extension, or a
// build without libXext, the host presents with XPutImage as before. Xlib
// types stay in src/host/linux/.
#include <X11/Xlib.h>

namespace gui_forms::host::linux_detail {

// One window's shared image, made at the raster's size on the window's
// visual and depth and remade when either size changes. UI thread only.
// The window releases it before the display closes.
class X11SharedImage final {
public:
  X11SharedImage() = default;
  X11SharedImage(const X11SharedImage &) = delete;
  X11SharedImage &operator=(const X11SharedImage &) = delete;
  // Detaches and frees a segment still held (release() was not called).
  ~X11SharedImage();

  // Makes or reuses the image for `width` by `height`. False when shared
  // presentation is not available: then, and after any failure to make or
  // attach a segment, the window stays on XPutImage for its life
  // (`unavailable`).
  [[nodiscard]] bool prepare(Display *display, Visual *visual, int depth, unsigned width, unsigned height);
  // The prepared image; its data is the shared segment. Valid until the
  // next prepare or release.
  [[nodiscard]] XImage *image() const noexcept { return image_; }
  // Submits one rectangle of the image to `drawable` at the same position.
  void put(Drawable drawable, GC gc, int left, int top, unsigned columns, unsigned rows);
  // Waits until the server has read every submitted rectangle, so the
  // next conversion may write the segment (XSync).
  void finish();
  // Detaches the segment from the server and this process and frees it.
  void release() noexcept;

  bool unavailable{};

private:
  Display *display_{};
  XImage *image_{};
  // The segment, opaque here: an XShmSegmentInfo (XShm.h) owned by this
  // object, allocated on first use.
  void *segment_{};
  unsigned width_{};
  unsigned height_{};
  Visual *visual_{};
  int depth_{};
};

} // namespace gui_forms::host::linux_detail
