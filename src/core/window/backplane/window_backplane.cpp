#include "gui_forms/window.hpp"

#include "../../display/chunk/display_chunk.hpp"
#include "../../display/replay/replay_display_chunk.hpp"
#include "backplane_composite.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

// The window's backplane image (E9-B; owner, 2026-10-22, option 1, and
// stage T5's replay, C6). See backplane_composite.hpp.

namespace gui_forms {
namespace {

// The direction of a copy between a part raster's rows and a framebuffer's,
// both premultiplied BGRA8.
enum class FramebufferCopy : std::uint8_t {
    into_framebuffer,
    from_framebuffer,
};

// Copies `rows` rows of `width` pixels between `image` and `framebuffer`, as
// they are. The caller has checked that both hold at least that many rows of
// that many pixels; the two buffers never overlap.
void copy_framebuffer_rows(const PartRasterView& image, PaintFramebuffer& framebuffer, std::uint32_t width,
                           std::uint32_t rows, FramebufferCopy direction) noexcept {
    const std::span<std::byte> pixels = framebuffer.pixels();
    const std::size_t framebuffer_row = framebuffer.row_bytes();
    const std::size_t row_length = static_cast<std::size_t>(width) * 4U;
    // The direction picks which buffer is written, and each buffer's row pitch.
    std::uint8_t* const raster_start = image.pixels.data();
    std::uint8_t* const native_start = reinterpret_cast<std::uint8_t*>(pixels.data());
    const bool into_framebuffer = direction == FramebufferCopy::into_framebuffer;
    std::uint8_t* const to_start = into_framebuffer ? native_start : raster_start;
    const std::uint8_t* const from_start = into_framebuffer ? raster_start : native_start;
    const std::size_t to_row_bytes = into_framebuffer ? framebuffer_row : image.row_bytes;
    const std::size_t from_row_bytes = into_framebuffer ? image.row_bytes : framebuffer_row;
    for (std::size_t row = 0U; row < rows; ++row) {
        std::memcpy(to_start + row * to_row_bytes, from_start + row * from_row_bytes, row_length);
    }
}

} // namespace

SurfaceRoot::EngineBackplane SurfaceRoot::engine_backplane(Point raster_origin, DeviceExtent extent, DeviceScale scale,
                                                 std::uint32_t margin) {
    EngineBackplane lent{};
    lent.generation = backplane_generation_;
    const double grown = static_cast<double>(margin);
    lent.origin = Point{raster_origin.x * scale.value - grown, raster_origin.y * scale.value - grown};
    BackplaneRegion region{};
    region.origin = lent.origin;
    region.extent = DeviceExtent{extent.width + margin * 2U, extent.height + margin * 2U};
    region.scale = scale;
    const std::shared_ptr<const PartRaster> held =
        engine_parts_.held_backplane(region, backplane_generation_, paint_leases_completed_);
    if (held) {
        lent.image = held;
        return lent;
    }
    const std::shared_ptr<PartRaster> made = PartRaster::create(region.extent, scale);
    if (!made || theme_ == nullptr) return lent;
    const PartRasterView image = (*made).view();
    // The window's backplane base for its grant, as the presentation
    // paints it.
    ControlVisualContext context;
    context.surface = active_ ? ControlSurfaceState::normal : ControlSurfaceState::deactivated;
    context.high_contrast = presentation_settings_.high_contrast;
    context.dark = presentation_settings_.colour_scheme == ColourScheme::dark;
    const SurfaceMaterial material = backplane_base(context);
    engine_parts_.bake_backplane_material(material, client_size_, region, &image_resources(), image,
                                          paint_leases_completed_);
    if (engine_parts_.patterns_waiting()) arm_engine_idle();
    // Then the backplane plane over it: replayed whole by the window's
    // renderer when it can (C6), else its engine part rasters alone.
    std::uint64_t skipped = 0U;
    const double width = static_cast<double>(region.extent.width) / scale.value;
    const double height = static_cast<double>(region.extent.height) / scale.value;
    const Rect covered{region.origin.x / scale.value, region.origin.y / scale.value, width, height};
    const bool replayed = replay_backplane(covered, region, image, false);
    if (!replayed && root_) composite_backplane_subtree(root_, covered, region, image, skipped);
    metrics_.set_backplane_replay(replayed);
    engine_parts_.hold_backplane(region, backplane_generation_, made, skipped, replayed, paint_leases_completed_);
    lent.image = made;
    return lent;
}

bool SurfaceRoot::replay_backplane(Rect covered, const BackplaneRegion& region, const PartRasterView& image,
                                   bool every_plane) {
    if (framebuffer_painter_ == nullptr || !root_) return false;
    const DeviceExtent extent = region.extent;
    if (extent.width == 0U || extent.height == 0U) return false;
    const double scale = region.scale.value;
    // A quarter pixel short of the extent, so that the framebuffer's
    // rounding up to whole pixels gives the extent exactly whatever the
    // scale's binary error.
    const Size logical{(static_cast<double>(extent.width) - 0.25) / scale,
                       (static_cast<double>(extent.height) - 0.25) / scale};
    // A popup backdrop's source and the backplane each keep their own.
    std::unique_ptr<PaintFramebuffer>& slot = every_plane ? backdrop_framebuffer_ : backplane_framebuffer_;
    double& slot_scale = every_plane ? backdrop_framebuffer_scale_ : backplane_framebuffer_scale_;
    PaintFramebuffer* framebuffer = slot.get();
    const bool reusable = framebuffer != nullptr && slot_scale == scale &&
                          (*framebuffer).width() == extent.width && (*framebuffer).height() == extent.height;
    if (!reusable) {
        slot.reset();
        slot_scale = 0.0;
        slot = (*framebuffer_painter_).create_framebuffer(logical, scale);
        slot_scale = scale;
        framebuffer = slot.get();
    }
    // Validated before anything is drawn: the exact extent, whole rows.
    if (framebuffer == nullptr || (*framebuffer).width() != extent.width ||
        (*framebuffer).height() != extent.height) {
        return false;
    }
    const std::size_t row_length = static_cast<std::size_t>(extent.width) * 4U;
    const std::size_t rows = static_cast<std::size_t>(extent.height);
    const std::size_t framebuffer_row = (*framebuffer).row_bytes();
    if (framebuffer_row < row_length || (*framebuffer).pixels().size() < framebuffer_row * rows ||
        image.row_bytes < row_length || image.pixels.size() < image.row_bytes * rows) {
        return false;
    }
    // The base, then every backplane chunk over it at its place.
    copy_framebuffer_rows(image, *framebuffer, extent.width, extent.height, FramebufferCopy::into_framebuffer);
    if (!(*framebuffer).begin(image_resources(), Rect{0.0, 0.0, logical.width, logical.height})) return false;
    Painter& painter = (*framebuffer).painter();
    painter.save();
    painter.translate(Point{-covered.x, -covered.y});
    if (every_plane) {
        for (std::size_t index = 0U; index < paint_plane_count; ++index) {
            replay_backplane_subtree(root_, covered, painter, static_cast<PaintPlane>(index));
        }
    } else {
        replay_backplane_subtree(root_, covered, painter, PaintPlane::backplane);
    }
    painter.restore();
    (*framebuffer).end();
    copy_framebuffer_rows(image, *framebuffer, extent.width, extent.height, FramebufferCopy::from_framebuffer);
    return true;
}

void SurfaceRoot::replay_backplane_subtree(const Control::Ptr& control, Rect clip, Painter& painter,
                                           PaintPlane plane) const {
    const Control& node = *control;
    if (!node.is_alive() || node.surface_root_ != this || !node.visible.read() || node.layout_collapsed_) return;
    const Rect bounds = absolute_bounds_of(node);
    const Rect visual = visual_bounds_of(node, node.effective_visual_outsets());
    const Rect drawn = Rect::intersection(visual, clip);
    const std::shared_ptr<const detail::DisplayChunk>& chunk = node.display_chunk_;
    if (!drawn.empty() && node.paint_plane_ == plane && chunk && (*chunk).plane() == plane) {
        // As the window paint replays a chunk: at the control's corner,
        // clipped to what it draws there.
        painter.save();
        painter.translate(Point{bounds.x, bounds.y});
        painter.clip_rect(Rect{drawn.x - bounds.x, drawn.y - bounds.y, drawn.width, drawn.height});
        static_cast<void>(detail::replay_display_chunk(*chunk, painter));
        painter.restore();
    }
    const Rect viewport = node.child_viewport_rectangle();
    const Rect children_clip =
        Rect::intersection(Rect{bounds.x + viewport.x, bounds.y + viewport.y, viewport.width, viewport.height}, clip);
    if (children_clip.empty()) return;
    for (const Control::Ptr& child : node.children_) {
        if (!child) continue;
        replay_backplane_subtree(child, children_clip, painter, plane);
    }
}

void SurfaceRoot::composite_backplane_subtree(const Control::Ptr& control, Rect clip, const BackplaneRegion& region,
                                         const PartRasterView& image, std::uint64_t& skipped) const {
    const Control& node = *control;
    if (!node.is_alive() || node.surface_root_ != this || !node.visible.read() || node.layout_collapsed_) return;
    const Rect bounds = absolute_bounds_of(node);
    const Rect visual = visual_bounds_of(node, node.effective_visual_outsets());
    const Rect drawn = Rect::intersection(visual, clip);
    const std::shared_ptr<const detail::DisplayChunk>& chunk = node.display_chunk_;
    if (!drawn.empty() && node.paint_plane_ == PaintPlane::backplane && chunk &&
        (*chunk).plane() == PaintPlane::backplane) {
        skipped = skipped + detail::composite_backplane_chunk(*chunk, Point{bounds.x, bounds.y}, drawn, region, image);
    }
    const Rect viewport = node.child_viewport_rectangle();
    const Rect children_clip =
        Rect::intersection(Rect{bounds.x + viewport.x, bounds.y + viewport.y, viewport.width, viewport.height}, clip);
    if (children_clip.empty()) return;
    for (const Control::Ptr& child : node.children_) {
        if (!child) continue;
        composite_backplane_subtree(child, children_clip, region, image, skipped);
    }
}

} // namespace gui_forms
