#include "pages.hpp"

#include "composite-controls_form.hpp"

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/composites/composite_control/composite_control.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms::gallery::pages {

// Owns the composite-controls page (cp34): the controls
// demo/pages/composite-controls.form describes, among them the generated
// TogglePill and CardWithAction, and their behaviour. The page listens to
// the composites' declared events (CompositeControl::raised): a pill's
// checked-changed refreshes the status line, and the card's action-invoked
// counts. Nothing here reaches a composite's own children.
class CompositeControlsPage final : public gui_forms::Component {
public:
    explicit CompositeControlsPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void show_status();
    void on_pill_raised(const CompositeEvent& event);
    void on_card_raised(const CompositeEvent& event);

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    CompositeControlsDocument<CompositeControlsPage> document_{};
    // How many times the card's action was invoked.
    std::uint64_t invocations_{};
};

namespace {

// "on" or "off" for one pill's registered checked property.
std::string_view checked_word(const composites::TogglePill& pill) {
    const bool checked = pill.read_checked();
    if (checked) return "on";
    return "off";
}

} // namespace

CompositeControlsPage::CompositeControlsPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    on((*document_.gallery_composites_wifi).raised(), *this, &CompositeControlsPage::on_pill_raised);
    on((*document_.gallery_composites_bluetooth).raised(), *this, &CompositeControlsPage::on_pill_raised);
    on((*document_.gallery_composites_airplane).raised(), *this, &CompositeControlsPage::on_pill_raised);
    on((*document_.gallery_composites_storage_offload).raised(), *this, &CompositeControlsPage::on_pill_raised);
    on((*document_.gallery_composites_storage).raised(), *this, &CompositeControlsPage::on_card_raised);
    show_status();
}

void CompositeControlsPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size. The copy of the child list keeps the iteration valid while the
    // root shrinks.
    Control& root = *document_.gallery_composite_controls_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

void CompositeControlsPage::show_status() {
    std::string status = "Wi-Fi ";
    status += checked_word(*document_.gallery_composites_wifi);
    status += " · Bluetooth ";
    status += checked_word(*document_.gallery_composites_bluetooth);
    status += " · Airplane mode ";
    status += checked_word(*document_.gallery_composites_airplane);
    (*document_.gallery_composites_pills_status).text = status;
}

// Every pill raises checked-changed with its new value as the payload.
void CompositeControlsPage::on_pill_raised(const CompositeEvent& event) {
    show_status();
    const bool* const checked = std::get_if<bool>(&event.payload);
    std::string last = "Last event: ";
    last += event.name;
    if (checked != nullptr) last += *checked ? " (true)" : " (false)";
    (*document_.gallery_composites_pills_last).text = last;
}

void CompositeControlsPage::on_card_raised(const CompositeEvent& event) {
    if (event.name != "action-invoked") return;
    ++invocations_;
    std::string shown = "Manage invoked ";
    shown += std::to_string(invocations_);
    shown += invocations_ == 1U ? " time." : " times.";
    (*document_.gallery_composites_cards_invoked).text = shown;
}

void add_composite_controls(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<CompositeControlsPage>(page));
}

}  // namespace gui_forms::gallery::pages
