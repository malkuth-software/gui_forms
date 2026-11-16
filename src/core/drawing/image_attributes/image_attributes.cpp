#include "../support/drawing_support.hpp"
#include <stxlib/optional.hpp>

void ImageAttributes::set_color_matrix(std::span<const double, 25> matrix) {
    require_alive();
    for (const double entry : matrix) require_finite(entry, "color matrix entry");
    std::array<double, 25> stored{};
    std::copy(matrix.begin(), matrix.end(), stored.begin());
    value_.color_matrix.set(stored);
}

void ImageAttributes::reset_color_matrix() {
    require_alive();
    value_.color_matrix.reset();
}

void ImageAttributes::set_remap_table(
    std::span<const ImageAttributesSnapshot::ColorRemap> table) {
    require_alive();
    if (table.size() > 4096U) {
        throw std::length_error("image color remap table limit exceeded");
    }
    std::vector<ImageAttributesSnapshot::ColorRemap> copy;
    copy.reserve(table.size());
    for (const gui_drawing::ImageAttributesSnapshot::ColorRemap& entry : table) {
        if (entry.old_color.is_empty() || entry.new_color.is_empty()) {
            throw std::invalid_argument("image color remaps require concrete colors");
        }
        std::vector<ImageAttributesSnapshot::ColorRemap>::iterator duplicate =
            copy.begin();
        while (duplicate != copy.end() &&
               (*duplicate).old_color.argb() != entry.old_color.argb()) {
            ++duplicate;
        }
        if (duplicate != copy.end()) {
            throw std::invalid_argument("image color remap source colors must be unique");
        }
        copy.push_back(entry);
    }
    value_.remap_table = std::move(copy);
}

void ImageAttributes::reset_remap_table() {
    require_alive();
    value_.remap_table.clear();
}

std::unique_ptr<ImageAttributes> ImageAttributes::clone() const {
    require_alive();
    std::unique_ptr<gui_drawing::ImageAttributes> result = std::make_unique<ImageAttributes>();
    (*result).value_ = value_;
    return result;
}

ImageAttributesSnapshot ImageAttributes::snapshot() const {
    require_alive();
    return value_;
}


} // namespace gui_drawing
