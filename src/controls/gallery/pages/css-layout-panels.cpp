#include "pages.hpp"

#include "css-layout-panels_form.hpp"

#include <array>

namespace gui_forms::gallery::pages {

// Owner of the CSS Layout Panels page that demo/pages/css-layout-panels.form
// describes: the containers of stage L that lay their children out by CSS
// rules, each as one group of specimens: the FlexPanel group (phase L3), the
// GridPanel group (L4), the BlockPanel group (L5) and the TextBlock group
// (L8). The form carries every layout setting; the page only mounts it.
class CssLayoutPanelsPage final : public gui_forms::Component {
public:
    CssLayoutPanelsPage(const std::shared_ptr<Surface>& body,
                        const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, a design surface of the same 1000 x 660
        // size. The copy of the child list keeps the iteration valid while
        // the root shrinks.
        Control& root = *document_.gallery_css_layout_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
    }

    CssLayoutDocument<CssLayoutPanelsPage> document_{};
};

void add_css_layout_panels(const std::shared_ptr<Surface>& page,
                           const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<CssLayoutPanelsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
