#include "x11_dri3_present.hpp"
#include "x11_present_direct.hpp"
#include "../services/x11_foreign_window.hpp"

#include <X11/Xlib-xcb.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xpresent.h>
#include <xcb/dri3.h>
#include <xcb/xcb.h>

#include <cstdlib>
#include <unistd.h>

namespace gui_forms::host::linux_detail {

namespace {

// The largest buffer made: the copy path's presentation limit (64 MiB).
constexpr std::size_t maximum_buffer_bytes = 64U * 1024U * 1024U;

// The server's DRM device for `window`'s screen (DRI3Open, provider 0),
// as a descriptor the caller owns; -1 when DRI3 is missing or refuses.
int open_server_device(xcb_connection_t *connection, ::Window window) {
  xcb_dri3_query_version_cookie_t version_cookie = xcb_dri3_query_version(connection, 1U, 0U);
  xcb_dri3_query_version_reply_t *version = xcb_dri3_query_version_reply(connection, version_cookie, nullptr);
  if (version == nullptr) return -1;
  const bool supported = (*version).major_version >= 1U;
  std::free(version);
  if (!supported) return -1;
  xcb_dri3_open_cookie_t open_cookie = xcb_dri3_open(connection, static_cast<xcb_drawable_t>(window), 0U);
  xcb_dri3_open_reply_t *opened = xcb_dri3_open_reply(connection, open_cookie, nullptr);
  if (opened == nullptr) return -1;
  int descriptor = -1;
  if ((*opened).nfd == 1U) descriptor = xcb_dri3_open_reply_fds(connection, opened)[0];
  std::free(opened);
  return descriptor;
}

} // namespace

X11Dri3Path::X11Dri3Path(Display *display, ::Window window, Visual *visual, int depth) noexcept
    : display_(display), window_(window), visual_(visual), depth_(depth) {}

X11Dri3Path::~X11Dri3Path() {
  free_slots();
  if (selection_ != 0U) XPresentFreeInput(display_, window_, selection_);
  selection_ = 0U;
  gbm_device_close(device_);
}

bool X11Dri3Path::usable() {
  if (checked_ != Check::unchecked) return checked_ == Check::usable;
  checked_ = Check::unsupported;
  if (x11_present_opcode(display_) < 0) return false;
  if (!x11_raster_layout(display_, visual_, depth_)) return false;
  xcb_connection_t *const connection = XGetXCBConnection(display_);
  if (connection == nullptr) return false;
  const int descriptor = open_server_device(connection, window_);
  if (!gbm_device_open(descriptor, device_)) return false;
  opcodes_[0] = x11_extension_opcode(display_, "DRI3");
  opcodes_[1] = x11_extension_opcode(display_, "Present");
  opcodes_[2] = x11_extension_opcode(display_, "XFIXES");
  selection_ = XPresentSelectInput(display_, window_, PresentIdleNotifyMask);
  checked_ = Check::usable;
  return true;
}

bool X11Dri3Path::make_slot(Dri3Slot &slot, presentation::PresentExtent extent) {
  const std::size_t bytes = static_cast<std::size_t>(extent.width) * 4U * static_cast<std::size_t>(extent.height);
  if (bytes == 0U || bytes > maximum_buffer_bytes) return false;
  // DRI3PixmapFromBuffer carries 16-bit sizes and stride.
  if (extent.width > 0xFFFFU || extent.height > 0xFFFFU) return false;
  GbmBuffer buffer{};
  if (!gbm_buffer_make(device_, extent, depth_ == 32, buffer)) return false;
  const std::size_t size = static_cast<std::size_t>(buffer.stride) * static_cast<std::size_t>(extent.height);
  if (buffer.stride > 0xFFFFU || size > 0xFFFFFFFFU) {
    gbm_buffer_free(buffer);
    return false;
  }
  const int descriptor = gbm_buffer_export(buffer);
  if (descriptor < 0) {
    gbm_buffer_free(buffer);
    return false;
  }
  xcb_connection_t *const connection = XGetXCBConnection(display_);
  const xcb_pixmap_t pixmap = xcb_generate_id(connection);
  // The request takes the descriptor (xcb closes it once sent); checked,
  // so a refusal is seen here.
  const xcb_void_cookie_t cookie = xcb_dri3_pixmap_from_buffer_checked(
      connection, pixmap, static_cast<xcb_drawable_t>(window_), static_cast<std::uint32_t>(size),
      static_cast<std::uint16_t>(extent.width), static_cast<std::uint16_t>(extent.height),
      static_cast<std::uint16_t>(buffer.stride), static_cast<std::uint8_t>(depth_), 32U, descriptor);
  xcb_generic_error_t *const error = xcb_request_check(connection, cookie);
  if (error != nullptr) {
    std::free(error);
    gbm_buffer_free(buffer);
    return false;
  }
  slot.buffer = buffer;
  slot.pixmap = static_cast<Pixmap>(pixmap);
  slot.reading = 0U;
  slot.shown_at = 0U;
  return true;
}

void X11Dri3Path::free_slot(Dri3Slot &slot) noexcept {
  if (slot.pixmap != 0U) {
    // After a loss the server may already have dropped the pixmap.
    const ForeignWindowScope scope(display_, window_, std::span<const int>(opcodes_));
    XFreePixmap(display_, slot.pixmap);
  }
  // The server keeps its own reference to the dmabuf while it reads it.
  gbm_buffer_free(slot.buffer);
  slot = Dri3Slot{};
}

void X11Dri3Path::free_slots() noexcept {
  holding_ = false;
  for (std::size_t index = 0U; index < count_; ++index) free_slot(slots_[index]);
  count_ = 0U;
  XFlush(display_);
}

presentation::DirectAcquire X11Dri3Path::acquire(presentation::PresentExtent extent) {
  if (!usable()) return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
  if (depth_ == 32 && !present_alpha)
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
  if (xcb_connection_has_error(XGetXCBConnection(display_)) != 0)
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
  if (holding_) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
  if (extent != extent_) {
    free_slots();
    extent_ = extent;
  }
  std::size_t chosen = count_;
  for (std::size_t index = 0U; index < count_; ++index) {
    if (slots_[index].reading != 0U) continue;
    chosen = index;
    break;
  }
  if (chosen == count_) {
    if (count_ == maximum_slots) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    if (!make_slot(slots_[count_], extent))
      return presentation::DirectAcquire::refused(presentation::DirectRefusal::resources);
    ++count_;
  }
  Dri3Slot &slot = slots_[chosen];
  if (!gbm_buffer_map(slot.buffer, extent))
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
  held_ = chosen;
  holding_ = true;
  presentation::DirectBuffer buffer{};
  buffer.pixels = slot.buffer.pixels;
  buffer.row_bytes = slot.buffer.mapped_stride;
  buffer.extent = extent;
  // The presents since this buffer was shown, and what GBM lets the route
  // claim of its contents: unknown, so the shim writes the whole frame.
  const std::uint32_t since =
      slot.shown_at == 0U ? 0U : static_cast<std::uint32_t>(presents_ - slot.shown_at + 1U);
  buffer.age = gbm_buffer_known_age(slot.buffer, since);
  return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome X11Dri3Path::present(std::span<const presentation::PresentRect> damage) {
  if (!holding_) return presentation::PresentOutcome::failed;
  holding_ = false;
  Dri3Slot &slot = slots_[held_];
  gbm_buffer_unmap(slot.buffer);
  rectangles_.clear();
  for (std::size_t index = 0U; index < damage.size(); ++index) {
    const presentation::PresentRect rect = damage[index];
    if (rect.empty()) continue;
    XRectangle native{};
    native.x = static_cast<short>(rect.left);
    native.y = static_cast<short>(rect.top);
    native.width = static_cast<unsigned short>(rect.right - rect.left);
    native.height = static_cast<unsigned short>(rect.bottom - rect.top);
    rectangles_.push_back(native);
  }
  ++serial_;
  if (serial_ == 0U) ++serial_;
  // As the Present path: the present's requests inside a scope that absorbs
  // their errors and waits for the server's answer; a rejection is lost.
  bool rejected = false;
  {
    ForeignWindowScope scope(display_, window_, std::span<const int>(opcodes_));
    XserverRegion update =
        XFixesCreateRegion(display_, rectangles_.data(), static_cast<int>(rectangles_.size()));
    XPresentPixmap(display_, window_, slot.pixmap, serial_, None, update, 0, 0, None, None, None,
                   PresentOptionNone, 0U, 0U, 0U, nullptr, 0);
    XFixesDestroyRegion(display_, update);
    rejected = scope.synchronize();
  }
  if (rejected) return presentation::PresentOutcome::lost;
  if (xcb_connection_has_error(XGetXCBConnection(display_)) != 0) return presentation::PresentOutcome::lost;
  slot.reading = serial_;
  ++presents_;
  slot.shown_at = presents_;
  return presentation::PresentOutcome::presented;
}

void X11Dri3Path::release() noexcept { free_slots(); }

void X11Dri3Path::idle(Pixmap pixmap, std::uint32_t serial) noexcept {
  for (std::size_t index = 0U; index < count_; ++index) {
    Dri3Slot &slot = slots_[index];
    if (slot.pixmap != pixmap) continue;
    if (slot.reading == serial) slot.reading = 0U;
    return;
  }
}

} // namespace gui_forms::host::linux_detail
