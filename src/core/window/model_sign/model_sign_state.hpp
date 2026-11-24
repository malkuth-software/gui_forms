#pragma once

// What a window keeps for the MCP in-use sign beyond its wash level (the
// title note and the composed wash effect), made the first time either is
// set: a window no model ever drove holds none (owner, 2026-11-24: the sign
// costs nothing while no model is connected).

#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>
#include <string>

namespace gui_forms::detail {

struct ModelSignState final {
    // Window::set_title_note's note, appended to the title text.
    std::string title_note{};
    // The wash as a group effect (a saturation matrix), made for the
    // window's wash level; reset when the level changes.
    std::shared_ptr<const PaintGroupEffect> wash_effect{};
};

} // namespace gui_forms::detail
