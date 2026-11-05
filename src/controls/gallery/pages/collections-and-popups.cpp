#include "pages.hpp"

#include "collections-and-popups_form.hpp"

namespace gui_forms::gallery::pages {
namespace {

// Owns the collections page: the controls demo/pages/collections-and-popups.form
// describes (two ListBoxes, three ComboBoxes and the status line) and their
// behaviour: the lists' initial selections and the status line's reports.
class CollectionsAndPopupsPage final : public Component {
public:
    explicit CollectionsAndPopupsPage(const std::shared_ptr<Surface>& body) {
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
        on((*single_).selection_changed(), *this, &CollectionsAndPopupsPage::on_location_selected);
        on((*single_).item_activated(), *this, &CollectionsAndPopupsPage::on_location_activated);
        (*multi_).select_index(1U);
        (*multi_).select_index(4U, true, false);
        on((*multi_).selection_changed(), *this, &CollectionsAndPopupsPage::on_features_selected);

        on((*profile_).selected_index_changed(), *this,
           &CollectionsAndPopupsPage::on_profile_committed);
        on((*document_.gallery_collections_combo_density).drop_down_changed(), *this,
           &CollectionsAndPopupsPage::on_density_popup);
        on((*search_).drop_down_changed(), *this, &CollectionsAndPopupsPage::on_search_popup);
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
        // What the form language does not state yet: the page style, and the
        // groups' paper face (Panel's own background, not the ambient
        // back-color).
        const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
            document_.gallery_collections_single, document_.gallery_collections_multi,
            document_.gallery_collections_combos};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_background(paper);
            (*group).set_style(page_style());
        }
    }

    void on_search_popup(const bool open) {
        (*status_).set_text(open ? "Stations matching \"" + std::string((*search_).text()) + "\": " +
                                       std::to_string((*search_).drop_down_items().size())
                                 : "Station search closed · the editor kept the focus");
    }

    void on_location_selected(const ListSelectionChange& change) {
        (*status_).set_text(change.current.empty()
            ? std::string("Locations: no selection")
            : "Location " + std::to_string(change.current.front() + 1U) +
                  " selected · selection_changed is the state event");
    }

    void on_location_activated(const std::size_t index) {
        (*status_).set_text("Location " + std::to_string(index + 1U) +
                            " activated · item_activated reports input only");
    }

    void on_features_selected(const ListSelectionChange& change) {
        (*status_).set_text(std::to_string(change.current.size()) +
                            " features selected · Shift and Ctrl extend the range");
    }

    void on_profile_committed() {
        (*status_).set_text("Profile committed: " + std::string((*profile_).selected_text()) +
                            " · overlay detached · focus restored");
    }

    void on_density_popup(const bool open) {
        (*status_).set_text(open ? "Density popup: OPEN · contained ListBox focus scope"
                                 : "Density popup: CLOSED · owner focus restored");
    }

    // Owns the document's controls; the shell's page body holds them too once
    // mount_document() has moved them there.
    CollectionsDocument<CollectionsAndPopupsPage> document_{};
    std::shared_ptr<ListBox> single_;
    std::shared_ptr<ListBox> multi_;
    std::shared_ptr<ComboBox> profile_;
    std::shared_ptr<ComboBox> search_;
    std::shared_ptr<Label> status_;
};

} // namespace

void add_collections(const std::shared_ptr<Surface>& page,
                     const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<CollectionsAndPopupsPage>(page));
}

} // namespace gui_forms::gallery::pages
