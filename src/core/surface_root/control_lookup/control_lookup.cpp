// SurfaceRoot::control: a lookup by stable ID over the root's subtree
// (APP_WINDOW_RULES_DESIGN B.3).

#include "gui_forms/surface_root/surface_root.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace gui_forms {

namespace {

// Counts the controls with `stable_id` in the subtree of `control`, keeping
// the first in `found`. Stops descending once a second is counted.
void count_matches(const Control::Ptr& control, std::string_view stable_id, std::size_t& matches,
                   Control::Ptr& found) {
    if ((*control).stable_id().value() == stable_id) {
        ++matches;
        if (matches == 1U) found = control;
    }
    const std::span<const Control::Ptr> children = (*control).children();
    for (const Control::Ptr& child : children) {
        if (matches > 1U) return;
        count_matches(child, stable_id, matches, found);
    }
}

} // namespace

ControlLookup SurfaceRoot::control(std::string_view stable_id) const {
    ControlLookup lookup{};
    if (stable_id.empty()) {
        lookup.error = ControlLookupError::empty_id;
        return lookup;
    }
    std::size_t matches = 0U;
    Control::Ptr found{};
    if (root_) count_matches(root_, stable_id, matches, found);
    if (matches == 0U) {
        lookup.error = ControlLookupError::not_found;
        return lookup;
    }
    if (matches > 1U) {
        lookup.error = ControlLookupError::ambiguous;
        return lookup;
    }
    lookup.control = std::move(found);
    return lookup;
}

} // namespace gui_forms
