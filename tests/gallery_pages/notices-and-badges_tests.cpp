#include "support.hpp"

namespace gallery_pages_tests {

// The notices-and-badges page (W-4): the four banners and their severities,
// the actions the page installs, the badges' counts and dot, the empty
// state, live-region semantics, and the Command that shows closed banners
// again.
void test_notices_and_badges(gui_forms::Window& window) {
    select_page(window, "notices-and-badges");
    require(window.find("gallery.notices-and-badges.heading") != nullptr,
            "notices-and-badges page shows its heading");
    const std::shared_ptr<InfoBanner> information =
        std::dynamic_pointer_cast<InfoBanner>(window.find("gallery.notices.information"));
    const std::shared_ptr<InfoBanner> warning =
        std::dynamic_pointer_cast<InfoBanner>(window.find("gallery.notices.warning"));
    const std::shared_ptr<InfoBanner> success =
        std::dynamic_pointer_cast<InfoBanner>(window.find("gallery.notices.success"));
    const std::shared_ptr<InfoBanner> error =
        std::dynamic_pointer_cast<InfoBanner>(window.find("gallery.notices.error"));
    require(information && warning && success && error, "the page shows four banners");
    require((*information).severity.read() == InfoBannerSeverity::information &&
                (*warning).severity.read() == InfoBannerSeverity::warning &&
                (*success).severity.read() == InfoBannerSeverity::success &&
                (*error).severity.read() == InfoBannerSeverity::error,
            "each banner shows its own severity");
    require(!(*error).closable.read() && (*information).closable.read(),
            "the error banner cannot be closed and the others can");
    const Control::Ptr retry = window.find("gallery.notices.warning.retry");
    require(retry && (*warning).action() == retry, "the warning banner offers its retry action");

    const std::shared_ptr<Badge> updates = std::dynamic_pointer_cast<Badge>(window.find("gallery.notices.updates"));
    const std::shared_ptr<Badge> drafts = std::dynamic_pointer_cast<Badge>(window.find("gallery.notices.drafts"));
    const std::shared_ptr<Badge> activity = std::dynamic_pointer_cast<Badge>(window.find("gallery.notices.activity"));
    require(updates && (*updates).display_text() == "99+", "a count past the maximum shows 99+");
    require(drafts && (*drafts).showing() && (*drafts).display_text() == "0", "show_zero shows a zero count");
    require(activity && (*activity).mode.read() == BadgeMode::dot && (*activity).showing(),
            "the activity badge is a dot");

    const std::shared_ptr<EmptyState> empty =
        std::dynamic_pointer_cast<EmptyState>(window.find("gallery.notices.empty.mail"));
    const Control::Ptr compose = window.find("gallery.notices.empty.compose");
    require(empty && (*empty).title.read() == "No messages" && compose && (*empty).action() == compose,
            "the empty state shows its title and its action");

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"live_region\"") != std::string::npos,
            "the banners publish live-region semantics");

    require((*information).on_semantic_action(SemanticAction::close, std::string_view{}) &&
                !(*information).visible.read(),
            "the close action hides the information banner");
    window.perform_layout();
    click(window, window.find("gallery.notices.reopen"));
    window.perform_layout();
    require((*information).visible.read(), "the reopen Command shows the closed banner again");
}

}  // namespace gallery_pages_tests
