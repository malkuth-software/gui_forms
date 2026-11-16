#include "wayland_dmabuf_present.hpp"

#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include <wayland-client.h>

#include <cerrno>
#include <unistd.h>

namespace gui_forms::host::wayland_detail {

namespace {

// The largest buffer made: the other paths' presentation limit (64 MiB).
constexpr std::size_t maximum_buffer_bytes = 64U * 1024U * 1024U;

// wl_buffer.release: the compositor no longer reads the buffer. `data` is
// the slot (WaylandDmabufPath::slots_), which outlives the buffer.
void dmabuf_released(void *data, wl_buffer *buffer) {
  static_cast<void>(buffer);
  WaylandDmabufPath::DmabufSlot &slot = *static_cast<WaylandDmabufPath::DmabufSlot *>(data);
  slot.held = false;
}

const wl_buffer_listener dmabuf_listener{&dmabuf_released};

} // namespace

WaylandDmabufPath::WaylandDmabufPath(wl_display *display, zwp_linux_dmabuf_v1 *dmabuf, wl_surface *surface,
                                     int render_node) noexcept
    : display_(display), dmabuf_(dmabuf), surface_(surface), render_node_(render_node) {}

WaylandDmabufPath::~WaylandDmabufPath() {
  free_slots();
  gbm_device_close(device_);
  // Never handed to GBM (no acquire ran): still this path's.
  if (render_node_ >= 0) close(render_node_);
  render_node_ = -1;
}

bool WaylandDmabufPath::make_slot(DmabufSlot &slot, presentation::PresentExtent extent) {
  const std::size_t bytes = static_cast<std::size_t>(extent.width) * 4U * static_cast<std::size_t>(extent.height);
  if (bytes == 0U || bytes > maximum_buffer_bytes) return false;
  linux_detail::GbmBuffer buffer{};
  if (!linux_detail::gbm_buffer_make(device_, extent, translucent, buffer)) return false;
  const int descriptor = linux_detail::gbm_buffer_export(buffer);
  if (descriptor < 0) {
    linux_detail::gbm_buffer_free(buffer);
    return false;
  }
  zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(dmabuf_);
  const std::uint32_t modifier_high = static_cast<std::uint32_t>(buffer.modifier >> 32U);
  const std::uint32_t modifier_low = static_cast<std::uint32_t>(buffer.modifier & 0xFFFFFFFFU);
  // The request duplicates the descriptor as it is marshalled.
  zwp_linux_buffer_params_v1_add(params, descriptor, 0U, 0U, buffer.stride, modifier_high, modifier_low);
  close(descriptor);
  wl_buffer *shared =
      zwp_linux_buffer_params_v1_create_immed(params, static_cast<std::int32_t>(extent.width),
                                              static_cast<std::int32_t>(extent.height), buffer.format, 0U);
  zwp_linux_buffer_params_v1_destroy(params);
  if (shared == nullptr) {
    linux_detail::gbm_buffer_free(buffer);
    return false;
  }
  slot.buffer = buffer;
  slot.shared = shared;
  slot.held = false;
  slot.shown_at = 0U;
  wl_buffer_add_listener(shared, &dmabuf_listener, &slot);
  return true;
}

void WaylandDmabufPath::free_slots() noexcept {
  writing_ = false;
  for (std::size_t index = 0U; index < count_; ++index) {
    DmabufSlot &slot = slots_[index];
    if (slot.shared != nullptr) wl_buffer_destroy(slot.shared);
    // The compositor keeps its own reference to the dmabuf it shows.
    linux_detail::gbm_buffer_free(slot.buffer);
    slot = DmabufSlot{};
  }
  count_ = 0U;
}

presentation::DirectAcquire WaylandDmabufPath::acquire(presentation::PresentExtent extent) {
  if (checked_ == Check::unchecked) {
    checked_ = Check::unsupported;
    const int descriptor = render_node_;
    render_node_ = -1;
    if (linux_detail::gbm_device_open(descriptor, device_)) checked_ = Check::usable;
  }
  if (checked_ != Check::usable)
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
  if (wl_display_get_error(display_) != 0)
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
  if (writing_) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
  if (extent != extent_ || translucent != made_translucent_) {
    free_slots();
    extent_ = extent;
    made_translucent_ = translucent;
  }
  std::size_t chosen = count_;
  for (std::size_t index = 0U; index < count_; ++index) {
    if (slots_[index].held) continue;
    chosen = index;
    break;
  }
  if (chosen == count_) {
    if (count_ == maximum_slots) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    if (!make_slot(slots_[count_], extent))
      return presentation::DirectAcquire::refused(presentation::DirectRefusal::resources);
    ++count_;
  }
  DmabufSlot &slot = slots_[chosen];
  if (!linux_detail::gbm_buffer_map(slot.buffer, extent))
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
  written_ = chosen;
  writing_ = true;
  presentation::DirectBuffer buffer{};
  buffer.pixels = slot.buffer.pixels;
  buffer.row_bytes = slot.buffer.mapped_stride;
  buffer.extent = extent;
  buffer.age = slot.shown_at == 0U ? 0U : static_cast<std::uint32_t>(presents_ - slot.shown_at + 1U);
  return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome WaylandDmabufPath::present(std::span<const presentation::PresentRect> damage) {
  if (!writing_) return presentation::PresentOutcome::failed;
  writing_ = false;
  DmabufSlot &slot = slots_[written_];
  linux_detail::gbm_buffer_unmap(slot.buffer);
  wl_surface_attach(surface_, slot.shared, 0, 0);
  for (std::size_t index = 0U; index < damage.size(); ++index) {
    const presentation::PresentRect rect = damage[index];
    if (rect.empty()) continue;
    wl_surface_damage_buffer(surface_, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
  }
  wl_surface_commit(surface_);
  slot.held = true;
  ++presents_;
  slot.shown_at = presents_;
  if (wl_display_flush(display_) < 0 && errno != EAGAIN) return presentation::PresentOutcome::lost;
  return presentation::PresentOutcome::presented;
}

void WaylandDmabufPath::release() noexcept { free_slots(); }

} // namespace gui_forms::host::wayland_detail
