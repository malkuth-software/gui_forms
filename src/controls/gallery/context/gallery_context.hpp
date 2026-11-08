#pragma once

#include "gallery_form.hpp"
#include "gallery_model.hpp"
#include "gui_forms/gui_forms.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::gallery {

struct GalleryTree;

// The Gallery's own controls and their state: the controls demo/gallery.form
// describes (owned here through `document`), the model they show and the
// window they live in. The form names no program code (FD-6 R6): attach()
// arms one window rule per interaction the Gallery answers, each a named
// action below that applies its interaction to the model and calls
// synchronize().
class GalleryContext final {
public:
    GalleryModel model;
    // Observes the Gallery window; set by attach().
    Window* window{};
    std::string drop_status;
    GalleryDocument<GalleryContext> document;

    // Observes `gallery_window` and arms the Gallery's rules on it. Call
    // once, after the document is initialized; the window's rule set keeps
    // the rules, and this context must outlive the window (the shell's page
    // context holds it). Throws std::logic_error when a rule is refused,
    // leaving the rules armed before it.
    void attach(Window& gallery_window);

    // Brings every document control up to date with the model after the
    // interaction `cause` (a stable ID, or "gallery.initial").
    void synchronize(std::string_view cause);

    // The rules' actions (attach() arms one each). Each reads what it needs
    // from the document's controls: an interaction trigger carries no
    // payload.
    static void reset_clicked(GalleryContext& context, const RuleFiring& firing);
    static void diagnostics_clicked(GalleryContext& context, const RuleFiring& firing);
    static void apply_clicked(GalleryContext& context, const RuleFiring& firing);
    static void precise_updates_clicked(GalleryContext& context, const RuleFiring& firing);
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

    // The controls the shell mounts, in document order: the command strip,
    // the four page groups and the diagnostics overlay.
    [[nodiscard]] std::vector<Control::Ptr> mount_controls() const;

private:
    // Connects collection_drop_.
    friend GalleryTree build_gallery_tree();

    void activate(std::string_view stable_id);

    SubscriptionToken collection_drop_;
};

} // namespace gui_forms::gallery
