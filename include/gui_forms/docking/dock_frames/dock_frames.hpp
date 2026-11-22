#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstdint>

namespace gui_forms {

// The document frames of the frames presentation (DK decisions 60 to 63):
// where a frame shows and how the arrangements a Window menu offers lay the
// frames out (WinForms' LayoutMdi). Renderer-free: the planners read a
// layout and an area size and give edits; the realizing site applies them.

// The arrangements: cascade (each frame one caption below and right of the
// one behind it, back to front), tile horizontally (frames one above
// another, each the area's width), tile vertically (side by side, each the
// area's height), and arrange icons (the minimized frames' icons numbered
// again from the first slot, closing gaps). Cascading and tiling leave
// minimized frames where they are and restore maximized ones; tiles keep
// clear of the row of icons. Past three frames, tiling makes a grid.
enum class DockFrameArrangement : std::uint8_t { cascade, tile_horizontal, tile_vertical, arrange_icons };

// What the planners measure frames with, in logical units.
struct GUI_FORMS_API DockFrameMetrics final {
    // A frame's caption: a cascade steps by it, and an icon is this tall.
    double caption_height{24.0};
    // A minimized frame's icon.
    double icon_width{160.0};
    // Whether the icons line the bottom of the area (WinForms' default,
    // MdiChildrenMinimizedAnchorBottom), else its top.
    bool icons_at_bottom{true};
};

// The rectangle of icon slot `slot` in an area of `area`: icons fill rows
// from the reading start, as many to a row as fit (at least one), the first
// row along the anchored edge.
[[nodiscard]] GUI_FORMS_API Rect dock_frame_icon_bounds(std::uint32_t slot, Size area,
                                                        const DockFrameMetrics& metrics) noexcept;

// Where a frame shows in an area of `area`: its bounds while normal, its
// icon while minimized, the whole area while maximized.
[[nodiscard]] GUI_FORMS_API Rect dock_frame_shown_bounds(const DockFrameMemory& frame, Size area,
                                                         const DockFrameMetrics& metrics) noexcept;

// The edits that arrange the area's frames in an area of `area` (frame
// edits only, in a program transaction; the caller may make it a user one).
// Empty when there is no frame to move or the area is empty. A document
// without a frame memory counts as a normal frame behind the others.
[[nodiscard]] GUI_FORMS_API DockTransaction plan_frame_arrangement(const DockLayout& layout,
                                                                   DockFrameArrangement arrangement, Size area,
                                                                   const DockFrameMetrics& metrics);

} // namespace gui_forms
