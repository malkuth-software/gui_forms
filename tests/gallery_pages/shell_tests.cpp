#include "support.hpp"

namespace gallery_pages_tests {

void test_page_contract_and_rendering() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    window.perform_layout();
    require(window.metrics_snapshot().control_count >= 200U,
            "the Gallery must retain a broad public control population");
    require(window.find("gallery.text.primary") &&
                window.find("gallery.collections.single.list") &&
                window.find("gallery.animation.easing") &&
                window.find("gallery.containers.split") &&
                window.find("gallery.images.picture.4") &&
                window.find("gallery.date.primary") &&
                window.find("gallery.host.clipboard.editor") &&
                window.find("gallery.layout.flow.0") &&
                window.find("gallery.layout.table") &&
                window.find("gallery.dock.canvas") &&
                window.find("gallery.anchor.canvas"),
            "the Gallery must publish stable IDs for every proving family");

    CountingPainter painter;
    window.paint(painter, {0.0, 0.0, 1280.0, 820.0});
    require(painter.commands > 150U,
            "the Gallery must generate a substantial retained painter command surface");
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        select_page(window, page.id);
    }
}

void test_gallery_semantic_surface() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    require(snapshot.node_count >= 30U && !snapshot.roots.empty(),
            "visible gallery page must publish a substantial semantic surface");
    require(snapshot.to_json().find("gallery.controls.button.0") != std::string::npos &&
                snapshot.to_json().find("\"role\":\"check_box\"") != std::string::npos &&
                snapshot.to_json().find("\"role\":\"radio_button\"") != std::string::npos,
            "gallery semantics must include distinct stock control roles and stable IDs");
}

} // namespace gallery_pages_tests
