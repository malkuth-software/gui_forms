#include "pages.hpp"

#include "collections-and-overlays_form.hpp"

namespace gui_forms::gallery::pages {
namespace {

// Owns the collections page: the controls demo/pages/collections-and-overlays.form
// describes (two ListBoxes, three ComboBoxes and the status line) and their
// behaviour: the lists' initial selections and the status line's reports.
class CollectionsAndOverlaysPage final : public Component {
public:
    explicit CollectionsAndOverlaysPage(const std::shared_ptr<Surface>& body) {
        document_.initialize_component(*this);
        mount_document(body);
        single_ = document_.gallery_collections_single_list;
        multi_ = document_.gallery_collections_multi_list;
        profile_ = document_.gallery_collections_combo_profile;
        search_ = document_.gallery_collections_combo_search;
        status_ = document_.gallery_collections_status;

        (*single_).select_index(2U);
        // selection_changed is the state event; item_activated (double-click,
        // Enter) is input only, so a programmatic select never "opens" a row.
        on((*single_).selection_changed(), *this, &CollectionsAndOverlaysPage::on_location_selected);
        on((*single_).item_activated(), *this, &CollectionsAndOverlaysPage::on_location_activated);
        (*multi_).select_index(1U);
        (*multi_).select_index(4U, true, false);
        on((*multi_).selection_changed(), *this, &CollectionsAndOverlaysPage::on_features_selected);

        on((*profile_).selected_index_changed(), *this,
           &CollectionsAndOverlaysPage::on_profile_committed);
        on((*document_.gallery_collections_combo_density).drop_down_changed(), *this,
           &CollectionsAndOverlaysPage::on_density_overlay);
        on((*search_).drop_down_changed(), *this, &CollectionsAndOverlaysPage::on_search_overlay);
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, a design surface of the same 1000 x 660
        // size, where each is placed by its bounds in design coordinates. The
        // copy of the child list keeps the iteration valid while the root
        // shrinks.
        Control& root = *document_.gallery_collections_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
    }

    void on_search_overlay(const bool open) {
        (*status_).text = open ? "Stations matching \"" + std::string((*search_).text.read()) + "\": " +
                                       std::to_string((*search_).drop_down_items().size())
                                 : "Station search closed · the editor kept the focus";
    }

    void on_location_selected(const ListSelectionChange& change) {
        (*status_).text = change.current.empty()
            ? std::string("Locations: no selection")
            : "Location " + std::to_string(change.current.front() + 1U) +
                  " selected · selection_changed is the state event";
    }

    void on_location_activated(const std::size_t index) {
        (*status_).text = "Location " + std::to_string(index + 1U) +
                            " activated · item_activated reports input only";
    }

    void on_features_selected(const ListSelectionChange& change) {
        (*status_).text = std::to_string(change.current.size()) +
                            " features selected · Shift and Ctrl extend the range";
    }

    void on_profile_committed() {
        (*status_).text = "Profile committed: " + std::string((*profile_).selected_text()) +
                            " · overlay detached · focus restored";
    }

    void on_density_overlay(const bool open) {
        (*status_).text = open ? "Density overlay: OPEN · contained ListBox focus scope"
                                 : "Density overlay: CLOSED · owner focus restored";
    }

    // Owns the document's controls; the shell's page body holds them too once
    // mount_document() has moved them there.
    CollectionsDocument<CollectionsAndOverlaysPage> document_{};
    std::shared_ptr<ListBox> single_;
    std::shared_ptr<ListBox> multi_;
    std::shared_ptr<ComboBox> profile_;
    std::shared_ptr<ComboBox> search_;
    std::shared_ptr<Label> status_;
};

} // namespace

void add_collections(const std::shared_ptr<Surface>& page,
                     const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<CollectionsAndOverlaysPage>(page));
}

} // namespace gui_forms::gallery::pages
