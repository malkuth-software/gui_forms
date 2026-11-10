#pragma once

#include "../chunk/display_chunk.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms::detail {

// Why a recording cannot be baked away from the thread that made it.
enum class DisplaySnapshotRefusal : std::uint8_t {
    none,
    // Nothing to bake: no commands, or an empty or oversized extent.
    empty,
    // A live surface is mutable content owned by its producer.
    live_surface,
    // An image is named by its ImageId in the window's registry, which only
    // the UI thread reads.
    image,
    // Prepared text is laid out against the window's text authority.
    prepared_text,
};

// An immutable copy of one recording, made on the UI thread for a baker on
// another thread (APP_WINDOW_RULES_DESIGN Part S, phase 2: "baking on a
// worker from an immutable display snapshot").
//
// Nothing is shared with the recording or its window across threads. The
// recorded chunk's caches are COPIED or SKIPPED, never borrowed:
// - a retained draw cache (RetainedDrawCache, renderer data the window's
//   painter fills on replay) is SKIPPED: the copy replays its command
//   uncached, and the baker's painter makes its own;
// - a part raster is COPIED pixel for pixel into a raster the snapshot
//   owns;
// - a baked surface material is COPIED into a material the snapshot owns;
// - strings and gradient stops are copied with their command.
// A recording that names what only the UI thread may read (a live surface,
// an image, prepared text) is refused instead (DisplaySnapshotRefusal).
//
// The snapshot is shared read-only (std::shared_ptr<const DisplaySnapshot>)
// with the baker, which only replays it; it lives until the last holder lets
// go, on whichever thread that is.
class DisplaySnapshot final {
public:
    // The recording `chunk`, whose logical bounds start at its origin, to be
    // baked into `logical_size` at `scale`.
    [[nodiscard]] const DisplayChunk& chunk() const noexcept { return chunk_; }
    [[nodiscard]] Size logical_size() const noexcept { return logical_size_; }
    [[nodiscard]] DeviceScale scale() const noexcept { return scale_; }
    // The device pixels a bake fills: each logical extent times the scale,
    // rounded up.
    [[nodiscard]] DeviceExtent device_extent() const noexcept { return extent_; }

    DisplaySnapshot(DisplayChunk chunk, Size logical_size, DeviceScale scale, DeviceExtent extent);

private:
    DisplayChunk chunk_;
    Size logical_size_{};
    DeviceScale scale_{};
    DeviceExtent extent_{};
};

struct DisplaySnapshotResult final {
    std::shared_ptr<const DisplaySnapshot> snapshot{};
    DisplaySnapshotRefusal refusal{DisplaySnapshotRefusal::none};
};

// Copies `chunk` under the rules above. The source chunk is only read; it
// stays the window's. Refused (no snapshot) for a chunk without commands, a
// logical size that is not finite and positive, a scale outside
// [minimum_device_scale, maximum_device_scale], a device extent past
// maximum_part_raster_side, or a command the rules refuse. UI thread.
[[nodiscard]] DisplaySnapshotResult make_display_snapshot(const DisplayChunk& chunk, Size logical_size,
                                                          DeviceScale scale);

} // namespace gui_forms::detail
