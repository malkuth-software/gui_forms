#include "pages.hpp"
#include "gui_forms/components/about_dialog/about_dialog.hpp"
#include "gui_forms/controls/callout.hpp"
#include "gui_forms/controls/lookup_combo_box.hpp"
#include "gui_forms/controls/modal_layer.hpp"
#include "gui_forms/controls/step_indicator.hpp"
#include "gui_forms/controls/wizard.hpp"

#include "guided-tasks_form.hpp"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::gallery::pages {

// Owns the guided-tasks page (roadmap row 2.5): the controls
// demo/pages/guided-tasks.form describes, and their behaviour. It gives the
// lookups their columns and rows, gates the wizard's first page on its
// check box and reports the wizard's outcome, advances the stand-alone step
// indicator with the wizard, and opens the callout, the modal layer and the
// About box from their buttons.
class GuidedTasksPage final : public gui_forms::Component {
public:
    explicit GuidedTasksPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void on_agree_changed(bool agreed);
    void on_page_changed(std::uint32_t page);
    void on_finished();
    void on_cancelled();
    void on_show_callout(ButtonBase& button);
    void on_open_modal(ButtonBase& button);
    void on_modal_ok(ButtonBase& button);
    void on_modal_cancel(ButtonBase& button);
    void on_modal_closed(DialogResult result);
    void on_about(ButtonBase& button);
    void on_about_closed(DialogResult result);

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    GuidedTasksDocument<GuidedTasksPage> document_{};
    std::shared_ptr<ModalLayer> modal_{};
    std::shared_ptr<AboutDialog> about_{};
};

GuidedTasksPage::GuidedTasksPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);

    const std::vector<LookupColumn> country_columns{{"Code", 60.0}, {"Country", 180.0}};
    LookupComboBox& country = *document_.gallery_guided_wizard_country;
    country.set_columns(country_columns);
    country.set_rows({{"de", {"DE", "Germany"}},
                      {"fr", {"FR", "France"}},
                      {"gb", {"GB", "United Kingdom"}},
                      {"jp", {"JP", "Japan"}},
                      {"nz", {"NZ", "New Zealand"}},
                      {"us", {"US", "United States"}}});
    country.selected_key = std::string{"nz"};
    LookupComboBox& currency = *document_.gallery_guided_lookup;
    currency.set_columns({{"Code", 60.0}, {"Currency", 150.0}, {"Symbol", 60.0}});
    currency.set_rows({{"eur", {"EUR", "Euro", "€"}},
                       {"gbp", {"GBP", "Pound sterling", "£"}},
                       {"jpy", {"JPY", "Yen", "¥"}},
                       {"nzd", {"NZD", "New Zealand dollar", "$"}},
                       {"usd", {"USD", "US dollar", "$"}}});
    currency.selected_key = std::string{"eur"};
    (*document_.gallery_guided_steps).set_steps({"Cart", "Address", "Payment", "Done"});

    (*document_.gallery_guided_wizard_account).can_advance = false;
    on((*document_.gallery_guided_wizard_agree).checked_changed(), *this, &GuidedTasksPage::on_agree_changed);
    Wizard& wizard = *document_.gallery_guided_wizard;
    on(wizard.page_changed(), *this, &GuidedTasksPage::on_page_changed);
    on(wizard.finished(), *this, &GuidedTasksPage::on_finished);
    on(wizard.cancelled(), *this, &GuidedTasksPage::on_cancelled);

    on((*document_.gallery_guided_callout_target).clicked(), *this, &GuidedTasksPage::on_show_callout);
    on((*document_.gallery_guided_open_modal).clicked(), *this, &GuidedTasksPage::on_open_modal);
    on((*document_.gallery_guided_modal_ok).clicked(), *this, &GuidedTasksPage::on_modal_ok);
    on((*document_.gallery_guided_modal_cancel).clicked(), *this, &GuidedTasksPage::on_modal_cancel);
    on((*document_.gallery_guided_about).clicked(), *this, &GuidedTasksPage::on_about);

    modal_ = make_control<ModalLayer>(StableId("gallery.guided.modal"));
    (*modal_).accessible_name = std::string{"Modal layer"};
    on((*modal_).closed(), *this, &GuidedTasksPage::on_modal_closed);
    about_ = std::make_shared<AboutDialog>(
        StableId("gallery.guided.about-dialog"),
        AboutInfo{.product = "GUI.Forms Gallery",
                  .version = "2.5",
                  .description = "The control gallery of GUI.Forms, a retained, CPU-rendered user interface library.",
                  .copyright = "Copyright the GUI.Forms authors. MIT licence.",
                  .credits = "Portsmouth Rapids typeface, MIT. HarfBuzz and FreeType text. Skia CPU rendering."});
    on((*about_).closed(), *this, &GuidedTasksPage::on_about_closed);
}

void GuidedTasksPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body. The modal layer's content and the callout
    // are kept out: they are shown only on request.
    Control& root = *document_.gallery_guided_tasks_document;
    static_cast<void>(root.remove_child((*document_.gallery_guided_modal_content).runtime_id()));
    Control& side = *document_.gallery_guided_side;
    static_cast<void>(side.remove_child((*document_.gallery_guided_callout).runtime_id()));
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

void GuidedTasksPage::on_agree_changed(bool agreed) {
    (*document_.gallery_guided_wizard_account).can_advance = agreed;
}

void GuidedTasksPage::on_page_changed(std::uint32_t page) {
    StepIndicator& steps = *document_.gallery_guided_steps;
    const std::uint32_t last = static_cast<std::uint32_t>(steps.steps().size() - 1U);
    steps.current_step = page < last ? page : last;
    (*document_.gallery_guided_wizard_outcome).text = "Not finished yet.";
}

void GuidedTasksPage::on_finished() {
    (*document_.gallery_guided_wizard_outcome).text = "Finished: the account is created.";
    StepIndicator& steps = *document_.gallery_guided_steps;
    steps.current_step = static_cast<std::uint32_t>(steps.steps().size() - 1U);
}

void GuidedTasksPage::on_cancelled() {
    (*document_.gallery_guided_wizard_outcome).text = "Cancelled.";
    (*document_.gallery_guided_wizard).current_page = std::uint32_t{0U};
}

void GuidedTasksPage::on_show_callout(ButtonBase&) {
    Callout& callout = *document_.gallery_guided_callout;
    if (callout.is_shown()) return;
    static_cast<void>(callout.show(document_.gallery_guided_callout_target));
}

void GuidedTasksPage::on_open_modal(ButtonBase&) {
    if ((*modal_).is_shown()) return;
    static_cast<void>((*modal_).show(document_.gallery_guided_open_modal, document_.gallery_guided_modal_content));
}

void GuidedTasksPage::on_modal_ok(ButtonBase&) {
    static_cast<void>((*modal_).close(DialogResult::ok));
}

void GuidedTasksPage::on_modal_cancel(ButtonBase&) {
    static_cast<void>((*modal_).close(DialogResult::cancel));
}

void GuidedTasksPage::on_modal_closed(DialogResult result) {
    (*document_.gallery_guided_result).text =
        result == DialogResult::ok ? "The modal layer closed with OK." : "The modal layer was cancelled.";
}

void GuidedTasksPage::on_about(ButtonBase&) {
    if ((*about_).shown()) return;
    static_cast<void>((*about_).show(document_.gallery_guided_about));
}

void GuidedTasksPage::on_about_closed(DialogResult) {
    (*document_.gallery_guided_result).text = "The About box closed.";
}

void add_guided_tasks(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<GuidedTasksPage>(page));
}

}  // namespace gui_forms::gallery::pages
