#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gui_forms {

// The stock icon set's identities and vector records (exhibit gap E10; owner
// decisions, 2026-11-13): Heroicons v2.2.0 (MIT, third_party/heroicons/),
// stored as vectors in GUI.Forms' own record format and baked to rasters at
// launch (stock_icons.hpp). Each icon is drawn separately for each size, so
// an icon is its name, its style and the authored grid it was drawn on.

// An icon's style: drawn with strokes (outline, on the 24-unit grid only) or
// filled (solid, on every grid).
enum class StockIconStyle : std::uint8_t { outline, solid };

// The grid an icon variant was drawn on, in logical units: the size it is
// meant to be shown at. A variant is chosen by the nearest authored size
// (nearest_stock_icon), never by scaling another size's drawing.
enum class StockIconSize : std::uint8_t { grid_16 = 16, grid_20 = 20, grid_24 = 24 };

// One stock icon: its name (lowercase words joined by hyphens, as
// stock_icon_names lists them), its style and its size variant.
struct GUI_FORMS_API StockIcon final {
    std::string name{};
    StockIconStyle style{StockIconStyle::solid};
    StockIconSize size{StockIconSize::grid_24};

    friend bool operator==(const StockIcon& left, const StockIcon& right) noexcept {
        return left.name == right.name && left.style == right.style && left.size == right.size;
    }
};

// How one layer of an icon is painted, in the colour the icon is drawn in.
enum class StockIconPaint : std::uint8_t { fill_nonzero, fill_even_odd, stroke };

// One layer of an icon's vectors: the layer's verbs end at `verb_end` and its
// points at `point_end` (each layer starts where the previous one ended, the
// first at zero); a stroke is `stroke_width` grid units wide with `cap` at its
// open ends and round joins.
struct GUI_FORMS_API StockIconLayer final {
    std::size_t verb_end{};
    std::size_t point_end{};
    StockIconPaint paint{StockIconPaint::fill_nonzero};
    double stroke_width{};
    StrokeCap cap{StrokeCap::round};

    friend bool operator==(const StockIconLayer& left, const StockIconLayer& right) noexcept {
        return left.verb_end == right.verb_end && left.point_end == right.point_end && left.paint == right.paint &&
               left.stroke_width == right.stroke_width && left.cap == right.cap;
    }
};

// An icon's drawing on its grid: layers of curved paths (PaintPath's verbs
// and points, in grid units with y down), each filled or stroked. Immutable
// once made and shared read-only (the built-in set holds one per variant for
// the process's life; a theme holds its replacements while it lives).
class GUI_FORMS_API StockIconVectors final {
public:
    // Makes the vectors, taking the arrays. Throws std::invalid_argument,
    // making nothing, unless `grid` is finite and in (0, 256], there is at
    // least one layer, the layers' ends rise and the last ends at the arrays'
    // sizes, every layer's path is valid (valid_paint_path) and every stroke
    // width is finite and in (0, grid].
    [[nodiscard]] static std::shared_ptr<const StockIconVectors> create(double grid, std::vector<PaintPathVerb> verbs,
                                                                         std::vector<Point> points,
                                                                         std::vector<StockIconLayer> layers);
    StockIconVectors(const StockIconVectors&) = delete;
    StockIconVectors& operator=(const StockIconVectors&) = delete;

    [[nodiscard]] double grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t layer_count() const noexcept { return layers_.size(); }
    // Layer `index` (below layer_count()) and its path, borrowed from these
    // vectors: valid while they live. Not for a temporary.
    [[nodiscard]] const StockIconLayer& layer(std::size_t index) const& noexcept { return layers_[index]; }
    const StockIconLayer& layer(std::size_t index) const&& = delete;
    [[nodiscard]] PaintPathView layer_path(std::size_t index) const& noexcept;
    PaintPathView layer_path(std::size_t index) const&& = delete;

    friend bool operator==(const StockIconVectors& left, const StockIconVectors& right) noexcept {
        return left.grid_ == right.grid_ && left.verbs_ == right.verbs_ && left.points_ == right.points_ &&
               left.layers_ == right.layers_;
    }

private:
    StockIconVectors(double grid, std::vector<PaintPathVerb> verbs, std::vector<Point> points,
                     std::vector<StockIconLayer> layers);

    double grid_{};
    std::vector<PaintPathVerb> verbs_{};
    std::vector<Point> points_{};
    std::vector<StockIconLayer> layers_{};
};

// A theme's drawing for one stock icon variant (ThemeDefinition::stock_icons):
// shown wherever `icon` would be, in the same colour.
struct GUI_FORMS_API StockIconReplacement final {
    StockIcon icon{};
    std::shared_ptr<const StockIconVectors> vectors{};

    friend bool operator==(const StockIconReplacement& left, const StockIconReplacement& right) noexcept {
        if (!(left.icon == right.icon)) return false;
        if (left.vectors == right.vectors) return true;
        return left.vectors != nullptr && right.vectors != nullptr && *left.vectors == *right.vectors;
    }
};

} // namespace gui_forms
