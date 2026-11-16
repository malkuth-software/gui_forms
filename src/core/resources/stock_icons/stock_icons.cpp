#include "gui_forms/resources/stock_icons/stock_icons.hpp"

#include "stock_icon_records.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

namespace data = detail::stock_icon_data;

// A stock icon ImageId: the top bit set (registry ids never set it; see
// image_registry.cpp's maximum_generation), the size code in bits 24 to 31
// (0 for the nearest, else the grid), the style in bits 16 to 23 and the
// name's index in bits 0 to 15. Every other bit is clear.
constexpr std::uint64_t stock_icon_tag = 1ULL << 63U;
constexpr std::uint64_t stock_icon_payload_mask = 0xFFFF'FFFFULL;

constexpr std::array<StockIconSize, 3> authored_sizes{StockIconSize::grid_16, StockIconSize::grid_20,
                                                      StockIconSize::grid_24};

// The index of `name` in data::names, or none.
std::optional<std::size_t> name_index(std::string_view name) noexcept {
    const std::string_view* const first = data::names.data();
    const std::string_view* const last = first + data::names.size();
    const std::string_view* const found = std::lower_bound(first, last, name);
    if (found == last || *found != name) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(found - first);
    return index;
}

// Orders variant records by name index, style and grid, as the generator
// sorted them.
struct VariantRecordBefore final {
    bool operator()(const detail::StockIconVariantRecord& record,
                    const std::array<std::uint32_t, 3>& key) const noexcept {
        const std::array<std::uint32_t, 3> own{record.name, record.style, record.grid};
        return own < key;
    }
};

// The index of the variant record for name index `name`, `style` and `size`,
// or none.
std::optional<std::size_t> variant_index(std::size_t name, StockIconStyle style, StockIconSize size) noexcept {
    const std::array<std::uint32_t, 3> key{static_cast<std::uint32_t>(name), static_cast<std::uint32_t>(style),
                                           static_cast<std::uint32_t>(size)};
    const detail::StockIconVariantRecord* const first = data::variants.data();
    const detail::StockIconVariantRecord* const last = first + data::variants.size();
    const detail::StockIconVariantRecord* const found = std::lower_bound(first, last, key, VariantRecordBefore{});
    if (found == last) return std::nullopt;
    const std::array<std::uint32_t, 3> own{(*found).name, (*found).style, (*found).grid};
    if (own != key) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(found - first);
    return index;
}

std::optional<std::size_t> variant_index(const StockIcon& icon) noexcept {
    const std::optional<std::size_t> name = name_index(icon.name);
    if (!name) return std::nullopt;
    return variant_index(*name, icon.style, icon.size);
}

// Variant record `index` as vectors: its layers' verbs, and its coordinates
// in grid units.
std::shared_ptr<const StockIconVectors> read_variant(std::size_t index) {
    const detail::StockIconVariantRecord& variant = data::variants[index];
    std::vector<PaintPathVerb> verbs;
    std::vector<Point> points;
    std::vector<StockIconLayer> layers;
    layers.reserve(variant.layer_count);
    for (std::size_t layer_index = variant.first_layer; layer_index < variant.first_layer + variant.layer_count;
         ++layer_index) {
        const detail::StockIconLayerRecord& layer = data::layers[layer_index];
        std::size_t coordinate = layer.first_coordinate;
        for (std::size_t verb_index = layer.first_verb; verb_index < layer.first_verb + layer.verb_count;
             ++verb_index) {
            const PaintPathVerb verb = static_cast<PaintPathVerb>(data::verbs[verb_index]);
            verbs.push_back(verb);
            const std::size_t count = paint_path_points_for(verb);
            for (std::size_t point = 0U; point < count; ++point) {
                const double x = static_cast<double>(data::coordinates[coordinate]) / 1000.0;
                const double y = static_cast<double>(data::coordinates[coordinate + 1U]) / 1000.0;
                points.push_back(Point{x, y});
                coordinate += 2U;
            }
        }
        StockIconLayer read{};
        read.verb_end = verbs.size();
        read.point_end = points.size();
        read.paint = static_cast<StockIconPaint>(layer.paint);
        read.stroke_width = static_cast<double>(layer.stroke_width) / 1000.0;
        read.cap = static_cast<StrokeCap>(layer.cap);
        layers.push_back(read);
    }
    std::shared_ptr<const StockIconVectors> vectors = StockIconVectors::create(
        static_cast<double>(variant.grid), std::move(verbs), std::move(points), std::move(layers));
    return vectors;
}

// The built-in set's vectors, one per variant record, read on first use.
struct BuiltInSet final {
    std::vector<std::shared_ptr<const StockIconVectors>> variants;
};

BuiltInSet read_built_in_set() {
    BuiltInSet set;
    set.variants.reserve(data::variant_count);
    for (std::size_t index = 0U; index < data::variant_count; ++index) set.variants.push_back(read_variant(index));
    return set;
}

const BuiltInSet& built_in_set() {
    // A function-local static: read once, thread-safe, kept until exit.
    // Never destroyed (rule 1: process termination releases none of our state).
    static const BuiltInSet* const set = new const BuiltInSet(read_built_in_set());
    return *set;
}

bool valid_layer_paint(StockIconPaint paint) noexcept {
    switch (paint) {
    case StockIconPaint::fill_nonzero:
    case StockIconPaint::fill_even_odd:
    case StockIconPaint::stroke:
        return true;
    default:
        return false;
    }
}

bool valid_cap(StrokeCap cap) noexcept {
    switch (cap) {
    case StrokeCap::butt:
    case StrokeCap::round:
    case StrokeCap::square:
        return true;
    default:
        return false;
    }
}

} // namespace

StockIconVectors::StockIconVectors(double grid, std::vector<PaintPathVerb> verbs, std::vector<Point> points,
                                   std::vector<StockIconLayer> layers)
    : grid_(grid), verbs_(std::move(verbs)), points_(std::move(points)), layers_(std::move(layers)) {}

std::shared_ptr<const StockIconVectors> StockIconVectors::create(double grid, std::vector<PaintPathVerb> verbs,
                                                                 std::vector<Point> points,
                                                                 std::vector<StockIconLayer> layers) {
    if (!std::isfinite(grid) || grid <= 0.0 || grid > 256.0) {
        throw std::invalid_argument("GUI.Forms stock icon grid must be finite and in (0, 256]");
    }
    if (layers.empty()) throw std::invalid_argument("GUI.Forms stock icon needs at least one layer");
    std::size_t verb_start = 0U;
    std::size_t point_start = 0U;
    for (const StockIconLayer& layer : layers) {
        if (layer.verb_end <= verb_start || layer.verb_end > verbs.size() || layer.point_end < point_start ||
            layer.point_end > points.size()) {
            throw std::invalid_argument("GUI.Forms stock icon layers must rise within the verbs and points");
        }
        const std::span<const PaintPathVerb> layer_verbs(verbs.data() + verb_start, layer.verb_end - verb_start);
        const std::span<const Point> layer_points(points.data() + point_start, layer.point_end - point_start);
        const PaintPathView path{layer_verbs, layer_points};
        if (!valid_paint_path(path) || !valid_layer_paint(layer.paint) || !valid_cap(layer.cap)) {
            throw std::invalid_argument("GUI.Forms stock icon layer has an invalid path or paint");
        }
        if (layer.paint == StockIconPaint::stroke &&
            (!std::isfinite(layer.stroke_width) || layer.stroke_width <= 0.0 || layer.stroke_width > grid)) {
            throw std::invalid_argument("GUI.Forms stock icon stroke width must be finite and in (0, grid]");
        }
        verb_start = layer.verb_end;
        point_start = layer.point_end;
    }
    if (verb_start != verbs.size() || point_start != points.size()) {
        throw std::invalid_argument("GUI.Forms stock icon layers must end at the verbs' and points' ends");
    }
    std::shared_ptr<const StockIconVectors> made(
        new StockIconVectors(grid, std::move(verbs), std::move(points), std::move(layers)));
    return made;
}

PaintPathView StockIconVectors::layer_path(std::size_t index) const& noexcept {
    const std::size_t verb_start = index == 0U ? 0U : layers_[index - 1U].verb_end;
    const std::size_t point_start = index == 0U ? 0U : layers_[index - 1U].point_end;
    const StockIconLayer& layer = layers_[index];
    const std::span<const PaintPathVerb> verbs(verbs_.data() + verb_start, layer.verb_end - verb_start);
    const std::span<const Point> points(points_.data() + point_start, layer.point_end - point_start);
    const PaintPathView path{verbs, points};
    return path;
}

std::span<const std::string_view> stock_icon_names() noexcept {
    const std::span<const std::string_view> names(data::names.data(), data::names.size());
    return names;
}

bool has_stock_icon(const StockIcon& icon) noexcept {
    return variant_index(icon).has_value();
}

std::optional<StockIcon> nearest_stock_icon(std::string_view name, StockIconStyle style, double logical_side) {
    if (!std::isfinite(logical_side) || logical_side <= 0.0) return std::nullopt;
    const std::optional<std::size_t> index = name_index(name);
    if (!index) return std::nullopt;
    std::optional<StockIconSize> chosen;
    double chosen_distance = 0.0;
    for (const StockIconSize size : authored_sizes) {
        if (!variant_index(*index, style, size)) continue;
        const double distance = std::abs(static_cast<double>(size) - logical_side);
        // Sizes rise, so an equal distance later is the larger size.
        if (!chosen || distance <= chosen_distance) {
            chosen = size;
            chosen_distance = distance;
        }
    }
    if (!chosen) return std::nullopt;
    StockIcon icon{std::string(name), style, *chosen};
    return icon;
}

std::shared_ptr<const StockIconVectors> stock_icon_vectors(const StockIcon& icon) {
    const std::optional<std::size_t> index = variant_index(icon);
    if (!index) return nullptr;
    const std::shared_ptr<const StockIconVectors> vectors = built_in_set().variants[*index];
    return vectors;
}

ImageId stock_icon_image(std::string_view name, StockIconStyle style) noexcept {
    const std::optional<std::size_t> index = name_index(name);
    if (!index) return ImageId{};
    bool drawn = false;
    for (const StockIconSize size : authored_sizes) {
        if (variant_index(*index, style, size)) drawn = true;
    }
    if (!drawn) return ImageId{};
    const std::uint64_t value =
        stock_icon_tag | (static_cast<std::uint64_t>(style) << 16U) | static_cast<std::uint64_t>(*index);
    return ImageId{value};
}

ImageId stock_icon_image(const StockIcon& icon) noexcept {
    const std::optional<std::size_t> index = name_index(icon.name);
    if (!index || !variant_index(*index, icon.style, icon.size)) return ImageId{};
    const std::uint64_t value = stock_icon_tag | (static_cast<std::uint64_t>(icon.size) << 24U) |
                                (static_cast<std::uint64_t>(icon.style) << 16U) |
                                static_cast<std::uint64_t>(*index);
    return ImageId{value};
}

bool is_stock_icon_image(ImageId image) noexcept {
    return stock_icon_image_parts(image).present();
}

stxlib::Optional<StockIconImage> stock_icon_image_parts(ImageId image) noexcept {
    if ((image.value & stock_icon_tag) == 0U) return stxlib::Optional<StockIconImage>::none();
    if ((image.value & ~(stock_icon_tag | stock_icon_payload_mask)) != 0U) return stxlib::Optional<StockIconImage>::none();
    const std::size_t name = static_cast<std::size_t>(image.value & 0xFFFFU);
    const std::uint64_t style = (image.value >> 16U) & 0xFFU;
    const std::uint64_t size = (image.value >> 24U) & 0xFFU;
    if (name >= data::name_count || style > static_cast<std::uint64_t>(StockIconStyle::solid)) return stxlib::Optional<StockIconImage>::none();
    StockIconImage parts{};
    parts.name = data::names[name];
    parts.style = static_cast<StockIconStyle>(style);
    if (size != 0U) {
        const bool authored = size == 16U || size == 20U || size == 24U;
        if (!authored) return stxlib::Optional<StockIconImage>::none();
        parts.size.set(static_cast<StockIconSize>(size));
    }
    return stxlib::Optional<StockIconImage>::of(parts);
}

std::optional<StockIcon> stock_icon_for_image(ImageId image, double logical_side) {
    const stxlib::Optional<StockIconImage> parts = stock_icon_image_parts(image);
    if (parts.absent()) return std::nullopt;
    if (((*parts.value())).size.present()) {
        StockIcon fixed{std::string(((*parts.value())).name), ((*parts.value())).style, (*((*parts.value())).size.value())};
        if (!has_stock_icon(fixed)) return std::nullopt;
        return fixed;
    }
    return nearest_stock_icon(((*parts.value())).name, ((*parts.value())).style, logical_side);
}

} // namespace gui_forms
