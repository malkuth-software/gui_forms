#include "pages.hpp"

#include "spinners-and-rings_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the spinners-and-rings page (W-4): the controls
// demo/pages/spinners-and-rings.form describes, and their behaviour. The
// Active check box starts and stops the three spinners, the Indeterminate
// check box turns the last ring between turning and its value, and a
// Command advances the stepped ring by ten, wrapping past its maximum.
class SpinnersAndRingsPage final : public gui_forms::Component {
public:
    explicit SpinnersAndRingsPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void on_active_changed(bool active);
    void on_indeterminate_changed(bool indeterminate);
    void on_advance();

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    SpinnersAndRingsDocument<SpinnersAndRingsPage> document_{};
    std::array<std::shared_ptr<Spinner>, 3> spinners_{};
    Command advance_{};
};

SpinnersAndRingsPage::SpinnersAndRingsPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    spinners_ = {document_.gallery_spinners_small, document_.gallery_spinners_medium,
                 document_.gallery_spinners_large};
    on((*document_.gallery_spinners_active).checked_changed(), *this, &SpinnersAndRingsPage::on_active_changed);
    on((*document_.gallery_rings_indeterminate).checked_changed(), *this,
       &SpinnersAndRingsPage::on_indeterminate_changed);
    advance_.set_text("Advance");
    advance_.set_description("Advance the stepped ring by ten");
    (*document_.gallery_rings_advance).bind(advance_);
    on(advance_.invoked(), *this, &SpinnersAndRingsPage::on_advance);
}

void SpinnersAndRingsPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size, where each is placed by its bounds in design coordinates. The
    // copy of the child list keeps the iteration valid while the root
    // shrinks.
    Control& root = *document_.gallery_spinners_and_rings_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

void SpinnersAndRingsPage::on_active_changed(bool active) {
    for (const std::shared_ptr<Spinner>& spinner : spinners_) {
        (*spinner).active = active;
    }
}

void SpinnersAndRingsPage::on_indeterminate_changed(bool indeterminate) {
    (*document_.gallery_rings_busy).indeterminate = indeterminate;
}

// Ten more, back to zero once past the maximum.
void SpinnersAndRingsPage::on_advance() {
    ProgressRing& ring = *document_.gallery_rings_stepped;
    const double next = ring.value.read() + 10.0;
    ring.value = next > ring.maximum.read() ? 0.0 : next;
}

void add_spinners_and_rings(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<SpinnersAndRingsPage>(page));
}

}  // namespace gui_forms::gallery::pages
