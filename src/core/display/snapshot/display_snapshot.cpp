#include "display_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace gui_forms::detail {

DisplaySnapshot::DisplaySnapshot(DisplayChunk chunk, const Size logical_size, const DeviceScale scale,
                                 const DeviceExtent extent)
    : chunk_(std::move(chunk)), logical_size_(logical_size), scale_(scale), extent_(extent) {}

namespace {

// What a command names that only the UI thread may read, or none.
DisplaySnapshotRefusal command_refusal(const DisplayCommand& command) noexcept {
    switch (command.operation) {
    case DisplayOperation::draw_live_surface: return DisplaySnapshotRefusal::live_surface;
    case DisplayOperation::draw_image:
    case DisplayOperation::draw_image_region:
    case DisplayOperation::draw_image_region_sampled:
    case DisplayOperation::fill_image_pattern: return DisplaySnapshotRefusal::image;
    case DisplayOperation::draw_prepared_text: return DisplaySnapshotRefusal::prepared_text;
    default:
        // Every other operation reads nothing UI-thread-only by its kind;
        // the flags below cover what it may still carry.
        break;
    }
    if (command.live_surface) return DisplaySnapshotRefusal::live_surface;
    if (command.prepared_text) return DisplaySnapshotRefusal::prepared_text;
    return DisplaySnapshotRefusal::none;
}

// A raster the snapshot owns with `source`'s pixels, or null when the copy
// cannot be made (the source's own extent and scale were admitted once, so
// only allocation fails here).
std::shared_ptr<const PartRaster> copy_part_raster(const PartRaster& source) {
    std::shared_ptr<PartRaster> copy = PartRaster::create(source.extent(), source.scale());
    if (!copy) return nullptr;
    const PartRasterView target = (*copy).view();
    const std::span<const std::uint8_t> pixels = source.pixels();
    // Both are tight rows of the same extent (PartRaster: row_bytes is
    // width * 4), so the buffers have the same size.
    if (target.pixels.size() != pixels.size()) return nullptr;
    if (!pixels.empty()) std::memcpy(target.pixels.data(), pixels.data(), pixels.size());
    std::shared_ptr<const PartRaster> shared = std::move(copy);
    return shared;
}

// The device extent of `logical_size` at `scale`, or none past
// maximum_part_raster_side.
std::optional<DeviceExtent> device_extent_of(const Size logical_size, const DeviceScale scale) noexcept {
    const double width = std::ceil(logical_size.width * scale.value);
    const double height = std::ceil(logical_size.height * scale.value);
    const double side = static_cast<double>(maximum_part_raster_side);
    if (!(width >= 1.0 && height >= 1.0 && width <= side && height <= side)) return std::nullopt;
    const DeviceExtent extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    return extent;
}

} // namespace

DisplaySnapshotResult make_display_snapshot(const DisplayChunk& chunk, const Size logical_size,
                                            const DeviceScale scale) {
    DisplaySnapshotResult result{};
    result.refusal = DisplaySnapshotRefusal::empty;
    const bool sized = std::isfinite(logical_size.width) && std::isfinite(logical_size.height) &&
                       logical_size.width > 0.0 && logical_size.height > 0.0;
    const bool scaled = std::isfinite(scale.value) && scale.value >= minimum_device_scale &&
                        scale.value <= maximum_device_scale;
    if (chunk.commands().empty() || !sized || !scaled) return result;
    const std::optional<DeviceExtent> extent = device_extent_of(logical_size, scale);
    if (!extent) return result;

    // Validated whole before anything is copied.
    for (const DisplayCommand& command : chunk.commands()) {
        const DisplaySnapshotRefusal refusal = command_refusal(command);
        if (refusal != DisplaySnapshotRefusal::none) {
            result.refusal = refusal;
            return result;
        }
    }

    DisplayCommandList commands;
    commands.reserve(chunk.commands().size());
    for (const DisplayCommand& command : chunk.commands()) {
        DisplayCommand copy = command;
        // Skipped: the window's painter fills it, and the baker's painter
        // keeps its own.
        copy.retained_cache.reset();
        if (command.part_raster) {
            copy.part_raster = copy_part_raster(*command.part_raster);
            if (!copy.part_raster) return result;
        }
        if (command.surface_material) {
            copy.surface_material = std::make_shared<const SurfaceMaterial>(*command.surface_material);
        }
        commands.push_back(std::move(copy));
    }
    DisplayChunk owned(chunk.generation(), chunk.plane(), chunk.logical_bounds(), std::move(commands),
                       chunk.opaque_cover());
    result.snapshot = std::make_shared<const DisplaySnapshot>(std::move(owned), logical_size, scale, *extent);
    result.refusal = DisplaySnapshotRefusal::none;
    return result;
}

} // namespace gui_forms::detail
