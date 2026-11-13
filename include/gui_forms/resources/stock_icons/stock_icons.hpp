#pragma once

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/resources/stock_icons/stock_icon_vectors.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

class Control;
class Painter;
class PartRaster;

// The stock icon set (exhibit gap E10; owner decisions, 2026-11-13):
// Heroicons v2.2.0, converted at build time by tools/stock_icon_codec.py
// into vector records embedded in the library, read once into
// StockIconVectors and baked to rasters at launch for each window scale
// (Window bakes the status icons when it is made and when its scale
// changes; any other icon is baked the first time it is shown at a size and
// scale). Nothing is rasterized per frame: a shown icon is a baked raster
// composited as a part raster. Icons take their colour from the theme
// (paint_stock_icon's caller passes the recipe's glyph or status colour),
// so dark schemes and high contrast follow; a theme may replace any variant
// by name (ThemeDefinition::stock_icons).

// The names of the icons the status marks use.
inline constexpr std::string_view stock_icon_information = "information-circle";
inline constexpr std::string_view stock_icon_warning = "exclamation-triangle";
inline constexpr std::string_view stock_icon_error = "exclamation-circle";
inline constexpr std::string_view stock_icon_question = "question-mark-circle";
inline constexpr std::string_view stock_icon_success = "check-circle";

// Every icon name in the set, sorted bytewise; static storage.
[[nodiscard]] GUI_FORMS_API std::span<const std::string_view> stock_icon_names() noexcept;

// Whether the set holds `icon` (its name in its style on its grid). Outline
// icons exist on the 24-unit grid only; a few solid icons lack the 16-unit
// drawing.
[[nodiscard]] GUI_FORMS_API bool has_stock_icon(const StockIcon& icon) noexcept;

// The variant of `name` in `style` to show `logical_side` units wide: the
// authored size nearest it (a tie goes to the larger, a choice: it keeps more
// of the drawing's detail). Empty for an unknown name, a style the name has
// no drawing in, or a side that is not finite and positive.
[[nodiscard]] GUI_FORMS_API std::optional<StockIcon> nearest_stock_icon(std::string_view name, StockIconStyle style,
                                                                       double logical_side);

// The built-in vectors of `icon`, or null when the set lacks it. Read from
// the embedded records once, on the first call for any icon (thread-safe),
// and kept for the process's life.
[[nodiscard]] GUI_FORMS_API std::shared_ptr<const StockIconVectors> stock_icon_vectors(const StockIcon& icon);

// Stock icons as images (the ImageSource kind of the E10 note): an ImageId
// naming an icon, accepted wherever an image is (PictureBox::image, a menu
// item's image, ErrorProvider::icon, a part's image). Such an id never names
// an image registry entry: registry ids leave the top bit clear and stock
// icon ids set it. The first form shows the variant nearest the drawn size;
// the second always shows `icon`'s own size, as long as the set has it.
// ImageId{} (no image) for an unknown name or variant.
[[nodiscard]] GUI_FORMS_API ImageId stock_icon_image(std::string_view name, StockIconStyle style) noexcept;
[[nodiscard]] GUI_FORMS_API ImageId stock_icon_image(const StockIcon& icon) noexcept;
[[nodiscard]] GUI_FORMS_API bool is_stock_icon_image(ImageId image) noexcept;

// What a stock icon id names: the icon's name (static storage), its style,
// and its fixed size if it has one (else the nearest is chosen when drawn).
struct GUI_FORMS_API StockIconImage final {
    std::string_view name{};
    StockIconStyle style{StockIconStyle::solid};
    std::optional<StockIconSize> size{};
};
[[nodiscard]] GUI_FORMS_API std::optional<StockIconImage> stock_icon_image_parts(ImageId image) noexcept;

// The icon chosen to draw `image` over a square `logical_side` units wide:
// its fixed size, else the nearest (nearest_stock_icon). Empty for an id
// that is not a stock icon's.
[[nodiscard]] GUI_FORMS_API std::optional<StockIcon> stock_icon_for_image(ImageId image, double logical_side);

// `vectors` rasterized as coverage into `coverage`, which the caller owns:
// side by side bytes, row by row, 0 uncovered to 255 covered, the grid
// mapped onto the square. Fills are sampled on sixteen sub-rows per pixel
// with exact horizontal coverage under their fill rule; strokes cover by
// the distance to the flattened path (round joins, the layer's caps).
// Overlapping layers combine as one colour painted over itself. Returns
// false, writing nothing, when `side` is 0 or above 4096 or `coverage` is
// smaller than side * side.
GUI_FORMS_API bool rasterize_stock_icon(const StockIconVectors& vectors, std::uint32_t side,
                                        std::span<std::uint8_t> coverage);

// `vectors` baked `side` device pixels square for display scale `scale`
// in `colour` (premultiplied, its alpha times the coverage): a part raster
// for Painter::draw_part_raster. Null when the side or the scale is out of
// PartRaster's range.
[[nodiscard]] GUI_FORMS_API std::shared_ptr<const PartRaster> bake_stock_icon(const StockIconVectors& vectors,
                                                                              std::uint32_t side, DeviceScale scale,
                                                                              Color colour);

// Draws the stock icon `image` names centred in `destination` (a square as
// wide as its shorter side), in `colour` at `opacity`, for `control`: the
// variant stock_icon_for_image chooses, or its theme's replacement
// (Theme::stock_icon_replacement). Attached, the icon is the baked raster
// for the window's scale from the process's cache (made on first use);
// detached, its vectors are drawn with fill_path and stroke_path. Draws
// nothing for an id that is not a stock icon's.
GUI_FORMS_API void paint_stock_icon(Painter& painter, const Control& control, ImageId image, Rect destination,
                                    Color colour, double opacity = 1.0);

// The stock icon bake counters (instrumentation): coverage rasterizations,
// coloured rasters made from them, the entries the process cache holds of
// each, and the scales Window has baked the status icons for.
struct GUI_FORMS_API StockIconBakeCounts final {
    std::uint64_t coverage_bakes{};
    std::uint64_t raster_bakes{};
    std::size_t cached_coverage{};
    std::size_t cached_rasters{};
    std::size_t prebaked_scales{};
};
[[nodiscard]] GUI_FORMS_API StockIconBakeCounts stock_icon_bake_counts() noexcept;

} // namespace gui_forms
