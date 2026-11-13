#include "gui_forms/controls/list_control.hpp"

#include <array>
#include <utility>

namespace gui_forms {

namespace {

// The events ListControl adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> list_control_interactions{{
    {.name = "selected_index_changed", .payload = "std::optional<std::size_t>", .mutable_payload = false},
}};

} // namespace

ListControl::ListControl(StableId stable_id) : Panel(std::move(stable_id)) {}

ListControl::~ListControl() = default;

bool ListControl::record_selected_index(std::optional<std::size_t> index) noexcept {
    if (selected_index_value_ == index) return false;
    selected_index_value_ = index;
    return true;
}

void ListControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), list_control_interactions.begin(), list_control_interactions.end());
}

} // namespace gui_forms
