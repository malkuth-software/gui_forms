// DomainUpDown (stage W): stepping, wrapping, sorting, the selection following
// the text, read-only fields, semantics, and the spin column's published
// arrow parts and states.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// Records every selected_index_changed payload.
struct RecordSelection final {
    std::vector<std::optional<std::size_t>>* seen{};
    void operator()(std::optional<std::size_t> index) const { (*seen).push_back(index); }
};

std::shared_ptr<DomainUpDown> make_domain(const char* id) {
    std::shared_ptr<DomainUpDown> domain = make_control<DomainUpDown>(StableId(std::string(id)));
    (*domain).set_requested_bounds({0.0, 0.0, 180.0, 32.0});
    (*domain).set_items({"AM", "FM", "WFM"});
    return domain;
}

void stepping_and_wrap() {
    std::shared_ptr<DomainUpDown> domain = make_domain("domain.step");
    Window window(domain, {180.0, 32.0});
    window.perform_layout();
    std::vector<std::optional<std::size_t>> seen;
    SubscriptionToken token = (*domain).selected_index_changed().subscribe(RecordSelection{&seen});
    require(!(*domain).selected_index.read() && (*domain).text.read().empty(), "a new domain has no selection");
    (*domain).down_button();
    require((*domain).selected_index.read() == 0U && (*domain).text.read() == "AM",
            "down with no selection selects the first item");
    (*domain).up_button();
    require((*domain).selected_index.read() == 0U, "up at the first item stays there without wrap");
    (*domain).wrap = true;
    (*domain).up_button();
    require((*domain).selected_index.read() == 2U && (*domain).text.read() == "WFM", "up wraps to the last item");
    (*domain).down_button();
    require((*domain).selected_index.read() == 0U, "down wraps to the first item");
    require(seen.size() == 3U && seen[0] == 0U && seen[1] == 2U && seen[2] == 0U,
            "selected_index_changed reports each change once, with the new selection");

    require(window.request_focus((*domain).editor()), "the editor takes focus");
    KeyEvent down{KeyAction::down, PhysicalKey::down};
    require(window.dispatch_key(down) && (*domain).selected_index.read() == 1U,
            "the down key steps to the next item before the editor sees it");

    const Rect spin = (*window.find("domain.step.spinner")).absolute_bounds();
    const Rect field = (*(*domain).editor()).absolute_bounds();
    // Both sit inside the control's 1-unit line border (sweep defect D12:
    // the borderless editor draws no frame of its own).
    require(spin.x == 161.0 && spin.width == 18.0 && spin.height == 30.0 && field.x == 1.0 &&
                field.y == 1.0 && field.width == 160.0 && field.height == 30.0,
            "the spin column takes the button width at the right and the field the rest, inside the border");
    const Point upper{spin.x + spin.width * 0.5, spin.y + spin.height * 0.25};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, upper}),
            "the upper arrow takes the press");
    require((*domain).selected_index.read() == 0U, "the upper arrow moves to the previous item");
    const DomainUpDown::Parts held = (*domain).parts();
    require(held.up_arrow.states.contains(AnatomyState::pressed) &&
                !held.down_arrow.states.contains(AnatomyState::pressed),
            "the held arrow publishes pressed");
    require(held.up_glyph.present && held.up_arrow.bounds.contains(Point{held.up_glyph.bounds.x, held.up_glyph.bounds.y}),
            "each arrow publishes its glyph inside it");
    require(window.dispatch_pointer({PointerAction::up, PointerButton::primary, upper}) &&
                !(*domain).parts().up_arrow.states.contains(AnatomyState::pressed),
            "releasing the arrow clears pressed");
}

void text_and_sorting() {
    std::shared_ptr<DomainUpDown> domain = make_domain("domain.text");
    Window window(domain, {180.0, 32.0});
    window.perform_layout();
    require(window.request_focus((*domain).editor()), "the editor takes focus");
    require(window.dispatch_text({"fm"}) && (*domain).selected_index.read() == 1U,
            "typed text selects the item it equals, ignoring ASCII case");
    require(window.dispatch_text({"x"}) && !(*domain).selected_index.read() && (*domain).text.read() == "fmx",
            "text that equals no item clears the selection and stays");
    (*domain).sorted = true;
    (*domain).set_items({"lsb", "USB", "AM", "cw"});
    require((*domain).items()[0] == "AM" && (*domain).items()[1] == "cw" &&
                (*domain).items()[2] == "lsb" && (*domain).items()[3] == "USB",
            "sorted items order by folded case");
    (*domain).text = "Usb";
    require((*domain).selected_index.read() == 3U, "set_text re-matches the selection");
    (*domain).remove_item_at(3U);
    require(!(*domain).selected_index.read() && (*domain).text.read() == "Usb",
            "removing the selected item clears the selection and keeps the text");
    bool threw = false;
    try {
        (*domain).selected_index = 9U;
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "an index past the end is refused");
}

void read_only_and_semantics() {
    std::shared_ptr<DomainUpDown> domain = make_domain("domain.read-only");
    (*domain).read_only = true;
    Window window(domain, {180.0, 32.0});
    window.perform_layout();
    require((*domain).states().read_only, "a read-only domain publishes read_only");
    require(window.request_focus((*domain).editor()), "the editor takes focus");
    std::vector<std::optional<std::size_t>> seen;
    SubscriptionToken token = (*domain).selected_index_changed().subscribe(RecordSelection{&seen});
    static_cast<void>(window.dispatch_text({"x"}));
    require((*domain).text.read().empty() && !(*domain).selected_index.read() && seen.empty(),
            "a character no item starts with changes nothing");
    static_cast<void>(window.dispatch_text({"f"}));
    require((*domain).text.read() == "FM" && (*domain).selected_index.read() == 1U && seen.size() == 1U,
            "a typed first letter selects the item it starts, ignoring case");
    (*domain).set_items({"AM", "FM", "WFM", "fax", "USB"});
    static_cast<void>(window.dispatch_text({"F"}));
    require((*domain).text.read() == "fax" && (*domain).selected_index.read() == 3U,
            "the same letter again moves to the next item it starts");
    static_cast<void>(window.dispatch_text({"f"}));
    require((*domain).text.read() == "FM" && (*domain).selected_index.read() == 1U,
            "the lookup wraps past the last item");
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}));
    require((*domain).text.read() == "FM", "a read-only field refuses deletion");
    require(!(*(*domain).editor()).undo() && (*domain).text.read() == "FM",
            "the editor has no history to undo into");
    (*domain).read_only = false;
    require(window.dispatch_text({"x"}) && (*domain).text.read() == "FMx" && !(*domain).selected_index.read(),
            "an editable field takes text again");
    (*domain).read_only = true;
    (*domain).set_items({"AM", "FM", "WFM"});
    (*domain).text = "";

    const SemanticDescriptor descriptor = (*domain).semantic_descriptor();
    require(descriptor.role == SemanticRole::combo_box &&
                has_semantic_state(descriptor.states, SemanticState::read_only),
            "a domain is a read-only combo box to accessibility");
    require((*domain).on_semantic_action(SemanticAction::increment, {}) &&
                (*domain).selected_index.read() == 0U,
            "increment moves to the next item");
    require((*domain).on_semantic_action(SemanticAction::set_value, "wfm") &&
                (*domain).selected_index.read() == 2U && (*domain).text.read() == "WFM",
            "set_value selects the matching item and shows its text");
    require(!(*domain).on_semantic_action(SemanticAction::set_value, "SSB"),
            "a read-only domain refuses a value that matches no item");

    // A typed character is its whole UTF-8 sequence: the two bytes of "É"
    // select the item they start, not the first item sharing the lead byte.
    (*domain).set_items({"AM", "À la carte", "Éclair"});
    static_cast<void>(window.dispatch_text({"É"}));
    require((*domain).selected_index.read() == 2U && (*domain).text.read() == "Éclair",
            "a typed multi-byte character selects the item it starts");
}

} // namespace

// DomainUpDown's properties are capabilities: selected_index and text are
// views onto the selection and the editor, the rest cells.
void property_capabilities() {
    std::shared_ptr<DomainUpDown> domain = make_domain("domain.capabilities");
    Window window(domain, {180.0, 32.0});
    window.perform_layout();
    std::vector<std::optional<std::size_t>> seen;
    const SubscriptionToken token = (*domain).selected_index_changed().subscribe(RecordSelection{&seen});
    (*domain).selected_index = std::optional<std::size_t>{1U};
    require((*domain).selected_index.read() == std::optional<std::size_t>{1U} && (*domain).text.read() == "FM" &&
                seen.size() == 1U,
            "a selected_index write shows the item and raises selected_index_changed");
    bool refused = false;
    try {
        (*domain).selected_index = std::optional<std::size_t>{7U};
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*domain).selected_index.read() == std::optional<std::size_t>{1U},
            "an index past the end is refused");
    (*domain).text = "wfm";
    require((*domain).selected_index.read() == std::optional<std::size_t>{2U} && seen.size() == 2U,
            "a text write re-matches the selection, ignoring ASCII case");
    (*domain).text = "Other";
    require(!(*domain).selected_index.read() && (*domain).text.read() == "Other",
            "text that equals no item clears the selection and is kept");
    (*domain).wrap = true;
    (*domain).sorted = true;
    (*domain).read_only = true;
    require((*domain).wrap.read() && (*domain).sorted.read() && (*domain).read_only.read() &&
                !(*(*domain).editor()).can_undo(),
            "wrap, sorted and read_only are cells; read_only clears the editor's history");
    refused = false;
    try {
        (*domain).button_width = 70.0;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*domain).button_width.read() == 18.0, "a button width over 64 is refused");
    static_cast<void>(token);
}

int main() {
    stepping_and_wrap();
    text_and_sorting();
    read_only_and_semantics();
    property_capabilities();
    std::cout << "DomainUpDown: stepping, wrap, typing, sorting, read-only, semantics and arrow parts passed\n";
    return 0;
}
