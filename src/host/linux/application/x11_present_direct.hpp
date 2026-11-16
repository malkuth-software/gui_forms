#pragma once
// The X11 host's direct presentation path (docs/user/presentation.md):
// MIT-SHM pixmaps the program writes in shared memory, presented with the
// Present extension (XPresentPixmap) with the damage as the update region.
// The server reads the pixmap until it reports it idle (PresentIdleNotify);
// a pixmap is written again only after that. Nothing here touches a GPU API:
// the pixmaps are shared memory, and whether the server flips or copies
// them is its own choice. Xlib, MIT-SHM and Present types stay in
// src/host/linux/; the Present headers stay in the .cpp.
//
// The path refuses as unsupported where it cannot work for the window's
// life: a build without libXpresent or libXext, a server without MIT-SHM
// pixmaps or Present, a remote display (the segment cannot be attached), a
// visual whose pixel is not the raster's (32-bit words, blue in the low
// byte, little-endian), or a 32-bit (ARGB) window presenting opaque, whose
// alpha the copy path rewrites. Up to three pixmaps; all of them still read
// by the server refuses busy; a segment that cannot be made refuses
// resources. A server error on one of the path's own requests (a pixmap
// refused, a present rejected) is absorbed by a ForeignWindowScope round
// those requests and becomes a refusal or a lost present, so the shim falls
// back; every other X error still reaches the host's handler. UI thread
// only, as every Xlib call of the host.
#include "../../../core/host/presentation/presentation_shim.hpp"
#include <X11/Xlib.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms::host::linux_detail {

// A Present event read from the X queue (read_x11_present_event).
struct X11PresentEvent final {
  // A PresentIdleNotify: `pixmap` of `window`, presented with `serial`, is
  // no longer read by the server.
  bool idle{};
  ::Window window{};
  Pixmap pixmap{};
  std::uint32_t serial{};
};

// The Present extension's major opcode on `display`, or -1 without it (or
// in a build without libXpresent).
[[nodiscard]] int x11_present_opcode(Display *display) noexcept;
// Reads `event` when it is a GenericEvent of the Present extension
// (`opcode`): returns true and fills `read` for an idle notice; false for
// any other event, which is left as it was.
[[nodiscard]] bool read_x11_present_event(Display *display, int opcode, XEvent &event, X11PresentEvent &read);
// Whether `visual` at `depth` stores the raster's pixel: 32-bit words with
// red, green and blue in bytes 2, 1 and 0, read little-endian by the
// server, so the raster's BGRA rows are its rows unchanged.
[[nodiscard]] bool x11_raster_layout(Display *display, Visual *visual, int depth) noexcept;
// The major opcode of the extension `name` on `display`, or -1 without it:
// what a ForeignWindowScope over a direct path's requests absorbs.
[[nodiscard]] int x11_extension_opcode(Display *display, const char *name) noexcept;

class X11PresentPath final : public presentation::PresentationDirectPath {
public:
  // `display` and `window` (observing) outlive the path: the window's
  // NativeWindow owns the shim that owns it and releases it before the
  // display closes.
  X11PresentPath(Display *display, ::Window window, Visual *visual, int depth) noexcept;
  X11PresentPath(const X11PresentPath &) = delete;
  X11PresentPath &operator=(const X11PresentPath &) = delete;
  ~X11PresentPath() override;

  presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
  presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
  void release() noexcept override;

  // A PresentIdleNotify for this window: the pixmap may be written again.
  // A pixmap this path no longer holds (released, or remade at a new
  // size) is ignored.
  void idle(Pixmap pixmap, std::uint32_t serial) noexcept;

  // Set by each present before it runs the shim: whether the raster's
  // alpha is presented (a 32-bit window refuses while it is not).
  bool present_alpha{};

private:
  struct PixmapSlot final {
    // An XShmSegmentInfo (XShm.h), opaque here; owned by the slot.
    void *segment{};
    std::byte *pixels{};
    std::size_t row_bytes{};
    Pixmap pixmap{};
    // The serial of the present still read by the server; 0 for none.
    std::uint32_t reading{};
    // The present that last showed this pixmap; 0 for never.
    std::uint64_t shown_at{};
  };
  static constexpr std::size_t maximum_slots = 3U;

  // Checks the server and the visual once; false refuses as unsupported.
  bool usable();
  bool make_slot(PixmapSlot &slot, presentation::PresentExtent extent);
  void free_slot(PixmapSlot &slot) noexcept;
  void free_slots() noexcept;

  Display *display_;
  ::Window window_;
  Visual *visual_;
  int depth_;
  enum class Check : std::uint8_t { unchecked, usable, unsupported };
  Check checked_{Check::unchecked};
  // The event selection made at the first acquire; 0 before.
  XID selection_{};
  // MIT-SHM's, Present's and XFixes' major opcodes: the requests whose
  // errors this path absorbs and maps to a fallback (A-gf-present-01-F1).
  std::array<int, 3> opcodes_{-1, -1, -1};
  std::array<PixmapSlot, maximum_slots> slots_{};
  std::size_t count_{};
  presentation::PresentExtent extent_{};
  std::size_t held_{};
  bool holding_{};
  std::uint32_t serial_{};
  std::uint64_t presents_{};
  // A present's update rectangles, reused.
  std::vector<XRectangle> rectangles_;
};

} // namespace gui_forms::host::linux_detail
