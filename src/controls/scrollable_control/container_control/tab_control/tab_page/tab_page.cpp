#include "gui_forms/controls/tab_page.hpp"
#include "../../../../panel/panel_anatomy.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

TabPage::TabPage(StableId stable_id, std::string initial_text)
    : Panel(std::move(stable_id)) {
    text = std::move(initial_text);
    // The page's tooltip shows over its tab header only, never over the
    // page's body (owner, 2026-10-28).
    set_tool_tip_presentation(ToolTipPresentation::owner_item);
    // No ground of its own: the owning TabControl's display (the card role)
    // shows through, in the window's colour scheme; a white page fixed the
    // light card in every scheme.
    set_background(Color::rgba(0, 0, 0, 0));
    // No border of its own, as Windows Forms' TabPage.BorderStyle (None):
    // the owning TabControl paints the display's frame (sweep defect D13:
    // a square line over the display's rounded card left corner notches).
    border_style = BorderStyle::none;
}

void TabPage::prepare_write(const decltype(text)&, std::string&) {
    require_mutable();
}

void TabPage::written(const decltype(text)&) {
    invalidate_tab();
}

// The key and the index are views: writing one clears the other, so an
// equal key still writes while an index is set, and the reverse.
void TabPage::write_view(const decltype(image_key)&, std::string incoming) {
    require_mutable();
    if (incoming.size() > 256U) throw std::invalid_argument("TabPage image key exceeds 256 bytes");
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("TabPage image key must be valid UTF-8");
    if (image_key_ == incoming && image_index_ == -1) return;
    image_key_ = std::move(incoming);
    image_index_ = -1;
    invalidate_tab();
}

std::string TabPage::read_view(const decltype(image_key)&) const {
    return image_key_;
}

void TabPage::write_view(const decltype(image_index)&, int incoming) {
    require_mutable();
    if (incoming < -1) throw std::out_of_range("TabPage image index must be -1 or nonnegative");
    if (image_index_ == incoming && image_key_.empty()) return;
    image_index_ = incoming;
    image_key_.clear();
    invalidate_tab();
}

int TabPage::read_view(const decltype(image_index)&) const {
    return image_index_;
}

// The tab is the owning TabControl's: it lays out and paints the tab again.
void TabPage::invalidate_tab() {
    invalidate(Dirty::paint | Dirty::semantics | Dirty::measure);
    if (const Control::Ptr owner = parent()) {
        (*owner).invalidate(Dirty::paint | Dirty::semantics | Dirty::measure);
    }
}

SemanticDescriptor TabPage::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::tab_panel;
    descriptor.name = accessible_name.read().empty() ? text.read() : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

namespace {

constexpr ControlAnatomy tab_page_anatomy{
    .control_id = "tab_page",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& TabPage::anatomy() const noexcept {
    return tab_page_anatomy;
}

// property_codemod begin TabPage (tools/property_families/tab_page.json; generated, do not edit)

// property_codemod end TabPage

} // namespace gui_forms
