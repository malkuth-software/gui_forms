#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/drawing.hpp"
#include "gui_forms/resources.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// Retained viewport over an owned GUI.Drawing bitmap. Applications own tools,
// document history, and selection state; RasterCanvas owns presentation,
// coordinate transforms, resource synchronization, and local invalidation.
class RasterCanvas : public Control {
public:
    explicit RasterCanvas(StableId stable_id);

    [[nodiscard]] const std::shared_ptr<gui_drawing::Bitmap>& bitmap() const noexcept {
        return bitmap_;
    }
    void set_bitmap(std::shared_ptr<gui_drawing::Bitmap> bitmap);
    void clear_bitmap();

    [[nodiscard]] double zoom() const noexcept { return zoom_; }
    void set_zoom(double zoom);
    [[nodiscard]] gui_drawing::PointF view_origin() const noexcept {
        return view_origin_;
    }
    void set_view_origin(gui_drawing::PointF origin);
    void set_view(double zoom, gui_drawing::PointF origin);
    [[nodiscard]] ImageSampling sampling() const noexcept { return sampling_; }
    void set_sampling(ImageSampling sampling);

    [[nodiscard]] bool transparency_grid() const noexcept {
        return transparency_grid_;
    }
    void set_transparency_grid(bool visible);
    [[nodiscard]] Color canvas_background() const noexcept { return background_; }
    void set_canvas_background(Color color);
    void set_transparency_colors(Color first, Color second);
    [[nodiscard]] double transparency_cell_size() const noexcept {
        return transparency_cell_size_;
    }
    // Zero selects an adaptive bounded cell size.
    void set_transparency_cell_size(double size);

    [[nodiscard]] std::uint64_t presented_generation() const noexcept {
        return presented_generation_;
    }
    [[nodiscard]] ImageResourceError last_resource_error() const noexcept {
        return last_resource_error_;
    }
    [[nodiscard]] bool synchronize_bitmap();

    [[nodiscard]] Rect bitmap_to_client(gui_drawing::RectI pixels) const noexcept;
    [[nodiscard]] gui_drawing::PointF client_to_bitmap(Point client) const noexcept;
    [[nodiscard]] gui_drawing::RectF visible_bitmap_bounds() const;

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry image;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    void on_detached_from_window() noexcept override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    struct Tile final {
        ImageId image;
        gui_drawing::RectI content;
        gui_drawing::RectI storage;
    };
    [[nodiscard]] bool publish_full_bitmap();
    void release_tiles(SurfaceRoot& owner) noexcept;
    [[nodiscard]] Rect damage_to_client(gui_drawing::RectI pixels) const noexcept;
    void paint_transparency_grid(Painter& painter, Rect bounds) const;

    std::shared_ptr<gui_drawing::Bitmap> bitmap_;
    std::vector<Tile> tiles_;
    std::uint64_t presented_generation_{};
    double zoom_{1.0};
    gui_drawing::PointF view_origin_{};
    ImageSampling sampling_{ImageSampling::nearest};
    bool transparency_grid_{true};
    Color background_{Color::rgba(112, 118, 126)};
    Color transparency_first_{Color::rgba(246, 247, 249)};
    Color transparency_second_{Color::rgba(211, 215, 220)};
    double transparency_cell_size_{};
    ImageResourceError last_resource_error_{ImageResourceError::none};
};

} // namespace gui_forms
