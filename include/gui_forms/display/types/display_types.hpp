#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

enum class PaintPlane : std::uint8_t {
    backplane = 0,
    control = 1,
    overlay = 2,
};

inline constexpr std::size_t paint_plane_count = 3;

[[nodiscard]] constexpr bool is_valid_paint_plane(PaintPlane plane) noexcept {
    return plane == PaintPlane::backplane || plane == PaintPlane::control ||
           plane == PaintPlane::overlay;
}

[[nodiscard]] constexpr std::size_t paint_plane_index(PaintPlane plane) noexcept {
    return static_cast<std::size_t>(plane);
}

struct GUI_FORMS_API DisplayChunkInfo final {
    std::uint64_t generation{};
    PaintPlane plane{PaintPlane::control};
    Rect logical_bounds{};
    std::size_t command_count{};
};

} // namespace gui_forms
