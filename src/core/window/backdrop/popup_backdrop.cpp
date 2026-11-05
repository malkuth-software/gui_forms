#include "gui_forms/window.hpp"

#include "../../display/recording_painter/recording_painter.hpp"
#include "../popup/popup_attachment.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// An in-surface popup's backdrop (owner, 2026-11-05, as revised): a popup
// drawn inside its owner's surface takes only its chain's tint, composited
// over the owner's pixels beneath the popup's content. GUI.Forms never blurs
// what lies outside a window's own surface; a popup's blur is the system
// material of a native popup window. See
// gui_forms/window/backdrop/backdrop_chain.hpp.

namespace gui_forms {

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
    const Color tint = backdrop_chain_solid(*popup.backdrop_chain);
    if (tint.alpha == 0U) return;
    const Control::Ptr overlay = popup.popup();
    if (!overlay || !(*overlay).is_alive() || (*overlay).surface_root_ != this || !(*overlay).visible_) return;
    const Rect bounds = absolute_bounds_of(*overlay);
    const Rect exposed = Rect::intersection(bounds, rectangle);
    if (exposed.empty()) return;
    // Opaque content occludes it: only current chunks count (a dirty one
    // may no longer cover, so it never does here).
    if (popup_content_covers(overlay, exposed, backdrop_cover_path_)) {
        backdrop_statistics_.occluded = backdrop_statistics_.occluded + 1U;
        return;
    }
    // The fallback: the tint as one opaque colour.
    const bool solid = presentation_settings_.reduced_transparency ||
                       compositor_state_.compositing == CompositorPresence::absent;
    const Color drawn = solid ? Color::rgba(tint.red, tint.green, tint.blue, 255U) : tint;
    candidate.fill_rect(exposed, drawn);
    backdrop_statistics_.draws = backdrop_statistics_.draws + 1U;
    if (solid) backdrop_statistics_.solid_fallbacks = backdrop_statistics_.solid_fallbacks + 1U;
    const double device_pixels = exposed.width * scale_ * exposed.height * scale_;
    backdrop_statistics_.drawn_pixels =
        backdrop_statistics_.drawn_pixels + static_cast<std::uint64_t>(std::max(device_pixels, 0.0));
}

} // namespace gui_forms
