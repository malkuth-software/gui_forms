#include "gui_forms/window.hpp"

#include "../../display/recording_painter/recording_painter.hpp"
#include "../popup/popup_attachment.hpp"
#include "backdrop_cache.hpp"
#include "backdrop_image.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// A popup's backdrop (owner, 2026-11-05, option 2): the popup counts as a
// window, and its chain is applied to its owner's finished pixels under it
// (the owner's backplane and every plane of its application root, as last
// recorded; never another popup, so never another backdrop), cached tile by
// tile in the popup's attachment and computed only where the popup's own
// content leaves it exposed. See gui_forms/window/backdrop/backdrop_chain.hpp.

namespace gui_forms {
namespace {

// A strip's place in the window, in window logical units.
Rect strip_rectangle(const detail::BackdropCache& cache, const detail::BackdropStrip& strip) noexcept {
    const double scale = cache.scale();
    const Rect bounds = cache.bounds();
    const Rect logical{bounds.x + static_cast<double>(strip.left) / scale,
                       bounds.y + static_cast<double>(strip.top) / scale,
                       static_cast<double>(strip.width) / scale, static_cast<double>(strip.height) / scale};
    return logical;
}

// The pixels of `view` from (`left`, `top`), `extent` of them: a view into
// the same storage. The caller has checked that they lie inside `view`.
PartRasterView sub_view(const PartRasterView& view, std::uint32_t left, std::uint32_t top,
                        DeviceExtent extent) noexcept {
    PartRasterView part{};
    const std::size_t start = static_cast<std::size_t>(top) * view.row_bytes + static_cast<std::size_t>(left) * 4U;
    const std::size_t length =
        static_cast<std::size_t>(extent.height - 1U) * view.row_bytes + static_cast<std::size_t>(extent.width) * 4U;
    part.pixels = view.pixels.subspan(start, length);
    part.extent = extent;
    part.row_bytes = view.row_bytes;
    return part;
}

} // namespace

bool SurfaceRoot::popup_content_covers(const Control::Ptr& overlay, const Rect rectangle,
                                       std::vector<Control::Ptr>& path) const {
    for (std::size_t index = 0U; index < paint_plane_count; ++index) {
        path.clear();
        if (find_paint_cover(overlay, rectangle, static_cast<PaintPlane>(index), path)) return true;
    }
    return false;
}

void SurfaceRoot::paint_popup_backdrop(detail::RecordingPainter& candidate, detail::PopupAttachment& popup,
                                       const Rect rectangle) {
    // Under high contrast there is no backdrop.
    if (!popup.backdrop_chain || presentation_settings_.high_contrast) return;
    const Control::Ptr overlay = popup.popup();
    if (!overlay || !(*overlay).is_alive() || (*overlay).surface_root_ != this || !(*overlay).visible_) return;
    const Rect bounds = absolute_bounds_of(*overlay);
    const Rect exposed = Rect::intersection(bounds, rectangle);
    if (exposed.empty()) return;
    // Opaque content occludes it: only current chunks count (a dirty one
    // may no longer cover, so it never does here).
    std::vector<Control::Ptr>& path = backdrop_cover_path_;
    if (popup_content_covers(overlay, exposed, path)) {
        backdrop_statistics_.occluded = backdrop_statistics_.occluded + 1U;
        return;
    }
    const BackdropChain& chain = *popup.backdrop_chain;
    const bool solid = presentation_settings_.reduced_transparency ||
                       compositor_state_.compositing == CompositorPresence::absent || framebuffer_painter_ == nullptr;
    if (!solid && paint_popup_backdrop_cache(popup, overlay, chain, bounds, exposed)) {
        candidate.save();
        candidate.clip_rect(exposed);
        candidate.draw_part_raster(popup.backdrop_cache.raster(), Point{bounds.x, bounds.y}, 1.0);
        candidate.restore();
        backdrop_statistics_.draws = backdrop_statistics_.draws + 1U;
        const double device_pixels = exposed.width * scale_ * exposed.height * scale_;
        backdrop_statistics_.drawn_pixels =
            backdrop_statistics_.drawn_pixels + static_cast<std::uint64_t>(std::max(device_pixels, 0.0));
        return;
    }
    // The fallback: the chain's tint as one solid colour.
    candidate.fill_rect(exposed, backdrop_chain_solid(chain));
    backdrop_statistics_.solid_fallbacks = backdrop_statistics_.solid_fallbacks + 1U;
}

bool SurfaceRoot::paint_popup_backdrop_cache(detail::PopupAttachment& popup, const Control::Ptr& overlay,
                                             const BackdropChain& chain, const Rect bounds, const Rect exposed) {
    detail::BackdropCache& cache = popup.backdrop_cache;
    if (!cache.prepare(chain, bounds, scale_)) return false;
    std::vector<detail::BackdropStrip>& missing = popup.backdrop_missing;
    cache.missing_strips(exposed, missing);
    // Tiles whose exposed part opaque content covers are left uncomputed;
    // the rest are recomputed in runs along each tile row.
    std::vector<detail::BackdropStrip>& strips = popup.backdrop_strips;
    strips.clear();
    std::vector<Control::Ptr>& path = backdrop_cover_path_;
    for (const detail::BackdropStrip& run : missing) {
        detail::BackdropStrip pending{};
        for (std::uint32_t left = run.left; left < run.left + run.width; left = left + detail::BackdropCache::tile_side) {
            detail::BackdropStrip tile{};
            tile.left = left;
            tile.top = run.top;
            tile.width = std::min(detail::BackdropCache::tile_side, run.left + run.width - left);
            tile.height = run.height;
            const Rect needed = Rect::intersection(strip_rectangle(cache, tile), exposed);
            const bool covered = needed.empty() || popup_content_covers(overlay, needed, path);
            if (!covered && pending.width != 0U) {
                pending.width = pending.width + tile.width;
                continue;
            }
            if (!covered) {
                pending = tile;
                continue;
            }
            if (pending.width != 0U) strips.push_back(pending);
            pending = detail::BackdropStrip{};
        }
        if (pending.width != 0U) strips.push_back(pending);
    }
    if (strips.empty()) return true;
    // One source for every strip: their bounds, grown by the margin, of the
    // owner's finished pixels.
    std::uint32_t left = strips.front().left;
    std::uint32_t top = strips.front().top;
    std::uint32_t right = left + strips.front().width;
    std::uint32_t bottom = top + strips.front().height;
    for (const detail::BackdropStrip& strip : strips) {
        left = std::min(left, strip.left);
        top = std::min(top, strip.top);
        right = std::max(right, strip.left + strip.width);
        bottom = std::max(bottom, strip.top + strip.height);
    }
    const std::uint32_t margin = cache.margin();
    const double scale = scale_;
    BackplaneRegion region{};
    region.origin = Point{bounds.x * scale + static_cast<double>(left) - static_cast<double>(margin),
                          bounds.y * scale + static_cast<double>(top) - static_cast<double>(margin)};
    region.extent = DeviceExtent{right - left + margin * 2U, bottom - top + margin * 2U};
    region.scale = DeviceScale{scale};
    const std::shared_ptr<PartRaster> source = PartRaster::create(region.extent, region.scale);
    if (!source || theme_ == nullptr) return false;
    const PartRasterView source_view = (*source).view();
    ControlVisualContext context;
    context.surface = active_ ? ControlSurfaceState::normal : ControlSurfaceState::deactivated;
    context.high_contrast = presentation_settings_.high_contrast;
    context.dark = presentation_settings_.colour_scheme == ColourScheme::dark;
    const SurfaceMaterial material = backplane_base(context);
    engine_parts_.bake_backplane_material(material, client_size_, region, &image_resources(), source_view,
                                          paint_leases_completed_);
    if (engine_parts_.patterns_waiting()) arm_engine_idle();
    const Rect covered{region.origin.x / scale, region.origin.y / scale,
                       static_cast<double>(region.extent.width) / scale,
                       static_cast<double>(region.extent.height) / scale};
    if (!replay_backplane(covered, region, source_view, true)) return false;
    PartRaster* const target = cache.writable_raster();
    if (target == nullptr) return false;
    const PartRasterView target_view = (*target).view();
    for (const detail::BackdropStrip& strip : strips) {
        const DeviceExtent grown{strip.width + margin * 2U, strip.height + margin * 2U};
        const PartRasterView strip_source = sub_view(source_view, strip.left - left, strip.top - top, grown);
        const PartRasterView strip_target =
            sub_view(target_view, strip.left, strip.top, DeviceExtent{strip.width, strip.height});
        if (!detail::compute_backdrop(chain, scale, strip_source, margin, strip_target, cache.work())) return false;
        cache.mark_valid(strip);
        backdrop_statistics_.computations = backdrop_statistics_.computations + 1U;
        backdrop_statistics_.computed_pixels = backdrop_statistics_.computed_pixels +
                                               static_cast<std::uint64_t>(strip.width) * strip.height;
    }
    return true;
}

void SurfaceRoot::invalidate_popup_backdrops(const Rect damage) noexcept {
    for (const std::shared_ptr<detail::PopupAttachment>& popup : popups_) {
        if (!(*popup).backdrop_chain) continue;
        const std::uint64_t dropped = (*popup).backdrop_cache.invalidate(damage);
        backdrop_statistics_.invalidated_tiles = backdrop_statistics_.invalidated_tiles + dropped;
    }
}

bool SurfaceRoot::popup_subtree(const Control& control) const noexcept {
    const Control* top = &control;
    Control::Ptr parent = control.parent();
    while (parent) {
        top = parent.get();
        parent = (*parent).parent();
    }
    return top != root_.get();
}

} // namespace gui_forms
