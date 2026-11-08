// SurfaceRoot::control: a lookup by stable ID over the root's subtree
// (APP_WINDOW_RULES_DESIGN B.3).

#include "gui_forms/surface_root/surface_root.hpp"

#include <string_view>
#include <utility>

namespace gui_forms {

ControlLookup SurfaceRoot::control(std::string_view stable_id) const {
    ControlLookup lookup{};
    if (stable_id.empty()) {
        lookup.error = ControlLookupError::empty_id;
        return lookup;
    }
    // The window's stable-ID table, which attaching keeps unique.
    Control::Ptr found = find(stable_id);
    if (!found) {
        lookup.error = ControlLookupError::not_found;
        return lookup;
    }
    lookup.control = std::move(found);
    return lookup;
}

} // namespace gui_forms
