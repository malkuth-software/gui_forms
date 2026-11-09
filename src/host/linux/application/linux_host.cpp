#include "linux_host_internal.hpp"
#include "../../../core/host/frame_capture/frame_capture.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../core/application/splashscreen_surface.hpp"
#include "../../posix/fonts/sealed_pages_posix.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include <thread>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shapeconst.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <clocale>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <unistd.h>

namespace gui_forms::host::linux_detail {
namespace {
struct WindowTitle final {
  std::weak_ptr<NativeWindow> owner{};
  const std::thread::id thread{std::this_thread::get_id()};
  HostServiceStatus operator()(const std::string_view text) const {
    if (thread != std::this_thread::get_id()) return {HostServiceError::wrong_thread};
    const std::shared_ptr<NativeWindow> window = owner.lock();
    if (!window || (*window).closed) return {HostServiceError::after_shutdown};
    try {
      if (text.size() > 65536U || text.find('\0') != std::string_view::npos ||
          !validate_utf8(text).valid()) return {HostServiceError::invalid_argument};
      const std::string title(text);
      NativeWindow& native = *window;
      if (native.accessibility) (*native.accessibility).set_title(title);
      XStoreName(native.runtime.display, native.xid, title.c_str());
      XChangeProperty(native.runtime.display, native.xid, native.runtime.atom("_NET_WM_NAME"),
          native.runtime.atom("UTF8_STRING"), 8, PropModeReplace,
          reinterpret_cast<const unsigned char*>(title.data()), static_cast<int>(title.size()));
      XFlush(native.runtime.display);
      return {};
    } catch (...) { return {HostServiceError::backend_failure}; }
  }
};
// True when the window manager has minimized the window
// (_NET_WM_STATE_HIDDEN).
bool window_state_hidden(NativeWindow& window) {
  Atom type{};
  int format{};
  unsigned long count{}, remaining{};
  unsigned char* data = nullptr;
  bool hidden = false;
  if (XGetWindowProperty(window.runtime.display, window.xid, window.runtime.atom("_NET_WM_STATE"),
                         0, 64, False, XA_ATOM, &type, &format, &count, &remaining,
                         &data) == Success && type == XA_ATOM && format == 32) {
    const Atom hidden_atom = window.runtime.atom("_NET_WM_STATE_HIDDEN");
    const Atom* atoms = reinterpret_cast<const Atom*>(data);
    for (unsigned long index = 0; index < count; ++index) {
      if (atoms[index] == hidden_atom) hidden = true;
    }
  }
  if (data != nullptr) XFree(data);
  return hidden;
}
// True when the window manager holds the window full screen or maximized
// (_NET_WM_STATE); a size request would fight it.
// True when the window manager holds the window full screen
// (_NET_WM_STATE_FULLSCREEN).
bool window_state_full_screen(NativeWindow& window) {
  Atom type{};
  int format{};
  unsigned long count{}, remaining{};
  unsigned char* data = nullptr;
  bool full_screen = false;
  if (XGetWindowProperty(window.runtime.display, window.xid, window.runtime.atom("_NET_WM_STATE"),
                         0, 64, False, XA_ATOM, &type, &format, &count, &remaining,
                         &data) == Success && type == XA_ATOM && format == 32) {
    const Atom fullscreen = window.runtime.atom("_NET_WM_STATE_FULLSCREEN");
    const Atom* atoms = reinterpret_cast<const Atom*>(data);
    for (unsigned long index = 0; index < count; ++index) {
      if (atoms[index] == fullscreen) full_screen = true;
    }
  }
  if (data != nullptr) XFree(data);
  return full_screen;
}
bool window_state_fixed(NativeWindow& window) {
  Atom type{};
  int format{};
  unsigned long count{}, remaining{};
  unsigned char* data = nullptr;
  bool fixed = false;
  if (XGetWindowProperty(window.runtime.display, window.xid, window.runtime.atom("_NET_WM_STATE"),
                         0, 64, False, XA_ATOM, &type, &format, &count, &remaining,
                         &data) == Success && type == XA_ATOM && format == 32) {
    const Atom fullscreen = window.runtime.atom("_NET_WM_STATE_FULLSCREEN");
    const Atom maximized_vertical = window.runtime.atom("_NET_WM_STATE_MAXIMIZED_VERT");
    const Atom maximized_horizontal = window.runtime.atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    const Atom hidden = window.runtime.atom("_NET_WM_STATE_HIDDEN");
    const Atom* atoms = reinterpret_cast<const Atom*>(data);
    for (unsigned long index = 0; index < count; ++index) {
      if (atoms[index] == fullscreen || atoms[index] == maximized_vertical ||
          atoms[index] == maximized_horizontal || atoms[index] == hidden) fixed = true;
    }
  }
  if (data != nullptr) XFree(data);
  return fixed;
}
// Asks for a client area of `client_size` logical units (device pixels; the
// X11 host runs at scale 1). The server or window manager answers with
// ConfigureNotify, which resizes the model on a later event.
struct WindowClientSize final {
  std::weak_ptr<NativeWindow> owner{};
  const std::thread::id thread{std::this_thread::get_id()};
  HostServiceStatus operator()(const Size client_size) const {
    if (thread != std::this_thread::get_id()) return {HostServiceError::wrong_thread};
    const std::shared_ptr<NativeWindow> window = owner.lock();
    if (!window || (*window).closed) return {HostServiceError::after_shutdown};
    try {
      NativeWindow& native = *window;
      if (!std::isfinite(client_size.width) || !std::isfinite(client_size.height) ||
          client_size.width > 32768.0 || client_size.height > 32768.0 ||
          client_size.width < std::max(1.0, native.entry.options.minimum_size.width) ||
          client_size.height < std::max(1.0, native.entry.options.minimum_size.height))
        return {HostServiceError::invalid_argument};
      if (native.fallback_fullscreen || window_state_fixed(native))
        return {HostServiceError::unsupported};
      XResizeWindow(native.runtime.display, native.xid,
                    static_cast<unsigned>(std::lround(client_size.width)),
                    static_cast<unsigned>(std::lround(client_size.height)));
      XFlush(native.runtime.display);
      return {};
    } catch (...) { return {HostServiceError::backend_failure}; }
  }
};
struct PresentationImage final {
  XImage* value{};
  PresentationImage() = default;
  ~PresentationImage() {
    if (value != nullptr) {
      // Pixel storage is borrowed from NativeWindow::presentation, not Xlib.
      (*value).data = nullptr;
      XDestroyImage(value);
    }
  }
  PresentationImage(const PresentationImage&) = delete;
  PresentationImage& operator=(const PresentationImage&) = delete;
};

struct NativeChannel final {
  unsigned long mask{0};
  unsigned long maximum{0};
  unsigned shift{0};
};
// The channels in the raster's byte order (premultiplied BGRA8): blue,
// green, red, then alpha.
// Alpha's mask is 0 (and the channel contributes nothing) unless the
// window has the ARGB visual (stage T5, C4), whose pixel carries the
// raster's premultiplied alpha unchanged.
inline constexpr std::size_t native_alpha_channel = 3U;
struct NativePixels final {
  const unsigned char* source{};
  std::size_t source_stride{0};
  std::size_t destination_stride{0};
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
  using Channels = std::array<NativeChannel, 4>;
  Channels channels{};
  // The alpha bits set in every pixel: the ARGB window's alpha mask while
  // its grant is opaque (alpha 255), else 0.
  std::uint32_t opaque_bits{0};
};

unsigned long pack_native_pixel(const unsigned char* const pixel,
                                 const std::array<NativeChannel, 4>& channels) {
  unsigned long value = 0;
  for (std::size_t channel = 0; channel < channels.size(); ++channel) {
    const NativeChannel component = channels[channel];
    if (component.mask == 0)
      continue;
    const std::uint64_t intensity = static_cast<std::uint64_t>(pixel[channel]);
    const std::uint64_t scaled_value = (intensity * static_cast<std::uint64_t>(component.maximum) + 127U) / 255U;
    const unsigned long scaled = static_cast<unsigned long>(scaled_value);
    value |= (scaled << component.shift) & component.mask;
  }
  return value;
}
void convert_native_pixels32(XImage& image, const NativePixels& pixels) {
  for (int row = pixels.top; row < pixels.bottom; ++row) {
    const std::size_t source_row = static_cast<std::size_t>(row) * pixels.source_stride;
    const std::size_t destination_row = static_cast<std::size_t>(row) * pixels.destination_stride;
    for (int column = pixels.left; column < pixels.right; ++column) {
      const std::size_t offset = static_cast<std::size_t>(column) * 4U;
      const unsigned long value = pack_native_pixel(pixels.source + source_row + offset, pixels.channels);
      const std::uint32_t packed = static_cast<std::uint32_t>(value) | pixels.opaque_bits;
      std::memcpy(image.data + destination_row + offset, &packed, sizeof(packed));
    }
  }
}
// The common visual: 32-bit pixels in native byte order with blue, green and
// red in the low three bytes (Xvfb and most TrueColor servers). That is the
// raster's own layout, so each row is copied as it is: a depth-24 window
// ignores the fourth byte, and the ARGB window presenting alpha takes the
// raster's premultiplied alpha there.
void copy_native_pixels32_bgra(XImage& image, const NativePixels& pixels) {
  const std::size_t row_length = static_cast<std::size_t>(pixels.right - pixels.left) * 4U;
  for (int row = pixels.top; row < pixels.bottom; ++row) {
    const unsigned char* const source = pixels.source + static_cast<std::size_t>(row) * pixels.source_stride +
                                        static_cast<std::size_t>(pixels.left) * 4U;
    unsigned char* const destination = reinterpret_cast<unsigned char*>(image.data) +
                                       static_cast<std::size_t>(row) * pixels.destination_stride +
                                       static_cast<std::size_t>(pixels.left) * 4U;
    std::memcpy(destination, source, row_length);
  }
}
// The fallback for other 32-bit visuals in native byte order with 8-bit
// channels: each channel is a byte moved into place; no scaling. Without an
// alpha channel the alpha term is masked to 0.
void convert_native_pixels32_bytes(XImage& image, const NativePixels& pixels) {
  const unsigned blue = pixels.channels[0].shift;
  const unsigned green = pixels.channels[1].shift;
  const unsigned red = pixels.channels[2].shift;
  const NativeChannel alpha_channel = pixels.channels[native_alpha_channel];
  const unsigned alpha = alpha_channel.shift;
  const std::uint32_t alpha_keep = alpha_channel.mask != 0 ? 0xFFFFFFFFU : 0U;
  for (int row = pixels.top; row < pixels.bottom; ++row) {
    const unsigned char* source = pixels.source + static_cast<std::size_t>(row) * pixels.source_stride +
                                  static_cast<std::size_t>(pixels.left) * 4U;
    unsigned char* destination = reinterpret_cast<unsigned char*>(image.data) +
                                 static_cast<std::size_t>(row) * pixels.destination_stride +
                                 static_cast<std::size_t>(pixels.left) * 4U;
    for (int column = pixels.left; column < pixels.right; ++column) {
      const std::uint32_t packed = (static_cast<std::uint32_t>(source[0]) << blue) |
                                   (static_cast<std::uint32_t>(source[1]) << green) |
                                   (static_cast<std::uint32_t>(source[2]) << red) |
                                   ((static_cast<std::uint32_t>(source[3]) << alpha) & alpha_keep) |
                                   pixels.opaque_bits;
      std::memcpy(destination, &packed, sizeof(packed));
      source += 4;
      destination += 4;
    }
  }
}
void convert_native_pixels_generic(XImage& image, const NativePixels& pixels) {
  for (int row = pixels.top; row < pixels.bottom; ++row) {
    const std::size_t source_row = static_cast<std::size_t>(row) * pixels.source_stride;
    for (int column = pixels.left; column < pixels.right; ++column) {
      const std::size_t offset = static_cast<std::size_t>(column) * 4U;
      const unsigned long value = pack_native_pixel(pixels.source + source_row + offset, pixels.channels);
      XPutPixel(&image, column, row, value | pixels.opaque_bits);
    }
  }
}
} // namespace

std::optional<NativePresentKernel> convert_present_rectangle(XImage& image, const NativePresentSource& source,
                                                             const int left, const int top, const int right,
                                                             const int bottom) {
  NativePixels pixels{};
  pixels.source = source.pixels;
  pixels.source_stride = source.row_bytes;
  pixels.destination_stride = static_cast<std::size_t>(image.bytes_per_line);
  pixels.left = left;
  pixels.top = top;
  pixels.right = right;
  pixels.bottom = bottom;
  // The raster's alpha is presented only while a non-opaque backdrop is
  // granted; an ARGB window under an opaque grant presents alpha 255.
  const unsigned long presented_alpha_mask = source.present_alpha ? source.alpha_mask : 0UL;
  const std::array<unsigned long, 4> masks{image.blue_mask, image.green_mask, image.red_mask, presented_alpha_mask};
  if (source.alpha_mask > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
  pixels.opaque_bits = source.present_alpha ? 0U : static_cast<std::uint32_t>(source.alpha_mask);
  for (std::size_t channel = 0; channel < masks.size(); ++channel) {
    if (masks[channel] > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    pixels.channels[channel].mask = masks[channel];
    if (masks[channel] != 0) {
      pixels.channels[channel].shift = static_cast<unsigned>(std::countr_zero(masks[channel]));
      pixels.channels[channel].maximum = masks[channel] >> pixels.channels[channel].shift;
    }
  }
  const bool native32 = image.bits_per_pixel == 32 &&
      (image.byte_order == LSBFirst) == (std::endian::native == std::endian::little);
  bool byte_channels = native32;
  for (std::size_t channel = 0; channel < pixels.channels.size(); ++channel) {
    // An absent alpha channel (mask 0) does not stop the byte path.
    if (channel == native_alpha_channel && pixels.channels[channel].mask == 0) continue;
    if (pixels.channels[channel].maximum != 255U || pixels.channels[channel].shift % 8U != 0U ||
        pixels.channels[channel].shift > 24U) byte_channels = false;
  }
  // The raster's own layout: blue, green, red at bytes 0, 1, 2, and alpha
  // either at byte 3 (presented) or absent, with no opaque bits to set.
  const NativePixels::Channels& channels = pixels.channels;
  const bool alpha_as_is = channels[native_alpha_channel].mask == 0
      ? pixels.opaque_bits == 0U
      : channels[native_alpha_channel].shift == 24U;
  if (byte_channels && channels[0].shift == 0U && channels[1].shift == 8U && channels[2].shift == 16U &&
      alpha_as_is) {
    copy_native_pixels32_bgra(image, pixels);
    return NativePresentKernel::plain_copy;
  }
  if (byte_channels) {
    convert_native_pixels32_bytes(image, pixels);
    return NativePresentKernel::byte_channels;
  }
  if (native32) {
    convert_native_pixels32(image, pixels);
    return NativePresentKernel::scaled32;
  }
  convert_native_pixels_generic(image, pixels);
  return NativePresentKernel::generic;
}

namespace {

// Copies each rectangle of the raster's pixels to the window (stage DP partial
// present): one native image for the raster, converted and submitted rectangle
// by rectangle. Rectangles are clipped to the window and widened to whole
// pixels. Returns false, copying nothing, when the raster or the native image
// cannot be used; `copied` then stays empty. Source and native conversion
// storage are disjoint, and both borrows end before the renderer can begin
// another frame.
bool present_raster_rectangles(NativeWindow& window, const std::span<const Rect> requested,
                               PresentedArea& copied) {
  render::SkiaRaster& raster = window.raster;
  const std::uint32_t width = raster.pixel_width();
  const std::uint32_t height = raster.pixel_height();
  const unsigned char* const source = static_cast<const unsigned char*>(raster.pixels());
  if (source == nullptr || width == 0 || height == 0 || width > 16384 || height > 16384) return false;
  if (!std::isfinite(window.size.width) || !std::isfinite(window.size.height) ||
      window.size.width <= 0.0 || window.size.height <= 0.0) return false;
  const double visible_width = std::min(static_cast<double>(width), window.size.width);
  const double visible_height = std::min(static_cast<double>(height), window.size.height);
  const Rect extent{0.0, 0.0, visible_width, visible_height};
  NativePresentSource present{};
  present.pixels = source;
  present.row_bytes = raster.row_bytes();
  present.alpha_mask = window.alpha_mask;
  present.present_alpha = window.present_alpha;
  PresentationImage owner{};
  Display* const display = window.runtime.display;
  // The window's own visual and depth: the ARGB visual's for a window
  // made with it (stage T5, C4).
  owner.value = XCreateImage(display, window.visual, static_cast<unsigned>(window.depth), ZPixmap, 0,
      nullptr, width, height, 32, 0);
  if (owner.value == nullptr) return false;
  XImage& image = *owner.value;
  const std::size_t maximum_row = static_cast<std::size_t>(width) * 4U;
  if (image.bytes_per_line <= 0 || image.bits_per_pixel <= 0 || image.bits_per_pixel > 32 ||
      static_cast<std::size_t>(image.bytes_per_line) > maximum_row || present.row_bytes < maximum_row) return false;
  const std::size_t bytes = static_cast<std::size_t>(image.bytes_per_line) * static_cast<std::size_t>(height);
  if (bytes > 64U * 1024U * 1024U) return false;
  window.presentation.resize(bytes);
  image.data = window.presentation.data();
  // The copy alone is timed: conversion, XPutImage and the flush.
  const std::chrono::steady_clock::time_point copy_started = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < requested.size(); ++index) {
    const Rect rectangle = requested[index];
    if (!rectangle.finite()) continue;
    const Rect clipped = Rect::intersection(rectangle, extent);
    if (clipped.empty()) continue;
    const int left = static_cast<int>(std::floor(clipped.x));
    const int top = static_cast<int>(std::floor(clipped.y));
    const int right = static_cast<int>(std::ceil(clipped.right()));
    const int bottom = static_cast<int>(std::ceil(clipped.bottom()));
    // Fails only for masks wider than 32 bits, so before any rectangle.
    if (!convert_present_rectangle(image, present, left, top, right, bottom)) return false;
    const unsigned columns = static_cast<unsigned>(right - left);
    const unsigned rows = static_cast<unsigned>(bottom - top);
    XPutImage(display, window.xid, window.gc, &image, left, top, left, top, columns, rows);
    copied.device_pixels += static_cast<std::uint64_t>(columns) * static_cast<std::uint64_t>(rows);
    ++copied.rectangles;
  }
  XFlush(display);
  copied.copy_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - copy_started).count());
  // Xlib submission is not a server-error/physical-display atomicity guarantee.
  return true;
}

#if defined(GUI_FORMS_PREPARED_TEXT)
// Returns true when a frame was presented and its damage acknowledged.
bool update_prepared_window(NativeWindow& window) {
  render::SkiaRaster& raster = window.raster;
  const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
  std::optional<PaintReceipt> committed{};
  try {
    const bool images_ready = raster.synchronize_images((*window.entry.model).image_resources());
    if (images_ready) {
      const PreparedTextStatus begun = raster.begin_prepared_frame(window.size, 1.0, window.damage);
      if (begun == PreparedTextStatus::success) {
        const std::optional<PaintReceipt> receipt = (*window.entry.model).paint(raster, window.damage.rectangles());
        raster.end_frame();
        if (receipt) {
          const PreparedTextStatus status = raster.commit_prepared_frame(*receipt);
          if (status == PreparedTextStatus::success) committed = *receipt;
          else raster.abort_prepared_frame();
        } else raster.abort_prepared_frame();
      } else raster.abort_prepared_frame();
    } else raster.abort_prepared_frame();
  } catch (...) {
    raster.abort_prepared_frame();
    // Keep host damage and the coherent front for exposure/recovery. A failed
    // replay cannot terminate the event loop or claim a presentation receipt.
  }
  const PaintReceipt front = raster.prepared_front_receipt();
  if (!front || raster.prepared_front_scale() != 1.0) return false;
  bool presented = false;
  PresentedArea copied;
  try { presented = present_raster_rectangles(window, window.damage.rectangles(), copied); }
  catch (...) { return false; }
  if (!presented || !committed) return false;
  const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
  const std::chrono::nanoseconds duration = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started);
  const std::uint64_t elapsed = static_cast<std::uint64_t>(duration.count());
  const bool acknowledged = window.report_presented(committed, elapsed, copied);
  if (acknowledged) window.damage.clear();
  else {
    // An exposure can repaint an already acknowledged revision. Clear only if
    // that exact front is still the current, presented model surface.
    const PaintLeaseSnapshot lease = (*window.entry.model).paint_lease_snapshot();
    if (lease.surface_epoch == front.surface_epoch && lease.content_revision == front.rendered_revision &&
        lease.presented_revision == front.rendered_revision && raster.prepared_front_matches(window.size, 1.0)) window.damage.clear();
  }
  return window.damage.empty();
}
#endif
std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}
Modifier modifiers(unsigned state) {
  Modifier result = Modifier::none;
  if ((state & ShiftMask) != 0U)
    result = result | Modifier::shift;
  if ((state & ControlMask) != 0U)
    result = result | Modifier::control;
  if ((state & Mod1Mask) != 0U)
    result = result | Modifier::alt;
  if ((state & Mod4Mask) != 0U)
    result = result | Modifier::meta;
  return result;
}
std::uint32_t key_usage(KeySym key) {
  if (key >= XK_a && key <= XK_z)
    return PhysicalKey::a + static_cast<std::uint32_t>(key - XK_a);
  if (key >= XK_1 && key <= XK_9)
    return 0x1EU + static_cast<std::uint32_t>(key - XK_1);
  if (key >= XK_F1 && key <= XK_F12)
    return PhysicalKey::f1 + static_cast<std::uint32_t>(key - XK_F1);
  switch (key) {
  case XK_0:
    return 0x27U;
  case XK_Return:
  case XK_KP_Enter:
    return PhysicalKey::enter;
  case XK_Escape:
    return PhysicalKey::escape;
  case XK_Menu:
    return PhysicalKey::context_menu;
  case XK_BackSpace:
    return PhysicalKey::backspace;
  case XK_Tab:
  case XK_ISO_Left_Tab:
    return PhysicalKey::tab;
  case XK_space:
    return PhysicalKey::space;
  case XK_minus:
    return PhysicalKey::minus;
  case XK_equal:
    return PhysicalKey::equal;
  case XK_Insert:
    return PhysicalKey::insert;
  case XK_Home:
    return PhysicalKey::home;
  case XK_End:
    return PhysicalKey::end;
  case XK_Page_Up:
    return PhysicalKey::page_up;
  case XK_Page_Down:
    return PhysicalKey::page_down;
  case XK_Delete:
    return PhysicalKey::delete_forward;
  case XK_Right:
    return PhysicalKey::right;
  case XK_Left:
    return PhysicalKey::left;
  case XK_Down:
    return PhysicalKey::down;
  case XK_Up:
    return PhysicalKey::up;
  case XK_KP_Add:
    return PhysicalKey::keypad_plus;
  case XK_KP_Subtract:
    return PhysicalKey::keypad_minus;
  case XK_bracketleft:
    return 0x2FU;
  case XK_bracketright:
    return 0x30U;
  case XK_backslash:
    return 0x31U;
  case XK_semicolon:
    return 0x33U;
  case XK_apostrophe:
    return 0x34U;
  case XK_grave:
    return 0x35U;
  case XK_comma:
    return 0x36U;
  case XK_period:
    return 0x37U;
  case XK_slash:
    return 0x38U;
  default:
    return 0U;
  }
}
std::filesystem::path fonts_directory() {
  const char *override_path = std::getenv("GUI_FORMS_FONT_DIR");
  if (override_path != nullptr)
    return override_path;
  std::error_code error;
  const std::filesystem::path executable =
      std::filesystem::read_symlink("/proc/self/exe", error);
  const std::filesystem::path local = executable.parent_path() / "fonts";
  if (std::filesystem::is_directory(local))
    return local;
  return executable.parent_path() / "../share/GUIForms/fonts";
}
// Every window borrows the process's one bundled font set: each face is read once, checked
// against the manifest and sealed read-only (gui_forms::detail::BundledFontSet), then shared.
void register_fonts(render::SkiaRaster &raster) {
  const std::filesystem::path directory = fonts_directory();
  const std::shared_ptr<gui_forms::detail::BundledFontSet> set =
      gui_forms::detail::shared_bundled_font_set(directory, gui_forms::host::posix_sealed_pages());
  if (!raster.register_bundled_fonts(set))
    throw std::runtime_error("Cannot load the bundled GUI.Forms fonts from " +
                             directory.string());
}
struct Closing final {
  NativeWindow *window;
  void operator()(HostCloseRequest &request) const {
    if ((*window).entry.options.close_request)
      (*window).entry.options.close_request(request);
  }
};
struct Dialog final {
  NativeWindow *window;
  HostDialogResult operator()(const HostDialogRequest &request) const {
    return (*(*window).services).show_dialog(request);
  }
};
struct ReadText final {
  NativeWindow *window;
  HostClipboardTextResult operator()() const {
    return (*(*window).services).read_clipboard_text();
  }
};
struct WriteText final {
  NativeWindow *window;
  HostServiceStatus operator()(std::string_view text) const {
    return (*(*window).services).write_clipboard_text(text);
  }
};
// The session-ending signal handler's state (Runtime::terminate_installed):
// the flag it sets and the wake pipe it writes, both written only while a
// handler is installed. Async-signal-safe: a sig_atomic_t store and write.
volatile std::sig_atomic_t session_end_signalled = 0;
volatile int session_end_wake = -1;
void session_end_signal(int) {
  session_end_signalled = 1;
  const int descriptor = session_end_wake;
  const char byte = 1;
  if (descriptor >= 0)
    static_cast<void>(::write(descriptor, &byte, 1));
}
// Installs session_end_signal for signal_number when its disposition is the
// default; previous receives the action to restore. Returns whether it
// installed.
bool install_session_end_signal(int signal_number, struct sigaction &previous) {
  if (sigaction(signal_number, nullptr, &previous) != 0)
    return false;
  if ((previous.sa_flags & SA_SIGINFO) != 0 || previous.sa_handler != SIG_DFL)
    return false;
  struct sigaction action{};
  action.sa_handler = session_end_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  return sigaction(signal_number, &action, nullptr) == 0;
}
} // namespace
void Wake::operator()() const noexcept {
  const char byte = 1;
  if (descriptor >= 0)
    static_cast<void>(::write(descriptor, &byte, 1));
}
void ActionQueue::post(Action action) {
  {
    const std::lock_guard<std::mutex> guard(mutex);
    pending.push_back(std::move(action));
  }
  wake();
}
void WindowAction::operator()() const {
  const std::shared_ptr<ActionQueue> target = queue.lock();
  if (target)
    (*target).post({window, kind});
}
Runtime::Runtime() {
  std::setlocale(LC_CTYPE, "");
  display = XOpenDisplay(nullptr);
  if (display == nullptr)
    throw std::runtime_error("Cannot open the X11 display");
  if (pipe(wake_pipe) != 0) {
    XCloseDisplay(display);
    display = nullptr;
    throw std::runtime_error("Cannot create host wake pipe");
  }
  for (unsigned i = 0; i < 2; ++i) {
    fcntl(wake_pipe[i], F_SETFL, fcntl(wake_pipe[i], F_GETFL) | O_NONBLOCK);
    fcntl(wake_pipe[i], F_SETFD, FD_CLOEXEC);
  }
  actions = std::make_shared<ActionQueue>();
  (*actions).wake = {wake_pipe[1]};
  XSetLocaleModifiers("");
  input_method = XOpenIM(display, nullptr, nullptr, nullptr);
  Bool supported = False;
  XkbSetDetectableAutoRepeat(display, True, &supported);
  XkbDescPtr keyboard =
      XkbGetKeyboard(display, XkbAllComponentsMask, XkbUseCoreKbd);
  if (keyboard != nullptr && (*keyboard).names != nullptr) {
    const char *rows[] = {"AE", "AD", "AC", "AB"};
    const std::uint32_t usages[][13] = {
        {0, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d,
         0x2e},
        {0, 0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f,
         0x30},
        {0, 0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34,
         0},
        {0, 0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0, 0}};
    for (unsigned code = (*keyboard).min_key_code;
         code <= (*keyboard).max_key_code; ++code) {
      const char *name = (*(*keyboard).names).keys[code].name;
      for (unsigned row = 0; row < 4; ++row) {
        if (name[0] != rows[row][0] || name[1] != rows[row][1] ||
            name[2] < '0' || name[2] > '1' || name[3] < '0' || name[3] > '9')
          continue;
        const unsigned column =
            static_cast<unsigned>((name[2] - '0') * 10 + name[3] - '0');
        if (column < 13)
          physical_keys[code] = usages[row][column];
      }
    }
  }
  if (keyboard != nullptr)
    XkbFreeKeyboard(keyboard, XkbAllComponentsMask, True);
  compositor = std::make_unique<X11CompositorWatch>(display, DefaultScreen(display));
  input_settings = std::make_unique<XSettingsWatch>(display, DefaultScreen(display));
  if (session_end_wake < 0) {
    session_end_signalled = 0;
    session_end_wake = wake_pipe[1];
    terminate_installed = install_session_end_signal(SIGTERM, previous_terminate);
    hangup_installed = install_session_end_signal(SIGHUP, previous_hangup);
    if (!terminate_installed && !hangup_installed)
      session_end_wake = -1;
  }
}
Runtime::~Runtime() {
  // Restored before the wake pipe closes, so the handler never writes to a
  // closed (or reused) descriptor.
  if (terminate_installed)
    sigaction(SIGTERM, &previous_terminate, nullptr);
  if (hangup_installed)
    sigaction(SIGHUP, &previous_hangup, nullptr);
  if (terminate_installed || hangup_installed)
    session_end_wake = -1;
  for (std::size_t i = 0; i < windows.size(); ++i)
    (*windows[i]).close(HostCloseReason::application, true);
  windows.clear();
  compositor.reset();
  input_settings.reset();
  actions.reset();
  if (input_method != nullptr)
    XCloseIM(input_method);
  if (display != nullptr)
    XCloseDisplay(display);
  for (unsigned i = 0; i < 2; ++i)
    if (wake_pipe[i] >= 0)
      ::close(wake_pipe[i]);
}
Atom Runtime::atom(const char *name) const {
  return XInternAtom(display, name, False);
}
double Runtime::double_click_distance() const noexcept {
  if (!input_settings)
    return default_double_click_distance;
  return (*input_settings).double_click_distance();
}
std::uint32_t Runtime::double_click_milliseconds() const noexcept {
  if (!input_settings)
    return default_double_click_milliseconds;
  const std::uint32_t interval = (*input_settings).double_click_milliseconds();
  return interval;
}
void Runtime::report_input_settings() {
  if (!input_settings)
    return;
  const HostInputSettingsEvent input{(*input_settings).double_click_milliseconds(),
                                     (*input_settings).double_click_origin(),
                                     (*input_settings).pointer_settings()};
  const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
  for (std::size_t i = 0; i < snapshot.size(); ++i)
    if ((*snapshot[i]).session)
      static_cast<void>((*snapshot[i]).dispatch(input));
}
std::shared_ptr<NativeWindow> Runtime::add(LinuxApplicationWindow entry,
                                           ::Window owner,
                                           HostWindowId window_id,
                                           bool initial) {
  const std::shared_ptr<NativeWindow> window =
      std::make_shared<NativeWindow>(*this, std::move(entry));
  (*window).window_id = window_id;
  (*window).initial = initial;
  windows.push_back(window);
  if (owner != 0)
    XSetTransientForHint(display, (*window).xid, owner);
  (*window).attach();
  return window;
}
void Runtime::report_activation() {
  if (!windowing)
    return;
  bool any = false;
  for (std::size_t i = 0; i < windows.size(); ++i)
    if (!(*windows[i]).closed && (*windows[i]).focused)
      any = true;
  if (any)
    ever_focused = true;
  if (ever_focused)
    (*windowing).report_application_active(any);
}
void Runtime::reclaim() {
  for (std::size_t i = windows.size(); i > 0; --i) {
    const NativeWindow &window = *windows[i - 1];
    if (window.initial || !window.closed)
      continue;
    windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(i - 1));
  }
}
NativeWindow::NativeWindow(Runtime &host, LinuxApplicationWindow source)
    : runtime(host), entry(std::move(source)),
      size(entry.options.capture_size.has_value()
               ? *entry.options.capture_size
               : entry.options.initial_size) {
  const int screen = DefaultScreen(runtime.display);
  size.height =
      std::min(size.height, std::max(64.0, static_cast<double>(DisplayHeight(
                                               runtime.display, screen)) -
                                               48.0));
  size.width =
      std::min(size.width, std::max(64.0, static_cast<double>(DisplayWidth(
                                              runtime.display, screen)) -
                                              16.0));
  visual = DefaultVisual(runtime.display, screen);
  depth = DefaultDepth(runtime.display, screen);
  const X11ArgbVisual &argb_visual = (*runtime.compositor).argb();
  // A window declared non-opaque gets the ARGB visual, which is fixed at
  // creation (PLAN.md §1.3): its own colormap, and border and background
  // pixels 0, without which the server answers BadMatch. A popup or Splash
  // takes it only while a compositing manager runs; without one it is
  // opaque on the default visual, made override-redirect by mark_transient
  // (owner, 2026-11-08: Q-S2 (b)).
  const bool translucent_transient =
      !entry.popup.has_value() || (*runtime.compositor).compositor_present();
  if ((*entry.model).non_opaque_declared() && argb_visual.found && translucent_transient) {
    colormap = XCreateColormap(runtime.display, RootWindow(runtime.display, screen),
                               argb_visual.visual, AllocNone);
    XSetWindowAttributes attributes{};
    attributes.colormap = colormap;
    attributes.border_pixel = 0UL;
    attributes.background_pixel = 0UL;
    // A popup or Splash (SPLASH_STUDY D5 (b)) is placed and stacked by the
    // program, never decorated or moved by a window manager.
    attributes.override_redirect = entry.popup.has_value() ? True : False;
    xid = XCreateWindow(runtime.display, RootWindow(runtime.display, screen), 60, 60,
                        static_cast<unsigned>(size.width), static_cast<unsigned>(size.height), 0,
                        argb_visual.depth, InputOutput, argb_visual.visual,
                        CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect, &attributes);
    visual = argb_visual.visual;
    depth = argb_visual.depth;
    alpha_mask = argb_visual.alpha_mask;
    argb = true;
  } else {
    xid = XCreateSimpleWindow(runtime.display, RootWindow(runtime.display, screen),
                              60, 60, static_cast<unsigned>(size.width),
                              static_cast<unsigned>(size.height), 0, 0, 0xffffff);
  }
  XSelectInput(runtime.display, xid,
               ExposureMask | StructureNotifyMask | FocusChangeMask |
                   PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                   KeyPressMask | KeyReleaseMask | EnterWindowMask |
                   LeaveWindowMask | PropertyChangeMask);
  XStoreName(runtime.display, xid, entry.options.title.c_str());
  XClassHint identity{};
  identity.res_name = const_cast<char *>(entry.options.title.c_str());
  identity.res_class = const_cast<char *>("GUI.Forms");
  XSetClassHint(runtime.display, xid, &identity);
  const unsigned long process_id = static_cast<unsigned long>(getpid());
  XChangeProperty(runtime.display, xid, runtime.atom("_NET_WM_PID"),
                  XA_CARDINAL, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(&process_id), 1);
  XChangeProperty(
      runtime.display, xid, runtime.atom("_NET_WM_NAME"),
      runtime.atom("UTF8_STRING"), 8, PropModeReplace,
      reinterpret_cast<const unsigned char *>(entry.options.title.data()),
      static_cast<int>(entry.options.title.size()));
  Atom protocols[] = {runtime.atom("WM_DELETE_WINDOW")};
  XSetWMProtocols(runtime.display, xid, protocols, 1);
  const unsigned long xdnd_version = 5;
  XChangeProperty(runtime.display, xid, runtime.atom("XdndAware"), XA_ATOM, 32,
                  PropModeReplace,
                  reinterpret_cast<const unsigned char *>(&xdnd_version), 1);
  XSizeHints hints{};
  hints.flags = PMinSize | PPosition;
  hints.x = 60;
  hints.y = 60;
  hints.min_width =
      static_cast<int>(std::min(entry.options.minimum_size.width, size.width));
  hints.min_height = static_cast<int>(
      std::min(entry.options.minimum_size.height, size.height));
  XSetWMNormalHints(runtime.display, xid, &hints);
  XWMHints wm{};
  wm.flags = InputHint;
  wm.input = True;
  XSetWMHints(runtime.display, xid, &wm);
  if (entry.popup.has_value())
    mark_transient(*entry.popup);
  utility = entry.tool_window || entry.floating != HostWindowFloating::none;
  if (utility) {
    // Set while unmapped, so the window manager reads them when it first
    // manages the window (EWMH: clients set _NET_WM_STATE directly before
    // mapping).
    const Atom type = runtime.atom("_NET_WM_WINDOW_TYPE_UTILITY");
    XChangeProperty(runtime.display, xid, runtime.atom("_NET_WM_WINDOW_TYPE"),
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&type), 1);
    const Atom skip = runtime.atom("_NET_WM_STATE_SKIP_TASKBAR");
    XChangeProperty(runtime.display, xid, runtime.atom("_NET_WM_STATE"),
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&skip), 1);
  }
  gc = XCreateGC(runtime.display, xid, 0, nullptr);
  if (runtime.input_method != nullptr)
    input = XCreateIC(runtime.input_method, XNInputStyle,
                      XIMPreeditNothing | XIMStatusNothing, XNClientWindow, xid,
                      XNFocusWindow, xid, nullptr);
  register_fonts(raster);
  (*entry.model).set_text_metrics_provider(&raster);
  (*entry.model).set_framebuffer_painter(&raster);
  (*entry.model)
      .metrics()
      .set_renderer("Skia CPU / HarfBuzz / FreeType / X11", true);
  services = std::make_unique<Services>(*this);
  // The availability record (stage DP): the X11 host copies each damage
  // rectangle.
  PresentationDeclaration presentation;
  presentation.contract_version = paint_contract_version;
  presentation.extensions.partial_present = true;
  (*entry.model).declare_presentation(presentation);
  session = std::make_unique<HostSession>(*entry.model, linux_capabilities(),
                                          services.get());
  closing_token = (*session).closing().subscribe(Closing{this});
}
// Types a popup or Splash window for the compositing manager (EWMH
// _NET_WM_WINDOW_TYPE: a tooltip, a splash screen, a popup menu) and gives a
// Splash an empty input shape (XFixes, ShapeInput), so the pointer passes
// through it to the windows beneath. Never focused: the input hint is off.
void NativeWindow::mark_transient(const HostPopupPlacement &popup) {
  const char *type_name = "_NET_WM_WINDOW_TYPE_POPUP_MENU";
  if (popup.kind == HostTransientKind::tool_tip)
    type_name = "_NET_WM_WINDOW_TYPE_TOOLTIP";
  if (popup.kind == HostTransientKind::splashscreen)
    type_name = "_NET_WM_WINDOW_TYPE_SPLASH";
  XSetWindowAttributes attributes{};
  attributes.override_redirect = True;
  XChangeWindowAttributes(runtime.display, xid, CWOverrideRedirect, &attributes);
  const Atom type = runtime.atom(type_name);
  XChangeProperty(runtime.display, xid, runtime.atom("_NET_WM_WINDOW_TYPE"),
                  XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(&type), 1);
  XWMHints wm{};
  wm.flags = InputHint;
  wm.input = False;
  XSetWMHints(runtime.display, xid, &wm);
  if (!host_transient_is_splash(popup.kind))
    return;
  const XserverRegion empty = XFixesCreateRegion(runtime.display, nullptr, 0);
  XFixesSetWindowShapeRegion(runtime.display, xid, ShapeInput, 0, 0, empty);
  XFixesDestroyRegion(runtime.display, empty);
}

NativeWindow::~NativeWindow() {
  (*entry.model).set_dispatch_wake_handler({});
  (*entry.model).set_paint_wake_handler({});
  (*entry.model).set_text_metrics_provider(nullptr);
  (*entry.model).set_framebuffer_painter(nullptr);
  if (session)
    (*session).shutdown();
  if (input != nullptr)
    XDestroyIC(input);
  if (gc != nullptr)
    XFreeGC(runtime.display, gc);
  if (xid != 0)
    XDestroyWindow(runtime.display, xid);
  if (colormap != None)
    XFreeColormap(runtime.display, colormap);
}
HostDispatchResult NativeWindow::dispatch(HostEventPayload payload) {
  return (*session).dispatch({++sequence, now_ns(), std::move(payload)});
}
void NativeWindow::report_compositor_state() {
  if (closed || !session)
    return;
  const CompositorGeneration next{compositor_generation.value + 1U};
  const bool present = (*runtime.compositor).compositor_present();
  const CompositorState state = x11_compositor_state(present, argb, next);
  CompositorState unchanged = state;
  unchanged.generation = (*entry.model).compositor_state().generation;
  if (compositor_generation.value != 0U && unchanged == (*entry.model).compositor_state())
    return;
  compositor_generation = next;
  static_cast<void>(dispatch(HostCompositorEvent{state}));
}
void NativeWindow::report_window_state() {
  const bool full_screen = fallback_fullscreen || window_state_full_screen(*this);
  if (window_state_reported && reported_full_screen == full_screen) return;
  if (!dispatch(HostWindowStateEvent{full_screen}).accepted()) return;
  window_state_reported = true;
  reported_full_screen = full_screen;
}
void NativeWindow::attach() {
  (*entry.model).set_dispatch_wake_handler(Wake{runtime.wake_pipe[1]});
  (*entry.model).set_paint_wake_handler(Wake{runtime.wake_pipe[1]});
  if (!dispatch(HostAttachEvent{size, 1.0}).accepted())
    throw std::runtime_error("Linux host attach rejected");
  report_compositor_state();
  report_window_state();
  if (runtime.input_settings) {
    const HostInputSettingsEvent input{(*runtime.input_settings).double_click_milliseconds(),
                                       (*runtime.input_settings).double_click_origin(),
                                       (*runtime.input_settings).pointer_settings()};
    static_cast<void>(dispatch(input));
  }
  accessibility = std::make_unique<LinuxAccessibility>(runtime.display, xid, *entry.model, entry.options.title);
  if (entry.options.initially_visible)
    action(ActionKind::show);
}
void NativeWindow::ready() {
  const WindowAction show{weak_from_this(), runtime.actions, ActionKind::show};
  const WindowAction hide{weak_from_this(), runtime.actions, ActionKind::hide};
  const WindowAction close_window{weak_from_this(), runtime.actions,
                                  ActionKind::close};
  if (entry.options.host_ready)
    entry.options.host_ready(Wake{runtime.wake_pipe[1]}, close_window,
                             Dialog{this}, ReadText{this}, WriteText{this});
  if (entry.options.full_screen_ready)
    entry.options.full_screen_ready(WindowAction{
        weak_from_this(), runtime.actions, ActionKind::fullscreen});
  if (entry.options.title_ready)
    entry.options.title_ready(WindowTitle{weak_from_this()});
  if (entry.options.client_size_ready)
    entry.options.client_size_ready(WindowClientSize{weak_from_this()});
  if (entry.options.visibility_ready)
    entry.options.visibility_ready(show, hide);
}
void NativeWindow::action(ActionKind kind) {
  if (closed)
    return;
  if (kind == ActionKind::close) {
    close(HostCloseReason::application);
    return;
  }
  if (kind == ActionKind::show) {
    visible = true;
    if (utility) {
      // A window manager removes _NET_WM_STATE when a window is withdrawn;
      // restate skip-taskbar before every map.
      const Atom skip = runtime.atom("_NET_WM_STATE_SKIP_TASKBAR");
      XChangeProperty(runtime.display, xid, runtime.atom("_NET_WM_STATE"),
                      XA_ATOM, 32, PropModeReplace,
                      reinterpret_cast<const unsigned char *>(&skip), 1);
    }
    XMapRaised(runtime.display, xid);
    damage.add({0, 0, size.width, size.height});
  }
  if (kind == ActionKind::hide) {
    visible = false;
    XUnmapWindow(runtime.display, xid);
  }
  if (kind == ActionKind::fullscreen) {
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char *data = nullptr;
    bool managed_fullscreen = false;
    if (XGetWindowProperty(runtime.display, DefaultRootWindow(runtime.display),
                           runtime.atom("_NET_SUPPORTED"), 0, 4096, False,
                           XA_ATOM, &type, &format, &count, &remaining,
                           &data) == Success &&
        type == XA_ATOM && format == 32) {
      const Atom *atoms = reinterpret_cast<const Atom *>(data);
      for (unsigned long i = 0; i < count; ++i)
        if (atoms[i] == runtime.atom("_NET_WM_STATE_FULLSCREEN"))
          managed_fullscreen = true;
    }
    if (data != nullptr)
      XFree(data);
    if (!managed_fullscreen || fallback_fullscreen) {
      if (!fallback_fullscreen) {
        int x{}, y{};
        ::Window child{};
        XTranslateCoordinates(runtime.display, xid,
                              DefaultRootWindow(runtime.display), 0, 0, &x, &y,
                              &child);
        restored_frame = {static_cast<double>(x), static_cast<double>(y),
                          size.width, size.height};
      }
      fallback_fullscreen = !fallback_fullscreen;
      XUnmapWindow(runtime.display, xid);
      XSetWindowAttributes attributes{};
      attributes.override_redirect = fallback_fullscreen ? True : False;
      XChangeWindowAttributes(runtime.display, xid, CWOverrideRedirect,
                              &attributes);
      XReparentWindow(runtime.display, xid, DefaultRootWindow(runtime.display),
                      0, 0);
      const Rect frame =
          fallback_fullscreen
              ? Rect{0, 0,
                     static_cast<double>(DisplayWidth(
                         runtime.display, DefaultScreen(runtime.display))),
                     static_cast<double>(DisplayHeight(
                         runtime.display, DefaultScreen(runtime.display)))}
              : restored_frame;
      XMoveResizeWindow(runtime.display, xid, static_cast<int>(frame.x),
                        static_cast<int>(frame.y),
                        static_cast<unsigned>(frame.width),
                        static_cast<unsigned>(frame.height));
      XMapRaised(runtime.display, xid);
      if (fallback_fullscreen)
        XSetInputFocus(runtime.display, xid, RevertToParent, CurrentTime);
      // No window manager reports the fallback: the host does.
      report_window_state();
      return;
    }
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = xid;
    event.xclient.message_type = runtime.atom("_NET_WM_STATE");
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2;
    event.xclient.data.l[1] =
        static_cast<long>(runtime.atom("_NET_WM_STATE_FULLSCREEN"));
    event.xclient.data.l[3] = 1;
    XSendEvent(runtime.display, DefaultRootWindow(runtime.display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
  }
}
void NativeWindow::close(HostCloseReason reason, bool force) {
  if (closed)
    return;
  // A session end is told to the closing handler, which cannot refuse it
  // here: the signal has already ended the session.
  if (force && reason == HostCloseReason::session_end)
    static_cast<void>(dispatch(HostCloseRequest{reason, false, false}));
  if (!force) {
    const HostDispatchResult result =
        dispatch(HostCloseRequest{reason, false, entry.options.hide_on_close});
    if (!result.accepted() || !result.close_allowed)
      return;
    if (entry.options.hide_on_close) {
      action(ActionKind::hide);
      if (runtime.windowing)
        (*runtime.windowing).report_hidden_on_close(window_id);
      return;
    }
  }
  if (accessibility) { (*accessibility).detach(); }
  closed = true;
  visible = false;
  static_cast<void>(dispatch(HostClosedEvent{reason}));
  (*entry.model).set_dispatch_wake_handler({});
  (*entry.model).set_paint_wake_handler({});
  (*session).shutdown();
  XUnmapWindow(runtime.display, xid);
  if (entry.options.print_metrics_on_close)
    std::cout << "{\"window\":" << (*entry.model).metrics_snapshot().to_json()
              << ",\"host\":" << (*session).snapshot().to_json() << "}\n";
  // Retired before the closed callback, so the callback may open a
  // replacement under the same stable id.
  if (runtime.windowing)
    (*runtime.windowing).retire(window_id);
  if (entry.options.closed)
    entry.options.closed();
}
void NativeWindow::event(XEvent &event) {
  if (closed)
    return;
  if (event.type == Expose) {
    damage.add({static_cast<double>(event.xexpose.x),
                static_cast<double>(event.xexpose.y),
                static_cast<double>(event.xexpose.width),
                static_cast<double>(event.xexpose.height)});
    return;
  }
  if (event.type == ConfigureNotify) {
    const Size resized{static_cast<double>(event.xconfigure.width),
                       static_cast<double>(event.xconfigure.height)};
    if (resized != size) {
      size = resized;
      static_cast<void>(dispatch(HostResizeEvent{size}));
      damage.add({0, 0, size.width, size.height});
    }
    // Moves arrive here too: ICCCM 4.1.5 has a reparenting window manager
    // send a synthetic ConfigureNotify when it moves the frame.
    if (runtime.windowing)
      (*runtime.windowing).report_native_frame(*this);
    return;
  }
  if (event.type == MapNotify || event.type == UnmapNotify) {
    visible = event.type == MapNotify;
    static_cast<void>(dispatch(HostOcclusionEvent{!visible}));
    return;
  }
  if (event.type == ClientMessage &&
      event.xclient.message_type == runtime.atom("WM_PROTOCOLS") &&
      static_cast<Atom>(event.xclient.data.l[0]) ==
          runtime.atom("WM_DELETE_WINDOW")) {
    close(HostCloseReason::user);
    return;
  }
  if (event.type == PropertyNotify &&
      event.xproperty.atom == runtime.atom("_NET_WM_STATE")) {
    // Minimized (iconified) windows carry _NET_WM_STATE_HIDDEN (EWMH).
    if (runtime.windowing)
      (*runtime.windowing)
          .report_minimized(window_id, window_state_hidden(*this));
    report_window_state();
    return;
  }
  if (event.type == FocusIn || event.type == FocusOut) {
    // Focus moved by a pointer or keyboard grab is not activation.
    if (event.xfocus.mode == NotifyNormal ||
        event.xfocus.mode == NotifyWhileGrabbed)
      focused = event.type == FocusIn;
    if (event.type == FocusOut)
      pressed_keys.fill(false);
    if (input != nullptr) {
      if (event.type == FocusIn)
        XSetICFocus(input);
      else
        XUnsetICFocus(input);
    }
    static_cast<void>(dispatch(HostActivationEvent{event.type == FocusIn}));
    return;
  }
  if (event.type == ClientMessage && drag_message(event.xclient))
    return;
  if (modal)
    return;
  if (event.type == MotionNotify || event.type == ButtonPress ||
      event.type == ButtonRelease || event.type == EnterNotify ||
      event.type == LeaveNotify) {
    PointerEvent pointer;
    pointer.pointer_id = 1;
    unsigned state = 0;
    if (event.type == MotionNotify) {
      pointer.action = PointerAction::move;
      pointer.position = {static_cast<double>(event.xmotion.x),
                          static_cast<double>(event.xmotion.y)};
      state = event.xmotion.state;
    } else if (event.type == EnterNotify || event.type == LeaveNotify) {
      pointer.action = event.type == EnterNotify ? PointerAction::enter
                                                 : PointerAction::leave;
      pointer.position = {static_cast<double>(event.xcrossing.x),
                          static_cast<double>(event.xcrossing.y)};
      state = event.xcrossing.state;
    } else {
      pointer.position = {static_cast<double>(event.xbutton.x),
                          static_cast<double>(event.xbutton.y)};
      state = event.xbutton.state;
      const unsigned button = event.xbutton.button;
      if (button >= 4 && button <= 7) {
        if (event.type == ButtonRelease)
          return;
        pointer.action = PointerAction::wheel;
        pointer.wheel_delta = {button == 6   ? 40.0
                               : button == 7 ? -40.0
                                             : 0.0,
                               button == 4   ? 40.0
                               : button == 5 ? -40.0
                                             : 0.0};
        // Forty logical pixels a notch.
        pointer.wheel_unit = WheelDeltaUnit::pixels;
      } else {
        pointer.action =
            event.type == ButtonPress ? PointerAction::down : PointerAction::up;
        pointer.button = button == 1   ? PointerButton::primary
                         : button == 3 ? PointerButton::secondary
                                       : PointerButton::middle;
        // X reports no click count: count it from the desktop's
        // double-click time and distance (linux_input_rules.hpp). A
        // release carries the count of the press it ends.
        if (event.type == ButtonPress) {
          static_cast<void>(next_click_count(
              click_history, button, static_cast<std::uint32_t>(event.xbutton.time),
              pointer.position, runtime.double_click_milliseconds(),
              runtime.double_click_distance()));
        }
        pointer.click_count = click_history.count;
      }
    }
    pointer.modifiers = modifiers(state);
    static_cast<void>(dispatch(pointer));
    // X has no context-menu event: the desktop convention is the right
    // button's press, which the host reports after the press itself.
    if (pointer.action == PointerAction::down && pointer.button == PointerButton::secondary)
      static_cast<void>(dispatch(HostContextRequestEvent{ContextRequestOrigin::pointer, pointer.position}));
    const Control::Ptr captured = (*entry.model).captured_control();
    const Control::Ptr target = captured ? captured : (*entry.model).hit_test(pointer.position);
    static_cast<void>((*services).set_custom_cursor(
        target ? (*target).effective_cursor_images() : CursorImagesPtr{}, 1.0,
        target ? (*target).effective_cursor() : CursorKind::arrow));
    return;
  }
  if (event.type == KeyPress || event.type == KeyRelease) {
    const KeySym symbol = XkbKeycodeToKeysym(
        runtime.display, static_cast<KeyCode>(event.xkey.keycode), 0, 0);
    KeyEvent key;
    key.action = event.type == KeyPress ? KeyAction::down : KeyAction::up;
    if (event.xkey.keycode < pressed_keys.size()) {
      key.repeat = event.type == KeyPress && pressed_keys[event.xkey.keycode];
      pressed_keys[event.xkey.keycode] = event.type == KeyPress;
    }
    key.physical_key = event.xkey.keycode < runtime.physical_keys.size()
                           ? runtime.physical_keys[event.xkey.keycode]
                           : 0;
    if (key.physical_key == 0)
      key.physical_key = key_usage(symbol);
    key.modifiers = modifiers(event.xkey.state);
    const HostDispatchResult result = dispatch(key);
    // The Menu key and Shift+F10 the window left unhandled ask for a
    // context menu (the desktop convention, emulated: X has no such event).
    const bool context_key = event.type == KeyPress && !result.handled && !key.repeat &&
                             requests_keyboard_context(key.physical_key, key.modifiers);
    if (context_key) {
      static_cast<void>(dispatch(HostContextRequestEvent{ContextRequestOrigin::keyboard, Point{}}));
      return;
    }
    if (event.type == KeyPress && !result.handled &&
        (event.xkey.state & (ControlMask | Mod1Mask | Mod4Mask)) == 0) {
      std::vector<char> bytes(128);
      KeySym translated{};
      Status status{};
      int length = input != nullptr
                       ? Xutf8LookupString(input, &event.xkey, bytes.data(),
                                           static_cast<int>(bytes.size()),
                                           &translated, &status)
                       : XLookupString(&event.xkey, bytes.data(),
                                       static_cast<int>(bytes.size()),
                                       &translated, nullptr);
      if (input != nullptr && status == XBufferOverflow && length > 0 &&
          length < 1024 * 1024) {
        bytes.resize(static_cast<std::size_t>(length) + 1);
        length = Xutf8LookupString(input, &event.xkey, bytes.data(),
                                   static_cast<int>(bytes.size()), &translated,
                                   &status);
      }
      if (length > 0 && length <= static_cast<int>(bytes.size()) &&
          static_cast<unsigned char>(bytes[0]) >= 32) {
        TextInputEvent text;
        text.text_utf8.assign(bytes.data(), static_cast<std::size_t>(length));
        static_cast<void>(dispatch(std::move(text)));
      }
    }
  }
}
void NativeWindow::update() {
  if (closed)
    return;
  if (entry.options.dispatch_pending) entry.options.dispatch_pending();
  if (closed)
    return;
  static_cast<void>((*entry.model).drain_posted_work());
  static_cast<void>(
      (*entry.model).poll_frame_schedule(std::chrono::steady_clock::now()));
  if (accessibility && !closed) { (*accessibility).update(); }
  // Gather before taking damage: a new frame beneath an overlay damages the
  // overlay's share (#69), and that damage is taken with the rest.
  const bool regions_taken = gather_live_frames();
  const DamageRegion fresh = (*entry.model).take_damage();
  for (std::size_t i = 0; i < fresh.rectangles().size(); ++i)
    damage.add(fresh.rectangles()[i]);
  // A taken region stays valid only if this tick presents it.
  if (regions_taken) live_region_lost = true;
  // The screen rectangles: retained damage and the frames drawn directly.
  DamageRegion screen = damage;
  for (std::size_t index = 0; index < live_regions.size(); ++index) {
    if (!live_regions[index].empty()) screen.add(live_regions[index]);
  }
  if (!visible || screen.empty()) {
    live_frames.clear();
    return;
  }
  const Rect frame_bounds = screen.bounds();
  const bool covers_window =
      frame_bounds.x <= 0.0 && frame_bounds.y <= 0.0 &&
      frame_bounds.x + frame_bounds.width >= size.width &&
      frame_bounds.y + frame_bounds.height >= size.height;
#if defined(GUI_FORMS_PREPARED_TEXT)
  live_frames.clear();
  const bool prepared_presented = update_prepared_window(*this);
  if (prepared_presented && regions_taken) live_region_lost = false;
  frame_capture_if_due(covers_window && prepared_presented);
  return;
#else
  raster.resize(size, 1.0);
  static_cast<void>(
      raster.synchronize_images((*entry.model).image_resources()));
  const std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now();
  // Compose: the new frames first, then retained painting of the damage
  // (overlay shares among it) over them, into the raster.
  raster.begin_frame(screen);
  compose_live_frames();
  std::optional<PaintReceipt> receipt{};
  if (!damage.empty()) {
    receipt = (*entry.model).paint(raster, damage.rectangles());
    // A fragment that fell back to retained painting is in `damage`.
    const std::span<const Rect> painted = damage.rectangles();
    for (std::size_t index = 0; index < painted.size(); ++index) screen.add(painted[index]);
  }
  raster.end_frame();
  live_frames.clear();
  // A declined paint presents nothing; the damage and the lost regions stay.
  if (!damage.empty() && !receipt)
    return;
  // Present: the rectangle set, once.
  PresentedArea copied;
  if (!present_raster_rectangles(*this, screen.rectangles(), copied))
    return;
  const std::uint64_t elapsed = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  if (regions_taken) live_region_lost = false;
  const bool acknowledged = report_presented(receipt, elapsed, copied);
  if (!receipt) {
    frame_capture_if_due(covers_window);
    return;
  }
  // The copy reached the window. A receipt for the revision already
  // presented is refused as not newer, but the frame is on screen, so the
  // damage is done (as the prepared-text path checks).
  const PaintLeaseSnapshot lease = (*entry.model).paint_lease_snapshot();
  const bool shown = acknowledged ||
      (lease.surface_epoch == (*receipt).surface_epoch &&
       lease.presented_revision == (*receipt).rendered_revision);
  if (shown) {
    damage.clear();
    frame_capture_if_due(covers_window);
  }
#endif
}
// Frame sequence, gather (stage DP decision 2). Each surface's new frame is
// taken once with the region it changed since this host last drew it; the
// core splits its clip around overlays, and each fragment is drawn from that
// one frame. Where the placement is unchanged, only the changed region is
// drawn and copied: outside it the raster already holds this frame's pixels.
// An opaque frame is drawn directly (as on Windows and macOS); one that is
// not, or any frame on the prepared-text raster, adds its region to the
// damage and retained painting composes it over what lies beneath.
bool NativeWindow::gather_live_frames() {
  live_updates.clear();
  live_frames.clear();
  live_frame_sources.clear();
  live_regions.clear();
  live_frame_pixels.clear();
  live_frame_rectangles.clear();
  if (!(*entry.model).has_live_surface_presentations()) return false;
  live_updates = (*entry.model).take_live_surface_presentations();
  const bool regions_usable = !live_region_lost;
  bool regions_taken = false;
  std::vector<Rect> changed_regions;
  changed_regions.reserve(live_updates.size());
  for (std::size_t index = 0; index < live_updates.size(); ++index) {
    const LiveSurfacePresentation& update = live_updates[index];
    std::size_t source = live_frames.size();
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (live_updates[earlier].surface == update.surface) {
        source = live_frame_sources[earlier];
        break;
      }
    }
    if (source == live_frames.size()) {
      LiveSurfaceFrame taken{};
      if (update.surface) taken = (*update.surface).acquire_for_presentation(this);
      if (taken) regions_taken = true;
      Rect changed = update.clip;
      if (!taken) {
        changed = {};
      } else if (update.damage_limited && regions_usable) {
        changed = gui_forms::detail::live_damage_in_window(
            taken.damage(), update.destination, taken.width(), taken.height(), 1.0);
      }
      live_frames.push_back(std::move(taken));
      changed_regions.push_back(changed);
      live_frame_pixels.push_back(0U);
      live_frame_rectangles.push_back(0U);
    }
    live_frame_sources.push_back(source);
    live_regions.push_back(Rect{});
    const Rect changed = changed_regions[source];
    const Rect region = update.damage_limited && regions_usable
        ? Rect::intersection(update.clip, changed) : (changed.empty() ? Rect{} : update.clip);
    if (region.empty()) continue;
#if defined(GUI_FORMS_PREPARED_TEXT)
    // The prepared raster publishes only retained paint transactions.
    const bool direct = false;
#else
    const bool direct = live_frames[source].opaque();
#endif
    if (direct) live_regions[index] = region;
    else damage.add(region);
    const Rect device = gui_forms::detail::align_damage_outward(region, 1.0);
    live_frame_pixels[source] += static_cast<std::uint64_t>(device.width) *
                                 static_cast<std::uint64_t>(device.height);
    ++live_frame_rectangles[source];
  }
  return regions_taken;
}

// Frame sequence, compose (stage DP). Inside begin_frame and before retained
// painting, so anything retained over a frame is painted after it.
void NativeWindow::compose_live_frames() {
  for (std::size_t index = 0; index < live_regions.size(); ++index) {
    const Rect region = live_regions[index];
    if (region.empty()) continue;
    raster.save();
    raster.clip_rect(region);
    const bool drawn = raster.draw_live_surface_frame(
        live_frames[live_frame_sources[index]], live_updates[index].destination, 1.0);
    raster.restore();
    if (drawn) continue;
    // Retained painting draws the surface's latest frame there instead.
    live_regions[index] = {};
    damage.add(region);
  }
}

// Frame sequence, report (stage DP). One present copied `copied`; the live
// frames gathered this tick are its live share, the rest retained, and the
// duration is split between them by the pixels each copied. A live-only tick
// has no receipt: its copy time is reported with the first live frame.
bool NativeWindow::report_presented(const std::optional<PaintReceipt>& receipt,
                                    const std::uint64_t elapsed, const PresentedArea copied) {
  std::uint64_t live_pixels = 0U;
  std::uint64_t live_frames_copied = 0U;
  for (std::size_t index = 0; index < live_frame_rectangles.size(); ++index) {
    if (live_frame_rectangles[index] == 0U) continue;
    live_pixels += live_frame_pixels[index];
    ++live_frames_copied;
  }
  if (live_pixels > copied.device_pixels) live_pixels = copied.device_pixels;
  bool acknowledged = false;
  std::uint64_t live_elapsed = elapsed;
  std::uint64_t unreported_copy = copied.copy_nanoseconds;
  if (receipt) {
    PresentedArea retained;
    retained.device_pixels = copied.device_pixels - live_pixels;
    retained.rectangles = copied.rectangles;
    // The window records the copy time with this report, accepted or not.
    retained.copy_nanoseconds = copied.copy_nanoseconds;
    unreported_copy = 0U;
    live_elapsed = copied.device_pixels == 0U ? 0U :
        static_cast<std::uint64_t>(static_cast<double>(elapsed) *
            static_cast<double>(live_pixels) / static_cast<double>(copied.device_pixels));
    acknowledged =
        (*entry.model).notify_presented(*receipt, elapsed - live_elapsed, retained);
  }
  const std::uint64_t live_duration =
      live_frames_copied == 0U ? 0U : live_elapsed / live_frames_copied;
  for (std::size_t index = 0; index < live_frame_rectangles.size(); ++index) {
    if (live_frame_rectangles[index] == 0U) continue;
    PresentedArea live;
    live.device_pixels = live_frame_pixels[index];
    live.rectangles = live_frame_rectangles[index];
    live.copy_nanoseconds = unreported_copy;
    unreported_copy = 0U;
    (*entry.model).notify_live_presented(live_duration, live);
  }
  live_frame_rectangles.clear();
  return acknowledged;
}

void NativeWindow::frame_capture_if_due(bool covers_window) {
  if (closed || frame_capture_completed || !covers_window ||
      entry.options.capture_path.empty())
    return;
  frame_capture_completed = true;
  // The Skia front raster is the framebuffer XPutImage just copied; encode it
  // through the private adapter seam and report through the shared contract.
  const std::vector<std::byte> encoded = raster.encode_png();
  frame_capture_saved = gui_forms::host::detail::complete_frame_capture(
      entry.options.capture_path, encoded);
  close(HostCloseReason::application);
}
void Runtime::dispatch(XEvent &event) {
  // A compositing manager started or stopped: every window reports again,
  // in window-open order (PLAN.md §2.4).
  if (compositor && (*compositor).selection_event(event)) {
    const std::vector<std::shared_ptr<NativeWindow>> reporting = windows;
    for (std::size_t i = 0; i < reporting.size(); ++i)
      (*reporting[i]).report_compositor_state();
    return;
  }
  if (event.type == SelectionRequest) {
    selection_request(event.xselectionrequest);
    return;
  }
  if (event.type == SelectionClear) {
    clipboard_owner = 0;
    clipboard_text.clear();
    clipboard_image = {};
    clipboard_png.clear();
    return;
  }
  if (event.type == DestroyNotify) {
    for (std::size_t i = transfers.size(); i > 0; --i)
      if (transfers[i - 1].requestor == event.xdestroywindow.window)
        transfers.erase(transfers.begin() + static_cast<std::ptrdiff_t>(i - 1));
  }
  if (event.type == PropertyNotify)
    selection_property(event.xproperty);
  if (input_settings && (*input_settings).settings_event(event)) {
    report_input_settings();
    return;
  }
  // Tray icon windows belong to a window's services, not to the window.
  for (std::size_t i = 0; i < windows.size(); ++i)
    if ((*windows[i]).services && (*(*windows[i]).services).tray_event(event))
      return;
  if (XFilterEvent(&event, None))
    return;
  const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
  for (std::size_t i = 0; i < snapshot.size(); ++i)
    if ((*snapshot[i]).xid == event.xany.window) {
      (*snapshot[i]).event(event);
      break;
    }
}
void Runtime::end_session() {
  // A copy: a closed callback may open a replacement window.
  const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
  for (std::size_t i = snapshot.size(); i > 0; --i)
    (*snapshot[i - 1]).close(HostCloseReason::session_end, true);
}
void Runtime::step(int maximum_wait) {
  char bytes[128];
  while (::read(wake_pipe[0], bytes, sizeof(bytes)) > 0) {
  }
  if ((terminate_installed || hangup_installed) && session_end_signalled != 0) {
    session_end_signalled = 0;
    end_session();
    return;
  }
  std::deque<Action> queued;
  {
    const std::lock_guard<std::mutex> guard((*actions).mutex);
    queued.swap((*actions).pending);
  }
  for (std::size_t i = 0; i < queued.size(); ++i) {
    const std::shared_ptr<NativeWindow> window = queued[i].window.lock();
    if (window)
      (*window).action(queued[i].kind);
  }
  accessibility_dispatch();
  bool dispatched = !queued.empty();
  while (XPending(display) > 0) {
    dispatched = true;
    XEvent event{};
    XNextEvent(display, &event);
    dispatch(event);
  }
  const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
  int wait = maximum_wait;
  const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
  for (std::size_t i = transfers.size(); i > 0; --i) {
    const long long delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            transfers[i - 1].deadline - now)
            .count();
    if (delay <= 0)
      transfers.erase(transfers.begin() + static_cast<std::ptrdiff_t>(i - 1));
    else
      wait = wait < 0 ? static_cast<int>(delay)
                      : std::min(wait, static_cast<int>(delay));
  }
  for (std::size_t i = 0; i < snapshot.size(); ++i) {
    NativeWindow &window = *snapshot[i];
    window.update();
    if (window.closed)
      continue;
    // The live clock (stage DP): a registered live surface is sampled at
    // least every 16 ms, as the Windows host's waitable timer does.
    if ((*window.entry.model).has_live_surface_presentations())
      wait = wait < 0 ? 16 : std::min(wait, 16);
    const std::optional<FrameTime> deadline = (*window.entry.model).next_wake();
    if (deadline) {
      const long long delay =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              *deadline - std::chrono::steady_clock::now())
              .count();
      const int milliseconds = static_cast<int>(std::clamp(
          delay, 0LL, static_cast<long long>(std::numeric_limits<int>::max())));
      wait = wait < 0 ? milliseconds : std::min(wait, milliseconds);
    }
  }
  XFlush(display);
  bool active = false;
  for (std::size_t i = 0; i < snapshot.size(); ++i)
    if (!(*snapshot[i]).closed)
      active = true;
  if (!active)
    return;
  // Closing callbacks can enqueue more work; the pipe keeps that work visible.
  if (dispatched || XPending(display) > 0 || wait == 0)
    return;
  accessibility_wait(ConnectionNumber(display), wake_pipe[0], wait);
}
HostDialogResult NativeWindow::dialog(const HostDialogRequest &request) {
  return retained_dialog(*this, request);
}
HostWindowFrame NativeWindow::frame() const {
  HostWindowFrame result;
  int x{}, y{};
  ::Window child{};
  if (!XTranslateCoordinates(runtime.display, xid,
                             DefaultRootWindow(runtime.display), 0, 0, &x, &y,
                             &child))
    return result;
  result.content = {static_cast<double>(x), static_cast<double>(y), size.width,
                    size.height};
  result.outer = result.content;
  // _NET_FRAME_EXTENTS: left, right, top, bottom, in pixels (EWMH).
  Atom type{};
  int format{};
  unsigned long count{}, remaining{};
  unsigned char *data = nullptr;
  if (XGetWindowProperty(runtime.display, xid,
                         runtime.atom("_NET_FRAME_EXTENTS"), 0, 4, False,
                         XA_CARDINAL, &type, &format, &count, &remaining,
                         &data) == Success &&
      type == XA_CARDINAL && format == 32 && count == 4 && data != nullptr) {
    // Format-32 properties arrive as an array of long (Xlib).
    const long *extents = reinterpret_cast<const long *>(data);
    const double left = static_cast<double>(extents[0]);
    const double right = static_cast<double>(extents[1]);
    const double top = static_cast<double>(extents[2]);
    const double bottom = static_cast<double>(extents[3]);
    result.outer = {result.content.x - left, result.content.y - top,
                    result.content.width + left + right,
                    result.content.height + top + bottom};
    result.outer_measured = true;
  }
  if (data != nullptr)
    XFree(data);
  return result;
}
HostServiceStatus NativeWindow::request_frame(Rect content) {
  if (closed)
    return {HostServiceError::after_shutdown};
  if (content.width < entry.options.minimum_size.width ||
      content.height < entry.options.minimum_size.height)
    return {HostServiceError::invalid_argument};
  if (fallback_fullscreen || window_state_fixed(*this))
    return {HostServiceError::unsupported};
  // Static gravity: the requested position is the client area's own, so
  // the window manager places its frame around it (ICCCM 4.1.2.3).
  XSizeHints hints{};
  hints.flags = PMinSize | USPosition | USSize | PWinGravity;
  hints.x = static_cast<int>(std::lround(content.x));
  hints.y = static_cast<int>(std::lround(content.y));
  hints.width = static_cast<int>(std::lround(content.width));
  hints.height = static_cast<int>(std::lround(content.height));
  hints.min_width = static_cast<int>(entry.options.minimum_size.width);
  hints.min_height = static_cast<int>(entry.options.minimum_size.height);
  hints.win_gravity = StaticGravity;
  XSetWMNormalHints(runtime.display, xid, &hints);
  XMoveResizeWindow(runtime.display, xid, hints.x, hints.y,
                    static_cast<unsigned>(hints.width),
                    static_cast<unsigned>(hints.height));
  XFlush(runtime.display);
  return {};
}
#include "linux_splashscreen.inc"
} // namespace gui_forms::host::linux_detail
namespace gui_forms::host {
std::unique_ptr<gui_forms::detail::SplashscreenSurface> open_platform_splashscreen(const Size size) {
  std::unique_ptr<gui_forms::detail::SplashscreenSurface> surface = linux_detail::open_x11_splashscreen(size);
  return surface;
}
// accessibility: ATK/AT-SPI publication of the semantic tree, including
// AtkText, verified by the AT-SPI client tests over a fixture and the
// Gallery (stage A). ATK is a required dependency of this host.
HostCapabilities linux_capabilities() {
  return {HostCapabilities::current_protocol_version, "linux-x11",
          HostCapability::lifecycle | HostCapability::accessibility |
              HostCapability::frame_capture |
              HostCapability::monitor_geometry |
              HostCapability::occlusion | HostCapability::scheduled_wake |
              HostCapability::pointer_input | HostCapability::keyboard_input |
              HostCapability::pointer_capture | HostCapability::cursor |
              HostCapability::clipboard | HostCapability::clipboard_images |
              HostCapability::dialogs | HostCapability::sound_cues |
              HostCapability::typed_drag_destination |
              HostCapability::runtime_windows | HostCapability::window_frames |
              HostCapability::screen_pointer | HostCapability::window_at_point |
              HostCapability::window_ordering |
              HostCapability::transparent_windows |
              HostCapability::compositor_notifications |
              HostCapability::transient_surfaces |
              HostCapability::opaque_transient_surfaces |
              HostCapability::window_state_reports,
          // X11 desktops follow the PC keyboard convention.
          pc_key_map()};
}
int run_linux_application(std::vector<LinuxApplicationWindow> windows,
                          LinuxApplicationOptions application) {
  const std::shared_ptr<linux_detail::RunOpenState> open_state =
      std::make_shared<linux_detail::RunOpenState>();
  try {
    // The run holds the bundled font set, so windows opened and closed during it share one
    // copy of each face.
    const std::shared_ptr<gui_forms::detail::BundledFontSet> run_fonts =
        gui_forms::detail::shared_bundled_font_set(linux_detail::fonts_directory(),
                                                   posix_sealed_pages());
    linux_detail::Runtime runtime;
    runtime.windowing = std::make_unique<linux_detail::X11Windowing>(runtime);
    // Ends the windowing on every exit from here, before ~Runtime closes the
    // remaining windows: the program's handles stop reaching it first.
    const linux_detail::WindowingEndGuard end_guard{
        open_state.get(), &application, runtime.windowing.get()};
    (*open_state).windowing = runtime.windowing.get();
    if (application.windowing_ready)
      application.windowing_ready(*runtime.windowing,
                                  linux_detail::RunOpen{open_state});
    // Owners first; a cycle or an unknown owner leaves windows unopened.
    std::vector<bool> opened(windows.size(), false);
    std::size_t remaining = windows.size();
    bool progress = true;
    while (remaining > 0 && progress) {
      progress = false;
      for (std::size_t i = 0; i < windows.size(); ++i) {
        if (opened[i])
          continue;
        if (!windows[i].owner_id.empty() &&
            !(*runtime.windowing).find(windows[i].owner_id).has_value())
          continue;
        const HostWindowAdmission admission =
            (*runtime.windowing).open(std::move(windows[i]), false);
        if (!admission.status.accepted())
          return 2;
        opened[i] = true;
        --remaining;
        progress = true;
      }
    }
    if (remaining > 0)
      return 2;
    (*runtime.windowing).mark_started();
    for (std::size_t i = 0; i < runtime.windows.size(); ++i)
      (*runtime.windows[i]).ready();
    bool primary_open = true;
    while (primary_open) {
      runtime.step();
      runtime.report_activation();
      runtime.reclaim();
      primary_open = false;
      for (std::size_t i = 0; i < runtime.windows.size(); ++i) {
        const linux_detail::NativeWindow &window = *runtime.windows[i];
        if (!window.closed && window.entry.owner_id.empty() &&
            !window.entry.tool_window)
          primary_open = true;
      }
    }
    // Portable contract: a requested capture that was never saved fails the
    // process even when the window closed normally.
    for (std::size_t i = 0; i < runtime.windows.size(); ++i) {
      const linux_detail::NativeWindow &window = *runtime.windows[i];
      if (!window.entry.options.capture_path.empty() &&
          !window.frame_capture_saved)
        return 6;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "GUI.Forms Linux host: " << error.what() << '\n';
    return 1;
  }
}
int run_linux(std::unique_ptr<gui_forms::Window> window,
              LinuxHostOptions options) {
  std::vector<LinuxApplicationWindow> windows;
  LinuxApplicationWindow entry;
  entry.stable_id = "main";
  entry.model = std::move(window);
  entry.options = std::move(options);
  windows.push_back(std::move(entry));
  return run_linux_application(std::move(windows));
}
} // namespace gui_forms::host
