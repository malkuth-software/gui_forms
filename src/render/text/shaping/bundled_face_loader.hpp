#pragma once

#include "text_shape_engine.hpp"
#include "../../../core/resources/fonts/bundled_font_set.hpp"

#include <memory>
#include <string_view>

namespace gui_forms::render::text {

// Reads one bundled fallback face from the process's shared set when a text engine first
// needs it (TextShapeEngine::register_lazy_fallback_typeface). Every engine given a copy
// borrows the same sealed bytes.
struct BundledFaceLoader final {
    std::shared_ptr<gui_forms::detail::BundledFontSet> set;
    // The manifest's static name.
    std::string_view file_name;

    [[nodiscard]] LazyFaceBytes operator()() const {
        const gui_forms::detail::FontFaceLoad load = (*set).face(file_name);
        const std::shared_ptr<const gui_forms::detail::SealedFontFace>* face = load.face();
        LazyFaceBytes result{};
        if (face == nullptr) return result;
        result.bytes = (*(*face)).bytes();
        result.owner = *face;
        return result;
    }
};

// The coverage table of a bundled fallback face that may be loaded lazily, or nullopt for a
// face to load at registration: a primary, a required face, or one without a current table.
[[nodiscard]] inline std::optional<std::span<const gui_forms::detail::CodepointRange>> lazy_bundled_coverage(
    const gui_forms::detail::BundledFaceUse& use) noexcept {
    if (use.role || use.required) return std::nullopt;
    const std::optional<std::span<const gui_forms::detail::CodepointRange>> coverage =
        gui_forms::detail::bundled_face_coverage(use.file_name);
    return coverage;
}

} // namespace gui_forms::render::text
