#pragma once
// The Wayland linux-dmabuf buffer route (docs/user/presentation.md; host
// capability linux_dmabuf, reserved until a Wayland host reports it):
// linear GBM buffers on the compositor's render node, mapped for CPU
// writing, shared as zwp_linux_dmabuf_v1 buffers, attached with
// wl_surface_damage_buffer for the damage and committed; a buffer is
// written again after wl_buffer.release. GBM only obtains, maps and
// exports the buffer: no EGL, rendering or GPU work. Built only with
// GUI_FORMS_ENABLE_WAYLAND_DMABUF_PRESENT (beside the wl_shm path's
// option), for a Wayland host to choose when GUI_FORMS_PRESENT names both
// "direct" and "dmabuf"; its fallback is the wl_shm path's copy.
//
// Refusals: a render node GBM declines is unsupported for the path's life;
// all three buffers held by the compositor is busy; a buffer that cannot be
// made or exported is resources; a failed mapping, a display in error or a
// failed flush is lost. A compositor that rejects the parameters raises a
// protocol error, seen as lost at the next acquire. The host's event loop
// thread only, as for WaylandShmPath.
#include "../gbm/gbm_buffer.hpp"
#include "../../../core/host/presentation/presentation_shim.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct wl_buffer;
struct wl_display;
struct wl_surface;
struct zwp_linux_dmabuf_v1;

namespace gui_forms::host::wayland_detail {

class WaylandDmabufPath final : public presentation::PresentationDirectPath {
public:
  // `display`, `dmabuf` (the bound global) and `surface` (observing)
  // outlive the path, as for WaylandShmPath. `render_node` is a descriptor
  // of the compositor's render node (its dmabuf feedback's main device),
  // owned by the path from here.
  WaylandDmabufPath(wl_display *display, zwp_linux_dmabuf_v1 *dmabuf, wl_surface *surface, int render_node) noexcept;
  WaylandDmabufPath(const WaylandDmabufPath &) = delete;
  WaylandDmabufPath &operator=(const WaylandDmabufPath &) = delete;
  ~WaylandDmabufPath() override;

  presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
  presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
  void release() noexcept override;

  // Whether the surface's pixels carry alpha (set by the host before each
  // present; a change remakes the buffers).
  bool translucent{};

  struct Slot final {
    linux_detail::GbmBuffer buffer{};
    wl_buffer *shared{};
    // The compositor holds the buffer (committed, no release yet).
    bool held{};
    // The present that last showed this buffer; 0 for never.
    std::uint64_t shown_at{};
  };

private:
  static constexpr std::size_t maximum_slots = 3U;
  enum class Check : std::uint8_t { unchecked, usable, unsupported };

  bool make_slot(Slot &slot, presentation::PresentExtent extent);
  void free_slots() noexcept;

  wl_display *display_;
  zwp_linux_dmabuf_v1 *dmabuf_;
  wl_surface *surface_;
  int render_node_;
  Check checked_{Check::unchecked};
  linux_detail::GbmDevice device_{};
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
