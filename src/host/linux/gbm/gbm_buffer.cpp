#include "gbm_buffer.hpp"

#include <gbm.h>
#include <unistd.h>

namespace gui_forms::host::linux_detail {

namespace {

// DRM_FORMAT_MOD_LINEAR (drm_fourcc.h): rows in memory order.
constexpr std::uint64_t linear_modifier = 0U;

} // namespace

bool gbm_device_open(int descriptor, GbmDevice &made) noexcept {
  made = GbmDevice{};
  if (descriptor < 0) return false;
  gbm_device *device = gbm_create_device(descriptor);
  if (device == nullptr) {
    close(descriptor);
    return false;
  }
  made.device = device;
  made.descriptor = descriptor;
  return true;
}

void gbm_device_close(GbmDevice &device) noexcept {
  if (device.device != nullptr) gbm_device_destroy(device.device);
  if (device.descriptor >= 0) close(device.descriptor);
  device = GbmDevice{};
}

bool gbm_buffer_make(GbmDevice &device, presentation::PresentExtent extent, bool alpha, GbmBuffer &made) noexcept {
  made = GbmBuffer{};
  if (device.device == nullptr || extent.width == 0U || extent.height == 0U) return false;
  // GBM_FORMAT_*8888 are little-endian words: XRGB8888 is bytes B, G, R, X.
  const std::uint32_t format = alpha ? GBM_FORMAT_ARGB8888 : GBM_FORMAT_XRGB8888;
  // Linear, so the CPU mapping is the buffer's own layout; rendering, so
  // the compositor may sample it.
  const std::uint32_t usage = GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING;
  gbm_bo *buffer = gbm_bo_create(device.device, extent.width, extent.height, format, usage);
  if (buffer == nullptr) return false;
  made.buffer = buffer;
  made.format = format;
  made.modifier = linear_modifier;
  made.stride = gbm_bo_get_stride(buffer);
  return true;
}

bool gbm_buffer_map(GbmBuffer &buffer, presentation::PresentExtent extent) noexcept {
  if (buffer.buffer == nullptr || buffer.pixels != nullptr) return false;
  std::uint32_t stride = 0U;
  void *mapping = nullptr;
  void *pixels = gbm_bo_map(buffer.buffer, 0U, 0U, extent.width, extent.height, GBM_BO_TRANSFER_READ_WRITE, &stride,
                            &mapping);
  if (pixels == nullptr || stride < extent.width * 4U) {
    if (pixels != nullptr) gbm_bo_unmap(buffer.buffer, mapping);
    return false;
  }
  buffer.pixels = static_cast<std::byte *>(pixels);
  buffer.mapped_stride = stride;
  buffer.mapping = mapping;
  return true;
}

void gbm_buffer_unmap(GbmBuffer &buffer) noexcept {
  if (buffer.buffer != nullptr && buffer.pixels != nullptr) gbm_bo_unmap(buffer.buffer, buffer.mapping);
  buffer.pixels = nullptr;
  buffer.mapped_stride = 0U;
  buffer.mapping = nullptr;
}

int gbm_buffer_export(const GbmBuffer &buffer) noexcept {
  if (buffer.buffer == nullptr) return -1;
  const int descriptor = gbm_bo_get_fd(buffer.buffer);
  return descriptor;
}

void gbm_buffer_free(GbmBuffer &buffer) noexcept {
  gbm_buffer_unmap(buffer);
  if (buffer.buffer != nullptr) gbm_bo_destroy(buffer.buffer);
  buffer = GbmBuffer{};
}

} // namespace gui_forms::host::linux_detail
