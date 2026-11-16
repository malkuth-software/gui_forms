#pragma once
// The X11 DRI3 buffer route (docs/user/presentation.md; host capability
// dri3_pixmaps): GBM buffers on the server's own DRM device (DRI3Open),
// mapped for CPU writing (age 0: a mapping may not keep their contents, so
// the whole frame is written; gbm_buffer_known_age), made into pixmaps with DRI3PixmapFromBuffer and
// presented with XPresentPixmap and the damage as the update region. GBM
// and DRI3 are used only to obtain the buffer and hand it to the server;
// no rendering, EGL context or GPU work. Built only with
// GUI_FORMS_ENABLE_X11_DRI3_PRESENT (which needs libXpresent and MIT-SHM's
// libXext beside GBM, xcb-dri3 and X11-xcb), chosen when GUI_FORMS_PRESENT
// names both "direct" and "dri3".
//
// Refusals: no DRI3 1.0 or Present, a remote display (DRI3Open fails), a
// device GBM declines, a visual not in the raster's layout, or a 32-bit
// window presenting opaque, are unsupported for the window's life; every
// pixmap still read by the server is busy (PresentIdleNotify frees one); a
// buffer or pixmap that cannot be made is resources; a mapping that fails
// or a broken connection is lost. Up to three buffers. UI thread only.
#include "../gbm/gbm_buffer.hpp"
#include "../../../core/host/presentation/presentation_shim.hpp"
#include <X11/Xlib.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms::host::linux_detail {

class X11Dri3Path final : public presentation::PresentationDirectPath {
public:
  // `display` and `window` (observing) outlive the path, as for
  // X11PresentPath.
  X11Dri3Path(Display *display, ::Window window, Visual *visual, int depth) noexcept;
  X11Dri3Path(const X11Dri3Path &) = delete;
  X11Dri3Path &operator=(const X11Dri3Path &) = delete;
  ~X11Dri3Path() override;

  presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
  presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
  void release() noexcept override;

  // A PresentIdleNotify for this window (Runtime::dispatch); a pixmap this
  // path no longer holds is ignored.
  void idle(Pixmap pixmap, std::uint32_t serial) noexcept;

  // Set by each present before it runs the shim, as for X11PresentPath.
  bool present_alpha{};

private:
  struct Dri3Slot final {
    GbmBuffer buffer{};
    Pixmap pixmap{};
    // The serial of the present still read by the server; 0 for none.
    std::uint32_t reading{};
    // The present that last showed this pixmap; 0 for never.
    std::uint64_t shown_at{};
  };
  static constexpr std::size_t maximum_slots = 3U;
  enum class Check : std::uint8_t { unchecked, usable, unsupported };

  // Checks the server, opens its device and selects idle notices once.
  bool usable();
  bool make_slot(Dri3Slot &slot, presentation::PresentExtent extent);
  void free_slot(Dri3Slot &slot) noexcept;
  void free_slots() noexcept;

  Display *display_;
  ::Window window_;
  Visual *visual_;
  int depth_;
  Check checked_{Check::unchecked};
  GbmDevice device_{};
  XID selection_{};
  std::array<Dri3Slot, maximum_slots> slots_{};
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
