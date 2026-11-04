#include "gui_forms/controls/scrollable_control/container_control/tab_control/tab_page/tab_page.hpp"
#include "../../../../panel/panel_anatomy.hpp"

#include "gui_forms/text.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

TabPage::TabPage(StableId stable_id, std::string text)
    : Panel(std::move(stable_id)), text_(std::move(text)) {
    // The page's tooltip shows over its tab header only, never over the
    // page's body (owner, 2026-10-28).
    set_tool_tip_presentation(ToolTipPresentation::owner_item);
    set_background(Color::rgba(255, 255, 255));
    // No border of its own, as Windows Forms' TabPage.BorderStyle (None):
    // the owning TabControl paints the display's frame (sweep defect D13:
    // a square line over the display's rounded card left corner notches).
    set_border_style(BorderStyle::none);
}

void TabPage::set_text(std::string text) {
    require_mutable();
    if (text_ == text) return;
    text_ = std::move(text);
    invalidate_tab();
}

void TabPage::set_image_key(std::string image_key) {
    require_mutable();
    if (image_key.size() > 256U) throw std::invalid_argument("TabPage image key exceeds 256 bytes");
    if (!validate_utf8(image_key).valid()) throw std::invalid_argument("TabPage image key must be valid UTF-8");
    if (image_key_ == image_key && image_index_ == -1) return;
    image_key_ = std::move(image_key);
    image_index_ = -1;
    invalidate_tab();
}

void TabPage::set_image_index(int image_index) {
    require_mutable();
    if (image_index < -1) throw std::out_of_range("TabPage image index must be -1 or nonnegative");
    if (image_index_ == image_index && image_key_.empty()) return;
    image_index_ = image_index;
    image_key_.clear();
    invalidate_tab();
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
    descriptor.name = accessible_name().empty() ? text_ : accessible_name();
    descriptor.description = accessible_description();
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

} // namespace gui_forms
