#pragma once
// GBM buffers for the Linux buffer routes (docs/user/presentation.md): the
// X11 DRI3 route and the Wayland linux-dmabuf route. GBM is used only to
// obtain a linear buffer, map it for CPU writing, and export it as a
// dmabuf descriptor for the window system. No rendering, EGL context or
// GPU work. Built only with GUI_FORMS_ENABLE_X11_DRI3_PRESENT or
// GUI_FORMS_ENABLE_WAYLAND_DMABUF_PRESENT; GBM types stay in
// src/host/linux/ (opaque here). UI thread only.
#include "../../../core/host/presentation/presentation_geometry.hpp"
#include <cstddef>
#include <cstdint>

struct gbm_bo;
struct gbm_device;

namespace gui_forms::host::linux_detail {

// A GBM device over a DRM node descriptor, which it owns.
struct GbmDevice final {
  gbm_device *device{};
  int descriptor{-1};
};

// Makes the device over `descriptor` (owned from here, closed on failure).
// False, leaving `made` empty, when GBM declines it.
[[nodiscard]] bool gbm_device_open(int descriptor, GbmDevice &made) noexcept;
// Destroys the device and closes its descriptor; `device` is left empty.
void gbm_device_close(GbmDevice &device) noexcept;

// One linear buffer and, between map and unmap, its CPU mapping.
struct GbmBuffer final {
  gbm_bo *buffer{};
  // The DRM fourcc it was made with (GBM_FORMAT_XRGB8888 or ARGB8888).
  std::uint32_t format{};
  // The layout modifier the window system is told (linear).
  std::uint64_t modifier{};
  std::uint32_t stride{};
  // While mapped: the first pixel, the mapping's row bytes and GBM's
  // handle for the unmap. Null otherwise.
  std::byte *pixels{};
  std::uint32_t mapped_stride{};
  void *mapping{};
};

// Makes a linear buffer of `extent` in the raster's byte order (blue in
// the low byte of 32-bit little-endian words), with alpha when `alpha`.
// False, leaving `made` empty, when GBM declines.
[[nodiscard]] bool gbm_buffer_make(GbmDevice &device, presentation::PresentExtent extent, bool alpha,
                                   GbmBuffer &made) noexcept;
// Maps the whole buffer for writing. False when the mapping fails.
//
// A mapping does not promise the buffer's earlier contents: a driver may
// map a staging copy that is not read back. GBM reports no buffer age, so
// a route over these buffers reports age 0 (unknown) and the shim writes
// the whole frame into the mapping each time (gbm_buffer_known_age).
[[nodiscard]] bool gbm_buffer_map(GbmBuffer &buffer, presentation::PresentExtent extent) noexcept;
// The age a route reports for a buffer GBM mapped: 0, unknown, whatever
// the presents since (GBM has no query for whether a mapping keeps the
// buffer's contents). One place, so that a driver query can answer here.
[[nodiscard]] std::uint32_t gbm_buffer_known_age(const GbmBuffer &buffer, std::uint32_t presents_since) noexcept;
// Ends the mapping; the writes reach the buffer.
void gbm_buffer_unmap(GbmBuffer &buffer) noexcept;
// A new dmabuf descriptor for the buffer, owned by the caller; -1 on
// failure.
[[nodiscard]] int gbm_buffer_export(const GbmBuffer &buffer) noexcept;
// Unmaps and destroys the buffer; `buffer` is left empty.
void gbm_buffer_free(GbmBuffer &buffer) noexcept;

} // namespace gui_forms::host::linux_detail
