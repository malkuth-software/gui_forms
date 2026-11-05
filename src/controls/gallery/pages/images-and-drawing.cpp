#include "pages.hpp"

#include "images-and-drawing_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the images page: the controls demo/pages/images-and-drawing.form
// describes (every PictureBox sizing mode, image opacity, the DrawingSurface
// painter composition, a RasterCanvas and two MaterialPanel materials) and
// what the form language does not state: the pictures' grounds, the board's
// paint callback, the raster's bitmap and sampling and the page style. The
// page has no interactive state.
class ImagesPage final : public gui_forms::Component {
public:
    explicit ImagesPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void fill_pictures();
    void fill_primitives();

    // Owns the document's controls; the shell's page body holds them too once
    // mount_document() has moved them there.
    ImagesDocument<ImagesPage> document_{};
};

ImagesPage::ImagesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    fill_pictures();
    fill_primitives();
}

void ImagesPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move into
    // the shell's page body, a design surface of the same 1000 x 660 size,
    // where each is placed by its bounds in design coordinates. The copy of
    // the child list keeps the iteration valid while the root shrinks.
    Control& root = *document_.gallery_images_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_images_modes, document_.gallery_images_opacity, document_.gallery_images_primitives};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
}

void ImagesPage::fill_pictures() {
    // The test cards' grounds alternate across the sizing modes.
    const std::array<std::shared_ptr<PictureBox>, 5> modes{
        document_.gallery_images_picture_0, document_.gallery_images_picture_1, document_.gallery_images_picture_2,
        document_.gallery_images_picture_3, document_.gallery_images_picture_4};
    for (std::size_t index = 0; index < modes.size(); ++index) {
        (*modes[index]).set_background(index % 2U == 0U ? Color::rgba(244, 247, 249) : Color::rgba(226, 234, 240));
    }
    const std::array<std::shared_ptr<PictureBox>, 3> opacities{
        document_.gallery_images_opacity_0, document_.gallery_images_opacity_1, document_.gallery_images_opacity_2};
    for (const std::shared_ptr<PictureBox>& picture : opacities) {
        (*picture).set_background(Color::rgba(230, 236, 241));
    }
}

void ImagesPage::fill_primitives() {
    (*document_.gallery_images_drawing_board).set_paint_callback(DrawingEffectsPaint());

    std::shared_ptr<gui_drawing::Bitmap> raster_bitmap =
        std::make_shared<gui_drawing::Bitmap>(12U, 12U);
    for (std::uint32_t index = 0U; index < 12U; ++index) {
        (*raster_bitmap).set_pixel(index, index,
                                   gui_drawing::Color::from_rgb(39U, 116U, 184U));
        (*raster_bitmap).set_pixel(11U - index, index,
                                   gui_drawing::Color::from_rgb(211U, 121U, 42U));
    }
    RasterCanvas& raster = *document_.gallery_images_raster_canvas;
    raster.set_bitmap(std::move(raster_bitmap));
    raster.set_zoom(6.0);
    raster.set_transparency_cell_size(8.0);
}

void add_images_and_drawing(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ImagesPage>(page));
}

} // namespace gui_forms::gallery::pages
