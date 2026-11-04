#pragma once

#include "../context/gallery_context.hpp"
#include "gui_forms/gui_forms.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace gui_forms::gallery {

// One direct child of the document's `gallery.root`: the registry-driven
// shell mounts each of them (command strip, compiled pages, diagnostics
// overlay). A page mount names the Gallery page (demo/gallery_page_specs.hpp)
// whose body shows it at `size`; the others have no page.
struct GalleryMount final {
    std::string_view id{};
    std::string_view page{};
    Size size{};
    Control::Ptr control;
};

struct GalleryTree final {
    std::vector<GalleryMount> mounts;
    std::shared_ptr<GalleryContext> context;

    [[nodiscard]] Control::Ptr mount(std::string_view id) const;
};

// Makes the controls demo/gallery.form describes and takes the mounts out of
// the declared boundary `gallery.root`; the shell owns their placement.
[[nodiscard]] GalleryTree build_gallery_tree();

} // namespace gui_forms::gallery
