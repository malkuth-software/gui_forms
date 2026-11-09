#include "gui_forms/plots/series_picker/series_picker.hpp"

namespace gui_forms::plots {

void SeriesPicker::reserve(std::size_t elements, std::size_t cells) {
    elements_.assign(elements, 0U);
    cell_start_.assign(cells + 1U, 0U);
    grid_valid_ = false;
    built_revision_ = ~std::uint64_t{0};
}

RectPick pick_rect(std::span<const Rect> rects, Point device) noexcept {
    RectPick pick{};
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        if (!rects[index].contains(device)) continue;
        pick.found = true;
        pick.index = index;
        return pick;
    }
    return pick;
}

} // namespace gui_forms::plots
