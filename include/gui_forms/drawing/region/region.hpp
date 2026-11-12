#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/graphics_path/graphics_path.hpp"

namespace gui_drawing {

struct GUI_FORMS_API RegionSnapshot final {
    std::vector<RectF> rectangles;
    std::vector<PathSnapshot> paths;
    std::vector<RectF> exclusions;
};

class GUI_FORMS_API Region final : public DrawingObject {
public:
    explicit Region(RectF rectangle);
    explicit Region(const GraphicsPath& path);
    void unite(RectF rectangle);
    void unite(const GraphicsPath& path);
    void exclude(RectF rectangle);
    [[nodiscard]] bool is_visible(PointF point) const;
    [[nodiscard]] RectF bounds() const;
    [[nodiscard]] RegionSnapshot snapshot() const;

private:
    RegionSnapshot value_;
};

} // namespace gui_drawing
