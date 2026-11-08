#include "pages.hpp"

#include "dock-and-anchor_form.hpp"

#include <cmath>
#include <string>

namespace gui_forms::gallery::pages {

// Owner of the Dock and Anchor page that demo/pages/dock-and-anchor.form
// describes: a padded dock canvas whose edges are hidden and re-ordered at
// runtime, an anchor canvas whose parent slot is rebased live, and the
// Windows Forms Splitter docked after a pane it resizes. The two toggles
// are page state kept here, not on the controls; the shell status line is
// reached through the shared context. The page keeps in code what a form
// does not carry: the canvases' and panes' faces, the splitter's minimum
// sizes and the handlers.
class DockAndAnchorPage final : public gui_forms::Component {
public:
    DockAndAnchorPage(const std::shared_ptr<Surface>& body,
                      const std::shared_ptr<PageContext>& context)
        : context_(context) {
        document_.initialize_component(*this);
        mount_form_children(*document_.gallery_dock_document, *body);
        (*document_.gallery_dock_group).set_style(page_style());
        (*document_.gallery_anchor_group).set_style(page_style());
        (*document_.gallery_splitter_group).set_style(page_style());
        (*document_.gallery_dock_canvas).set_background(Color::rgba(238, 243, 247));
        (*document_.gallery_anchor_canvas).set_background(Color::rgba(247, 249, 251));
        (*document_.gallery_splitter_canvas).set_background(Color::rgba(247, 249, 251));
        (*document_.gallery_splitter_folders).set_background(Color::rgba(225, 235, 243));
        (*document_.gallery_splitter_files).set_background(paper);
        (*document_.gallery_splitter).set_min_size(80.0);
        (*document_.gallery_splitter).set_min_extra(120.0);
        on((*document_.gallery_dock_toggle_left).clicked(), *this, &DockAndAnchorPage::on_toggle_left);
        on((*document_.gallery_dock_swap_top).clicked(), *this, &DockAndAnchorPage::on_swap_top);
        on((*document_.gallery_anchor_resize).clicked(), *this, &DockAndAnchorPage::on_resize);
        on((*document_.gallery_splitter).splitter_moved(), *this, &DockAndAnchorPage::on_splitter_moved);
    }

private:
    void report(const std::string& text) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (context && (*context).status) (*(*context).status).text = text;
    }

    void on_toggle_left() {
        const bool visible = !(*document_.gallery_dock_left).visible.read();
        (*document_.gallery_dock_left).visible = visible;
        (*document_.gallery_dock_toggle_left).text = visible ? "Hide left edge" : "Restore left edge";
        report(visible ? "Dock · left edge restored · fill contracted"
                       : "Dock · hidden edge released · fill expanded");
    }

    void on_swap_top() {
        const bool next = !top_b_front_;
        const std::shared_ptr<Button>& old_first = next ? document_.gallery_dock_top_first : document_.gallery_dock_top_second;
        const std::shared_ptr<Button>& new_first = next ? document_.gallery_dock_top_second : document_.gallery_dock_top_first;
        const std::optional<std::size_t> destination =
            (*document_.gallery_dock_canvas).child_index((*old_first).runtime_id());
        if (!destination ||
            !(*document_.gallery_dock_canvas).set_child_index((*new_first).runtime_id(), *destination)) {
            return;
        }
        top_b_front_ = next;
        (*document_.gallery_dock_swap_top).text = next ? "Move Top A behind Top B"
                                   : "Move Top B behind Top A";
        report(next ? "Dock · Top B now consumes the edge first"
                    : "Dock · Top A now consumes the edge first");
    }

    void on_resize() {
        expanded_ = !expanded_;
        (*document_.gallery_anchor_group).set_design_bounds(
            *document_.gallery_anchor_canvas, expanded_ ? Rect{18.0, 38.0, 730.0, 190.0}
                                       : Rect{18.0, 38.0, 600.0, 170.0});
        (*document_.gallery_anchor_resize).text = expanded_ ? "Restore specimen" : "Expand specimen";
        report(expanded_ ? "Anchor · parent expanded · stretch/right/center updated"
                         : "Anchor · parent restored from retained design slot");
    }

    void on_splitter_moved(const SplitterMoveEvent& move) {
        const long width = std::lround(move.split_position);
        report("Splitter · Folders is " + std::to_string(width) + " wide");
    }

    std::weak_ptr<PageContext> context_;
    DockAnchorDocument<DockAndAnchorPage> document_{};
    bool top_b_front_{};
    bool expanded_{};
};

void add_dock_and_anchor(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<DockAndAnchorPage>(page, context));
}

} // namespace gui_forms::gallery::pages
