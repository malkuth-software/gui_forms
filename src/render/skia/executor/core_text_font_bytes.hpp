#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace gui_drawing::render {

// The installed face CoreText matches for a family name, upright at normal
// width, at weight 700 when bold and 400 otherwise. This is the request
// Skia's CoreText font manager made before ADR-004 phase 7. The family name
// is mandatory, so CoreText never substitutes another family: the raster
// module asks this only for a family the substitution table does not map,
// and falls back to the content family when it answers nullopt (stage F).
//
// The result is one self-contained sfnt (face index 0) rebuilt from the
// face's tables, so a face inside a collection needs no index. Nullopt when
// no installed family has that name, a listed table cannot be read, or the
// rebuilt font would exceed maximum_bytes.
//
// Variable-font limit: the rebuilt sfnt carries the face's tables unchanged,
// so it is the default instance of a variable font, not the instance CoreText
// selected. A bold match CoreText made on a variable family therefore comes
// back at the default weight, and the raster module files it by its OS/2
// value rather than in the bold slot. Static families (the tested Helvetica
// case) are unaffected.
[[nodiscard]] GUI_FORMS_API std::optional<std::vector<std::byte>> core_text_font_bytes(std::string_view family, bool bold,
                                                                         std::size_t maximum_bytes);

} // namespace gui_drawing::render
