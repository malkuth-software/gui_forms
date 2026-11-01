#pragma once

#include "gallery_model.hpp"
#include "gui_forms/gui_forms.hpp"

#include <string>
#include <string_view>

namespace gui_forms::gallery {

class GalleryContext final {
public:
    GalleryModel model;
    Window* window{};
    ImageId status_badge;
    std::string drop_status;

    void synchronize(std::string_view cause);
};

} // namespace gui_forms::gallery
