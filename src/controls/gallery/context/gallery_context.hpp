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
// window they live in. Each handler serves one object of the document (owner
// decision FD6-2), connected by build_gallery_tree, since a form names no
// program code (FD-6 R6); it applies its interaction to the model and calls
// synchronize().
class GalleryContext final {
public:
    GalleryModel model;
    // Observes the Gallery window; set once the window exists.
    Window* window{};
    std::string drop_status;
    GalleryDocument<GalleryContext> document;

    // Brings every document control up to date with the model after the
    // interaction `cause` (a stable ID, or "gallery.initial").
    void synchronize(std::string_view cause);

    void reset_clicked(ButtonBase& button);
    void diagnostics_clicked(ButtonBase& button);
    void apply_clicked(ButtonBase& button);
    void precise_updates_clicked(ButtonBase& button);
    void classic_relief_clicked(ButtonBase& button);
    void quiet_relief_clicked(ButtonBase& button);
    void three_state_clicked(ButtonBase& button);
    void contract_link_clicked(ButtonBase& button);
    void text_changed(const std::string& text);
    void slider_changed(double value);
    void collection_selection_changed(const ListSelectionChange& change);
    // Not a document handler: CollectionList::dropped(), connected in code.
    void collection_dropped(const std::string& summary);

    // The controls the shell mounts, in document order: the command strip,
    // the four page groups and the diagnostics overlay.
    [[nodiscard]] std::vector<Control::Ptr> mount_controls() const;

private:
    // Connect the handlers.
    friend GalleryTree build_gallery_tree();
    friend void connect_handlers(GalleryContext& context);

    void activate(std::string_view stable_id);

    // The handlers' connections, made after the document's controls; they
    // end before `document` releases those controls (declared after it).
    std::vector<SubscriptionToken> connections_;
    SubscriptionToken collection_drop_;
};

// Connects the context's handlers to the document's controls, once, after
// initialize_component (gallery_controls.cpp).
void connect_handlers(GalleryContext& context);

} // namespace gui_forms::gallery
