#pragma once
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include <memory>
#include <vector>

namespace gui_forms {
// Top-down, straight-alpha sRGB. No native handles or platform byte ordering.
struct GUI_FORMS_API CursorImage final {
    int width{};
    int height{};
    double scale{1.0};
    std::vector<Color> rgba;
};

// Immutable owned representations of one logical cursor. Hotspots use fractions
// of logical width/height from the top-left, not fractions of width minus one.
class GUI_FORMS_API CursorImages final {
public:
    static std::shared_ptr<const CursorImages> create(
        std::vector<CursorImage> images, double hotspot_x, double hotspot_y);
    [[nodiscard]] const std::vector<CursorImage>& images() const noexcept { return images_; }
    [[nodiscard]] const CursorImage& select(double device_scale) const noexcept;
    [[nodiscard]] CursorImage rasterize(double device_scale) const;
    [[nodiscard]] int hotspot_x(const CursorImage& image) const noexcept;
    [[nodiscard]] int hotspot_y(const CursorImage& image) const noexcept;
    [[nodiscard]] double normalized_x() const noexcept { return hotspot_x_; }
    [[nodiscard]] double normalized_y() const noexcept { return hotspot_y_; }
private:
    CursorImages(std::vector<CursorImage> images, double x, double y);
    std::vector<CursorImage> images_;
    double hotspot_x_{};
    double hotspot_y_{};
};
using CursorImagesPtr = std::shared_ptr<const CursorImages>;
} // namespace gui_forms
