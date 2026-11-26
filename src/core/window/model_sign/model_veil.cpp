// The AI-mode veil of the automation's sign (owner, 2026-11-24): grey
// barber-pole stripes over a window's control surfaces while a model holds
// control, and the X cursor over the window. The stripes take the
// progress bar's stripe geometry (progress_bar.cpp, owner rulings of
// 2026-11-11): the band 0.4 of the pitch, leaning 45 degrees back. They
// stand still (round 4: the decoration costs almost nothing, and moving
// stripes repainted every control surface under them about 25 times a
// second at 2x). One pattern runs in window space, so the stripes of
// neighbouring controls line up.
//
// Control surfaces are found by semantic role: buttons, fields, lists,
// scroll bars, tabs, menus, toolbars and the like are striped whole; a
// container (a group, a pane, a tab page) is searched below; content (a
// canvas, an image, a document, a label) is left clear. The surfaces are
// gathered again when the window's semantics or layout change.

#include "model_sign_state.hpp"

#include "gui_forms/window.hpp"
#include "../../display/recording_painter/recording_painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace gui_forms {

namespace {

// The stripes' geometry, in logical units.
constexpr double veil_pitch = 16.0;
constexpr double veil_band_ratio = 0.4;
// tan(45 degrees): the far side of a band runs one thickness ahead.
constexpr double veil_slant_per_unit = 1.0;
// Grey, half covering in light themes and dark alike.
constexpr Color veil_light{96U, 96U, 96U, 92U};
constexpr Color veil_dark{160U, 160U, 160U, 84U};
// The deepest control tree the search walks.
constexpr std::size_t veil_depth_limit = 256U;

enum class VeilKind : std::uint8_t {
    // Striped whole.
    surface,
    // Searched below.
    container,
    // Left clear, and not searched.
    content,
};

VeilKind veil_kind(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::button:
    case SemanticRole::check_box:
    case SemanticRole::radio_button:
    case SemanticRole::link:
    case SemanticRole::text_box:
    case SemanticRole::list:
    case SemanticRole::combo_box:
    case SemanticRole::slider:
    case SemanticRole::scroll_bar:
    case SemanticRole::tab:
    case SemanticRole::numeric_field:
    case SemanticRole::date_picker:
    case SemanticRole::calendar:
    case SemanticRole::property_grid:
    case SemanticRole::menu_bar:
    case SemanticRole::menu_bar_item:
    case SemanticRole::menu:
    case SemanticRole::menu_item:
    case SemanticRole::toolbar:
    case SemanticRole::radio_group:
    case SemanticRole::split_button:
    case SemanticRole::menu_button:
    case SemanticRole::tree:
    case SemanticRole::table:
    case SemanticRole::tree_grid:
    case SemanticRole::status_bar:
    case SemanticRole::toggle_switch:
    case SemanticRole::search_box:
        return VeilKind::surface;
    case SemanticRole::image:
    case SemanticRole::figure:
    case SemanticRole::document:
    case SemanticRole::static_text:
    case SemanticRole::heading:
    case SemanticRole::progress_bar:
    case SemanticRole::meter:
    case SemanticRole::tool_tip:
    case SemanticRole::live_region:
        return VeilKind::content;
    case SemanticRole::generic:
    case SemanticRole::group:
    case SemanticRole::list_item:
    case SemanticRole::check_list_item:
    case SemanticRole::tab_group:
    case SemanticRole::split_pane:
    case SemanticRole::date_cell:
    case SemanticRole::property_group:
    case SemanticRole::property_row:
    case SemanticRole::separator:
    case SemanticRole::pane:
    case SemanticRole::tab_panel:
    case SemanticRole::tree_item:
    case SemanticRole::row:
    case SemanticRole::cell:
    case SemanticRole::column_header:
    case SemanticRole::row_header:
    case SemanticRole::dialog:
    case SemanticRole::count:
        return VeilKind::container;
    }
    return VeilKind::container;
}

struct VeilVisit final {
    Control::Ptr control{};
    std::size_t depth{};
};

} // namespace

void SurfaceRoot::gather_veil_surfaces(detail::ModelSignState& sign) {
    sign.veil_rects.clear();
    sign.veil_semantics = semantic_generation_;
    sign.veil_layout = layout_pass_sequence_;
    if (!root_ || !(*root_).is_alive()) return;
    std::vector<VeilVisit> pending{};
    pending.push_back(VeilVisit{root_, 0U});
    while (!pending.empty()) {
        const VeilVisit visit = std::move(pending.back());
        pending.pop_back();
        const Control::Ptr& control = visit.control;
        if (!control || !(*control).is_alive() || !(*control).effectively_visible()) continue;
        const SemanticDescriptor descriptor = (*control).semantic_descriptor();
        const VeilKind kind = visit.depth == 0U ? VeilKind::container : veil_kind(descriptor.role);
        if (kind == VeilKind::content) continue;
        if (kind == VeilKind::surface) {
            const Rect bounds = absolute_bounds_of(*control);
            if (!bounds.empty() && bounds.finite()) sign.veil_rects.push_back(bounds);
            continue;
        }
        // A container's own parts that are surfaces (a scrolling control's
        // scroll bars, a tab control's tabs, a ribbon's commands inside its
        // groups: parts nest, round 3), then its children.
        const std::vector<SemanticNode> parts = (*control).semantic_virtual_children();
        std::vector<const SemanticNode*> part_work{};
        for (const SemanticNode& part : parts) part_work.push_back(&part);
        while (!part_work.empty()) {
            const SemanticNode& part = *part_work.back();
            part_work.pop_back();
            const VeilKind part_kind = veil_kind(part.role);
            if (part_kind == VeilKind::surface) {
                if (!part.bounds.empty() && part.bounds.finite()) sign.veil_rects.push_back(part.bounds);
                continue;
            }
            if (part_kind == VeilKind::content) continue;
            for (const SemanticNode& inner : part.children) part_work.push_back(&inner);
        }
        if (visit.depth >= veil_depth_limit) continue;
        for (const Control::Ptr& child : (*control).children()) pending.push_back(VeilVisit{child, visit.depth + 1U});
    }
}

void SurfaceRoot::damage_veil(const detail::ModelSignState& sign) {
    for (const Rect& rect : sign.veil_rects) add_damage_all_planes(rect);
}

void SurfaceRoot::paint_model_veil(detail::RecordingPainter& candidate, std::span<const Rect> painted) {
    if (!model_sign_ || !(*model_sign_).veil_shown || presentation_settings_.high_contrast) return;
    detail::ModelSignState& sign = *model_sign_;
    if (sign.veil_semantics != semantic_generation_ || sign.veil_layout != layout_pass_sequence_) {
        gather_veil_surfaces(sign);
    }
    if (sign.veil_rects.empty()) return;
    const bool dark = presentation_settings_.colour_scheme == ColourScheme::dark;
    const Color colour = dark ? veil_dark : veil_light;
    const double band = veil_pitch * veil_band_ratio;
    for (const Rect& surface : sign.veil_rects) {
        for (const Rect& rectangle : painted) {
            const Rect area = Rect::intersection(surface, rectangle);
            if (area.empty()) continue;
            candidate.save();
            candidate.clip_rect(area);
            // Bands in window space: a band at `along` covers x from along
            // to along + band at the area's top, leaning back by the
            // area's height at its bottom.
            const double top = area.y;
            const double bottom = area.y + area.height;
            const double slant = (bottom - top) * veil_slant_per_unit;
            const double first = std::floor((area.x - band - slant) / veil_pitch);
            const double last = area.x + area.width;
            for (double along = first * veil_pitch; along <= last; along += veil_pitch) {
                const std::array<Point, 4> quad{Point{along + slant, top}, Point{along + slant + band, top},
                                                Point{along + band, bottom}, Point{along, bottom}};
                candidate.fill_polygon(std::span<const Point>(quad), colour, PolygonFillRule::nonzero);
            }
            candidate.restore();
        }
    }
}

void Window::set_model_veil(bool shown) {
    require_root_thread("model veil");
    if (!shown && !model_sign_) return;
    detail::ModelSignState& sign = model_sign_state();
    if (sign.veil_shown == shown) return;
    sign.veil_shown = shown;
    if (shown) gather_veil_surfaces(sign);
    damage_veil(sign);
    if (!shown) sign.veil_rects.clear();
}

bool Window::model_veil() const noexcept {
    const bool shown = model_sign_ && (*model_sign_).veil_shown;
    return shown;
}

std::uint64_t Window::model_veil_frames() const noexcept {
    // The stripes stand still: no frame of theirs is ever asked for.
    return 0U;
}

namespace {

// The X cursor: the arrow with a red cross at its foot, drawn at 1x and 2x
// (straight-alpha RGBA, top-down).
CursorImage x_cursor_image(int scale) {
    const int size = 24 * scale;
    CursorImage image{};
    image.width = size;
    image.height = size;
    image.scale = static_cast<double>(scale);
    image.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), Color{0U, 0U, 0U, 0U});
    const double unit = static_cast<double>(scale);
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const double x = (static_cast<double>(column) + 0.5) / unit;
            const double y = (static_cast<double>(row) + 0.5) / unit;
            Color& pixel = image.rgba[static_cast<std::size_t>(row) * static_cast<std::size_t>(size) +
                                      static_cast<std::size_t>(column)];
            // The arrow: a triangle from the hotspot, its left edge
            // upright, its right edge leaning 45 degrees, 15 units tall;
            // black inside a one-unit white rim.
            const bool in_outer = y >= 0.0 && y <= 16.0 && x >= 0.0 && x <= y * 0.72 + 0.6;
            const bool in_inner = y >= 1.6 && y <= 14.6 && x >= 1.0 && x <= (y - 1.6) * 0.72 + 0.4;
            if (in_outer) pixel = in_inner ? Color{0U, 0U, 0U, 255U} : Color{255U, 255U, 255U, 255U};
            // The cross: two strokes 2.4 units wide over a white rim, in the
            // lower right square from (12, 12) to (22, 22).
            const double cx = x - 17.0;
            const double cy = y - 17.0;
            if (std::abs(cx) <= 5.6 && std::abs(cy) <= 5.6) {
                const double first = std::abs(cx - cy) / 1.41421356;
                const double second = std::abs(cx + cy) / 1.41421356;
                const double nearest = std::min(first, second);
                if (nearest <= 2.4) pixel = Color{255U, 255U, 255U, 255U};
                if (nearest <= 1.3 && std::abs(cx) <= 4.6 && std::abs(cy) <= 4.6) pixel = Color{200U, 32U, 32U, 255U};
            }
        }
    }
    return image;
}

} // namespace

bool SurfaceRoot::model_cursor(CursorImagesPtr& images, CursorKind& fallback) const {
    if (!model_sign_ || !(*model_sign_).veil_shown) return false;
    // Never destroyed (exit releases nothing).
    static const CursorImagesPtr* const shared =
        new CursorImagesPtr(CursorImages::create({x_cursor_image(1), x_cursor_image(2)}, 0.0, 0.0));
    images = *shared;
    fallback = CursorKind::forbidden;
    return true;
}

} // namespace gui_forms
