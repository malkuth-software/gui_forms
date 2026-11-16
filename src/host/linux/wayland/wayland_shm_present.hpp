#pragma once
// The Wayland direct presentation path (docs/user/presentation.md):
// wl_shm buffers in memfd shared memory, attached to the window's surface
// with wl_surface_damage_buffer for the damage and committed. The
// compositor reads a buffer until it sends wl_buffer.release; a buffer is
// written again only after that. No GPU API is used: wl_shm is shared
// memory, and linux-dmabuf (which needs GBM or EGL) is not.
//
// GUI.Forms has no Wayland host yet: this path is written for it and is
// built only with GUI_FORMS_ENABLE_WAYLAND_PRESENT. A Wayland host's copy
// path would be the same protocol with one buffer and a full-frame write
// after each release, so the direct path is its normal path and the copy
// path its fallback. Wayland types stay in src/host/linux/wayland/.
//
// Up to three buffers. All still held by the compositor refuses busy; a
// buffer that cannot be made (memfd, mmap or the pool) refuses resources;
// a display in error (wl_display_get_error) or a failed flush is lost. The
// pixel format is WL_SHM_FORMAT_ARGB8888 (premultiplied, little-endian
// 32-bit words: the raster's BGRA bytes) for a translucent surface and
// XRGB8888 for an opaque one, both formats every compositor supports.
//
// One execution context: the host's event loop thread, which dispatches
// the display's queue (and so the release events) and paints.
#include "../../../core/host/presentation/presentation_shim.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct wl_buffer;
struct wl_display;
struct wl_shm;
struct wl_surface;

namespace gui_forms::host::wayland_detail {

class WaylandShmPath final : public presentation::PresentationDirectPath {
public:
  // `display`, `shm` and `surface` (observing) outlive the path: the
  // host's window owns the shim that owns it and releases it before the
  // surface is destroyed.
  WaylandShmPath(wl_display *display, wl_shm *shm, wl_surface *surface) noexcept;
  WaylandShmPath(const WaylandShmPath &) = delete;
  WaylandShmPath &operator=(const WaylandShmPath &) = delete;
  ~WaylandShmPath() override;

  presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
  presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
  void release() noexcept override;

  // Whether the surface's pixels carry alpha (set by the host before each
  // present; a change remakes the buffers).
  bool translucent{};

  struct Slot final {
    wl_buffer *buffer{};
    std::byte *pixels{};
    std::size_t bytes{};
    // The compositor holds the buffer (attached and committed, no release
    // yet).
    bool held{};
    // The present that last showed this buffer; 0 for never.
    std::uint64_t shown_at{};
  };

private:
  static constexpr std::size_t maximum_slots = 3U;

  bool make_slot(Slot &slot, presentation::PresentExtent extent);
  void free_slots() noexcept;

  wl_display *display_;
  wl_shm *shm_;
  wl_surface *surface_;
  // The slots' addresses are the release listener's data: the array is
  // never moved while a buffer exists.
  std::array<Slot, maximum_slots> slots_{};
  std::size_t count_{};
  presentation::PresentExtent extent_{};
  bool made_translucent_{};
  std::size_t written_{};
  bool writing_{};
  std::uint64_t presents_{};
};

} // namespace gui_forms::host::wayland_detail
