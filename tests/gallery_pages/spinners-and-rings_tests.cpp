#include "support.hpp"

namespace gallery_pages_tests {

// The spinners-and-rings page (W-4): the three spinners and their Active
// check box, the determinate rings' values, the stepped ring's Advance
// Command, the indeterminate ring and its check box, and the progress-bar
// semantics both controls publish.
void test_spinners_and_rings(gui_forms::Window& window) {
    select_page(window, "spinners-and-rings");
    require(window.find("gallery.spinners-and-rings.heading") != nullptr,
            "spinners-and-rings page shows its heading");
    const std::shared_ptr<Spinner> small = std::dynamic_pointer_cast<Spinner>(window.find("gallery.spinners.small"));
    const std::shared_ptr<Spinner> large = std::dynamic_pointer_cast<Spinner>(window.find("gallery.spinners.large"));
    require(small && large && (*small).active.read() && (*large).active.read(), "the spinners start active");
    click(window, window.find("gallery.spinners.active"));
    window.perform_layout();
    require(!(*small).active.read() && !(*large).active.read(), "clearing Active stops every spinner");
    click(window, window.find("gallery.spinners.active"));
    window.perform_layout();
    require((*small).active.read(), "checking Active starts them again");

    const std::shared_ptr<ProgressRing> quarter =
        std::dynamic_pointer_cast<ProgressRing>(window.find("gallery.rings.quarter"));
    const std::shared_ptr<ProgressRing> full = std::dynamic_pointer_cast<ProgressRing>(window.find("gallery.rings.full"));
    require(quarter && (*quarter).lit_dots() == ProgressRing::dot_count / 4U, "the quarter ring lights a quarter");
    require(full && (*full).lit_dots() == ProgressRing::dot_count, "the full ring lights every dot");

    const std::shared_ptr<ProgressRing> stepped =
        std::dynamic_pointer_cast<ProgressRing>(window.find("gallery.rings.stepped"));
    require(stepped && (*stepped).value.read() == 10.0, "the stepped ring starts at ten");
    click(window, window.find("gallery.rings.advance"));
    window.perform_layout();
    require((*stepped).value.read() == 20.0, "Advance adds ten");

    const std::shared_ptr<ProgressRing> busy = std::dynamic_pointer_cast<ProgressRing>(window.find("gallery.rings.busy"));
    require(busy && (*busy).indeterminate.read(), "the busy ring starts indeterminate");
    click(window, window.find("gallery.rings.indeterminate"));
    window.perform_layout();
    require(!(*busy).indeterminate.read(), "clearing Indeterminate shows the ring's value");

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"progress_bar\"") != std::string::npos,
            "spinners and rings publish progress-bar semantics");
}

}  // namespace gallery_pages_tests
