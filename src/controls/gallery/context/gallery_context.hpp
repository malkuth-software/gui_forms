#pragma once

#include "gallery_form_pages_form.hpp"
#include "gallery_model.hpp"
#include "gallery_window.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::gallery {

// The Gallery program's state beside its window: the model the window's
// form binds (demo/gallery.form, FD-6) and the window it serves. The
// shell's page context owns it (the window's root Tag), so it lives as long
// as the window's controls. attach() arms one window rule per interaction
// the Gallery answers, each a named action below that applies its
// interaction to the model and calls synchronize(); the form names no
// program code (FD-6 R6).
class GalleryContext final {
public:
    // Declared first: the window's and the pages' bindings read it.
    GalleryModel model;
    // The Gallery's own pages (demo/gallery_form_pages.form), bound to
    // `model` by make_gallery (initialize_component); the shell moves their
    // groups into the page bodies.
    GalleryFormPagesDocument<GalleryModel> pages;
    // Observes the Gallery window, which owns this context through its
    // root; set by attach().
    GalleryWindow* window{};
    std::string drop_status;

    // Observes `gallery_window`, completes what the forms do not state and
    // arms the Gallery's rules on it. Call once, after the pages are made and
    // before the shell moves them. Throws std::logic_error when a rule is refused, leaving
    // the rules armed before it.
    void attach(GalleryWindow& gallery_window);

    // Brings what the bindings do not carry (the relief style, the text, the
    // collection row, the instrument, the status and value texts and the
    // diagnostics) up to date with the model after the interaction `cause`
    // (a stable ID, or "gallery.initial").
    void synchronize(std::string_view cause);

    // Shows `theme` in the Gallery window alone (Part K's per-window
    // override); null returns to the application's theme. The hook a
    // settings page uses to preview a theme. UI thread.
    void show_window_theme(std::shared_ptr<const Theme> theme);

    // The rules' actions (attach() arms one each). Each reads what it needs
    // from the window's controls or the model: an interaction trigger
    // carries no payload.
    static void reset_clicked(GalleryContext& context, const RuleFiring& firing);
    static void diagnostics_clicked(GalleryContext& context, const RuleFiring& firing);
    static void apply_clicked(GalleryContext& context, const RuleFiring& firing);
    static void precise_updates_clicked(GalleryContext& context, const RuleFiring& firing);
    static void invalidation_overlay_clicked(GalleryContext& context, const RuleFiring& firing);
    static void classic_relief_clicked(GalleryContext& context, const RuleFiring& firing);
    static void quiet_relief_clicked(GalleryContext& context, const RuleFiring& firing);
    static void three_state_clicked(GalleryContext& context, const RuleFiring& firing);
    static void contract_link_clicked(GalleryContext& context, const RuleFiring& firing);
    static void text_changed(GalleryContext& context, const RuleFiring& firing);
    static void slider_changed(GalleryContext& context, const RuleFiring& firing);
    static void collection_selection_changed(GalleryContext& context, const RuleFiring& firing);
    // Not a rule: CollectionList::dropped() is outside the rule vocabulary
    // and carries its summary, so it stays a subscription.
    void collection_dropped(const std::string& summary);

    // The controls the relief style and a reset reach: the command strip,
    // the four page groups and the diagnostics.
    [[nodiscard]] std::vector<Control::Ptr> styled_controls() const;

private:
    void activate(std::string_view stable_id);

    // Ends before the model and the window's controls (declared last).
    SubscriptionToken collection_drop_;
};

} // namespace gui_forms::gallery
