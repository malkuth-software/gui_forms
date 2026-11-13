#include "gui_forms/components/about_dialog/about_dialog.hpp"

#include "gui_forms/controls/task_dialog.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

constexpr std::string_view ok_id = "ok";

// The page an About box shows: the heading names the product and version,
// the text is the description and the copyright as two paragraphs, and the
// credits sit behind the expander.
TaskDialogPage about_page(const AboutInfo& info) {
    TaskDialogPage page;
    page.caption = info.caption.empty() ? "About " + info.product : info.caption;
    page.heading = info.product;
    if (!info.version.empty()) {
        if (!page.heading.empty()) page.heading += ' ';
        page.heading += info.version;
    }
    page.text = info.description;
    if (!info.copyright.empty()) {
        if (!page.text.empty()) page.text += "\n\n";
        page.text += info.copyright;
    }
    page.icon = TaskDialogIcon::information;
    page.buttons.push_back({.stable_id = std::string(ok_id), .standard = TaskDialogStandardButton::ok});
    page.default_button = std::string(ok_id);
    if (!info.credits.empty()) {
        page.expander = TaskDialogExpander{.text = info.credits,
                                           .collapsed_button_text = "Show credits",
                                           .expanded_button_text = "Hide credits"};
    }
    // Escape closes the box, as an About form's OK button is also its
    // cancel button.
    page.allow_cancel = true;
    return page;
}

} // namespace

AboutDialog::AboutDialog(StableId stable_id, AboutInfo info) : info_(std::move(info)) {
    // set_page validates the text before the dialog holds anything.
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(std::move(stable_id));
    (*dialog).set_page(about_page(info_));
    task_dialog_ = std::move(dialog);
    on((*task_dialog_).closed(), *this, &AboutDialog::on_task_dialog_closed);
}

AboutDialog::~AboutDialog() {
    release_task_dialog();
}

bool AboutDialog::show(const Control::Ptr& owner) {
    if (!is_alive() || !task_dialog_) throw std::logic_error("GUI.Forms cannot show a disposed AboutDialog");
    const bool opened = (*task_dialog_).show(owner);
    return opened;
}

bool AboutDialog::shown() const noexcept {
    const bool showing = task_dialog_ && (*task_dialog_).shown();
    return showing;
}

void AboutDialog::close() {
    if (!shown()) return;
    (*task_dialog_).close(ok_id);
}

void AboutDialog::on_task_dialog_closed(const TaskDialogResult&) {
    // OK, Escape and a revoked dialog all acknowledge the box.
    closed_.emit(DialogResult::ok);
}

void AboutDialog::release_task_dialog() noexcept {
    const std::shared_ptr<TaskDialog> dialog = std::move(task_dialog_);
    task_dialog_.reset();
    if (!dialog) return;
    try {
        (*dialog).dispose();
    } catch (...) {
        // Disposal off the UI thread throws; the window closes the dialog's
        // overlay when it shuts down.
    }
}

void AboutDialog::on_dispose() noexcept {
    release_task_dialog();
}

namespace {

// The events AboutDialog adds to those of Component.
constexpr std::array<InteractionDeclaration, 1> about_dialog_interactions{{
    {.name = "closed", .payload = "DialogResult", .mutable_payload = false},
}};

} // namespace

void AboutDialog::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), about_dialog_interactions.begin(), about_dialog_interactions.end());
}

} // namespace gui_forms
