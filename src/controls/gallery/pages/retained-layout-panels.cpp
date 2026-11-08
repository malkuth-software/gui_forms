#include "pages.hpp"

#include "retained-layout-panels_form.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Retained Layout Panels page: the four FlowLayoutPanelCompat
// directions and the mixed-track TableLayoutPanelCompat that
// demo/pages/retained-layout-panels.form describes, and their behaviour. The
// two range controls inside the table share one application-owned
// Value<double>, and the table's check box binds a Value<bool>; the page
// observes those values, never the controls.
class LayoutPanelsPage final : public gui_forms::Component {
public:
    LayoutPanelsPage(const std::shared_ptr<Surface>& body, const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        (*document_.gallery_layout_table_percent_one).bind(percent_one_);
        // Same Value<double> as the slider: the bar follows the model, not
        // the slider's input events.
        (*document_.gallery_layout_table_percent_two).bind(table_share_);
        (*document_.gallery_layout_table_progress).bind(table_share_);
        on(table_share_.changed(), *this, &LayoutPanelsPage::on_share_changed);
        on(percent_one_.changed(), *this, &LayoutPanelsPage::on_percent_one_changed);
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, a design surface of the same 1000 x 660
        // size. The copy of the child list keeps the iteration valid while
        // the root shrinks.
        Control& root = *document_.gallery_layout_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        const std::array<std::shared_ptr<ScaledGroupBox>, 6> groups{
            document_.gallery_layout_flow_group,  document_.gallery_layout_flow_card_0,
            document_.gallery_layout_flow_card_1, document_.gallery_layout_flow_card_2,
            document_.gallery_layout_flow_card_3, document_.gallery_layout_table_group};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
        note_ = document_.gallery_layout_note;
    }

    void on_share_changed(const double value) {
        (*note_).text = "Shared table value " + std::to_string(static_cast<int>(value)) +
                          " · slider and progress bar bind one Value<double>; authored bounds never become the "
                          "parent-assigned slot.";
    }

    void on_percent_one_changed(const bool checked) {
        (*note_).text = checked ? "Percent · 1 share checked · the check box binds a Value<bool>; authored bounds "
                                    "never become the parent-assigned slot."
                                  : "Percent · 1 share cleared · the check box binds a Value<bool>; authored bounds "
                                    "never become the parent-assigned slot.";
    }

    RetainedLayoutDocument<LayoutPanelsPage> document_{};
    std::shared_ptr<Label> note_;
    Value<double> table_share_{68.0};
    Value<bool> percent_one_{true};
};

void add_layout_panels(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<LayoutPanelsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
