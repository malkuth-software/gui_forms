#pragma once

#include "gui_forms/basic_controls.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms {

enum class OverlayHorizontalAlignment : std::uint8_t {
    near,
    center,
    far,
};

enum class OverlayVerticalPreference : std::uint8_t {
    below,
    above,
};

struct AnchoredOverlayPlacement final {
    Size preferred_size{};
    OverlayHorizontalAlignment horizontal_alignment{
        OverlayHorizontalAlignment::near};
    OverlayVerticalPreference vertical_preference{
        OverlayVerticalPreference::below};
    double gap{};
    double viewport_margin{4.0};
    bool allow_vertical_flip{true};

    friend bool operator==(const AnchoredOverlayPlacement& left,
                           const AnchoredOverlayPlacement& right) noexcept {
        return left.preferred_size == right.preferred_size &&
               left.horizontal_alignment == right.horizontal_alignment &&
               left.vertical_preference == right.vertical_preference &&
               left.gap == right.gap &&
               left.viewport_margin == right.viewport_margin &&
               left.allow_vertical_flip == right.allow_vertical_flip;
    }
};

struct AnchoredOverlayPlacementResult final {
    Rect bounds{};
    bool placed_above{};
    bool width_clamped{};
    bool height_clamped{};
};

// Resolves an owner-relative overlay into logical window-client coordinates.
// The result is deterministic, bounded to the client margin, and flips across
// the anchor only when the preferred side cannot contain the requested height.
[[nodiscard]] AnchoredOverlayPlacementResult resolve_anchored_overlay(
    Rect anchor_bounds, Size client_size,
    AnchoredOverlayPlacement placement);

} // namespace gui_forms
