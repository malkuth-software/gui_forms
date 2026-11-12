#pragma once

// The authority of the theme engine over a root's paint (B10): the keyed
// part records and their rasters, the paint place and the backplane under a
// part, the per-paint assembly slots, the pointer the engine reads, and the
// part transitions and ambient effects the motion clock runs.
//
// Holders: ThemeEngine and EffectInstance (during a paint), PartMotion (a
// part's transition values), MotionClock (ambient frames).
//
// Stale use: ThemeEngine and EffectInstance reach the root from the control
// being painted (attached_surface_root(), null once it is detached, when
// they paint nothing of the root's) and only during that root's paint.
// PartMotion lives in a record of the root's engine part table and the
// motion clock is a member of the root, so both are destroyed with it; an
// ambient frame or repaint names its record by slot and generation, and a
// stale pair answers false and repaints nothing. The borrowed assembly
// slots are valid within one ThemeEngine::paint_planned call. The functions
// are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gui_forms::detail {

class SurfaceEngineAccess final {
public:
    SurfaceEngineAccess() = delete;

    using PaintPlace = SurfaceRoot::EnginePaintPlace;
    using Backplane = SurfaceRoot::EngineBackplane;
    using UnkeptAssemblyArray = std::array<UnkeptPartAssembly, unkept_part_assembly_count>;

    // The first inline-styled part under a theme with rules of its own
    // (SurfaceRoot::note_inline_with_theme).
    static void note_inline_with_theme(SurfaceRoot& root, const Control& control) {
        root.note_inline_with_theme(control);
    }
    // Makes or updates a keyed part's record (SurfaceRoot::note_engine_part).
    [[nodiscard]] static std::optional<PartRecordHandle> note_part(SurfaceRoot& root, const Control& control,
                                                                   const PartKey& key, AnatomyStateSet states) {
        const std::optional<PartRecordHandle> result = root.note_engine_part(control, key, states);
        return result;
    }
    // Where a part drawn with `painter` lands (SurfaceRoot::engine_paint_place).
    [[nodiscard]] static PaintPlace paint_place(const SurfaceRoot& root, const Control& control,
                                                const Painter& painter) {
        const PaintPlace result = root.engine_paint_place(control, painter);
        return result;
    }
    // What a keyed part's paint read and where it was drawn.
    static void note_paint(SurfaceRoot& root, PartRecordHandle handle, std::uint64_t inputs_read,
                           const Control& painted_by, Rect extent) {
        root.note_engine_paint(handle, inputs_read, painted_by, extent);
    }
    static void note_backplane_reader(SurfaceRoot& root, PartRecordHandle handle, bool reads) noexcept {
        root.note_engine_backplane_reader(handle, reads);
    }
    // The record `handle` names; null when the handle is stale.
    [[nodiscard]] static EnginePartRecord* part_record(SurfaceRoot& root, PartRecordHandle handle) noexcept {
        EnginePartRecord* const result = root.engine_part_record(handle);
        return result;
    }
    // The per-paint assembly slots (SurfaceRoot::engine_paint_material,
    // engine_unkept_assemblies, engine_unkept_next): borrowed within one
    // paint_planned call.
    [[nodiscard]] static SurfaceMaterial& paint_material(SurfaceRoot& root) noexcept {
        SurfaceMaterial& result = root.engine_paint_material();
        return result;
    }
    [[nodiscard]] static UnkeptAssemblyArray& unkept_assemblies(SurfaceRoot& root) noexcept {
        UnkeptAssemblyArray& result = root.engine_unkept_assemblies();
        return result;
    }
    [[nodiscard]] static std::size_t& unkept_next(SurfaceRoot& root) noexcept {
        std::size_t& result = root.engine_unkept_next();
        return result;
    }
    // The pointer's window position, or none while it is outside the window.
    [[nodiscard]] static std::optional<Point> pointer(const SurfaceRoot& root) noexcept {
        const std::optional<Point> result = root.engine_pointer();
        return result;
    }
    [[nodiscard]] static PartRasterResult part_raster(SurfaceRoot& root, PartRecordHandle handle,
                                                      const PartRasterRequest& request) {
        const PartRasterResult result = root.engine_part_raster(handle, request);
        return result;
    }
    [[nodiscard]] static PartRasterResult unkeyed_raster(SurfaceRoot& root, const PartRasterRequest& request) {
        const PartRasterResult result = root.engine_unkeyed_raster(request);
        return result;
    }
    // The finish tile held for `key`, or null once queued
    // (SurfaceRoot::engine_finish_tile).
    [[nodiscard]] static std::shared_ptr<const PartRaster> finish_tile(SurfaceRoot& root, PartRecordHandle waiter,
                                                                       const FinishTileKey& key) {
        std::shared_ptr<const PartRaster> result = root.engine_finish_tile(waiter, key);
        return result;
    }
    static void queue_keyframe(SurfaceRoot& root, const PartRasterRequest& request) {
        root.engine_queue_keyframe(request);
    }
    static void run_ambient(SurfaceRoot& root, PartRecordHandle handle, FrameInterval interval) {
        root.run_engine_ambient(handle, interval);
    }
    static void stop_ambient(SurfaceRoot& root, PartRecordHandle handle) noexcept {
        root.stop_engine_ambient(handle);
    }
    // The window's backplane under a part (SurfaceRoot::engine_backplane).
    [[nodiscard]] static Backplane backplane(SurfaceRoot& root, Point raster_origin, DeviceExtent extent,
                                             DeviceScale scale, std::uint32_t margin) {
        const Backplane result = root.engine_backplane(raster_origin, extent, scale, margin);
        return result;
    }
    // One of a part's transition values stepped (PartMotion).
    static void part_moved(SurfaceRoot& root, PartRecordHandle handle, bool settled) {
        root.engine_part_moved(handle, settled);
    }
    // One ambient frame for the record in `slot` with `generation`
    // (MotionClock); false when the effect ends.
    static bool ambient_frame(SurfaceRoot& root, std::uint32_t slot, std::uint32_t generation, std::uint64_t& seen) {
        const bool result = root.engine_ambient_frame(slot, generation, seen);
        return result;
    }
    // The part repaints at its static pose (MotionClock).
    static void repaint_part(SurfaceRoot& root, std::uint32_t slot, std::uint32_t generation) {
        root.repaint_engine_part(slot, generation);
    }
};

} // namespace gui_forms::detail
