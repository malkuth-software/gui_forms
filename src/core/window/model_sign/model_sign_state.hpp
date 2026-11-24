#pragma once

// What a window keeps for the MCP in-use sign beyond its wash level (the
// title note, the composed wash effect and the AI-mode veil), made the
// first time one is set: a window no model ever drove holds none (owner, 2026-11-24: the sign
// costs nothing while no model is connected).

#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>
#include <string>
#include <vector>

namespace gui_forms::detail {

struct ModelSignState final {
    // Every member's default constructor allocates nothing (stxlib::Unique
    // asks for a constructor that cannot throw).
    ModelSignState() noexcept {}

    // Window::set_title_note's note, appended to the title text.
    std::string title_note{};
    // The wash as a group effect (a saturation matrix), made for the
    // window's wash level; reset when the level changes.
    std::shared_ptr<const PaintGroupEffect> wash_effect{};
    // The AI-mode veil (Window::set_model_veil, model_veil.cpp): whether it
    // shows, the control surfaces it stripes (window coordinates) and the
    // semantic generation and layout pass they were gathered at, and its
    // motion.
    bool veil_shown{};
    std::vector<Rect> veil_rects{};
    std::uint64_t veil_semantics{};
    std::uint64_t veil_layout{};
    FrameRequestToken veil_wake{};
    FrameTime veil_start{};
    double veil_phase{};
    std::uint64_t veil_frames{};
};

} // namespace gui_forms::detail
