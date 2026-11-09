#pragma once

// Helpers shared by dock_site.cpp and dock_site_auto_hide.cpp; private to
// DockSite's sources.

#include "gui_forms/control.hpp"
#include "gui_forms/docking.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms::dock_site_detail {

// The PageStrip's preferred height at text scale 1.
inline constexpr double strip_preferred_height = 26.0;

// A seam is 3 logical units whatever the text scale (owner decision DK-41,
// T7-1).
inline constexpr double seam_bar_thickness = 3.0;

inline Rect to_window(Rect absolute, Rect local) noexcept {
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

inline Rect centred(Rect outer, double width, double height) noexcept {
    const Rect inner{outer.x + (outer.width - width) * 0.5, outer.y + (outer.height - height) * 0.5, width, height};
    return inner;
}

// A rectangle moved by an offset (from one control's coordinates into
// another's).
inline Rect moved(Rect bounds, Point offset) noexcept {
    const Rect result{bounds.x + offset.x, bounds.y + offset.y, bounds.width, bounds.height};
    return result;
}

inline bool contains_node(std::span<const DockNodeId> nodes, DockNodeId node) noexcept {
    for (const DockNodeId held : nodes) {
        if (held == node) return true;
    }
    return false;
}

// Whether `control` can take focus now.
inline bool focus_candidate(const Control& control) noexcept {
    const bool candidate = control.focusable.read() && control.eligible_for_input();
    return candidate;
}

// The first control in `root`'s subtree, in child order, that can take
// focus, or null.
inline Control::Ptr first_focusable(const Control::Ptr& root) {
    if (!root) return {};
    const std::span<const Control::Ptr> children = (*root).children();
    for (const Control::Ptr& child : children) {
        if (!child) continue;
        if (focus_candidate(*child)) return child;
        Control::Ptr nested = first_focusable(child);
        if (nested) return nested;
    }
    return {};
}

// The PageStrip's height for the default font, as PageStrip::measure gives
// it; side strips take it as their band's thickness.
inline double strip_extent(const Control& control) {
    const double scale = control.effective_text_scale();
    const ResolvedTextLayout metrics = control.resolve_text_layout_utf8("Mg", control.effective_font(FontSpec{}));
    const double height = std::max(strip_preferred_height * scale, metrics.logical_size.height + 10.0 * scale);
    return height;
}

} // namespace gui_forms::dock_site_detail
