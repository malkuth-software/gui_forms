#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/object/drawing_object.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

namespace gui_drawing {

/*
 * M11e image identity only. Pixel storage, locking, codecs, and mutation are
 * intentionally M11f work; this object lets commands and ABI ownership be
 * proven without smuggling a backend surface into the portable contract.
 */
class GUI_FORMS_API ImageReference final : public DrawingObject {
public:
    ImageReference(std::uint64_t stable_id, std::uint32_t width,
                   std::uint32_t height, PixelFormat pixel_format,
                   std::uint64_t generation = 1);
    [[nodiscard]] ImageSnapshot snapshot() const;

private:
    ImageSnapshot value_;
};

} // namespace gui_drawing
