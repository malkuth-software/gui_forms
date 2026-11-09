#include "pages.hpp"

#include "notices-and-badges_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the notices-and-badges page: the controls
// demo/pages/notices-and-badges.form describes, and their behaviour. The
// page installs the action buttons the form cannot place (a banner's and
// the empty state's action slots), a Command whose button shows every
// closed banner again, and one whose button shows a Toast in the window.
class NoticesAndBadgesPage final : public gui_forms::Component {
public:
    explicit NoticesAndBadgesPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void install_actions();
    void on_reopen();
    void on_show_toast();

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    NoticesAndBadgesDocument<NoticesAndBadgesPage> document_{};
    std::array<std::shared_ptr<InfoBanner>, 4> banners_{};
    Command reopen_{};
    Command show_toast_{};
    // Numbers the toasts the page shows, so that each has its own ID.
    std::uint64_t toasts_shown_{};
};

NoticesAndBadgesPage::NoticesAndBadgesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    install_actions();
    reopen_.set_text("Show closed banners");
    reopen_.set_description("Show every closed banner again");
    (*document_.gallery_notices_reopen).bind(reopen_);
    on(reopen_.invoked(), *this, &NoticesAndBadgesPage::on_reopen);
    show_toast_.set_text("Show a toast");
    show_toast_.set_description("Show a brief notice above the bottom of the window");
    (*document_.gallery_notices_show_toast).bind(show_toast_);
    on(show_toast_.invoked(), *this, &NoticesAndBadgesPage::on_show_toast);
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

// The toast belongs to the button that asked for it: it closes when the
// page goes and the button leaves the window. A new toast replaces the one
// that shows.
void NoticesAndBadgesPage::on_show_toast() {
    const std::shared_ptr<Button>& owner = document_.gallery_notices_show_toast;
    Window* const window = (*owner).attached_window();
    if (window == nullptr) return;
    ++toasts_shown_;
    ToastContent content{};
    content.message = "Message archived.";
    content.action_text = "Undo";
    content.severity = NoticeSeverity::success;
    const std::shared_ptr<Toast> toast =
        make_control<Toast>(StableId("gallery.notices.toast." + std::to_string(toasts_shown_)), std::move(content));
    const std::uint64_t id = (*window).show_toast(owner, toast);
    static_cast<void>(id);
}

void add_notices_and_badges(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<NoticesAndBadgesPage>(page));
}

}  // namespace gui_forms::gallery::pages
