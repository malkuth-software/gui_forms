#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/drawing.hpp"
#include "gui_forms/resources.hpp"
#include "gui_forms/controls/raster_canvas/raster_canvas_capabilities/raster_canvas_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// Retained viewport over an owned GUI.Drawing bitmap. Applications own tools,
// document history, and selection state; RasterCanvas owns presentation,
// coordinate transforms, resource synchronization, and local invalidation.
class RasterCanvas : public Control, public RasterCanvasCapabilities<RasterCanvas> {
public:
    explicit RasterCanvas(StableId stable_id);

    void clear_bitmap();

    // A change of origin alone moves the painted canvas by a scroll copy
    // (Control::scroll_content) when the last paint follows the view
    // (declare_paint_follows_view) and the view is exact (pan_copies_exactly);
    // otherwise, and for a change of zoom, the canvas is repainted.
    void set_view(double zoom, gui_drawing::PointF origin);

    void set_transparency_colors(Color first, Color second);

    [[nodiscard]] std::uint64_t presented_generation() const noexcept {
        return presented_generation_;
    }
    [[nodiscard]] ImageResourceError last_resource_error() const noexcept {
        return last_resource_error_;
    }
    [[nodiscard]] bool synchronize_bitmap();

    // Bitmap pixels to client coordinates: (pixel - view_origin) * zoom, or,
    // when the zoom and the device scale are powers of two, from the
    // bitmap's origin at content_offset.
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
    // Whether everything the last paint drew moves with the view's origin,
    // so that a pan can copy it. RasterCanvas::on_paint declares it for its
    // own paint (except an automatic transparency cell size, which follows
    // the visible area); a subclass that paints instead declares it from
    // its own on_paint. Until declared, a pan repaints.
    void declare_paint_follows_view(bool follows) noexcept { paint_follows_view_ = follows; }

private:
    struct Tile final {
        ImageId image;
        gui_drawing::RectI content;
        gui_drawing::RectI storage;
    };
    [[nodiscard]] bool publish_full_bitmap();
    void release_tiles(SurfaceRoot& owner) noexcept;
    [[nodiscard]] Rect damage_to_client(gui_drawing::RectI pixels) const noexcept;
    // The checker over `bounds`, its cells counted from `anchor` (the
    // bitmap's origin in client coordinates), so that it moves with a pan.
    void paint_transparency_grid(Painter& painter, Rect bounds, Point anchor) const;
    // The window's device scale, or 1 detached.
    [[nodiscard]] double device_scale() const noexcept;
    // Whether the zoom and the device scale are powers of two: the inverse
    // placement the renderer samples through is then exact.
    [[nodiscard]] bool binary_view() const noexcept;
    // The bits of a device pixel count that the painted coordinates may
    // reach: twice the bitmap's extent, from 10 to 23.
    [[nodiscard]] int offset_reach_bits() const noexcept;
    // The bitmap's origin in client coordinates: -view_origin * zoom, in a
    // binary view snapped to the device fraction single precision keeps
    // beside the reach (2^-(23 - offset_reach_bits)). A pan by whole device
    // pixels then moves every coordinate the canvas paints by exactly that.
    [[nodiscard]] Point content_offset() const noexcept;
    // Whether a copied pan keeps exactly the pixels a repaint draws: a
    // binary view whose painted coordinates stay within the reach.
    [[nodiscard]] bool pan_copies_exactly() const noexcept;
    // Where the visible part of the bitmap is drawn, in client coordinates.
    [[nodiscard]] Rect visible_destination() const noexcept;

    std::shared_ptr<gui_drawing::Bitmap> bitmap_;
    std::vector<Tile> tiles_;
    std::uint64_t presented_generation_{};
    double zoom_{1.0};
    gui_drawing::PointF view_origin_{};
    Color transparency_first_{Color::rgba(246, 247, 249)};
    Color transparency_second_{Color::rgba(211, 215, 220)};
    ImageResourceError last_resource_error_{ImageResourceError::none};
    bool paint_follows_view_{};
    // property_codemod begin RasterCanvas (tools/property_families/raster_canvas.json; generated, do not edit)
private:
    friend struct RasterCanvasBitmapCapability<RasterCanvas>;
    friend struct RasterCanvasZoomCapability<RasterCanvas>;
    friend struct RasterCanvasViewOriginCapability<RasterCanvas>;
    friend struct RasterCanvasSamplingCapability<RasterCanvas>;
    friend struct RasterCanvasTransparencyGridCapability<RasterCanvas>;
    friend struct RasterCanvasCanvasBackgroundCapability<RasterCanvas>;
    friend struct RasterCanvasTransparencyCellSizeCapability<RasterCanvas>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(bitmap)& cell, std::shared_ptr<gui_drawing::Bitmap> incoming);
    [[nodiscard]] std::shared_ptr<gui_drawing::Bitmap> read_view(const decltype(bitmap)& cell) const;
    void write_view(const decltype(zoom)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(zoom)& cell) const;
    void write_view(const decltype(view_origin)& cell, gui_drawing::PointF incoming);
    [[nodiscard]] gui_drawing::PointF read_view(const decltype(view_origin)& cell) const;
    void prepare_write(const decltype(sampling)& cell, ImageSampling& incoming);
    void written(const decltype(sampling)& cell);
    void prepare_write(const decltype(transparency_grid)& cell, bool& incoming);
    void written(const decltype(transparency_grid)& cell);
    void prepare_write(const decltype(canvas_background)& cell, Color& incoming);
    void written(const decltype(canvas_background)& cell);
    void prepare_write(const decltype(transparency_cell_size)& cell, double& incoming);
    void written(const decltype(transparency_cell_size)& cell);
    // property_codemod end RasterCanvas
};

} // namespace gui_forms
