#include "demo_parts.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/choice_group.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/date_time_picker.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/controls/progress_bar.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/toggle_switch.hpp"

#include "template-dashboard_form.hpp"
#include "template-record_form.hpp"
#include "template-settings_form.hpp"
#include "template-sign-in_form.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

// The ui-templates page (the survey's "UI templates as Gallery content",
// worth it with no library API): four starting screens written as form
// documents under demo/pages/templates/, a sign-in, a settings screen, a
// record screen and a dashboard. A ChoiceGroup shows one at a time; each
// document's root is placed on the page whole, so the page shows exactly
// what an application would get by copying the .form file (the Source
// button in the page header shows it). The page adds only light behaviour:
// the sign-in check, the settings save message, the record navigation and
// the dashboard's quota.
namespace gui_forms::gallery::pages {
namespace {

struct CustomerRecord final {
    std::string_view name{};
    std::string_view company{};
    std::string_view email{};
    std::string_view phone{};
    std::size_t status{};
    DateTimeValue since{};
    bool newsletter{};
    std::string_view notes{};
};

constexpr std::array<CustomerRecord, 3> records{{
    {"Ada Brennan", "Abbott Joinery", "ada@abbott.example", "+44 113 496 0000", 1U,
     DateTimeValue{2019, 4U, 2U, 0U, 0U, 0U, 0U}, true, "Prefers email. Orders oak in spring."},
    {"Bao Tran", "Birch & Vale", "bao@birchvale.example", "+351 22 000 0000", 0U,
     DateTimeValue{2024, 9U, 17U, 0U, 0U, 0U, 0U}, false, "Met at the Porto trade fair."},
    {"Carla Mendes", "Cobalt Freight", "carla@cobalt.example", "+372 600 0000", 2U,
     DateTimeValue{2016, 1U, 11U, 0U, 0U, 0U, 0U}, true, "No orders since March; follow up."},
}};

constexpr std::array<std::string_view, 4> template_names{"Sign-in", "Settings", "Record", "Dashboard"};
constexpr std::array<std::string_view, 4> template_notes{
    "demo/pages/templates/template-sign-in.form: a centred card with email, password, keep-me-signed-in "
    "and the primary action. The page checks that both fields are filled in.",
    "demo/pages/templates/template-settings.form: account fields, notification switches and appearance "
    "choices in three groups, with Save and Cancel.",
    "demo/pages/templates/template-record.form: one customer's fields in a details group with record "
    "navigation; the page moves through three fixture records.",
    "demo/pages/templates/template-dashboard.form: four key figures, a quota bar and recent activity.",
};

class UiTemplatesPage final : public gui_forms::Component {
public:
    explicit UiTemplatesPage(const std::shared_ptr<Surface>& body) : body_(body) {
        sign_in_.initialize_component(*this);
        settings_.initialize_component(*this);
        record_.initialize_component(*this);
        dashboard_.initialize_component(*this);
    }

    void build() {
        Surface& body = *body_;
        add_page_heading(body, "templates", "UI TEMPLATES",
                         "Starting screens written as form documents: copy a .form file into an application and "
                         "edit it. Each is stock controls only.");
        chooser_ = make_control<ChoiceGroup>(StableId("gallery.templates.choice"));
        (*chooser_).accessible_name = "Template";
        (*chooser_).set_items({ChoiceItem{0, std::string(template_names[0])}, ChoiceItem{1, std::string(template_names[1])},
                               ChoiceItem{2, std::string(template_names[2])}, ChoiceItem{3, std::string(template_names[3])}});
        body.add_at(chooser_, {24.0, 90.0, 520.0, 28.0});
        roots_ = {sign_in_.gallery_templates_sign_in_document, settings_.gallery_templates_settings_document,
                  record_.gallery_templates_record_document, dashboard_.gallery_templates_dashboard_document};
        for (const Control::Ptr& root : roots_) body.add_at(root, {24.0, 128.0, 640.0, 460.0});
        note_ = label("gallery.templates.note", "", 11.0, 400, "ink");
        (*note_).text_wrapping = TextWrapping::word;
        body.add_at(note_, {688.0, 128.0, 288.0, 180.0});
        body.add_at(wrapped_label("gallery.templates.copy",
                          "The Source button in the page header shows the four documents and this page's code.", 10.0,
                          400, "muted"),
                    {688.0, 316.0, 288.0, 60.0});

        (*sign_in_.gallery_templates_sign_in_password).use_system_password_character = true;
        (*record_.gallery_templates_record_notes).multiline = true;
        (*record_.gallery_templates_record_notes).word_wrap = true;
        (*dashboard_.gallery_templates_dashboard_quota_bar).set_range(0.0, 100.0);
        show_record();
        show_quota(68.0);
        (*chooser_).selected_index = 0;
        show_template(0);
    }

    void connect() {
        on((*chooser_).changed(), *this, &UiTemplatesPage::show_template);
        on((*sign_in_.gallery_templates_sign_in_submit).clicked(), *this, &UiTemplatesPage::on_sign_in);
        on((*sign_in_.gallery_templates_sign_in_forgot).clicked(), *this, &UiTemplatesPage::on_forgot);
        on((*settings_.gallery_templates_settings_save).clicked(), *this, &UiTemplatesPage::on_settings_save);
        on((*settings_.gallery_templates_settings_cancel).clicked(), *this, &UiTemplatesPage::on_settings_cancel);
        on((*record_.gallery_templates_record_previous).clicked(), *this, &UiTemplatesPage::on_record_previous);
        on((*record_.gallery_templates_record_next).clicked(), *this, &UiTemplatesPage::on_record_next);
        on((*record_.gallery_templates_record_save).clicked(), *this, &UiTemplatesPage::on_record_save);
        on((*record_.gallery_templates_record_delete).clicked(), *this, &UiTemplatesPage::on_record_delete);
        on((*dashboard_.gallery_templates_dashboard_refresh).clicked(), *this, &UiTemplatesPage::on_refresh);
    }

private:
    void show_template(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= roots_.size()) return;
        for (std::size_t root = 0; root < roots_.size(); ++root) {
            (*roots_[root]).visible = root == static_cast<std::size_t>(index);
        }
        (*note_).text = std::string(template_notes[static_cast<std::size_t>(index)]);
    }

    // -- sign-in ----------------------------------------------------------
    void on_sign_in() {
        const std::string email = (*sign_in_.gallery_templates_sign_in_email).text.read();
        const std::string password = (*sign_in_.gallery_templates_sign_in_password).text.read();
        std::string message = "Signed in as " + email + " (nothing is sent anywhere)";
        if (email.find('@') == std::string::npos) message = "Enter an email address";
        if (email.find('@') != std::string::npos && password.empty()) message = "Enter your password";
        (*sign_in_.gallery_templates_sign_in_message).text = message;
    }

    void on_forgot() {
        (*sign_in_.gallery_templates_sign_in_message).text = "An application would send a reset link here";
    }

    // -- settings ---------------------------------------------------------
    void on_settings_save() {
        const bool mail = (*settings_.gallery_templates_settings_notify_mail).checked.read();
        (*settings_.gallery_templates_settings_message).text =
            std::string("Saved") + (mail ? ", with email digests" : ", without email digests");
    }

    void on_settings_cancel() { (*settings_.gallery_templates_settings_message).text = "Changes discarded"; }

    // -- record -----------------------------------------------------------
    void show_record() {
        const CustomerRecord& shown = records[record_index_];
        (*record_.gallery_templates_record_name).text = std::string(shown.name);
        (*record_.gallery_templates_record_company).text = std::string(shown.company);
        (*record_.gallery_templates_record_email).text = std::string(shown.email);
        (*record_.gallery_templates_record_phone).text = std::string(shown.phone);
        (*record_.gallery_templates_record_status).selected_index = stxlib::Optional<std::size_t>::of(shown.status);
        (*record_.gallery_templates_record_since).value = shown.since;
        (*record_.gallery_templates_record_newsletter).checked = shown.newsletter;
        (*record_.gallery_templates_record_notes).text = std::string(shown.notes);
        (*record_.gallery_templates_record_position).text =
            "Record " + std::to_string(record_index_ + 1U) + " of " + std::to_string(records.size());
        (*record_.gallery_templates_record_previous).enabled = record_index_ > 0U;
        (*record_.gallery_templates_record_next).enabled = record_index_ + 1U < records.size();
    }

    void on_record_previous() {
        if (record_index_ == 0U) return;
        --record_index_;
        show_record();
    }

    void on_record_next() {
        if (record_index_ + 1U >= records.size()) return;
        ++record_index_;
        show_record();
    }

    void on_record_save() {
        (*record_.gallery_templates_record_message).text =
            "Saved " + (*record_.gallery_templates_record_name).text.read() + " (in this page only)";
    }

    void on_record_delete() {
        (*record_.gallery_templates_record_message).text = "An application would ask before deleting";
    }

    // -- dashboard --------------------------------------------------------
    void show_quota(double percent) {
        (*dashboard_.gallery_templates_dashboard_quota_bar).value = percent;
        (*dashboard_.gallery_templates_dashboard_quota_value).text =
            std::to_string(static_cast<int>(percent)) + "% of target";
    }

    void on_refresh() {
        quota_ = quota_ >= 95.0 ? 40.0 : quota_ + 9.0;
        show_quota(quota_);
    }

    std::shared_ptr<Surface> body_;
    TemplateSignInDocument<UiTemplatesPage> sign_in_{};
    TemplateSettingsDocument<UiTemplatesPage> settings_{};
    TemplateRecordDocument<UiTemplatesPage> record_{};
    TemplateDashboardDocument<UiTemplatesPage> dashboard_{};
    std::shared_ptr<ChoiceGroup> chooser_;
    std::array<Control::Ptr, 4> roots_{};
    std::shared_ptr<Label> note_;
    std::size_t record_index_{};
    double quota_{68.0};
};

} // namespace

void add_ui_templates(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    const std::shared_ptr<UiTemplatesPage> owner = gui_forms::detail::make_library_shared<UiTemplatesPage>(page);
    (*owner).build();
    (*context).components.add(owner);
    (*owner).connect();
}

} // namespace gui_forms::gallery::pages
