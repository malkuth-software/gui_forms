#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <stdexcept>

namespace gui_forms {
namespace {

template <typename Value>
void require_pointer_for_count(const Value* values, std::size_t value_count,
                               const char* message) {
    if (value_count != 0U && values == nullptr) {
        throw std::invalid_argument(message);
    }
}

} // namespace

MaterialBorderEdges MaterialBorderEdges::from_parts(
    const MaterialBorder* top_value, const MaterialBorder* right_value,
    const MaterialBorder* bottom_value, const MaterialBorder* left_value) {
    MaterialBorderEdges result;
    if (top_value != nullptr) result.top.set(*top_value);
    if (right_value != nullptr) result.right.set(*right_value);
    if (bottom_value != nullptr) result.bottom.set(*bottom_value);
    if (left_value != nullptr) result.left.set(*left_value);
    return result;
}

SurfaceMaterial SurfaceMaterial::from_parts(
    const MaterialFillLayer* fill_values, std::size_t fill_count,
    const MaterialShadow* shadow_values, std::size_t shadow_count,
    const MaterialBorder* border_value, double radius) {
    return from_parts(fill_values, fill_count, shadow_values, shadow_count,
                      border_value, nullptr, nullptr, 0U, radius);
}

SurfaceMaterial SurfaceMaterial::from_parts(
    const MaterialFillLayer* fill_values, std::size_t fill_count,
    const MaterialShadow* shadow_values, std::size_t shadow_count,
    const MaterialBorder* border_value,
    const MaterialBorderEdges* border_edges_value, double radius) {
    return from_parts(fill_values, fill_count, shadow_values, shadow_count,
                      border_value, border_edges_value, nullptr, 0U, radius);
}

SurfaceMaterial SurfaceMaterial::from_parts(
    const MaterialFillLayer* fill_values, std::size_t fill_count,
    const MaterialShadow* shadow_values, std::size_t shadow_count,
    const MaterialBorder* border_value,
    const MaterialBorderEdges* border_edges_value,
    const MaterialKeyline* keyline_values, std::size_t keyline_count,
    double radius) {
    if (fill_count == 0U || fill_count > maximum_fill_layers) {
        throw std::invalid_argument("surface material fill count is outside the retained limit");
    }
    if (shadow_count > maximum_shadows) {
        throw std::invalid_argument("surface material shadow count exceeds the retained limit");
    }
    if (keyline_count > maximum_keylines) {
        throw std::invalid_argument("surface material keyline count exceeds the retained limit");
    }
    require_pointer_for_count(fill_values, fill_count,
                              "surface material fills cannot be null when count is nonzero");
    require_pointer_for_count(shadow_values, shadow_count,
                              "surface material shadows cannot be null when count is nonzero");
    require_pointer_for_count(keyline_values, keyline_count,
                              "surface material keylines cannot be null when count is nonzero");

    SurfaceMaterial result;
    result.fills.assign(fill_values, fill_values + fill_count);
    if (shadow_count == 0U) {
        result.shadows.clear();
    } else {
        result.shadows.assign(shadow_values, shadow_values + shadow_count);
    }
    if (border_value == nullptr) {
        result.border.reset();
    } else {
        result.border.set(*border_value);
    }
    if (border_edges_value == nullptr) {
        result.border_edges = {};
    } else {
        result.border_edges = *border_edges_value;
    }
    if (keyline_count == 0U) {
        result.keylines.clear();
    } else {
        result.keylines.assign(keyline_values,
                               keyline_values + keyline_count);
    }
    result.corner_radius = radius;
    return result;
}

} // namespace gui_forms
