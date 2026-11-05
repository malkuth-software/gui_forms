#include "gui_forms/controls/raster_canvas/raster_canvas.hpp"

#include "gui_forms/window.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace gui_forms {
namespace {

[[nodiscard]] bool valid_origin(gui_drawing::PointF origin) noexcept {
    constexpr double limit = 1'000'000'000.0;
    return std::isfinite(origin.x) && std::isfinite(origin.y) &&
        std::abs(origin.x) <= limit && std::abs(origin.y) <= limit;
}

} // namespace

RasterCanvas::RasterCanvas(StableId stable_id) : Control(std::move(stable_id)) {
    set_focusable(true);
    set_tab_stop(true);
    set_cursor(CursorKind::crosshair);
}

void RasterCanvas::set_bitmap(std::shared_ptr<gui_drawing::Bitmap> bitmap) {
    require_mutable();
    if (bitmap_ == bitmap) return;
    if (SurfaceRoot* owner = surface_root()) release_tiles(*owner);
    bitmap_ = std::move(bitmap);
    tiles_.clear();
    presented_generation_ = 0U;
    last_resource_error_ = ImageResourceError::none;
    if (bitmap_ && window()) static_cast<void>(publish_full_bitmap());
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void RasterCanvas::clear_bitmap() { set_bitmap({}); }

void RasterCanvas::set_zoom(double zoom) {
    set_view(zoom, view_origin_);
}

void RasterCanvas::set_view_origin(gui_drawing::PointF origin) {
    set_view(zoom_, origin);
}

void RasterCanvas::set_view(double zoom, gui_drawing::PointF origin) {
    require_mutable();
    if (!std::isfinite(zoom) || zoom < 1.0 / 64.0 || zoom > 256.0) {
        throw std::invalid_argument("canvas zoom must be within [1/64, 256]");
    }
    if (!valid_origin(origin)) {
        throw std::invalid_argument("canvas view origin must be finite and bounded");
    }
    if (zoom_ == zoom && view_origin_ == origin) return;
    zoom_ = zoom;
    view_origin_ = origin;
    invalidate(Dirty::paint | Dirty::semantics);
}

void RasterCanvas::set_sampling(ImageSampling sampling) {
    require_mutable();
    if (sampling != ImageSampling::nearest && sampling != ImageSampling::linear) {
        throw std::invalid_argument("canvas image sampling is outside the enum");
    }
    if (sampling_ == sampling) return;
    sampling_ = sampling;
    invalidate(Dirty::paint);
}

void RasterCanvas::set_transparency_grid(bool visible) {
    require_mutable();
    if (transparency_grid_ == visible) return;
    transparency_grid_ = visible;
    invalidate(Dirty::paint);
}

void RasterCanvas::set_canvas_background(Color color) {
    require_mutable();
    if (background_ == color) return;
    background_ = color;
    invalidate(Dirty::paint);
}

void RasterCanvas::set_transparency_colors(Color first, Color second) {
    require_mutable();
    if (transparency_first_ == first && transparency_second_ == second) return;
    transparency_first_ = first;
    transparency_second_ = second;
    invalidate(Dirty::paint);
}

void RasterCanvas::set_transparency_cell_size(double size) {
    require_mutable();
    if (!std::isfinite(size) || (size != 0.0 && (size < 2.0 || size > 128.0))) {
        throw std::invalid_argument(
            "canvas transparency cell size must be zero or within [2, 128]");
    }
    if (transparency_cell_size_ == size) return;
    transparency_cell_size_ = size;
    invalidate(Dirty::paint);
}

bool RasterCanvas::publish_full_bitmap() {
    Window* owner = window();
    if (!bitmap_ || owner == nullptr) return false;
    const gui_drawing::ImageSnapshot snapshot = (*bitmap_).snapshot();
    // Tiles use the existing image resource/cache path. A one-pixel gutter
    // preserves bilinear samples across the 512-pixel content boundaries.
    constexpr std::int32_t side = 512;
    const std::int32_t width = static_cast<std::int32_t>(snapshot.width);
    const std::int32_t height = static_cast<std::int32_t>(snapshot.height);
    const std::size_t count = static_cast<std::size_t>((width + side - 1) / side) *
                              static_cast<std::size_t>((height + side - 1) / side);
    tiles_.reserve(count);
    std::vector<std::byte> packed;
    try {
        for (std::int32_t y = 0; y < height; y += side) {
            for (std::int32_t x = 0; x < width; x += side) {
                const gui_drawing::RectI content{x, y, std::min(side, width - x),
                                                       std::min(side, height - y)};
                const std::int32_t left = std::max(0, x - 1);
                const std::int32_t top = std::max(0, y - 1);
                const gui_drawing::RectI storage{left, top,
                    std::min(width, x + content.width + 1) - left,
                    std::min(height, y + content.height + 1) - top};
                const std::size_t offset = static_cast<std::size_t>(storage.y) * snapshot.row_bytes() +
                    static_cast<std::size_t>(storage.x) * 4U;
                const std::size_t bytes = static_cast<std::size_t>(storage.height - 1) * snapshot.row_bytes() +
                    static_cast<std::size_t>(storage.width) * 4U;
                std::span<const std::byte> pixels(snapshot.pixels().data() + offset, bytes);
                std::uint64_t stride = snapshot.row_bytes();
                // A load owns complete, tightly packed rows. The source is a
                // subrectangle of the document with its larger stride.
                const std::size_t tight_row = static_cast<std::size_t>(storage.width) * 4U;
                packed.resize(tight_row * static_cast<std::size_t>(storage.height));
                for (std::int32_t row = 0; row < storage.height; ++row) {
                    std::copy_n(pixels.data() + static_cast<std::size_t>(row) * snapshot.row_bytes(),
                                tight_row, packed.data() + static_cast<std::size_t>(row) * tight_row);
                }
                pixels = packed;
                stride = static_cast<std::uint64_t>(storage.width) * 4U;
                const ImageLoadResult loaded = (*owner).load_bgra32_premultiplied(
                    static_cast<std::uint32_t>(storage.width), static_cast<std::uint32_t>(storage.height),
                    stride, pixels);
                last_resource_error_ = loaded.error;
                if (!loaded) {
                    release_tiles(*owner);
                    return false;
                }
                tiles_.push_back({loaded.image, content, storage});
            }
        }
    } catch (...) {
        release_tiles(*owner);
        throw;
    }
    presented_generation_ = snapshot.generation;
    return true;
}

void RasterCanvas::release_tiles(SurfaceRoot& owner) noexcept {
    for (std::size_t index = 0; index < tiles_.size(); ++index) {
        try { static_cast<void>(owner.remove_image(tiles_[index].image)); }
        catch (...) {}
    }
    tiles_.clear();
    presented_generation_ = 0;
}

bool RasterCanvas::synchronize_bitmap() {
    require_mutable();
    if (!bitmap_) return true;
    Window* owner = window();
    if (!owner) return false;
    for (std::size_t index = 0; index < tiles_.size(); ++index) {
        if (!(*owner).image_resources().find(tiles_[index].image)) {
            release_tiles(*owner);
            break;
        }
    }
    if (tiles_.empty()) {
        const bool published = publish_full_bitmap();
        if (published) invalidate(Dirty::paint | Dirty::semantics);
        return published;
    }

    const gui_drawing::BitmapDamageSnapshot changes = (*bitmap_).changes_since(presented_generation_);
    if (changes.empty()) return true;
    const gui_drawing::ImageSnapshot snapshot = (*bitmap_).snapshot();
    for (std::size_t change = 0; change < changes.rectangles.size(); ++change) {
        const gui_drawing::RectI dirty = changes.rectangles[change];
        for (std::size_t index = 0; index < tiles_.size(); ++index) {
            Tile& tile = tiles_[index];
            const gui_drawing::RectI rect = gui_drawing::RectI::intersection(dirty, tile.storage);
            if (rect.empty()) continue;
            const std::size_t source_offset = static_cast<std::size_t>(rect.y) * snapshot.row_bytes() +
                static_cast<std::size_t>(rect.x) * 4U;
            const std::size_t required = static_cast<std::size_t>(rect.height - 1) * snapshot.row_bytes() +
                static_cast<std::size_t>(rect.width) * 4U;
            const std::span<const std::byte> patch(snapshot.pixels().data() + source_offset, required);
            const std::uint64_t source_row_bytes = snapshot.row_bytes();
            const ImageLoadResult updated = (*owner).patch_bgra32_premultiplied(
                tile.image, static_cast<std::uint32_t>(rect.x - tile.storage.x),
                static_cast<std::uint32_t>(rect.y - tile.storage.y),
                static_cast<std::uint32_t>(rect.width), static_cast<std::uint32_t>(rect.height),
                source_row_bytes, patch, *this, damage_to_client(dirty));
            last_resource_error_ = updated.error;
            if (!updated) return false;
            tile.image = updated.image;
        }
    }
    presented_generation_ = snapshot.generation;
    return true;
}

Rect RasterCanvas::bitmap_to_client(gui_drawing::RectI pixels) const noexcept {
    return {(static_cast<double>(pixels.x) - view_origin_.x) * zoom_,
            (static_cast<double>(pixels.y) - view_origin_.y) * zoom_,
            static_cast<double>(pixels.width) * zoom_,
            static_cast<double>(pixels.height) * zoom_};
}

gui_drawing::PointF RasterCanvas::client_to_bitmap(Point client) const noexcept {
    return {client.x / zoom_ + view_origin_.x,
            client.y / zoom_ + view_origin_.y};
}

gui_drawing::RectF RasterCanvas::visible_bitmap_bounds() const {
    if (!bitmap_) return {};
    const Rect client = client_rectangle();
    const double left = std::clamp(view_origin_.x, 0.0,
                                   static_cast<double>((*bitmap_).width()));
    const double top = std::clamp(view_origin_.y, 0.0,
                                  static_cast<double>((*bitmap_).height()));
    const double right = std::clamp(view_origin_.x + client.width / zoom_,
                                    0.0, static_cast<double>((*bitmap_).width()));
    const double bottom = std::clamp(view_origin_.y + client.height / zoom_,
                                     0.0, static_cast<double>((*bitmap_).height()));
    return {left, top, std::max(0.0, right - left),
            std::max(0.0, bottom - top)};
}

Rect RasterCanvas::damage_to_client(gui_drawing::RectI pixels) const noexcept {
    if (sampling_ == ImageSampling::linear) {
        --pixels.x;
        --pixels.y;
        pixels.width += 2;
        pixels.height += 2;
    }
    return Rect::intersection(bitmap_to_client(pixels), client_rectangle());
}

void RasterCanvas::paint_transparency_grid(Painter& painter, Rect bounds) const {
    painter.fill_rect(bounds, transparency_first_);
    const double area = std::max(0.0, bounds.width * bounds.height);
    const double tile = transparency_cell_size_ == 0.0
        ? std::max(8.0, std::ceil(std::sqrt(area / 2048.0)))
        : transparency_cell_size_;
    const std::int64_t columns = static_cast<std::int64_t>(
        std::ceil(bounds.width / tile));
    const std::int64_t rows = static_cast<std::int64_t>(
        std::ceil(bounds.height / tile));
    for (std::int64_t row = 0; row < rows; ++row) {
        for (std::int64_t column = row & 1; column < columns; column += 2) {
            painter.fill_rect(
                {bounds.x + static_cast<double>(column) * tile,
                 bounds.y + static_cast<double>(row) * tile,
                 std::min(tile, bounds.right() -
                                   (bounds.x + static_cast<double>(column) * tile)),
                 std::min(tile, bounds.bottom() -
                                   (bounds.y + static_cast<double>(row) * tile))},
                transparency_second_);
        }
    }
}

void RasterCanvas::on_paint(Painter& painter, Rect) {
    const Rect bounds = client_rectangle();
    painter.fill_rect(bounds, background_);
    if (!bitmap_ || tiles_.empty()) return;
    const gui_drawing::RectF visible = visible_bitmap_bounds();
    if (visible.empty()) return;
    const Rect destination{(visible.x - view_origin_.x) * zoom_,
        (visible.y - view_origin_.y) * zoom_, visible.width * zoom_, visible.height * zoom_};
    if (transparency_grid_) paint_transparency_grid(painter, destination);
    for (std::size_t index = 0; index < tiles_.size(); ++index) {
        const Tile& tile = tiles_[index];
        const Rect clip = Rect::intersection(bitmap_to_client(tile.content), destination);
        if (clip.empty()) continue;
        painter.save();
        painter.clip_rect(clip);
        painter.draw_image_region_sampled(tile.image,
            {0, 0, static_cast<double>(tile.storage.width), static_cast<double>(tile.storage.height)},
            bitmap_to_client(tile.storage), sampling_, 1.0);
        painter.restore();
    }
}

SemanticDescriptor RasterCanvas::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::image;
    descriptor.name = accessible_name();
    descriptor.description = accessible_description();
    if (bitmap_) {
        descriptor.value = std::to_string((*bitmap_).width()) + " x " +
            std::to_string((*bitmap_).height()) + " at " +
            std::to_string(zoom_ * 100.0) + "%";
    }
    descriptor.exposed = bitmap_ != nullptr || !descriptor.name.empty() ||
                         !descriptor.description.empty();
    return descriptor;
}

void RasterCanvas::on_attached_to_window() {
    if (bitmap_ && !publish_full_bitmap()) {
        throw std::runtime_error(
            std::string("RasterCanvas could not publish its bitmap resource: ") +
            std::string(image_resource_error_name(last_resource_error_)));
    }
}

void RasterCanvas::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    release_tiles(former_window);
}

void RasterCanvas::on_detached_from_window() noexcept {
    tiles_.clear();
    presented_generation_ = 0U;
}

void RasterCanvas::on_dispose() noexcept {
    if (SurfaceRoot* owner = surface_root()) release_tiles(*owner);
    bitmap_.reset();
    tiles_.clear();
    presented_generation_ = 0U;
    Control::on_dispose();
}


namespace {

constexpr std::array<PartField<RasterCanvas::Parts>, 4> raster_canvas_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RasterCanvas::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &RasterCanvas::Parts::face},
    {.declaration = {.name = "image", .role = PartRole::icon, .composed_from = {}},
     .member = &RasterCanvas::Parts::image},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &RasterCanvas::Parts::focus},
}};

constexpr std::array<StateField<RasterCanvas::States>, 5> raster_canvas_state_fields{{
    {.state = AnatomyState::normal, .member = &RasterCanvas::States::normal},
    {.state = AnatomyState::enabled, .member = &RasterCanvas::States::enabled},
    {.state = AnatomyState::disabled, .member = &RasterCanvas::States::disabled},
    {.state = AnatomyState::focused, .member = &RasterCanvas::States::focused},
    {.state = AnatomyState::focus_visible, .member = &RasterCanvas::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> raster_canvas_parts =
    part_declarations(raster_canvas_part_fields);
constexpr std::array<AnatomyState, 5> raster_canvas_states =
    state_declarations(raster_canvas_state_fields);

constexpr ControlAnatomy raster_canvas_anatomy{
    .control_id = "raster_canvas",
    .parts = raster_canvas_parts,
    .states = raster_canvas_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& RasterCanvas::anatomy() const noexcept {
    return raster_canvas_anatomy;
}

void RasterCanvas::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    if (bitmap_ && !tiles_.empty()) {
        // The visible part of the bitmap, where on_paint draws its tiles.
        const gui_drawing::RectF visible = visible_bitmap_bounds();
        if (!visible.empty()) {
            const Rect destination{(visible.x - view_origin_.x) * zoom_,
                                   (visible.y - view_origin_.y) * zoom_,
                                   visible.width * zoom_, visible.height * zoom_};
            record.image = PartGeometry::from_bounds(destination);
        }
    }
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), raster_canvas_part_fields, record, geometry);
}

RasterCanvas::Parts RasterCanvas::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), raster_canvas_part_fields, published.parts(), record);
    return record;
}

RasterCanvas::States RasterCanvas::states() const {
    States record;
    read_states(raster_canvas_state_fields, anatomy_states(), record);
    return record;
}

} // namespace gui_forms
