#pragma once

#include <cstdint>

namespace gui_forms {

// Mirrors the five System.Windows.Forms PictureBoxSizeMode policies. Image
// storage remains owned by Window's renderer-neutral ImageRegistry; PictureBox
// is only a retained presentation consumer of a generational ImageId.
enum class PictureBoxSizeMode : std::uint8_t {
    normal,
    stretch_image,
    auto_size,
    center_image,
    zoom,
};

} // namespace gui_forms
