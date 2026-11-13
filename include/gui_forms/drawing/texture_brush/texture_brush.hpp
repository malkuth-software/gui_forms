#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/bitmap/bitmap.hpp"
#include "gui_forms/drawing/brush/brush.hpp"
#include "gui_forms/drawing/image_reference/image_reference.hpp"

#include <memory>

namespace gui_drawing {

class GUI_FORMS_API TextureBrush final : public Brush {
public:
    explicit TextureBrush(const Bitmap& image,
                          WrapMode wrap_mode = WrapMode::tile);
    explicit TextureBrush(const ImageReference& image,
                          WrapMode wrap_mode = WrapMode::tile);

    void set_wrap_mode(WrapMode mode);
    void set_transform(Matrix transform);
    void reset_transform();
    void translate_transform(double x, double y);
    void scale_transform(double x, double y);
    void rotate_transform(double degrees);
    [[nodiscard]] std::unique_ptr<TextureBrush> clone() const;
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    explicit TextureBrush(BrushSnapshot value);
    BrushSnapshot value_;
};

} // namespace gui_drawing
