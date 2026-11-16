#include "wayland_shm_present.hpp"

#include <wayland-client.h>

#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

namespace gui_forms::host::wayland_detail {

namespace {

// The largest buffer made: the X11 path's presentation limit (64 MiB).
constexpr std::size_t maximum_buffer_bytes = 64U * 1024U * 1024U;

// wl_buffer.release: the compositor no longer reads the buffer. `data` is
// the slot (WaylandShmPath::slots_), which outlives the buffer.
void buffer_released(void *data, wl_buffer *buffer) {
  static_cast<void>(buffer);
  WaylandShmPath::Slot &slot = *static_cast<WaylandShmPath::Slot *>(data);
  slot.held = false;
}

const wl_buffer_listener buffer_listener{&buffer_released};

} // namespace

WaylandShmPath::WaylandShmPath(wl_display *display, wl_shm *shm, wl_surface *surface) noexcept
    : display_(display), shm_(shm), surface_(surface) {}

WaylandShmPath::~WaylandShmPath() { free_slots(); }

bool WaylandShmPath::make_slot(Slot &slot, presentation::PresentExtent extent) {
  const std::size_t row_bytes = static_cast<std::size_t>(extent.width) * 4U;
  const std::size_t bytes = row_bytes * static_cast<std::size_t>(extent.height);
  if (bytes == 0U || bytes > maximum_buffer_bytes) return false;
  const int file = memfd_create("gui_forms-present", MFD_CLOEXEC);
  if (file < 0) return false;
  if (ftruncate(file, static_cast<off_t>(bytes)) != 0) {
    close(file);
    return false;
  }
  void *mapped = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
  if (mapped == MAP_FAILED) {
    close(file);
    return false;
  }
  wl_shm_pool *pool = wl_shm_create_pool(shm_, file, static_cast<std::int32_t>(bytes));
  const std::uint32_t format = translucent ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888;
  wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<std::int32_t>(extent.width),
                                                static_cast<std::int32_t>(extent.height),
                                                static_cast<std::int32_t>(row_bytes), format);
  // The buffer keeps the pool's memory; the pool and the descriptor go.
  wl_shm_pool_destroy(pool);
  close(file);
  if (buffer == nullptr) {
    munmap(mapped, bytes);
    return false;
  }
  slot.buffer = buffer;
  slot.pixels = static_cast<std::byte *>(mapped);
  slot.bytes = bytes;
  slot.held = false;
  slot.shown_at = 0U;
  wl_buffer_add_listener(buffer, &buffer_listener, &slot);
  return true;
}

void WaylandShmPath::free_slots() noexcept {
  writing_ = false;
  for (std::size_t index = 0U; index < count_; ++index) {
    Slot &slot = slots_[index];
    // A destroyed buffer the compositor still shows stays on screen until
    // the next attach; its memory stays mapped on the compositor's side.
    if (slot.buffer != nullptr) wl_buffer_destroy(slot.buffer);
    if (slot.pixels != nullptr) munmap(slot.pixels, slot.bytes);
    slot = Slot{};
  }
  count_ = 0U;
}

presentation::DirectAcquire WaylandShmPath::acquire(presentation::PresentExtent extent) {
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
  written_ = chosen;
  writing_ = true;
  const Slot &slot = slots_[chosen];
  presentation::DirectBuffer buffer{};
  buffer.pixels = slot.pixels;
  buffer.row_bytes = static_cast<std::size_t>(extent.width) * 4U;
  buffer.extent = extent;
  buffer.age = slot.shown_at == 0U ? 0U : static_cast<std::uint32_t>(presents_ - slot.shown_at + 1U);
  return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome WaylandShmPath::present(std::span<const presentation::PresentRect> damage) {
  if (!writing_) return presentation::PresentOutcome::failed;
  writing_ = false;
  Slot &slot = slots_[written_];
  wl_surface_attach(surface_, slot.buffer, 0, 0);
  for (std::size_t index = 0U; index < damage.size(); ++index) {
    const presentation::PresentRect rect = damage[index];
    if (rect.empty()) continue;
    wl_surface_damage_buffer(surface_, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
  }
  wl_surface_commit(surface_);
  slot.held = true;
  ++presents_;
  slot.shown_at = presents_;
  // EAGAIN leaves the requests queued for the next flush; any other error
  // is a broken connection.
  if (wl_display_flush(display_) < 0 && errno != EAGAIN) return presentation::PresentOutcome::lost;
  return presentation::PresentOutcome::presented;
}

void WaylandShmPath::release() noexcept { free_slots(); }

} // namespace gui_forms::host::wayland_detail
