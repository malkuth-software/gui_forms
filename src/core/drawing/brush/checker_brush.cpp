#include "../support/drawing_support.hpp"

CheckerBrush::CheckerBrush(double cell_size, Color first, Color second)
    : cell_size_(cell_size), first_(first), second_(second) {
    require_finite(cell_size, "checker cell size");
    if (cell_size <= 0.0 || cell_size > 1'000'000.0) {
        throw std::invalid_argument("checker cell size must be positive and bounded");
    }
}

BrushSnapshot CheckerBrush::snapshot() const {
    require_alive();
    BrushSnapshot result;
    result.kind = BrushKind::checker;
    result.primary = first_;
    result.secondary = second_;
    result.cell_size = cell_size_;
    return result;
}


} // namespace gui_drawing
