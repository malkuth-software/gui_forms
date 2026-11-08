#include "pages.hpp"

#include "notices-and-badges_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the notices-and-badges page: the controls
// demo/pages/notices-and-badges.form describes, and their behaviour. The
// page installs the action buttons the form cannot place (a banner's and
// the empty state's action slots), and a Command whose button shows every
// closed banner again.
class NoticesAndBadgesPage final : public gui_forms::Component {
public:
    explicit NoticesAndBadgesPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void install_actions();
    void on_reopen();

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    NoticesAndBadgesDocument<NoticesAndBadgesPage> document_{};
    std::array<std::shared_ptr<InfoBanner>, 4> banners_{};
    Command reopen_{};
};

NoticesAndBadgesPage::NoticesAndBadgesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    install_actions();
    reopen_.set_text("Show closed banners");
    reopen_.set_description("Show every closed banner again");
    (*document_.gallery_notices_reopen).bind(reopen_);
    on(reopen_.invoked(), *this, &NoticesAndBadgesPage::on_reopen);
}

void NoticesAndBadgesPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size, where each is placed by its bounds in design coordinates. The
    // copy of the child list keeps the iteration valid while the root
    // shrinks.
    Control& root = *document_.gallery_notices_and_badges_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_notices_banners, document_.gallery_notices_badges,
        document_.gallery_notices_empty};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
    banners_ = {document_.gallery_notices_information, document_.gallery_notices_warning,
                document_.gallery_notices_success, document_.gallery_notices_error};
}

// The warning banner offers a retry and the empty state a first message;
// neither slot had an action before, so nothing is detached.
void NoticesAndBadgesPage::install_actions() {
    const std::shared_ptr<Button> retry = make_control<Button>(StableId("gallery.notices.warning.retry"), "Retry");
    const Control::Ptr retry_previous = (*document_.gallery_notices_warning).set_action(retry);
    static_cast<void>(retry_previous);
    const std::shared_ptr<Button> compose =
        make_control<Button>(StableId("gallery.notices.empty.compose"), "Write a message");
    const Control::Ptr compose_previous = (*document_.gallery_notices_empty_mail).set_action(compose);
    static_cast<void>(compose_previous);
}

void NoticesAndBadgesPage::on_reopen() {
    for (const std::shared_ptr<InfoBanner>& banner : banners_) {
        (*banner).visible = true;
    }
}

void add_notices_and_badges(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<NoticesAndBadgesPage>(page));
}

}  // namespace gui_forms::gallery::pages
